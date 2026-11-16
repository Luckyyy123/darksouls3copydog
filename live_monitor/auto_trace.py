"""Bounded resident-memory export and automatic event decoding. No target calls."""
import struct
from collections import Counter


MAGIC = 0x4350445452434531
HEADER_SIZE = 64
ENTRY_SIZE = 256
CAPACITY = 256
EVENT_NAMES = {1: 'attack_observe', 2: 'plan', 3: 'submit', 4: 'action_call_returned',
               5: 'animation_confirmed', 6: 'cancelled', 7: 'run_started',
               8: 'riposte_candidate', 9: 'riposte_blocked', 10: 'riposte_requested',
               11: 'riposte_confirmed', 12: 'riposte_failed', 13: 'native_parry_cue', 14: 'ground_blast_roll',
               15: 'diagnostic_parry_gate', 16: 'diagnostic_parry_forced'}
FIELDS = ('sequence', 'event_kind', 'tick32', 'self', 'target', 'attack_animation',
          'self_animation', 'tool', 'self_mode', 'target_mode', 'attack_started_tick32',
          'elapsed_ms', 'plan_start_ms', 'plan_end_ms', 'reason', 'source', 'profile',
          'have_dispatch', 'dispatch_ms', 'defense_phase', 'defense_kind', 'defense_requests',
          'confirmations', 'submitted_tick32', 'confirmed_tick32', 'action', 'distance_mm',
          'target_weapon', 'target_network_id', 'actual_unfiltered_dispatch_ms',
          'diagnostic_window_packed')
ROLL_CLOCK_SOURCES = {1: 'online_name_id_mapping', 2: 'archived_weapon_mode_name_mapping',
                      3: 'current_episode_stable_id'}
PARRY_UNKNOWN_REASONS = {0:'unrecorded', 1:'other', 2:'pose_unavailable_or_stale',
                         3:'attack_shape_unavailable', 4:'outside_trusted_horizon',
                         5:'rotation_limit', 6:'work_budget', 7:'indeterminate_sweep'}
DIAGNOSTIC_FORCE_SOURCE = 1 << 28
DIAGNOSTIC_UNATTRIBUTED_SOURCE = 1 << 29
DIAGNOSTIC_GATES = ('enabled_shift', 'target_attack', 'native_readable', 'self_state',
                    'local_input', 'tool_model', 'range', 'ccd', 'timing', 'final_snapshot',
                    'transaction', 'no_hitstun', 'dedup', 'queue')
DIAGNOSTIC_ATTEMPTS = {1:'blocked_hitstun', 2:'unreadable_self', 3:'duplicate',
                       4:'input_disabled', 5:'queue_failed', 6:'submitted', 7:'not_parryable',
                       8:'waiting_timing', 9:'timing_missed', 10:'timing_unknown'}
DIAGNOSTIC_ORIGINAL_DECISIONS = {0:'skip', 1:'wait', 2:'press', 3:'no_plan'}
ROLL_SELF_STATES = ('ok', 'unreadable', 'changed', 'no_hp', 'no_stamina', 'flags',
                    'action', 'animation', 'recovery')


def signed64(value):
    return value - (1 << 64) if value & (1 << 63) else value


def decode_diagnostic_timing(event):
    timing_tag = event.get('actual_unfiltered_dispatch_ms') or 0
    timing_basis = timing_tag & ~0xff
    if timing_basis not in (0x100, 0x200):
        return
    event.update(diagnostic_timing_basis='permission_band_candidate' if timing_basis == 0x100
                 else 'permission_onset_candidate',
                 diagnostic_temporal_decision={1:'skip', 2:'wait', 3:'press'}.get(timing_tag & 0xff, 'unknown'),
                 diagnostic_temporal_reason=event['reason'],
                 diagnostic_contact_proven=False)
    packed = event.get('diagnostic_window_packed')
    if packed is not None:
        lo, hi = packed & 0xffffffff, (packed >> 32) & 0xffffffff
        event['diagnostic_request_lo_ms'] = (lo - (1 << 32) if lo & (1 << 31) else lo) / 1000
        event['diagnostic_request_hi_ms'] = (hi - (1 << 32) if hi & (1 << 31) else hi) / 1000


