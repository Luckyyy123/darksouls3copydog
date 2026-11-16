import struct
import unittest

from auto_trace import (AutoTraceReader, CAPACITY, DIAGNOSTIC_ATTEMPTS, DIAGNOSTIC_FORCE_SOURCE,
                        DIAGNOSTIC_UNATTRIBUTED_SOURCE, ENTRY_SIZE, MAGIC, resident_build, resident_exports, decode_timing)


class Memory:
    def __init__(self, data, base=0x100000):
        self.data, self.base = data, base

    def bytes(self, address, count):
        offset = address-self.base
        return bytes(self.data[offset:offset+count]) if 0 <= offset and offset+count <= len(self.data) else None


def fixture():
    data = bytearray(4096)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 128+24, 0x20b)
    struct.pack_into('<II', data, 128+136, 512, 128)
    struct.pack_into('<5I', data, 512+20, 2, 2, 700, 720, 740)
    struct.pack_into('<2I', data, 700, 1000, 1100)
    struct.pack_into('<2I', data, 720, 800, 840)
    struct.pack_into('<2H', data, 740, 0, 1)
    data[800:817] = b'CopydogAutoTrace\0'
    data[840:855] = b'CopydogBuildId\0'
    data[1100:1110] = b'SC3-test\0\0'
    return data


class ExportTests(unittest.TestCase):
    def test_current_contact_rejection_keeps_geometry_and_decision(self):
        for geometry, expected in ((524288, 'partial_swept_clear'), (1048576, 'unknown_geometry'),
                                   (8192, 'possible_contact'), (0, 'resource_window')):
            event = dict(event_kind=2, source=1024 | geometry, reason=16, action=1,
                         actual_unfiltered_dispatch_ms=12000, confirmed_tick32=1, submitted_tick32=0)
            decode_timing(event, 0)
            self.assertEqual((event['contact_proof_missing'], event['geometry_model'], event['frame_decision']),
                             (True, expected, 'wait'))
        event = dict(event_kind=2, source=1024 | 1048576 | (4 << 21), reason=16, action=0,
                     actual_unfiltered_dispatch_ms=12000, confirmed_tick32=1, submitted_tick32=0)
        decode_timing(event, 0)
        self.assertEqual(event['geometry_unknown_reason'], 'outside_trusted_horizon')

    def test_accepted_ground_blast_keeps_projectile_and_source(self):
        for source, label in ((1, 'estimated_floor'), (2, 'anchor_fuse'), (3, 'observed_area'), (4, 'observed_age_unknown')):
            record = {'event_kind':14, 'source':source, 'profile':861, 'plan_start_ms':18}
            decode_timing(record, 0)
            self.assertEqual((record['projectile_id'], record['eta_ms'], record['blast_source']), (861, 18, label))

    def test_early_cue_reports_actor_and_stage(self):
        for reason, stage in ((1, 'sampling'), (2, 'confirmed'), (3, 'cancelled')):
            record = {'event_kind':13, 'source':131072, 'reason':reason, 'profile':10019001, 'self_animation':0}
            decode_timing(record, 0)
            self.assertEqual(record['cue_stage'], stage)
            self.assertEqual(record['target_visible_animation'], 10019001)
            self.assertEqual(record['self_animation'], 0)

    def test_dynamic_native_phase_and_geometry_labels(self):
        for source in (528,784):
            event={'event_kind':2,'source':source}
            decode_timing(event,(42<<32)|58)
            self.assertEqual(event['timing_clock'],'native_timeact_dynamic_alignment')
            self.assertEqual(event['phase_q_min_ms'],42)
            self.assertEqual(event['phase_q_max_ms'],58)
            self.assertEqual(bool(event['geometry_model']),bool(source&256))

    def test_resident_data_exports(self):
        data = fixture(); reader = Memory(data)
        exports = resident_exports(reader, dict(base=reader.base, size=len(data)), {'CopydogAutoTrace', 'CopydogBuildId'})
        self.assertEqual(exports['CopydogAutoTrace'], reader.base+1000)
        self.assertEqual(resident_build(reader, exports), 'SC3-test')

    def test_rejects_forwarded_export(self):
        data = fixture(); struct.pack_into('<I', data, 700, 600)
        reader = Memory(data)
        exports = resident_exports(reader, dict(base=reader.base, size=len(data)), {'CopydogAutoTrace'})
        self.assertNotIn('CopydogAutoTrace', exports)

    def test_rejects_bad_array_bounds(self):
        data = fixture(); struct.pack_into('<I', data, 512+24, 0xffffff)
        reader = Memory(data)
        with self.assertRaises(ValueError):
            resident_exports(reader, dict(base=reader.base, size=len(data)), {'CopydogAutoTrace'})


