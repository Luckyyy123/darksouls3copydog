"""Bounded, platform-independent labels for read-only parry telemetry.

These labels implement the user's no-HP-loss + immediate frontal-riposte rule.
They do not observe the engine's authoritative parry result or prove manual input.
"""
from collections import OrderedDict, deque
from copy import deepcopy
import csv
import math
from pathlib import Path
import re


DEFAULT_PARRY_CSV = (Path(__file__).resolve().parent / "../../../animation_frame_audit_20261112/parry_behavior_windows.csv").resolve()
FRONTAL_EXECUTION = {30201, 30202}
TICK_MASK = (1 << 32) - 1
# The sample's GetTickCount64 clock is coarser than the DLL's timeGetTime.
CLOCK_QUANTUM_MS = 16
VERIFIED_TRACE_STATES = ("ready", "entry_changed_retry")


def load_parry_ids(path=None):
    if path is None:
        path=DEFAULT_PARRY_CSV
        if not path.is_file():
            header=Path(__file__).resolve().parents[1]/'src/parry_animations.h'
            entries=set(map(int,re.findall(r'\bcase\s+([0-9]+)\s*:',header.read_text(encoding='ascii'))))
            if not entries:
                raise ValueError(f'No parry animation IDs in {header}')
            return entries
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        return {int(row["runtime_id"]) for row in csv.DictReader(stream)
                if row.get("attack_type") == "64" and row.get("interval_valid", "True").lower() == "true"}


def low_animation(actor):
    value = actor.get("animation") if actor else None
    return value % 100000 if isinstance(value, int) and value >= 0 else None


