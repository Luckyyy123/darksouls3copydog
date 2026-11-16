"""Read-only action-name sidecar; candidate timings are not calibration truth."""
import argparse
from collections import Counter
from contextlib import ExitStack
from copy import deepcopy
import datetime
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parent
STABLE_MS = 12.0
MAX_GAP_MS = 100.0
MAX_PAIR_LAG_MS = 1000.0


def read_action_name(reader, actor_address):
    module = reader.ptr(actor_address+0x1f90) if actor_address else 0
    action = reader.ptr(module+0x28) if module else 0
    raw = reader.bytes(action+0x898, 80*2) if action else None
    if raw is None:
        return None, 'unavailable'
    end = next((i for i in range(0,len(raw),2) if raw[i:i+2] == b'\0\0'), None)
    if end is None:
        return None, 'unterminated_80_character_buffer'
    try:
        return raw[:end].decode('utf-16-le', errors='strict'), 'read'
    except UnicodeError:
        return None, 'invalid_utf16'


def context_key(actor):
    equip = actor.get('equipment') or {}
    return (actor.get('actor'), actor.get('entity_id'),
            equip.get('left_selected_raw'), equip.get('right_selected_raw'), equip.get('hand_mode'))


class NameEpochTracker:
    def __init__(self, parry_ids=()):
        self.parry_ids = set(parry_ids)
        self.slots = {}
        self.sequence = 0
        self.counts = Counter()

    def attack_id(self, animation):
        return isinstance(animation, int) and animation >= 0 and 30000 <= animation % 100000 < 50000 \
            and animation % 100000 not in (30201, 30202) and animation not in self.parry_ids

    def clear(self, reason, t_ms):
        events = []
        for slot, state in self.slots.items():
            if state['epoch'] and not state['paired']:
                events.append(dict(kind='attack_name_unpaired',slot=slot,t_ms=t_ms,
                                   epoch=state['epoch'],name=state['name'],reason=reason))
        self.slots.clear()
        return events

    def update(self, actor, t_ms):
        slot = actor['slot']
        old = self.slots.get(slot)
        key = context_key(actor)
        known_context = isinstance(key[0],int) and key[0] > 0 and isinstance(key[1],int) \
            and all(isinstance(value,int) and value > 0 for value in key[2:4]) and key[4] in (1,2,3)
        name = actor.get('action_name')
        animation = actor.get('animation')
        gap = bool(old and (t_ms-old['last_ms'] > MAX_GAP_MS or t_ms < old['last_ms']))
        same_context = bool(old and key == old['key'] and not gap)
        changed_name = not same_context or name != old['name']
        events = []
        if changed_name:
            if old and old['epoch'] and not old['paired']:
                events.append(dict(kind='attack_name_unpaired',slot=slot,t_ms=t_ms,
                                   epoch=old['epoch'],name=old['name'],reason='name_context_or_sampling_gap_changed'))
            self.sequence += 1
            is_attack = isinstance(name,str) and name.casefold().startswith('attack')
            state = dict(key=key,name=name,animation=animation,last_ms=t_ms,
                         epoch=self.sequence if is_attack else None,first_ms=t_ms,
                         baseline_animation=old['animation'] if same_context else animation,
                         onset_observed=same_context and known_context and old['name'] is not None,
                         paired=False,candidate=None)
            self.slots[slot] = state
        else:
            state = old
        changed_id = same_context and old['animation'] != animation
        if changed_name or changed_id:
            events.append(dict(kind='action_name_or_id_changed',t_ms=t_ms,slot=slot,
                               epoch=state['epoch'],name_onset_observed=state['onset_observed'],
                               context_same=same_context,animation_changed=changed_id,actor=deepcopy(actor)))
        if state['epoch'] and not state['paired'] and state['onset_observed']:
            candidate = state['candidate']
            if candidate and animation != candidate['animation']:
                state['candidate'] = candidate = None
            if changed_id and self.attack_id(animation) and animation != state['baseline_animation']:
                state['candidate'] = candidate = dict(animation=animation,first_ms=t_ms,samples=1)
            elif candidate and animation == candidate['animation']:
                candidate['samples'] += 1
            if candidate and t_ms-candidate['first_ms'] >= STABLE_MS:
                lag = candidate['first_ms']-state['first_ms']
                if 0 <= lag <= MAX_PAIR_LAG_MS:
                    events.append(dict(kind='action_name_attack_id_candidate',slot=slot,t_ms=t_ms,
                                       epoch=state['epoch'],action_name=name,
                                       name_first_t_ms=state['first_ms'],attack_id_first_t_ms=candidate['first_ms'],
                                       confirmed_t_ms=t_ms,lag_ms=round(lag,3),animation=animation,
                                       stable_observed_ms=round(t_ms-candidate['first_ms'],3),
                                       stable_samples=candidate['samples'],actor=deepcopy(actor),
                                       quality='same_identity_weapon_mode_name_epoch_candidate_not_calibration_truth',
                                       attack_id_classification='30000_to_49999_suffix_excluding_known_parries_and_frontal_criticals'))
                    self.counts['paired_candidates'] += 1
                else:
                    events.append(dict(kind='attack_name_unpaired',slot=slot,t_ms=t_ms,
                                       epoch=state['epoch'],name=name,reason='attack_id_outside_1000ms_candidate_horizon'))
                state['paired'] = True
        state['animation'],state['last_ms'] = animation,t_ms
        for event in events:
            self.counts[event['kind']] += 1
        return events

    def absent(self, present_slots, t_ms):
        events=[]
        for slot in set(self.slots)-set(present_slots):
            state=self.slots.pop(slot)
            events.append(dict(kind='actor_unavailable',slot=slot,t_ms=t_ms,
                               epoch=state['epoch'],name=state['name']))
        return events


