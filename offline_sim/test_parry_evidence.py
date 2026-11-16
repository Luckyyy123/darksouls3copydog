"""Replay recorded SC18 decisions against the production contact-authorization predicate."""
import argparse
from collections import Counter
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
FIXTURE = HERE / 'trajectories' / 'sc18_parry_gate.json'
BUILD = 'SC18-20261116-001736'
WINDOW_START = '2026-11-16T00:59:00.000+08:00'
END = '2026-11-16T00:59:23.000+08:00'
SIX = (2885, 2903, 2922, 2937, 2959, 2976)
FRAME = {'skip': 0, 'wait': 1, 'press': 2}
GEOMETRY = (256, 4096, 8192, 524288, 1048576)


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def records(path):
    # A running capture may end with a partial line.
    with path.open('rb') as stream:
        for line in stream:
            if line.endswith(b'\n') and line.strip():
                yield json.loads(line)


def extract(folder):
    rows = [e for e in records(folder / 'auto_trace.jsonl')
            if e.get('kind') == 'auto_trace' and e.get('build_id') == BUILD and e['wall'] < END]
    require(rows and rows[0]['event'] == 'run_started', 'Missing initial SC18 run record')
    require(len({(e['game_pid'], e['build_id'], e['session_id']) for e in rows}) == 1,
            'Cannot assign these records to a single process and resident run')
    zero = rows[0]
    participants = {}

    def observation(plan):
        pair = plan['self'], plan['target']
        return dict(seq=plan['sequence'], tick_ms=(plan['tick32'] - zero['tick32']) & 0xffffffff,
                    observed_ms=round(plan['t_ms'] - zero['t_ms'], 3),
                    pair=participants.setdefault(pair, len(participants) + 1),
                    animation=plan['attack_animation'], distance_mm=plan['distance_mm'],
                    source=plan['source'], decision=plan['frame_decision'])

    requests = []
    submit_rows = {}
    plans = [p for p in rows if p.get('event') == 'plan' and p.get('frame_decision') in FRAME]
    same_observation = ('self', 'target', 'attack_animation', 'tick32', 'source', 'distance_mm', 't_ms')
    for submit in rows:
        if submit.get('event') != 'submit' or submit['defense_kind'] != 2:
            continue
        matches = [p for p in plans if p['sequence'] < submit['sequence'] and p['frame_decision'] == 'press'
                   and all(p[key] == submit[key] for key in same_observation)]
        require(len(matches) == 1, f"Ambiguous plan for submit {submit['sequence']}")
        requests.append({**observation(matches[0]), 'submit_seq': submit['sequence']})
        submit_rows[submit['sequence']] = submit

    recent = [p for p in plans if p['wall'] >= WINDOW_START and p.get('timing_clock') == 'native_animation_frame']
    decisions = Counter(f"{p['geometry_model']}/{p['frame_decision']}" for p in recent)
    special = [observation(p) for p in recent if p['geometry_model'] != 'resource_window']
    recent_presses = [p['sequence'] for p in recent if p['frame_decision'] == 'press']
    six_outcomes = extract_outcomes(folder, submit_rows)
    return dict(build=BUILD, run='A', first_trace_sequence=zero['sequence'], first_trace_wall=zero['wall'],
                end_exclusive=END, clock='tick_ms and observed_ms start at the first trace record in run A',
                pair_key='Local self and target pair, numbered in first-request order',
                outcome_units='Offsets from submit in ms. self_hp_drops: [sample offset, HP before, HP after]. '
                              'paired_reaction_execution: [target 70030 sample offset, self frontal sample offset, animation]. '
                              'Action names use the separate collector wall clock; actor changes use samples from the first 1500 ms.',
                facts=dict(opponent_feint_intent=None, physical_input_origin=None, physical_contact_time_ms=None),
                requests=requests, six_outcomes=six_outcomes,
                reported_feint_window=dict(start_inclusive=WINDOW_START, end_exclusive=END, plans=len(recent),
                    counts=dict(sorted(decisions.items())), press_plan_sequences=recent_presses,
                    all_nonresource_plans=special))