def decode_parry_diagnostic(event):
    """Decode either an ABI record or the same words saved by a pre-diagnostic monitor."""
    if 'diagnostic_evaluated_mask' in event:
        return
    decode_diagnostic_timing(event)
    passed, blocked, evaluated = event['plan_start_ms'], event['plan_end_ms'], event['have_dispatch']
    states = {}
    for index, gate in enumerate(DIAGNOSTIC_GATES):
        bit = 1 << index
        if not evaluated & bit:
            states[gate] = 'inconsistent' if (passed | blocked) & bit else 'not_evaluated'
        elif bool(passed & bit) == bool(blocked & bit):
            states[gate] = 'inconsistent'
        else:
            states[gate] = 'blocked' if blocked & bit else 'passed'
    self_state = event['dispatch_ms']
    event.update(diagnostic_passed_mask=passed, diagnostic_blocked_mask=blocked,
                 diagnostic_evaluated_mask=evaluated, diagnostic_gate_states=states,
                 diagnostic_original_reason=event['reason'],
                 diagnostic_original_source=event['source'] & ~DIAGNOSTIC_FORCE_SOURCE,
                 diagnostic_episode_tick32=event['submitted_tick32'],
                 diagnostic_self_native_animation=event['confirmed_tick32'] or None,
                 diagnostic_self_state_code=self_state,
                 diagnostic_self_state=ROLL_SELF_STATES[self_state] if 0 <= self_state < len(ROLL_SELF_STATES) else 'unknown')
    if event['event_kind'] == 15:
        event['diagnostic_original_decision'] = DIAGNOSTIC_ORIGINAL_DECISIONS.get(event['profile'], 'unknown')
        if event['diagnostic_original_decision'] in ('no_plan', 'unknown'):
            event['diagnostic_original_reason'] = None
    else:
        event['diagnostic_attempt_result'] = DIAGNOSTIC_ATTEMPTS.get(event['profile'], 'unknown')
    # These words carry masks, an episode tick and a native ID in both diagnostic events.
    # Dispatch times and transaction confirmation belong to the regular kinds 3–6.
    for name in ('plan_start_ms', 'plan_end_ms', 'have_dispatch', 'dispatch_ms', 'submitted_tick32', 'confirmed_tick32'):
        event[name] = None


def decode_timing(event, packed_q):
    kind, source = event['event_kind'], event['source']
    if source & DIAGNOSTIC_FORCE_SOURCE:
        decode_diagnostic_timing(event)
    if kind in (15, 16):
        decode_parry_diagnostic(event)
        return
    if kind in (3, 4, 5, 6) and source & DIAGNOSTIC_FORCE_SOURCE and event.get('defense_kind') == 2:
        event['diagnostic_forced'] = True
        event['diagnostic_animation_attribution_unknown'] = bool(source & DIAGNOSTIC_UNATTRIBUTED_SOURCE)
        # A forced transaction can begin without an original timing plan. The gate
        # event carries the original decision. Only kind 5 supplies a measured delay.
        event['plan_start_ms'] = event['plan_end_ms'] = None
        if kind != 5:
            event['dispatch_ms'] = event['have_dispatch'] = None
        return
    if kind == 14:
        event.update(projectile_id=event['profile'], eta_ms=event['plan_start_ms'],
                     blast_source={1:'estimated_floor', 2:'anchor_fuse', 3:'observed_area',
                                   4:'observed_age_unknown'}.get(source, 'unknown'))
    elif kind == 13:
        event['cue_stage'] = {0:'idle', 1:'sampling', 2:'confirmed', 3:'cancelled'}.get(event['reason'], 'unknown')
        event['target_visible_animation'] = event['profile']
    elif kind in (2,3,4,5,6) and source & 1024:
        event.update(timing_clock='native_animation_frame',phase_q_min_ms=packed_q >> 32,
                     turning_motion_observed=bool(source & 65536),
                     early_native_attack=bool(source & 131072),
                     before_visible_id=bool(source & 262144),
                     decision_cadence='post_physics_playtime_change' if source&16384 else 'observed_animation_update',
                     native_sample='last_published_slot' if source&32768 else 'pending_batch',
                     phase_q_max_ms=packed_q & 0xffffffff,
                     dispatch_source='uncalibrated_two_update_prior' if source & 2048 else 'measured_native_origin',
                     geometry_model=('eligible_band_swept_contact' if source & 256 else 'eligible_band_swept_clear'
                                     if source & 4096 else 'possible_contact' if source & 8192 else
                                     'partial_swept_clear' if source & 524288 else 'unknown_geometry'
                                     if source & 1048576 else 'resource_window'))
        if kind==2:
            if source & 1048576:
                event['geometry_unknown_reason'] = PARRY_UNKNOWN_REASONS.get((source >> 21) & 7, 'other')
            event.update(contact_proof_missing=event['reason']==16,
                         frame_decision={0:'skip',1:'wait',2:'press'}.get(event['action'],'unknown'),
                         next_frame_ms=event['actual_unfiltered_dispatch_ms']/1000,
                         native_ring_index=event['confirmed_tick32'],intervening_ring_records_min=event['submitted_tick32'])
            event['submitted_tick32']=event['confirmed_tick32']=None
        return
    if kind in (2,3,4) and source & 512:
        event.update(timing_clock='native_timeact_dynamic_alignment',
                     geometry_model='endpoint_velocity_candidate_500ms' if source & 256 else None,
                     phase_q_min_ms=packed_q >> 32, phase_q_max_ms=packed_q & 0xffffffff)
        return
    if kind in (2,3,4,5,6) and source == (32|256):
        event['geometry_model']='native_pose_endpoint_velocity_50ms'
        event['predicted_contact_ms']=packed_q/1000.
        return
    if kind in (2, 3, 4, 5, 6) and source in (56, 56 | 128):
        # The old source=56, profile=0 writer did not populate word 30.
        # Even 0/0 was an unrecorded slot, not a measured instant roll.
        profile = event['profile']
        clock = ROLL_CLOCK_SOURCES.get(profile)
        low, high = packed_q >> 32, packed_q & 0xffffffff
        if clock is None:
            clock = 'not_recorded' if profile == 0 else 'unknown'
            q_source, low, high = clock, None, None
        elif not 0 <= low <= high <= 1000:
            q_source, low, high = 'invalid_bounds', None, None
        else:
            q_source = 'measured_request_to_roll_name' if source & 128 else 'unmeasured_candidate'
        event.update(timing_clock='native_action_phase_name', configured_forced_lead_candidate=False,
                     roll_clock_source=clock, roll_q_source=q_source, roll_q_min_ms=low, roll_q_max_ms=high,
                     phase_q_min_ms=low, phase_q_max_ms=high)
    elif kind in (2, 3, 4) and source & 16 and not source & 32:
        event.update(timing_clock='native_action_phase_name', configured_forced_lead_candidate=bool(source & 64),
                     phase_q_min_ms=packed_q >> 32, phase_q_max_ms=packed_q & 0xffffffff)