def main():

    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session',type=Path)
    parser.add_argument('--interval-ms',type=float,default=5.0)
    args=parser.parse_args()
    if not 2 <= args.interval_ms <= 1000:
        raise SystemExit('Invalid interval')
    session=args.session or Path(json.loads((ROOT/'active_session.json').read_text(encoding='utf-8-sig'))['folder'])
    session=session.resolve()
    allowed=(ROOT/'sessions').resolve()
    if session.parent!=allowed or not session.is_dir():
        raise SystemExit('Session must be an existing monitor-owned session directory')
    metadata=json.loads((session/'metadata.json').read_text(encoding='utf-8-sig'))
    deadline=datetime.datetime.fromisoformat(metadata['deadline'])
    if deadline.tzinfo is None:
        raise SystemExit('Session deadline requires timezone')
    if (session/'STOP').exists() or datetime.datetime.now(datetime.timezone.utc)>=deadline:
        raise SystemExit('Session already stopped or expired')
    with ExitStack() as cleanup:
        run_capture(args,session,metadata,deadline,cleanup)


def run_capture(args,session,metadata,deadline,cleanup):
    import msvcrt
    import os
    import shutil
    from win_memory import K, find_game_reader, game_process_status
    from trial_labels import load_parry_ids
    from monitor import atomic_json

    lock=cleanup.enter_context((session/'action_names.lock').open('a+b'))
    lock.seek(0)
    try:
        msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
    except OSError:
        raise SystemExit('An action-name sidecar already owns this session')
    journal=cleanup.enter_context((session/'action_names.jsonl').open('a',encoding='utf-8',buffering=1))
    start=time.perf_counter();started_wall=datetime.datetime.now(datetime.timezone.utc).astimezone().isoformat()
    run_id=f'action_names_{os.getpid()}_{time.perf_counter_ns()}'
    reader=None;game=None;game_base=0;tracker=NameEpochTracker(load_parry_ids())
    count=0;next_find=0.;next_status=0.;state='detecting_game_process';error=None;stop_reason=None
    max_interval=0.;last_sample=None;last_actors=[];next_error=0.

    def clock():
        return dict(t_ms=round((time.perf_counter()-start)*1000,3),perf_counter_ns=time.perf_counter_ns(),
                    tick32=int(K.GetTickCount64())&0xffffffff,
                    wall=datetime.datetime.now(datetime.timezone.utc).astimezone().isoformat(timespec='milliseconds'))

    def emit(event):
        journal.write(json.dumps({**clock(),**event,'run_id':run_id,'game_pid':reader.pid if reader else None},
                                 ensure_ascii=False,allow_nan=False)+'\n')

    def status(running):
        value=dict(running=running,state=state,pid=os.getpid(),run_id=run_id,
                   session=str(session),started=started_wall,updated=clock()['wall'],deadline=metadata['deadline'],
                   stop_file=str(session/'STOP'),**game_process_status(reader,game,state),
                   interval_target_ms=args.interval_ms,max_observed_interval_ms=round(max_interval,3),
                   samples=count,counts=dict(tracker.counts),read_errors=reader.read_errors if reader else None,
                   actors=last_actors,error=error,stop_reason=stop_reason,
                   game_detection='DarkSoulsIII.exe only',memory_access='read_only',
                   time_reference='t_ms is sidecar-relative; perf_counter_ns is common machine monotonic clock; tick32 is coarse GetTickCount64',
                   pairing='candidate only; name epoch then changed full C8 attack ID sustained12ms; onset backdated; no game-rate assumption')
        atomic_json(session/'action_names_status.json',value)

    emit(dict(kind='sidecar_started',metadata_deadline=metadata['deadline'],name_path='actor+1f90 -> +28 -> +898, UTF16[80]',
              target_interval_ms=args.interval_ms,scope='no injection, calls, inputs, writes or hashes'))
    status(True)
    try:
        while datetime.datetime.now(datetime.timezone.utc)<deadline:
            now=time.perf_counter()
            if (session/'STOP').exists():
                stop_reason='session_stop_file';break
            if reader and not reader.alive():
                ended_pid=reader.pid
                reader.close();reader=None;game=None;last_actors=[];game_base=0;last_sample=None
                state='waiting_for_game';error=None;next_find=now
                for event in tracker.clear('game_process_ended',clock()['t_ms']):
                    emit({**event,'detached_pid':ended_pid})
                emit(dict(kind='game_detached',detached_pid=ended_pid))
                status(True)
            if reader is None and now>=next_find:
                next_find=now+1
                try:
                    reader,game,state=find_game_reader()
                    error=(game or {}).get('telemetry_error')
                    if reader:
                        game_base=game['base']
                        emit(dict(kind='game_attached',base=game_base,path=game['path']))
                except OSError as ex:
                    game=None;state='game_process_scan_unavailable';error=str(ex)
            if reader:
                sample_t=clock()['t_ms']
                world=reader.ptr(game_base+0x477fdb8)
                slots=reader.ptr(world+0x40) if world else 0
                addresses=[reader.ptr(world+0x80) if world else 0]
                addresses.extend(reader.ptr(slots+slot*0x38) if slots else 0 for slot in range(1,6))
                actors=[]
                for slot,address in enumerate(addresses):
                    actor=reader.actor(address,slot)
                    if actor is None:continue
                    if slot==0:
                        equiproot=reader.ptr(game_base+0x47572b8)
                        actor['equipment']=reader.equipment(reader.ptr(equiproot+0x10)) if equiproot else actor['equipment']
                    actor['action_name'],actor['action_name_read_quality']=read_action_name(reader,address)
                    actors.append(actor)
                    for event in tracker.update(actor,sample_t):emit(event)
                for event in tracker.absent([a['slot'] for a in actors],sample_t):emit(event)
                last_actors=[{k:a.get(k) for k in ('slot','actor','entity_id','animation','action_name','action_name_read_quality','equipment','position')} for a in actors]
                count+=1;state='collecting' if actors else 'waiting_for_character'
                if last_sample is not None:max_interval=max(max_interval,(now-last_sample)*1000)
                last_sample=now
                unavailable=[a['slot'] for a in actors if a['action_name'] is None]
                if unavailable and now>=next_error:
                    emit(dict(kind='name_read_unavailable',slots=unavailable));next_error=now+1
            if now>=next_status:
                next_status=now+1;status(True);journal.flush()
                if shutil.disk_usage(session).free<512*1024*1024:
                    stop_reason='disk_space_below_512MiB';break
            time.sleep(max(.001,(args.interval_ms/1000 if reader else .2)-(time.perf_counter()-now)))
        if stop_reason is None:stop_reason='session_deadline'
    except KeyboardInterrupt:
        stop_reason='console_interrupt'
    except Exception as ex:
        error=repr(ex);stop_reason='sidecar_error';emit(dict(kind='sidecar_error',error=error))
        raise
    finally:
        if reader:
            reader.close();reader=None
        game=None
        last_actors=[]
        for event in tracker.clear(stop_reason,clock()['t_ms']):emit(event)
        state='stopped';emit(dict(kind='sidecar_stopped',reason=stop_reason,samples=count));status(False)


if __name__=='__main__':
    main()