def extract_outcomes(folder, submits):
    events = list(records(folder / 'events.jsonl'))
    names = [e for e in records(folder / 'action_names.jsonl')
             if e.get('kind') == 'action_name_or_id_changed' and
             '2026-11-16T00:55:03' <= e['wall'] < '2026-11-16T00:55:30']
    results = []
    for seq in SIX:
        submit = submits[seq]
        trials = []
        for e in events:
            receipt = e.get('automatic_trace_submission') or {}
            if (e.get('kind') == 'trial_result' and receipt.get('sequence') == seq and
                    all(receipt.get(k) == submit[k] for k in ('session_id', 'self', 'target')) and
                    e['start_copydog_build_id'] == BUILD and 0 <= e['parry_t_ms'] - submit['t_ms'] <= 1500):
                trials.append(e)
        require(trials, f'No archived trial evidence for submit {seq}')
        after = lambda t: round(t - submit['t_ms'], 3)
        drops = [[after(e['hp_drop_observed_t_ms']), e['last_hp'], e['last_hp'] - e['hp_loss']]
                 for e in trials if e.get('hp_loss') and e.get('hp_drop_observed_t_ms') is not None]
        paired = [[after(e['target_reaction_t_ms']), after(e['execution_t_ms']), e['execution_animation']]
                  for e in trials if e['label'] == 'success' and e['paired_target_actor'] == submit['target']]
        visible = [after(e['t_ms']) for e in events if e.get('kind') == 'actor_animation' and
                   e.get('actor') == submit['target'] and e.get('animation') == 6000 and
                   0 <= e['t_ms'] - submit['t_ms'] <= 1500]
        first_names = {}
        start = datetime.fromisoformat(submit['wall'])
        for e in names:
            if e['game_pid'] != submit['game_pid']:
                continue
            delta = round((datetime.fromisoformat(e['wall']) - start).total_seconds() * 1000)
            actor = e.get('actor') or {}
            name = actor.get('action_name')
            if 0 <= delta <= 1500 and ((name == 'DamageParry' and actor.get('actor') == submit['target']) or
                                      (name == 'DamageMiddle' and actor.get('actor') == submit['self'])):
                first_names.setdefault(name, delta)
        results.append(dict(submit_seq=seq, trial_ids=[e['trial_id'] for e in trials],
                            self_hp_drops=drops, action_name_wall_offsets_ms=first_names,
                            target_visible_6000_offsets_ms=visible, paired_reaction_execution=paired))
    return results


BRIDGE = r'''
#include <stdio.h>
#include "dynamic_parry.h"
int main(void) {
    unsigned source, decision;
    while (scanf("%u %u", &source, &decision) == 2) {
        int allowed = CopydogParryContactAuthorizes(source);
        printf("%d %d\n", allowed, allowed && decision == CD_FRAME_PRESS);
    }
    return ferror(stdin) || ferror(stdout);
}
'''