def resident_exports(reader, module, wanted):
    base, size = module['base'], module['size']

    def read(rva, count):
        if rva < 0 or count < 0 or rva+count > size:
            return None
        return reader.bytes(base+rva, count)

    dos = read(0, 64)
    if dos is None or dos[:2] != b'MZ':
        raise ValueError('Resident DOS header unavailable')
    nt_offset = struct.unpack_from('<I', dos, 60)[0]
    nt = read(nt_offset, 144)
    if nt is None or nt[:4] != b'PE\0\0' or struct.unpack_from('<H', nt, 24)[0] != 0x20b:
        raise ValueError('Resident PE32+ header unavailable')
    export_rva, export_size = struct.unpack_from('<II', nt, 136)
    header = read(export_rva, 40)
    if not export_rva or not export_size or header is None:
        return {}
    nfunc, nname, funcs_rva, names_rva, ords_rva = struct.unpack_from('<5I', header, 20)
    if nname > 4096 or nfunc > 4096 or nname > nfunc:
        raise ValueError('Resident export counts outside bounds')
    names = read(names_rva, nname*4)
    ords = read(ords_rva, nname*2)
    funcs = read(funcs_rva, nfunc*4)
    if names is None or ords is None or funcs is None:
        raise ValueError('Resident export arrays unavailable')
    found = {}
    for index in range(nname):
        rva = struct.unpack_from('<I', names, index*4)[0]
        name = read(rva, min(128, max(0, size-rva)))
        if name is None or b'\0' not in name:
            continue
        name = name.split(b'\0', 1)[0].decode('ascii', errors='replace')
        if name not in wanted:
            continue
        ordinal = struct.unpack_from('<H', ords, index*2)[0]
        if ordinal >= nfunc:
            continue
        target = struct.unpack_from('<I', funcs, ordinal*4)[0]
        if target == 0 or target >= size or export_rva <= target < export_rva+export_size:
            continue
        found[name] = base+target
    return found


def resident_build(reader, exports):
    address = exports.get('CopydogBuildId')
    raw = reader.bytes(address, 65) if address else None
    if raw is None or b'\0' not in raw:
        return None
    return raw.split(b'\0', 1)[0].decode('ascii', errors='replace')