def valid_hp(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and 0 <= value < 10000000


def request_count(row):
    value = (row.get("automatic") or {}).get("requests")
    return value if isinstance(value, int) and value >= 0 else None


def identity(actor):
    return tuple(actor.get(key) for key in ("actor", "entity_id", "generation")) if actor else None


def tick32(value):
    return type(value) is int and 0 <= value <= TICK_MASK


def tick_delta(later, earlier):
    return ((later-earlier+(1 << 31)) & TICK_MASK) - (1 << 31)


def recent_automatic_submission(row):
    trace = (row.get("automatic") or {}).get("trace") or {}
    submission = trace.get("recent_parry_submission")
    actor = row.get("self") or {}
    if trace.get("state") not in VERIFIED_TRACE_STATES or not submission:
        return None
    pid, session = row.get("pid"), trace.get("session_id")
    if (type(pid) is not int or pid <= 0 or type(session) is not int or session <= 0 or
            submission.get("session_id") != session or submission.get("game_pid", pid) != pid or
            type(submission.get("sequence")) is not int or submission["sequence"] <= 0 or
            not tick32(submission.get("tick32")) or type(submission.get("age_ms")) is not int or
            not 0 <= submission["age_ms"] <= 400 or not actor.get("actor") or
            submission.get("self") != actor["actor"] or not submission.get("target") or
            row.get("target_actor") not in (None, submission["target"])):
        return None
    sample_tick = (row.get("automatic") or {}).get("tick32")
    if tick32(sample_tick) and ((sample_tick-submission["tick32"]) & TICK_MASK) != submission["age_ms"]:
        return None
    targets = [p for p in row.get("others", []) if p and p.get("actor") == submission.get("target")]
    if len(targets) != 1:
        return None
    network_id = submission.get("target_network_id")
    if network_id is not None and targets[0].get("entity_id") != network_id:
        return None
    return {**deepcopy(submission), "game_pid": pid, "self_identity": identity(actor),
            "target_identity": identity(targets[0])}


def actor_context(actor):
    if not actor:
        return None
    return deepcopy({key: actor.get(key) for key in (
        "actor", "entity_id", "generation", "slot", "animation", "hp", "equipment",
        "distance_to_self", "height_difference", "position", "latency",
        "observed_animation_started_t_ms", "observed_animation_age_ms")})


class TrialTracker:
    def __init__(self, parry_ids=None, timeout_ms=3000, pair_window_ms=500, damage_grace_ms=150):
        self.parry_ids = set(load_parry_ids() if parry_ids is None else parry_ids)
        self.timeout_ms = float(timeout_ms)
        self.pair_window_ms = float(pair_window_ms)
        self.damage_grace_ms = float(damage_grace_ms)
        if self.timeout_ms <= 0 or self.pair_window_ms < 0 or self.damage_grace_ms < 0:
            raise ValueError("Invalid trial timing limits")
        self.active = None
        self.previous = None
        self.reactions = deque(maxlen=32)
        self.sequence = 0
        self.last_t_ms = None
        self.counts = {"success": 0, "failed": 0, "incomplete": 0}
        self.scope = None
        self.seen_candidate = None
        self.pending_submission = None
        self.last_claimed_by_run = OrderedDict()

    def _finish(self, label, reason, t_ms):
        if not self.active:
            return []
        trial = self.active
        self.active = None
        self.pending_submission = None
        self.counts[label] += 1
        return [{"kind": "trial_result", "t_ms": t_ms, "label": label, "reason": reason,
                 **trial, "label_scope": "observed_no_hp_loss_and_paired_frontal_execution",
                 "native_parry_success": "not_directly_observed"}]

    def flush(self, reason="monitor_stopped"):
        result = self._finish("incomplete", reason, self.last_t_ms)
        self.previous = None
        self.reactions.clear()
        return result

    @staticmethod
    def _compact(row):
        actor = row.get("self")
        return {"t_ms": float(row["t_ms"]),
                "self": {key: actor.get(key) for key in ("actor", "entity_id", "generation", "animation", "hp")} if actor else None,
                "others": {other["actor"]: {key: other.get(key) for key in ("actor", "entity_id", "generation", "animation", "slot")}
                           for other in row.get("others", []) if other and other.get("actor")},
                "requests": request_count(row), "tick32": (row.get("automatic") or {}).get("tick32")}

    def _confirm_submission(self, row):
        trial, pending = self.active, self.pending_submission
        if not trial or not pending:
            return
        submission, first_tick, last_tick = pending
        actor = row.get("self") or {}
        trace = (row.get("automatic") or {}).get("trace") or {}
        if (trace.get("state") not in VERIFIED_TRACE_STATES or trace.get("session_id") != trial["trace_session_id"] or
                identity(actor) != trial["self_identity"] or actor.get("animation") != trial["parry_animation"] or
                not any(identity(p) == submission["target_identity"] for p in row.get("others", []))):
            return
        defense = (row.get("automatic") or {}).get("defense") or {}
        confirmed = defense.get("confirmed_tick32")
        if (defense.get("kind") != 2 or defense.get("phase") != 2 or
                defense.get("self") != submission["self"] or defense.get("target") != submission["target"] or
                defense.get("tool") != submission.get("tool") or defense.get("tool") is None or
                defense.get("submitted_tick32") != submission["tick32"] or
                defense.get("previous_animation") != trial["parry_animation"] or not tick32(confirmed) or
                not 0 <= tick_delta(confirmed, submission["tick32"]) <= 400 or
                tick_delta(confirmed, first_tick) < -CLOCK_QUANTUM_MS or
                tick_delta(confirmed, last_tick) > CLOCK_QUANTUM_MS):
            return
        run = trial["game_pid"], trial["trace_session_id"]
        if submission["sequence"] > self.last_claimed_by_run.get(run, 0):
            self.last_claimed_by_run[run] = submission["sequence"]
            self.last_claimed_by_run.move_to_end(run)
            if len(self.last_claimed_by_run) > 8:
                self.last_claimed_by_run.popitem(last=False)
            trial.update(automatic_trace_submission=submission, automatic_status="automatic_request_observed",
                         automatic_submission_sequence=submission["sequence"], automatic_confirmed_tick32=confirmed)
            if trial["requested_target_actor"] is None:
                trial["requested_target_actor"] = submission["target"]
        self.pending_submission = None

    def _pair_execution(self, t_ms, current_others):
        trial = self.active
        if not trial or trial["execution_t_ms"] is None or trial["paired_target_actor"] is not None:
            return []
        eligible = [reaction for reaction in self.reactions
                    if trial["parry_t_ms"] <= reaction["t_ms"] <= trial["deadline_t_ms"]
                    and abs(reaction["t_ms"] - trial["execution_t_ms"]) <= self.pair_window_ms
                    and reaction["actor"] in trial["baseline_target_actors"]
                    and reaction["actor"] in current_others
                    and reaction["identity"] == identity(trial["baseline_target_identities"][reaction["actor"]])
                    and reaction["identity"] == identity(current_others[reaction["actor"]])
                    and (trial["requested_target_actor"] is None or reaction["actor"] == trial["requested_target_actor"])]
        identities = {reaction["actor"] for reaction in eligible}
        if len(identities) > 1:
            return self._finish("incomplete", "ambiguous_frontal_execution_target", t_ms)
        if eligible:
            reaction = min(eligible, key=lambda r: abs(r["t_ms"]-trial["execution_t_ms"]))
            trial["paired_target_actor"] = reaction["actor"]
            trial["target_reaction_t_ms"] = reaction["t_ms"]
            trial["target_slot"] = reaction["slot"]
            trial["paired_target_identity"] = reaction["identity"]
            return [{"kind": "paired_frontal_execution", "trial_id": trial["trial_id"],
                     "t_ms": t_ms, "target_actor": reaction["actor"], "target_slot": reaction["slot"]}]
        return []

    def update(self, row):
        t_ms = float(row["t_ms"])
        if not math.isfinite(t_ms):
            raise ValueError("Sample timestamp must be finite")
        events = []
        if self.last_t_ms is not None and t_ms < self.last_t_ms:
            events += self.flush("sampling_clock_restarted")
        automatic = row.get("automatic") or {}
        trace = automatic.get("trace") or {}
        scope = row.get("pid"), automatic.get("build_id"), None
        if trace.get("state") in VERIFIED_TRACE_STATES:
            scope = (*scope[:2], trace.get("session_id"))
        elif trace and self.scope and scope[:2] == self.scope[:2]:
            scope = self.scope
        # A first successful header/build read fills in a previously unknown
        # value. A conflicting or lost known instance ends an old trial.
        if self.previous is not None and (scope[0] != self.scope[0] or any(
                old is not None and old != new for old, new in zip(self.scope[1:], scope[1:]))):
            events += self.flush("game_or_trace_instance_changed")
        self.scope = scope
        raw_submission = trace.get("recent_parry_submission") or {}
        seen_key = (*scope, raw_submission.get("sequence"))
        if (raw_submission and trace.get("state") in VERIFIED_TRACE_STATES and
                (self.seen_candidate is None or self.seen_candidate[0] != seen_key)):
            candidate = recent_automatic_submission(row)
            if candidate:
                self.seen_candidate = seen_key, candidate["self_identity"], candidate["target_identity"]
        self.last_t_ms = t_ms
        current = self._compact(row)
        actor, previous = current["self"], self.previous
        prior_self = previous["self"] if previous else None
        same_self = bool(actor and prior_self and identity(actor) == identity(prior_self))
        changed = bool(actor and (not same_self or actor.get("animation") != prior_self.get("animation")))
        parry_start = bool(actor and changed and actor.get("animation") in self.parry_ids)
        delta_requests = (current["requests"] - previous["requests"]
                          if previous and current["requests"] is not None and previous["requests"] is not None
                          and current["requests"] >= previous["requests"] else 0)
        while self.reactions and self.reactions[0]["t_ms"] < t_ms-self.timeout_ms-self.pair_window_ms:
            self.reactions.popleft()
        if previous:
            for address, other in current["others"].items():
                old = previous["others"].get(address)
                # A newly appearing actor already in 70030 has an unknown onset.
                if old and identity(other) == identity(old) and low_animation(other) == 70030 and low_animation(old) != 70030:
                    self.reactions.append({"t_ms": t_ms, "actor": address, "slot": other["slot"], "identity": identity(other)})

        if self.active:
            trial = self.active
            self._confirm_submission(row)
            if not actor or identity(actor) != trial["self_identity"]:
                events += self._finish("incomplete", "self_identity_lost_or_changed", t_ms)
            elif not valid_hp(actor.get("hp")):
                events += self._finish("incomplete", "self_hp_unavailable", t_ms)
            elif (trial["requested_target_actor"] is not None and trial["requested_target_actor"] not in current["others"]) or (
                    trial["baseline_target_actors"] and not set(trial["baseline_target_actors"]).intersection(current["others"])) or any(
                    identity(current["others"][address]) != identity(baseline)
                    for address,baseline in trial["baseline_target_identities"].items() if address in current["others"]):
                events += self._finish("incomplete", "target_identity_lost_or_replaced", t_ms)
            else:
                hp_limit = (trial["execution_t_ms"]+self.damage_grace_ms
                            if trial["execution_t_ms"] is not None else trial["deadline_t_ms"])
                if trial["last_hp_t_ms"] <= hp_limit and actor["hp"] < trial["last_hp"]:
                    trial["hp_loss"] += trial["last_hp"]-actor["hp"]
                    trial["hp_drop_observed_t_ms"] = t_ms
                    events += self._finish("failed", "self_hp_loss", t_ms)
                else:
                    trial["last_hp"], trial["last_hp_t_ms"] = actor["hp"], t_ms

        if self.active and parry_start:
            if self.active["execution_t_ms"] is None:
                events += self._finish("failed", "next_parry_before_frontal_execution", t_ms)
            else:
                events += self._finish("incomplete", "next_parry_before_validation_completed", t_ms)

        if self.active:
            trial = self.active
            low = low_animation(actor)
            if changed and low in FRONTAL_EXECUTION and trial["execution_t_ms"] is None:
                if t_ms <= trial["deadline_t_ms"]:
                    trial["execution_t_ms"] = t_ms
                    trial["execution_animation"] = actor["animation"]
                    events.append({"kind": "frontal_execution_observed", "trial_id": trial["trial_id"], "t_ms": t_ms})
                else:
                    events += self._finish("failed", "frontal_execution_after_deadline", t_ms)
            elif changed and trial["execution_t_ms"] is None and low is not None and 30000 <= low < 35000 and actor["animation"] not in self.parry_ids:
                events += self._finish("failed", "intervening_attack_before_execution", t_ms)
            if self.active:
                events += self._pair_execution(t_ms, current["others"])
            if self.active:
                trial = self.active
                if trial["paired_target_actor"] is not None and trial["paired_target_actor"] not in current["others"]:
                    events += self._finish("incomplete", "paired_target_identity_lost", t_ms)
                elif trial["execution_t_ms"] is not None and trial["paired_target_actor"] is not None and t_ms >= trial["execution_t_ms"]+self.damage_grace_ms:
                    events += self._finish("success", "paired_frontal_execution_without_self_hp_loss", t_ms)
                elif trial["execution_t_ms"] is None and t_ms >= trial["deadline_t_ms"]:
                    events += self._finish("failed", "no_followup_frontal_execution", t_ms)
                elif trial["execution_t_ms"] is not None and trial["paired_target_actor"] is None and t_ms >= min(trial["deadline_t_ms"],trial["execution_t_ms"]+self.pair_window_ms):
                    events += self._finish("failed", "no_paired_target_frontal_reaction", t_ms)

        if parry_start:
            self.sequence += 1
            baseline_hp = prior_self.get("hp") if same_self else None
            submission = recent_automatic_submission(row)
            if (submission and same_self and self.seen_candidate ==
                    (seen_key, submission["self_identity"], submission["target_identity"])):
                last_tick = (submission["tick32"] + submission["age_ms"]) & TICK_MASK
                first_tick = previous["tick32"] if tick32(previous["tick32"]) else last_tick
                if 0 <= tick_delta(last_tick, first_tick) <= 400:
                    self.pending_submission = submission, first_tick, last_tick
            self.active = {
                "trial_id": self.sequence, "parry_t_ms": t_ms, "deadline_t_ms": t_ms+self.timeout_ms,
                "self_actor": actor.get("actor"), "self_identity": identity(actor), "parry_animation": actor.get("animation"),
                "baseline_hp": baseline_hp, "last_hp": actor.get("hp"), "last_hp_t_ms": t_ms,
                "hp_loss": 0, "execution_t_ms": None, "execution_animation": None,
                "paired_target_actor": None, "paired_target_identity": None, "target_reaction_t_ms": None, "target_slot": None,
                "baseline_target_actors": list(current["others"]),
                "baseline_target_identities": deepcopy(current["others"]),
                "start_self_context": actor_context(row.get("self")),
                "start_target_context": [actor_context(other) for other in row.get("others", []) if other],
                "start_network_snapshot_id": row.get("network_snapshot_id"),
                "start_display_latency_snapshot_id": row.get("display_latency_snapshot_id"),
                "game_pid": scope[0], "start_copydog_build_id": scope[1], "trace_session_id": scope[2],
                "automatic_trace_submission": None, "automatic_submission_sequence": None,
                "automatic_confirmed_tick32": None, "input_origin": "undetermined",
                "requested_target_actor": row.get("target_actor"), "automatic_status": "unknown",
                "automatic_request_increments": delta_requests,
            }
            self._confirm_submission(row)
            events.append({"kind": "parry_animation_start", "trial_id": self.sequence,
                           "t_ms": t_ms, "animation": actor.get("animation")})
            if not same_self or not valid_hp(baseline_hp) or not valid_hp(actor.get("hp")):
                events += self._finish("incomplete", "missing_pre_parry_hp_or_identity", t_ms)
            elif actor["hp"] < baseline_hp:
                self.active["hp_loss"] = baseline_hp-actor["hp"]
                events += self._finish("failed", "self_hp_loss_at_parry_start", t_ms)

        self.previous = current
        return events
