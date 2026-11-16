"""Classify offline sidecar action-name records. No process, network or input access."""

from datetime import datetime
import math
from pathlib import Path
import re


SOURCE = 'action_names.jsonl'
_BOW = re.compile(r'(?:Attack)?(Crossbow|Greatbow|Bow)(Left|Right|Both)?'
                  r'(Start|Loop|Hold|Aim|Fire|Shoot|Shot|Release|Reload|End|Cancel)',
                  re.ASCII | re.IGNORECASE)
_SPELL = re.compile(r'(?:Attack)?(?:Magic|Spell|Cast|Sorcery|Miracle|Pyromancy)'
                    r'(?:(?:Cast|Magic|Spell|Sorcery|Miracle|Pyromancy|Left|Right|Both|Normal|'
                    r'Attack|Start|Loop|Hold|Charge|Fire|Release|End|Cancel|Fast|Slow|Heavy|Light))*',
                    re.ASCII | re.IGNORECASE)


def _source(source):
    return isinstance(source, (str, Path)) and str(source).replace('\\', '/').rsplit('/', 1)[-1] == SOURCE


def _slot(row):
    slot = row.get('slot')
    return slot if type(slot) is int and 0 <= slot <= 5 else None


def _time(row):
    t = row.get('t_ms')
    return t if type(t) in (int, float) and 0 <= t and math.isfinite(t) else None


def _action(row):
    actor = row.get('actor')
    if not isinstance(actor, dict) or actor.get('action_name_read_quality') != 'read':
        return None
    name = actor.get('action_name')
    if isinstance(name, str) and name.isascii() and 0 < len(name) <= 80:
        return name
    return None


def classify_event(row, source):
    """Map a trusted-schema onset candidate. For raw streams use RangedReview.consume for dedupe."""
    if not _source(source) or not isinstance(row, dict) or row.get('kind') != 'action_name_or_id_changed':
        return None
    slot, t, name = _slot(row), _time(row), _action(row)
    if (slot is None or t is None or not name or row.get('context_same') is not True
            or row.get('name_onset_observed') is not True):
        return None
    bow = _BOW.fullmatch(name)
    if bow:
        family = bow.group(1).lower()
        verb = bow.group(3).lower()
        if verb in ('fire','shoot','shot','release'):
            stage = 'fire_animation'
        elif verb == 'reload':
            stage = 'reload_animation'
        elif verb in ('cancel','end'):
            stage = 'end_animation'
        else:
            stage = 'hold_animation' if verb in ('loop','hold','aim') else 'ready_animation'
    elif _SPELL.fullmatch(name):
        family = 'spell'
        lower = name.lower()
        if lower.endswith(('fire','release')):
            stage = 'cast_release_animation'
        elif lower.endswith(('loop','hold','charge')):
            stage = 'cast_hold_animation'
        elif lower.endswith(('cancel','end')):
            stage = 'cast_end_animation'
        else:
            stage = 'cast_animation'
    else:
        return None
    result = dict(t_ms=t, slot=slot, side='self' if slot == 0 else 'remote', family=family,
                  stage=stage, action_name=name, evidence='action_name_onset_only',
                  projectile_id=None, spell_id=None, school=None)
    machine_clock = row.get('perf_counter_ns')
    if type(machine_clock) is int and machine_clock >= 0:
        result['perf_counter_ns'] = machine_clock
    if isinstance(row.get('wall'), str):
        try:
            wall = datetime.fromisoformat(row['wall'])
            if wall.tzinfo is not None:
                result['wall'] = wall.isoformat()
        except ValueError:
            pass
    return result


class RangedReview:
    """Keep at most one last name/epoch per actor slot; use one instance per input session."""

    def __init__(self):
        self.run = None
        self.last = {}

    def consume(self, row, source):
        if not _source(source) or not isinstance(row, dict):
            return None
        run = row.get('run_id')
        kind = row.get('kind')
        if run != self.run or kind in ('sidecar_started','sidecar_stopped','game_attached','game_detached'):
            self.last.clear()
            self.run = run
        slot = _slot(row)
        if kind == 'actor_unavailable':
            self.last.pop(slot, None)
        if kind != 'action_name_or_id_changed' or slot is None or _time(row) is None:
            return None
        key = (_action(row), row.get('epoch'))
        prior = self.last.get(slot)
        self.last[slot] = key
        if (prior == key or (prior is None and row.get('animation_changed') is True)):
            # ID-only rows repeat name_onset_observed from the start of the epoch.
            # If a file tail begins mid-epoch an ID change alone is not a known onset.
            return None
        return classify_event(row,source)