def trace_fixture(latest=0, session=123):
    data = bytearray(100000)
    struct.pack_into('<8Q', data, 2000, MAGIC, 1, CAPACITY, ENTRY_SIZE, session, latest, 0, 0)
    for seq in range(max(1, latest-CAPACITY+1), latest+1):
        values = [0]*32
        values[0] = values[31] = seq
        values[1], values[2], values[3], values[4] = 3, 1000, 100, 200
        values[12] = values[13] = values[16] = (1 << 64)-1
        values[20] = 2
        values[28] = 55
        struct.pack_into('<32Q', data, 2000+64+((seq-1) % CAPACITY)*ENTRY_SIZE, *values)
    memory = Memory(data)
    return memory, AutoTraceReader(memory.base+2000, dict(base=memory.base, size=len(data)))


class TraceTests(unittest.TestCase):
    def test_permission_scheduler_window_keeps_ring_commit_word(self):
        for kind in (3, 15, 16):
            memory, reader = trace_fixture(1)
            packed = ((4500 & 0xffffffff) << 32) | (-12500 & 0xffffffff)
            for word, value in ((1, kind), (15, DIAGNOSTIC_FORCE_SOURCE),
                                (16, 8), (29, 0x102), (30, packed)):
                struct.pack_into('<Q', memory.data, 2064 + word*8, value)
            event = reader.poll(memory)[-1]
            self.assertEqual(event['diagnostic_temporal_decision'], 'wait')
            self.assertEqual(event['diagnostic_request_lo_ms'], -12.5)
            self.assertEqual(event['diagnostic_request_hi_ms'], 4.5)
            self.assertFalse(event['diagnostic_contact_proven'])
            self.assertEqual(event['sequence'], 1)
            self.assertEqual(struct.unpack_from('<Q', memory.data, 2064 + 31*8)[0], 1)

    def diagnostic(self, kind, profile, passed, blocked, evaluated):
        memory, reader = trace_fixture(1)
        source = 1024 | (DIAGNOSTIC_FORCE_SOURCE if kind == 16 else 0)
        for word, value in ((1, kind), (12, passed), (13, blocked), (14, 16), (15, source),
                            (16, profile), (17, evaluated), (18, 1), (23, 0xfffffff8), (24, 0)):
            struct.pack_into('<Q', memory.data, 2064 + word*8, value)
        event = reader.poll(memory)[-1]
        self.assertIsNone(reader.snapshot(1000)['recent_parry_submission'])
        return event

    def test_diagnostic_gate_preserves_original_decision_and_layer_coverage(self):
        ccd = 1 << 7
        for profile, decision in enumerate(('skip', 'wait', 'press', 'no_plan')):
            event = self.diagnostic(15, profile, 1, ccd, 1 | ccd)
            self.assertEqual(event['event'], 'diagnostic_parry_gate')
            self.assertEqual(event['diagnostic_original_decision'], decision)
            self.assertEqual(event['diagnostic_original_reason'], None if decision == 'no_plan' else 16)
            self.assertEqual(event['diagnostic_gate_states']['enabled_shift'], 'passed')
            self.assertEqual(event['diagnostic_gate_states']['ccd'], 'blocked')
            self.assertEqual(event['diagnostic_gate_states']['queue'], 'not_evaluated')
            self.assertEqual(event['diagnostic_episode_tick32'], 0xfffffff8)
            self.assertEqual(event['diagnostic_self_state'], 'unreadable')
            self.assertIsNone(event['diagnostic_self_native_animation'])
            self.assertIsNone(event['submitted_tick32'])
            self.assertIsNone(event['confirmed_tick32'])
            self.assertNotIn('frame_decision', event)
        bad = self.diagnostic(15, 30, 3, 1, 5)
        self.assertEqual(bad['diagnostic_original_decision'], 'unknown')
        for gate in ('enabled_shift', 'target_attack', 'native_readable'):
            self.assertEqual(bad['diagnostic_gate_states'][gate], 'inconsistent')

    def test_diagnostic_attempt_codes_never_become_submission_or_original_decision(self):
        for code, label in {**DIAGNOSTIC_ATTEMPTS, 99:'unknown'}.items():
            event = self.diagnostic(16, code, 0, 0, 0)
            self.assertEqual(event['event'], 'diagnostic_parry_forced')
            self.assertEqual(event['diagnostic_attempt_result'], label)
            self.assertEqual(event['diagnostic_original_source'], 1024)
            self.assertNotIn('diagnostic_original_decision', event)
        for kind in (3, 4, 5, 6):
            memory, reader = trace_fixture(1)
            struct.pack_into('<Q', memory.data, 2064 + 8, kind)
            struct.pack_into('<Q', memory.data, 2064 + 15*8, 1024 | DIAGNOSTIC_FORCE_SOURCE | DIAGNOSTIC_UNATTRIBUTED_SOURCE)
            event = reader.poll(memory)[-1]
            self.assertTrue(event['diagnostic_forced'])
            self.assertTrue(event['diagnostic_animation_attribution_unknown'])
            self.assertNotIn('phase_q_min_ms', event)
            self.assertIsNone(event['plan_start_ms'])
            request = reader.snapshot(1000)['recent_parry_submission']
            if kind == 3:
                self.assertTrue(request['diagnostic_forced'])
                self.assertTrue(request['diagnostic_animation_attribution_unknown'])
            else:
                self.assertIsNone(request)
            self.assertIsNone(reader.snapshot(1000)['last_confirmed_dispatch'])

    def test_frame_plan_distinguishes_interval_and_native_queue_records(self):
        memory,reader=trace_fixture(1)
        for word,value in ((1,2),(15,1024|2048|256|65536),(23,2),(24,9),(25,1),(29,11765),(30,24)):
            struct.pack_into('<Q',memory.data,2064+word*8,value)
        event=reader.poll(memory)[-1]
        self.assertEqual((event['frame_decision'],event['native_ring_index'],event['intervening_ring_records_min']),
                         ('wait',9,2))
        self.assertAlmostEqual(event['next_frame_ms'],11.765)
        self.assertEqual(event['geometry_model'],'eligible_band_swept_contact')
        self.assertTrue(event['turning_motion_observed'])
        self.assertIsNone(event['confirmed_tick32'])
    def decoded_timing(self, kind, source, profile, low, high):
        memory, reader = trace_fixture(1)
        for word, value in ((1, kind), (15, source), (16, profile), (29, 177), (30, (low << 32) | high)):
            struct.pack_into('<Q', memory.data, 2064 + word*8, value)
        return reader.poll(memory)[-1]

    def test_roll_clock_sources_and_candidate_q(self):
        for profile, source_name in ((1, 'online_name_id_mapping'), (2, 'archived_weapon_mode_name_mapping'),
                                     (3, 'current_episode_stable_id')):
            with self.subTest(profile=profile):
                event = self.decoded_timing(2, 56, profile, 0, 32)
                self.assertEqual(event['timing_clock'], 'native_action_phase_name')
                self.assertEqual(event['roll_clock_source'], source_name)
                self.assertEqual(event['roll_q_source'], 'unmeasured_candidate')
                self.assertEqual((event['roll_q_min_ms'], event['roll_q_max_ms']), (0, 32))
                self.assertFalse(event['configured_forced_lead_candidate'])

    def test_measured_roll_q_survives_transaction_events(self):
        for kind in (2, 3, 4, 5, 6):
            with self.subTest(kind=kind):
                event = self.decoded_timing(kind, 56 | 128, 3, 4, 22)
                self.assertEqual(event['roll_q_source'], 'measured_request_to_roll_name')
                self.assertEqual((event['roll_q_min_ms'], event['roll_q_max_ms']), (4, 22))
                self.assertEqual((event['phase_q_min_ms'], event['phase_q_max_ms']), (4, 22))
                self.assertEqual(event['actual_unfiltered_dispatch_ms'], 177 if kind == 5 else None)

    def test_legacy_roll_profile_zero_does_not_fabricate_q(self):
        for low, high in ((0, 0), (4, 22)):
            with self.subTest(low=low, high=high):
                event = self.decoded_timing(3, 56, 0, low, high)
                self.assertEqual(event['roll_clock_source'], 'not_recorded')
                self.assertEqual(event['roll_q_source'], 'not_recorded')
                for key in ('roll_q_min_ms', 'roll_q_max_ms', 'phase_q_min_ms', 'phase_q_max_ms'):
                    self.assertIsNone(event[key])

    def test_unknown_roll_metadata_not_claimed_as_q(self):
        event = self.decoded_timing(3, 184, 99, 2, 10)
        self.assertEqual(event['roll_clock_source'], 'unknown')
        self.assertEqual(event['roll_q_source'], 'unknown')
        self.assertIsNone(event['roll_q_min_ms'])
        event = self.decoded_timing(3, 184, 2, 30, 10)
        self.assertEqual(event['roll_q_source'], 'invalid_bounds')
        self.assertIsNone(event['roll_q_max_ms'])

    def test_preserves_parry_fields_and_does_not_retype_riposte(self):
        for source in (24, 24 | 64):
            event = self.decoded_timing(3, source, 2, 4, 22)
            self.assertEqual(event['timing_clock'], 'native_action_phase_name')
            self.assertEqual(event['configured_forced_lead_candidate'], bool(source & 64))
            self.assertEqual((event['phase_q_min_ms'], event['phase_q_max_ms']), (4, 22))
            self.assertNotIn('roll_clock_source', event)
        for kind, source in ((2, 0), (3, 8), (1, 56), (7, 56), (10, 184), (5, 24)):
            event = self.decoded_timing(kind, source, 3, 4, 22)
            self.assertNotIn('timing_clock', event)
            self.assertNotIn('roll_q_source', event)

    def test_committed_event_signed_fields_and_submission(self):
        memory, reader = trace_fixture(1)
        events = reader.poll(memory)
        self.assertEqual(events[-1]['event'], 'submit')
        self.assertEqual(events[-1]['plan_start_ms'], -1)
        self.assertIsNone(events[-1]['actual_unfiltered_dispatch_ms'])
        self.assertEqual(reader.snapshot(1050)['recent_parry_submission']['age_ms'], 50)
        self.assertEqual(reader.poll(memory), [])
        self.assertIsNone(reader.snapshot(3000)['recent_parry_submission'])

    def test_uncommitted_entry_is_retried(self):
        memory, reader = trace_fixture(1)
        struct.pack_into('<Q', memory.data, 2064+31*8, 0)
        self.assertFalse(any(e['kind'] == 'auto_trace' for e in reader.poll(memory)))
        self.assertEqual(reader.cursor, 0)
        self.assertEqual(reader.state, 'entry_changed_retry')
        struct.pack_into('<Q', memory.data, 2064+31*8, 1)
        self.assertEqual(reader.poll(memory)[0]['sequence'], 1)

    def test_ring_overflow_records_missing(self):
        memory, reader = trace_fixture(260)
        events = reader.poll(memory)
        self.assertEqual(reader.cursor, 260)
        self.assertEqual(reader.dropped, 4)
        self.assertEqual(len([e for e in events if e['kind'] == 'auto_trace']), 256)
        self.assertEqual(next(e for e in events if e['kind'] == 'auto_trace_gap')['lost_events'], 4)

    def test_same_address_run_restart_resets_cursor(self):
        memory, reader = trace_fixture(1)
        reader.poll(memory)
        struct.pack_into('<Q', memory.data, 2000+32, 999)
        events = reader.poll(memory)
        self.assertEqual(reader.session, 999)
        self.assertEqual(reader.cursor, 1)
        self.assertEqual(events[0]['kind'], 'auto_trace_session')

    def test_out_of_module_buffer_rejected(self):
        memory, reader = trace_fixture(1)
        reader.module['size'] = 4096
        self.assertEqual(reader.poll(memory), [])
        self.assertEqual(reader.state, 'unavailable_or_unloaded')

    def test_second_begin_read_detects_mutation(self):
        memory, reader = trace_fixture(1)
        original = memory.bytes
        def mutate(address, count):
            if address == memory.base+2064 and count == 8:
                return b'\0'*8
            return original(address, count)
        memory.bytes = mutate
        self.assertFalse(any(e['kind'] == 'auto_trace' for e in reader.poll(memory)))
        self.assertEqual(reader.cursor, 0)

    def test_session_change_during_batch_discards_events(self):
        memory, reader = trace_fixture(1)
        original = memory.bytes
        reads = [0]
        def mutate(address, count):
            if address == memory.base+2000 and count == 64:
                reads[0] += 1
                if reads[0] == 2:
                    struct.pack_into('<Q', memory.data, 2032, 456)
            return original(address, count)
        memory.bytes = mutate
        self.assertEqual(reader.poll(memory), [])
        self.assertEqual(reader.cursor, 0)
        self.assertEqual(reader.state, 'header_changed_retry')


if __name__ == '__main__':
    unittest.main()
