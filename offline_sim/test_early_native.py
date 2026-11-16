"""Replay observed SC16 early-native phases through the SC17 C decision code."""
import argparse
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
SRC = HERE.parent / 'src'
DATA = HERE / 'trajectories' / 'sc17_85fps_early_native.json'
IDLE, SAMPLE, CONFIRMED = 0, 1, 2
SKIP, WAIT, PRESS = 0, 1, 2
START = 1000  # Arbitrary rebasing of the anonymised monotonic clock.


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def run(exe, commands, *args):
    completed = subprocess.run([exe, *args], input='\n'.join(commands) + '\n', text=True,
                               capture_output=True, check=True)
    return completed.stdout.splitlines()


def observe_entry(exe, fixture, main_first):
    """Exercise discovery itself: the C reader receives neither target ID nor expected stage."""
    commands, cases = [], []
    for e in fixture['episodes']:
        gear = [*e['attacker_equipment'], *e['defender_equipment']]
        commands.append('B ' + ' '.join(map(str, gear)))
        stream = [(n['record'], n['action_name'], 'side') for n in e['prior_name_changes']]
        stream.append((e['cue_record'], e['action_name'], 'cue'))
        stream.extend((r, None, 'main') for r in e['before_cue'] + e['samples'][:-1])
        stream.sort(key=lambda item: (item[0][0], (item[2] == 'main') != main_first))
        name = None
        processed = []
        for row, changed_name, source in stream:
            if changed_name is not None:
                name = changed_name
            if name is None:
                continue
            check(name.isascii() and name.isalnum(), f'Unexpected action name in case {e["case"]}')
            when, visible, native, previous, phase, duration, index = row
            if native is None or native == visible:
                # A visible-matching selection might have hidden a newer
                # native track. Unknown is the strongest replayable statement.
                index, native, previous, phase, duration = -1, -1, 0., 0., 0.
            commands.append(f'O {when} {name} {visible} {index} {native} {previous} {phase} {duration}')
            processed.append((row, source))
        cases.append((e, processed))

    responses = iter(run(exe, commands, '--observed'))
    at_cue = confirmed = first_observed = 0
    confirmation_times, missed_first = [], []
    for e, processed in cases:
        expected = fixture['target_native_id']
        cue = e['cue_record']
        first = None
        cue_status = found = None
        for (row, source) in processed:
            stage, native, motion_ready, calls = map(int, next(responses).split())
            check(not motion_ready and calls == 0, 'Replay invented a bone pose or a request')
            same_early_record = row[0] >= -28 and row[1] != expected and row[2:] == cue[2:]
            if same_early_record and first is None:
                first = stage >= SAMPLE and native == expected
            if source == 'cue':
                cue_status = stage >= SAMPLE and native == expected
            if (row[0] >= 0 and found is None and row[1] != expected and row[2] == expected
                    and row[4] > cue[4] and stage == CONFIRMED and native == expected):
                found = row[0]
        if not first:
            missed_first.append(e['case'])
        at_cue += bool(cue_status)
        first_observed += bool(first)
        confirmed += found is not None
        if found is not None:
            confirmation_times.append(found)
    check(next(responses, None) is None, 'Unexpected production-entry stages')
    order = 'main-first' if main_first else 'sidecar-first'
    print(f'production selection ({order} at equal ms): earliest identical record '
          f'{first_observed}/65, awake at name cue {at_cue}/65, confirmed before visible '
          f'{confirmed}/65, confirmed at '
          f'{(min(confirmation_times), statistics.median(confirmation_times), max(confirmation_times)) if confirmation_times else None} ms; '
          f'earliest-record misses {missed_first}')
    check(at_cue == 65 and confirmed == 65, f'Production entry lost a name cue ({order})')
    return first_observed


def frame(row, expected, weapon, mode, *, retained=0, onset=START):
    time, _, native, previous, current, duration, index = row
    if native is None:
        token, index, native, previous, current, duration = 0, 0, -1, 0., 0., 0.
    else:
        token = 1
    return (f'F 1 1 {weapon} {mode} {expected} {onset} {onset + time} {retained} '
            f'{token} {index} {native} {previous} {current} {duration}')