def replay(exe, data):
    requests = data['requests']
    require(len(requests) == len({r['submit_seq'] for r in requests}) == 35, 'Expected 35 distinct requests')
    require(len({r['seq'] for r in requests}) == 35 and all(r['seq'] < r['submit_seq'] and
            r['decision'] == 'press' for r in requests), 'Expected a separate preceding PRESS for every request')
    require(data['build'] == BUILD and data['run'] == 'A' and data['end_exclusive'] == END, 'Wrong capture scope')
    require(data['facts'] == dict(opponent_feint_intent=None, physical_input_origin=None,
                                 physical_contact_time_ms=None), 'The recordings have no ground truth for these fields')
    w = data['reported_feint_window']
    nonresource = w['all_nonresource_plans']
    require(w['plans'] == sum(w['counts'].values()) == 355 and sum(v for k,v in w['counts'].items()
            if not k.startswith('resource_window/')) == len(nonresource) == 10, 'Window plan totals changed')
    require(w['counts']['resource_window/press'] == len(w['press_plan_sequences']) == 7 and
            set(w['press_plan_sequences']).issubset({r['seq'] for r in requests}), 'Missing window submissions')
    require({r['seq'] for r in nonresource if r['source'] & 8192} == {3529,3530},
            'Expected two observed POSSIBLE decisions in the reported window')
    outcomes = {r['submit_seq']:r for r in data['six_outcomes']}
    ranges = {r['submit_seq']:r['distance_mm'] for r in requests}
    require([ranges[k] for k in SIX] == [530,1345,1628,2735,1504,2658], 'Observed six ranges changed')
    require(set(outcomes) == set(SIX), 'Missing one of the six observed outcomes')
    for seq,r in outcomes.items():
        close = seq in (2885,2903,2922,2959)
        drops = [old-new for _,old,new in r['self_hp_drops']]
        require(drops == ([171] if close else []), f'Incorrect recorded HP drop for {seq}')
        require(('DamageMiddle' in r['action_name_wall_offsets_ms']) == close and
                ('DamageParry' in r['action_name_wall_offsets_ms']) == (not close), f'Incorrect reaction for {seq}')
        require(bool(r['target_visible_6000_offsets_ms']) == (not close) and
                bool(r['paired_reaction_execution']) == (not close), f'Incorrect paired execution for {seq}')
    require(outcomes[2959]['trial_ids'] == [172,173], 'One legacy submission was entered under two trial IDs')

    observed = [(r['source'],FRAME[r['decision']]) for r in requests+nonresource]
    # All 32 combinations include non-geometry clock bits; none is a recorded strike.
    synthetic = [(1024 | sum(bit for i,bit in enumerate(GEOMETRY) if mask & (1 << i)), frame)
                 for mask in range(32) for frame in range(3)]
    inputs = ''.join(f'{source} {frame}\n' for source,frame in observed+synthetic)
    result = subprocess.run([exe], input=inputs, text=True, capture_output=True, check=True)
    answers = [tuple(map(int,s.split())) for s in result.stdout.splitlines()]
    require(len(answers) == len(observed)+len(synthetic), 'Incomplete production-gate replay')
    require(all(pair == (0,0) for pair in answers[:35]), 'A recorded resource-only request gained permission')
    for row,pair in zip(nonresource,answers[35:len(observed)]):
        expected = int(row['seq'] in (3531,3681,3682))
        require(pair == (expected,0), f"Incorrect geometry or window authorization for plan {row['seq']}")
    for i,pair in enumerate(answers[len(observed):]):
        mask,frame = divmod(i,3)
        require(pair == (int(mask==1),int(mask==1 and frame==2)), f'Synthetic bitmask {mask}/{frame}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--verify-capture', type=Path, help='Optional: verify against a caller-supplied private capture; not needed by the default replay')
    args = parser.parse_args()
    data = json.loads(FIXTURE.read_bytes())
    if args.verify_capture:
        require(extract(args.verify_capture) == data, 'Re-extracted evidence differs from the fixture')
    compiler = args.compiler or os.environ.get('COPYDOG_GCC') or shutil.which('gcc')
    if not compiler:
        parser.error('Pass --compiler, set COPYDOG_GCC, or put MinGW-w64 gcc on PATH.')
    compiler = Path(compiler).expanduser().resolve(strict=True)
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH',''))
    with tempfile.TemporaryDirectory(prefix='sc18-parry-evidence-') as tmp:
        exe = Path(tmp) / ('gate.exe' if os.name == 'nt' else 'gate')
        for opt in ('-O0','-O2'):
            subprocess.run([compiler,'-std=c11',opt,'-Wall','-Wextra','-Werror','-I',HERE.parent/'src',
                            '-x','c','-','-o',exe,'-lm'], input=BRIDGE, text=True, env=env, check=True)
            replay(exe,data)
            print(f'{opt} recorded: 35/35 old PRESS requests lack contact permission, including the two '
                  'with an observed paired execution. Reported feint window: 355 plans, 0 recorded '
                  'CONTACT+PRESS, opponent intent unknown. Synthetic: 96 source/decision combinations passed.')


if __name__ == '__main__':
    main()
