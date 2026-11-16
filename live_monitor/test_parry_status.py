import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from auto_trace import DIAGNOSTIC_FORCE_SOURCE, DIAGNOSTIC_UNATTRIBUTED_SOURCE, decode_parry_diagnostic
from parry_status import current_run, diagnostic_lines, diagnostic_mode_line


class ParryStatusTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.status = dict(running=True, game_pid=8, copydog_build_id='SC18', updated='08:20',
                           automatic_trace=dict(session_id=3, last_sequence=3))
        self.scope = dict(game_pid=8, build_id='SC18', session_id=3, wall='08:00')
        self.begin = dict(self.scope, kind='auto_trace_session', t_ms=100)
        base = dict(self.scope, kind='auto_trace', self=20, target=32, defense_kind=2, source=1024,
                    tick32=500, submitted_tick32=500, t_ms=110)
        self.request = dict(base, event_kind=3, sequence=1)
        self.confirm = dict(base, event_kind=5, sequence=2, self_animation=32100)
        self.roll = dict(base, event_kind=3, sequence=3, defense_kind=1)
        self.link = dict(session_id=3, sequence=1, self=20, target=32, tick32=500)
        self.trial = dict(kind='trial_result', start_copydog_build_id='SC18', parry_t_ms=130,
                          wall_logged='08:10', self_actor=20, parry_animation=32100, label='success')

    def write(self, name, rows):
        with (self.folder/name).open('w', encoding='utf-8') as output:
            for row in rows:
                output.write(json.dumps(row)+'\n')
            output.write('{"partially_written":')

    def diagnostic(self, sequence, kind, profile):
        # An older monitor keeps words 0–28 under their generic names.
        return dict(self.request, sequence=sequence, event_kind=kind, event='unknown', profile=profile,
                    plan_start_ms=1, plan_end_ms=128, have_dispatch=129, reason=16, dispatch_ms=0,
                    source=1024 | (DIAGNOSTIC_FORCE_SOURCE if kind == 16 else 0),
                    submitted_tick32=480, confirmed_tick32=300020100)

    def test_gate_and_attempt_survive_old_capturer_without_claiming_an_input(self):
        old_gate = self.diagnostic(1, 15, 1)
        decoded_gate = self.diagnostic(2, 15, 3)
        decode_parry_diagnostic(decoded_gate)
        attempt = self.diagnostic(3, 16, 6)
        self.write('auto_trace.jsonl', [self.begin, old_gate, decoded_gate, attempt])
        self.write('events.jsonl', [])
        counts, recent = current_run(self.folder, self.status)
        self.assertEqual((counts['diagnostic_gate_snapshots'], counts['original_wait'], counts['original_no_plan']),
                         (2, 1, 1))
        for gate, state in (('enabled_shift', 'passed'), ('ccd', 'blocked')):
            self.assertEqual(counts[f'diagnostic_gate_{gate}_{state}'], 2)
        self.assertEqual(counts['diagnostic_gate_queue_not_evaluated'], 1)
        self.assertEqual((counts['attempt_submitted'], counts['diagnostic_attempts'], counts['plans']), (1, 1, 0))
        self.assertEqual((counts['requests'], counts['forced_requests'], counts['confirmed_requests'], recent), (0, 0, 0, []))
        summary = '\n'.join(diagnostic_lines(counts))
        self.assertIn('刀弧接触　通过 0，拦截 2，未评估 0', summary)
        self.assertIn('强制标记的 DLL 提交尝试 0', summary)
        self.assertIn('本次心跳没有保存', diagnostic_mode_line(self.status, counts))

    def test_normal_and_forced_requests_queue_results_and_own_animation_stay_distinct(self):
        marked = DIAGNOSTIC_FORCE_SOURCE | 1024 | 256
        normal = dict(self.request, source=1024 | 256)
        normal_done = dict(self.confirm, source=normal['source'])
        failed = dict(self.request, sequence=3, source=marked, defense_requests=2,
                      target_network_id=4, attack_animation=32001, attack_started_tick32=0)
        rejected = dict(failed, sequence=4, event_kind=6)
        accepted = dict(failed, sequence=5, defense_requests=3)
        returned = dict(accepted, sequence=6, event_kind=4)
        animation = dict(accepted, sequence=7, event_kind=5, self_animation=32100)
        wrong_source = dict(failed, sequence=8, source=normal['source'], event_kind=5, self_animation=32100)
        attempt = self.diagnostic(9, 16, 5)
        attempt['have_dispatch'] |= 1 << 13
        attempt['plan_end_ms'] |= 1 << 13
        self.write('auto_trace.jsonl', [self.begin, normal, normal_done, failed, rejected, accepted,
                                         returned, animation, wrong_source, attempt])
        self.write('events.jsonl', [self.trial])
        self.status['automatic_trace']['last_sequence'] = 9
        counts, recent = current_run(self.folder, self.status)
        self.assertEqual((counts['requests'], counts['forced_requests'], counts['forced_queue_returns'],
                          counts['forced_confirmed_requests'], counts['attempt_queue_failed']), (3, 2, 1, 1, 1))
        self.assertEqual((counts['contact_requests'], counts['ordinary_contact_requests'], counts['confirmed_requests']),
                         (3, 1, 2))
        self.assertEqual(counts['diagnostic_gate_queue_blocked'], 1)
        self.assertEqual((counts['forced_episodes'], counts['repeated_forced_requests']), (1, 1))
        self.assertEqual((counts['strict_success'], counts['matched_success']), (1, 0))
        self.assertIn('诊断强制请求', recent[0][-1])

    def test_unattributed_forced_request_does_not_claim_animation_or_outcome(self):
        marked = 1024 | DIAGNOSTIC_FORCE_SOURCE | DIAGNOSTIC_UNATTRIBUTED_SOURCE
        req = dict(self.request, source=marked)
        returned = dict(req, sequence=2, event_kind=4)
        # Even a malformed recording of kind 5 cannot restore attribution across a replacement.
        confirmation = dict(self.confirm, sequence=3, source=marked)
        self.write('auto_trace.jsonl', [self.begin, req, returned, confirmation])
        self.write('events.jsonl', [dict(self.trial, automatic_trace_submission=self.link)])
        counts, recent = current_run(self.folder, self.status)
        self.assertEqual((counts['forced_requests'], counts['forced_queue_returns'],
                          counts['forced_animation_unattributed'], counts['forced_confirmed_requests']), (1, 1, 1, 0))
        self.assertEqual((counts['matched_success'], counts['strict_success']), (0, 1))
        self.assertIn('同期动画归属未确定', recent[0][-1])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows monitor')
    def test_mode_from_resident_export_distinguishes_off_from_unreadable(self):
        from win_memory import SYMBOLS, automatic_snapshot

        class Fixture:
            def __init__(self, value):
                self.result = value

            def value(self, address, fmt):
                return self.result if address == 808 and fmt == '<i' else None

        symbol = 'CopydogDiagnosticParryEnabled'
        self.assertIn(symbol, SYMBOLS)
        for value, message in ((0, '已关闭'), (1, '其他自动出手已屏蔽'), (None, '未读到开关值')):
            mode = automatic_snapshot(Fixture(value), {symbol:808})['diagnostic_parry_enabled']
            self.assertEqual(mode, value)
            self.assertIn(message, diagnostic_mode_line(dict(diagnostic_parry_enabled=mode, copydog_symbols_available=[symbol]), None))
        self.assertIsNone(automatic_snapshot(Fixture(1), {})['diagnostic_parry_enabled'])
        self.assertIsNone(diagnostic_mode_line({}, None))

    def test_current_run_and_one_to_one_matching(self):
        enabled = diagnostic_mode_line(dict(copydog_build_id='SC23-20261116-190000',
                                            diagnostic_parry_enabled=1), None)
        disabled = diagnostic_mode_line(dict(copydog_build_id='SC23-20261116-190000',
                                             diagnostic_parry_enabled=0), None)
        self.assertIn('统一弹反', enabled)
        self.assertIn('解除诊断总屏蔽', enabled)
        self.assertIn('旧算法不会回切', disabled)
        old_begin = dict(self.begin, session_id=2, t_ms=50)
        other = dict(self.request, game_pid=9)
        after = dict(self.request, sequence=4, wall='08:21')
        self.write('auto_trace.jsonl', [old_begin, other, self.begin, self.request, self.confirm,
                                         self.roll, self.roll, after])
        self.write('events.jsonl', [dict(self.trial, parry_t_ms=60), dict(self.trial, game_pid=9),
                                    dict(self.trial, trace_session_id=2),
                                    dict(self.trial, automatic_status='automatic_request_observed'),
                                    dict(self.trial, automatic_trace_submission=self.link)])
        result, recent = current_run(self.folder, self.status)
        self.assertEqual(recent[0][-1], '同期无伤正面处决')
        self.assertEqual((result['requests'], result['rolls'], result['confirmed_requests']), (1, 1, 1))
        self.assertEqual((result['trials'], result['strict_success'], result['matched_success']), (2, 2, 1))
        self.assertEqual(result['missing_records'], 0)

    def test_duplicates_stale_evidence_and_gaps_stay_unattributed(self):
        self.write('auto_trace.jsonl', [self.begin, self.request, self.confirm])
        self.write('events.jsonl', [dict(self.trial, automatic_trace_submission=self.link),
                                    dict(self.trial, automatic_trace_submission=self.link, parry_t_ms=150),
                                    dict(self.trial, automatic_trace_submission=self.link, parry_t_ms=990),
                                    dict(self.trial, automatic_trace_submission=self.link, parry_animation=32)])
        result, recent = current_run(self.folder, self.status)
        self.assertEqual(recent[0][-1], '尚无唯一配对的结算')
        self.assertEqual((result['matched_success'], result['ambiguous_requests'], result['missing_records']), (0, 1, 1))

    def test_unavailable_or_waiting_has_no_counts(self):
        self.assertIsNone(current_run(self.folder, dict(self.status, game_pid=None)))
        self.assertIsNone(current_run(self.folder, dict(self.status, running=False)))
        self.write('auto_trace.jsonl', [dict(self.begin, session_id=2)])
        self.assertIsNone(current_run(self.folder, self.status))