def replay(exe, fixture):
    check(fixture['actual_contact_time_ms'] is None and fixture['complete_queue_observed'] is None
          and not fixture['bones_observed'], 'Capture truth fields changed')
    episodes = fixture['episodes']
    check([e['case'] for e in episodes] == list(range(1, 66)), 'Expected 65 complete cue onsets')
    commands, prefixes = [], []
    for episode in episodes:
        cue = episode['cue_record']
        expected = cue[2]
        check(expected == fixture['target_native_id'] and cue[1] != expected
              and cue[3] == 0 and cue[4] > 0, f'Bad original cue for {episode["case"]}')
        rows = [cue]
        for row in episode['samples']:
            if row[1] == expected:
                break
            if row[2] is None or row[2] != row[1]:
                # If the selected pending ID differs from visible, the capture
                # reader used the newest valid pending record. A selected ID
                # equal to visible merely reflects its preference: pass unknown.
                rows.append(row)
            else:
                rows.append([row[0], row[1], None, None, None, None, None])
        visible = episode['samples'][-1]
        check(visible[1] == expected and visible[0] > 0, 'Missing later visible outcome')
        _, right, mode = episode['attacker_equipment']
        commands.append('R')
        commands.extend(frame(row, expected, right, mode) for row in rows)
        prefixes.append((episode['case'], visible[0], rows))

    results = iter(int(v) for v in run(exe, commands))
    opportunities = []
    for case, visible, rows in prefixes:
        state = [next(results) for _ in rows]
        check(state[0] == SAMPLE, f'Case {case} failed to wake on cue')
        check(CONFIRMED in state, f'Case {case} never confirmed before visible')
        index = state.index(CONFIRMED)
        check(0 < rows[index][0] < visible and rows[index][2] == rows[0][2]
              and rows[index][4] > rows[0][4], f'Case {case} confirmed without fresh progress')
        distinct = []
        last_phase = rows[0][4]
        last_time = 0
        for row, stage in zip(rows, state):
            when, _, native, _, phase, _, _ = row
            if native != rows[0][2] or phase <= last_phase or when <= last_time:
                continue
            step = when - last_time  # Causal observed update, without a future timestamp.
            if stage == CONFIRMED:
                distinct.append((when, step))
            last_phase, last_time = phase, when
        opportunities.append((case, rows[index][0], distinct))
    check(next(results, None) is None, 'Unexpected native stages')
    return opportunities


def check_synthetic_timing(exe, opportunities):
    status, lower = run(exe, ['G'])[0].split()
    first = float(lower)
    analytic = 170.  # gap .1 / speed .01 = 10 ms after the modelled 160 ms.
    check(int(status) == 2 and first <= analytic + 1e-8 and analytic - first <= .05 + 1e-8,
          'Production CCD missed the synthetic first contact')
    checks = []
    for _, _, frames in opportunities:
        checks.extend(f'W {first - t:.12f} {first + .05 - t:.12f} {step}' for t, step in frames)
    decisions = iter(int(v) for v in run(exe, checks))
    for case, _, frames in opportunities:
        chosen = None
        for when, _ in frames:
            if next(decisions) == PRESS and chosen is None:
                chosen = when
        check(chosen is not None and chosen + 100. <= analytic < chosen + 140.,
              f'Case {case} failed the independent synthetic [100,140) timing oracle')
    # The point-contact upper endpoint is excluded; equality at the lower endpoint is valid.
    boundaries = [(130, 40, PRESS), (140, 50, WAIT), (90, 12, SKIP), (100, 12, PRESS),
                  (99, 12, SKIP), (125, 5, WAIT), (120, 5, PRESS), (145, 50, WAIT)]
    actual = [int(x) for x in run(exe, [f'W {tti} {tti} {step}' for tti, step, _ in boundaries])]
    check(actual == [d for _, _, d in boundaries], f'Unexpected boundary decisions {actual}')
    return len(boundaries)