class AutoTraceReader:
    def __init__(self, address, module):
        self.address, self.module = address, module
        self.session = None
        self.cursor = 0
        self.dropped = 0
        self.last_event = None
        self.last_submission = None
        self.last_confirmed_dispatch = None
        self.state = 'awaiting_header'
        self.event_counts = Counter()

    def _header(self, reader):
        base, size = self.module['base'], self.module['size']
        if not base <= self.address or self.address+HEADER_SIZE+CAPACITY*ENTRY_SIZE > base+size:
            return None
        raw = reader.bytes(self.address, HEADER_SIZE)
        if raw is None:
            return None
        h = struct.unpack('<8Q', raw)
        return h if h[:4] == (MAGIC, 1, CAPACITY, ENTRY_SIZE) and h[4] else None

    def poll(self, reader):
        header = self._header(reader)
        if header is None:
            self.state = 'unavailable_or_unloaded'
            return []
        session, latest = header[4:6]
        events = []
        if session != self.session:
            self.session, self.cursor = session, 0
            self.last_event = self.last_submission = self.last_confirmed_dispatch = None
            self.event_counts.clear()
            events.append(dict(kind='auto_trace_session', session_id=session))
        if latest < self.cursor:
            self.state = 'sequence_regressed'
            return []
        start = self.cursor+1
        oldest = max(1, latest-CAPACITY+1)
        missing = max(0, oldest-start)
        if missing:
            events.append(dict(kind='auto_trace_gap', session_id=session, lost_events=missing,
                               first_missing_sequence=start, next_available_sequence=oldest,
                               reason='initial_attach_backlog_overwritten' if not self.cursor else 'ring_overflow'))
            start = oldest
        decoded = []
        for sequence in range(start, latest+1):
            address = self.address+HEADER_SIZE+((sequence-1) % CAPACITY)*ENTRY_SIZE
            raw = reader.bytes(address, ENTRY_SIZE)
            if raw is None:
                break
            values = struct.unpack('<32Q', raw)
            repeated_begin = reader.bytes(address, 8)
            if values[0] != sequence or values[31] != sequence or repeated_begin != raw[:8]:
                break
            event = dict(zip(FIELDS, values))
            for name in ('plan_start_ms', 'plan_end_ms', 'profile'):
                event[name] = signed64(event[name])
            decode_timing(event, values[30])
            if event['event_kind'] != 5:
                event['actual_unfiltered_dispatch_ms'] = None
            if 8 <= event['event_kind'] <= 12:
                event['riposte_state'] = values[18]
                event['native_throw_id'] = values[25]
            event.update(kind='auto_trace', event=EVENT_NAMES.get(event['event_kind'], 'unknown'), session_id=session)
            decoded.append(event)
        after = self._header(reader)
        if after is None or after[:5] != header[:5]:
            self.state = 'header_changed_retry'
            return []
        self.dropped += missing
        if missing:
            self.cursor = start-1
        for event in decoded:
            self.cursor = event['sequence']
            self.last_event = event
            self.event_counts[event['event']] += 1
            if event['event_kind'] == 3 and event['defense_kind'] == 2:
                self.last_submission = {key: event[key] for key in
                                        ('session_id', 'sequence', 'tick32', 'self', 'target', 'attack_animation',
                                         'tool', 'self_mode', 'target_mode', 'target_network_id')}
                if event.get('diagnostic_forced'):
                    self.last_submission['diagnostic_forced'] = True
                    self.last_submission['diagnostic_animation_attribution_unknown'] = event['diagnostic_animation_attribution_unknown']
            if event['event_kind'] == 5 and not event.get('diagnostic_animation_attribution_unknown'):
                self.last_confirmed_dispatch = {key: event[key] for key in
                                                ('tick32', 'dispatch_ms', 'actual_unfiltered_dispatch_ms',
                                                 'defense_kind', 'tool', 'self_mode')}
        self.state = 'ready' if self.cursor == latest else 'entry_changed_retry'
        return events+decoded

    def snapshot(self, tick32):
        submission = self.last_submission
        if submission is not None:
            age = (tick32-submission['tick32']) & 0xffffffff
            submission = {**submission, 'age_ms': age} if age <= 1000 else None
        return dict(state=self.state, session_id=self.session, last_sequence=self.cursor,
                    dropped_events=self.dropped,
                    event_counts=dict(self.event_counts),
                    last_event=self.last_event['event'] if self.last_event else None,
                    recent_parry_submission=submission,
                    last_confirmed_dispatch=self.last_confirmed_dispatch)