def check_untrusted_inputs(exe, fixture):
    episode = fixture['episodes'][0]
    cue = episode['cue_record']
    expected = cue[2]
    _, weapon, mode = episode['attacker_equipment']
    second = next(r for r in episode['samples'] if r[2] == expected and r[4] > cue[4])
    missing = [second[0], cue[1], None, None, None, None, None]
    duplicate = [second[0], *cue[1:]]
    competing = [second[0], cue[1], expected + 10, *second[3:]]
    cases = [(None, cue, 1), (cue, duplicate, 0), (cue, missing, 0),
             (cue, competing, 0), (cue, second, 1)]
    for previous, row, retained in cases:
        commands = ['R']
        if previous:
            commands.append(frame(previous, expected, weapon, mode))
        commands.append(frame(row, expected, weapon, mode, retained=retained))
        stages = [int(v) for v in run(exe, commands)]
        check(stages[-1] != CONFIRMED, 'Untrusted source incorrectly confirmed')

    eligible = next(r for r in episode['samples'] if r[0] >= 12 and r[2] == expected
                    and r[4] > cue[4] and r[1] != expected)
    sequence = [frame(cue, expected, weapon, mode), frame(eligible, expected, weapon, mode)]
    for wrong, reason in [(competing, 'replacement'), (cue, 'same-ID phase rewind')]:
        changed = [eligible[0] + 12, *wrong[1:]]
        later = [eligible[0] + 24, *eligible[1:]]
        commands = ['R', *sequence, frame(changed, expected, weapon, mode),
                    frame(later, expected, weapon, mode),
                    frame(cue, expected, weapon, mode, onset=START + 250),
                    frame(eligible, expected, weapon, mode, onset=START + 250)]
        stages = list(map(int, run(exe, commands)))
        check(stages == [SAMPLE, CONFIRMED, IDLE, IDLE, SAMPLE, CONFIRMED],
              f'Incorrect cancellation or same-ID episode restart after {reason}: {stages}')
    stale_origin = [0, cue[1], expected, 200., 212., cue[5], cue[6]]
    check(run(exe, ['R', frame(stale_origin, expected, weapon, mode)]) == [str(IDLE)],
          'An old advancing record was accepted at a new cue')
    missing_after = [eligible[0] + 12, *missing[1:]]
    check(int(run(exe, ['R', *sequence, frame(missing_after, expected, weapon, mode)])[-1]) == IDLE,
          'A missing read after confirmation allowed the current observation to act')
    return len(cases) + 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get('COPYDOG_GCC') or shutil.which('gcc')
    if not compiler:
        parser.error('Pass --compiler, set COPYDOG_GCC, or put MinGW-w64 gcc on PATH.')
    compiler = Path(compiler).expanduser().resolve(strict=True)
    environment = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
    fixture = json.loads(DATA.read_bytes())
    with tempfile.TemporaryDirectory(prefix='early-native-replay-') as tmp:
        exe = Path(tmp) / ('replay.exe' if os.name == 'nt' else 'replay')
        for opt in ('-O0', '-O2'):
            subprocess.run([compiler, '-std=c11', opt, '-Wall', '-Wextra', '-Werror',
                            HERE / 'early_native_replay.c', SRC / 'prediction_geometry.c',
                            '-o', exe, '-lm'], check=True, env=environment)
            opportunities = replay(exe, fixture)
            boundaries = check_synthetic_timing(exe, opportunities)
            negatives = check_untrusted_inputs(exe, fixture)
            count = len(opportunities)
            times = [at for _, at, _ in opportunities]
            print(f'{opt} observed episodes: {count}/{count} woke and confirmed before visible; '
                  f'confirmation at {min(times)}/{statistics.median(times)}/{max(times)} ms '
                  f'(min/median/max); '
                  f'real contact-time labels: 0/{count}')
            print(f'{opt} synthetic CCD + [100,140) activation: {count}/{count} schedules cover '
                  f'the known point; {boundaries} window checks; {negatives} untrusted inputs')
            if os.name == 'nt':
                integration = Path(tmp) / 'observed.exe'
                units = ('defense', 'parry', 'parry_empirical', 'roll_direction', 'control',
                         'combat', 'combat_core', 'game_address', 'prediction', 'prediction_geometry')
                subprocess.run([compiler, '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror', '-Wno-multichar',
                                HERE.parent / 'tests' / 'test_native_cue_dispatch.c',
                                *(SRC / (name + '.c') for name in units), '-o', integration, '-lwinmm'],
                               check=True, env=environment)
                for main_first in (False, True):
                    observe_entry(integration, fixture, main_first)
            else:
                print(f'{opt} Win32 production-entry replay requires Windows and has not run.')


if __name__ == '__main__':
    main()
