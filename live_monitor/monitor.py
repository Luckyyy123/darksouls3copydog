"""Ten-hour read-only combat capture, surviving game/DLL exits and restarts."""
import argparse
import collections
from contextlib import ExitStack
import datetime
import gzip
import json
import math
import msvcrt
import os
from pathlib import Path
import shutil
import time

from win_memory import find_game_reader, game_process_status, modules, telemetry_symbols, automatic_snapshot, foreground_inputs
from trial_labels import TrialTracker
from auto_trace import AutoTraceReader, resident_build

ROOT=Path(__file__).resolve().parent
ZONE=datetime.timezone(datetime.timedelta(hours=8),'Asia/Shanghai')


def wall():
    return datetime.datetime.now(ZONE).isoformat(timespec='milliseconds')


def atomic_json(path,value):
    temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
    for attempt in range(6):
        try:
            os.replace(temp,path)
            return
        except PermissionError:
            if attempt==5:
                raise
            time.sleep(.01)


class RotatingSamples:
    def __init__(self,folder,rotate_seconds=600):
        self.folder=folder;self.rotate_seconds=rotate_seconds
        self.stream=None;self.part=0;self.opened=0.;self.path=None

    def write(self,row,now):
        if self.stream is None or now-self.opened>=self.rotate_seconds:
            self.close();self.part+=1;self.opened=now
            self.path=self.folder/f'samples_{self.part:05d}.jsonl.gz'
            self.stream=gzip.open(self.path,'wt',encoding='utf-8',compresslevel=1,newline='\n')
        self.stream.write(json.dumps(row,ensure_ascii=False,separators=(',',':'),allow_nan=False)+'\n')

    def flush(self):
        if self.stream:
            self.stream.flush()

    def close(self):
        if self.stream:
            self.stream.close();self.stream=None


def read_json_optional(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError,ValueError):
        return None


def elapsed32(now,previous):
    return (now-previous)&0xffffffff


def copydog_latency(automatic,slot):
    values=automatic.get('latency_raw')
    sends=automatic.get('last_outgoing_hit_tick32')
    raw=values[slot-1] if values and 1<=slot<=len(values) else None
    send=sends[slot-1] if sends and 1<=slot<=len(sends) else None
    age=elapsed32(automatic['tick32'],send) if send else None
    return dict(copydog_damage_ack_half_ms=raw,last_outgoing_hit_tick32=send,
                last_outgoing_hit_age_ms=age,
                sample_age_ms=None,
                quality='heuristic_half_round_trip_no_sample_timestamp' if raw is not None else 'unavailable',
                usable_for_zero_latency_comparison=False,
                initial_or_ambiguous_value=raw in (0,256) if raw is not None else None,
                stale_or_unproven=age is None or age>5000,
                warning='Damage notification/ack estimator, not DS3ConnectionInfo ping; same values may be fresh or stale')


def context_actor(actor,prior,t_ms,generations):
    if actor is None:
        return None
    slot=actor['slot']
    same=bool(prior and actor['actor']==prior['actor'] and actor['entity_id']==prior['entity_id'])
    if not same:
        generations[slot]+=1
    actor['generation']=generations[slot]
    changed=not same or actor['animation']!=prior['animation']
    actor['observed_animation_started_t_ms']=t_ms if changed else prior['observed_animation_started_t_ms']
    actor['observed_animation_age_ms']=round(t_ms-actor['observed_animation_started_t_ms'],3)
    actor['animation_origin_quality']='first_observation_not_native_phase'
    return actor


class ScopedTrials:
    """Keep one trial sequence while recording the observed process and DLL run boundaries."""
    def __init__(self,tracker):
        self.tracker=tracker
        self.game_pid=None
        self.attachment=None
        self.process_baseline=None
        self.module_key=None
        self.build_id=None
        self.trace_session_id=None
        self.module_baseline=None
        self.module_sequence=0

    def identity(self):
        return dict(game_pid=self.game_pid,game_attachment=self.attachment,build_id=self.build_id,
                    trace_session_id=self.trace_session_id,
                    module_run=self.module_sequence if self.module_baseline is not None else None)

    def status(self):
        def since(baseline):
            if baseline is None:
                return None
            return {key:value-baseline.get(key,0) for key,value in self.tracker.counts.items()}
        return dict(trial_counts=self.tracker.counts.copy(),
                    current_process_trial_counts=since(self.process_baseline),
                    current_module_trial_counts=since(self.module_baseline),
                    game_attachment=self.attachment,module_run=self.identity()['module_run'],
                    trace_session_id=self.trace_session_id)

    def _tag(self,items):
        if not items:
            return []
        observed=self.identity()
        return [{**observed,**item} for item in items]

    def _begin_module(self,key,build_id):
        self.module_key=key
        self.build_id=build_id
        self.trace_session_id=None
        self.module_baseline=None
        if key is not None:
            self.module_sequence+=1
            self.module_baseline=self.tracker.counts.copy()

    def begin_process(self,pid,attachment):
        items=self.end_process('game_process_changed') if self.game_pid is not None else []
        self.game_pid=pid
        self.attachment=attachment
        self.process_baseline=self.tracker.counts.copy()
        return items

    def end_process(self,reason):
        items=self._tag(self.tracker.flush(reason))
        self._begin_module(None,None)
        self.game_pid=None
        self.attachment=None
        self.process_baseline=None
        return items

    def observe_module(self,key,build_id):
        replaced=(key!=self.module_key or
                  (self.build_id is not None and build_id is not None and self.build_id!=build_id))
        if replaced:
            items=self._tag(self.tracker.flush('copydog_module_changed'))
            self._begin_module(key,build_id)
            return items
        if build_id is not None:
            self.build_id=build_id
        return []

    def observe_trace_session(self,session_id,observed_build=None):
        if session_id is None or self.module_key is None or session_id==self.trace_session_id:
            return []
        items=[]
        if self.trace_session_id is not None:
            items=self._tag(self.tracker.flush('copydog_run_session_changed'))
            # The same mapped address may now contain a different build.
            self._begin_module(self.module_key,observed_build)
        self.trace_session_id=session_id
        return items

    def update(self,row):
        return self._tag(self.tracker.update(row))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--hours',type=float,default=10.)
    parser.add_argument('--interval-ms',type=float,default=5.)
    parser.add_argument('--rotate-seconds',type=float,default=600.)
    args=parser.parse_args()
    if not 0<args.hours<=168 or not 2<=args.interval_ms<=1000 or args.rotate_seconds<=0:
        raise SystemExit('Invalid monitoring duration/interval')
    folder=args.output.resolve();folder.mkdir(parents=True,exist_ok=True)
    if (folder/'STOP').exists():
        raise SystemExit('STOP already exists; choose a fresh session directory')
    with ExitStack() as cleanup:
        run_capture(args,folder,cleanup)


def run_capture(args,folder,cleanup):
    lock=cleanup.enter_context((ROOT/'capture.lock').open('a+b'))
    lock.seek(0)
    try:
        msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
    except OSError:
        raise SystemExit('Another combat monitor is already running')
    cleanup.callback((folder/'STOP').touch)
    start=time.perf_counter();deadline=start+args.hours*3600
    reader=None;game=None;game_base=0;symbols={};previous={};generations=collections.defaultdict(int)
    trace_reader=None;last_display=None
    samples=0;world_samples=0;attach_count=0;net_id=0;last_network=None
    next_find=0.;next_modules=0.;next_beat=0.;next_network=0.;last_sample=None
    intervals=collections.deque(maxlen=2000);durations=collections.deque(maxlen=2000)
    trials=ScopedTrials(TrialTracker());logger=RotatingSamples(folder,args.rotate_seconds)
    cleanup.callback(logger.close)
    events=cleanup.enter_context((folder/'events.jsonl').open('a',encoding='utf-8',buffering=1))
    network_log=cleanup.enter_context((folder/'network_snapshots.jsonl').open('a',encoding='utf-8',buffering=1))
    trace_log=cleanup.enter_context((folder/'auto_trace.jsonl').open('a',encoding='utf-8',buffering=1))
    stop_reason='max_duration_reached';state='detecting_game_process';error=None;latest_row=None

    def event(kind,**values):
        events.write(json.dumps(dict(kind=kind,wall=wall(),t_ms=round((time.perf_counter()-start)*1000,3)) |
                                trials.identity() | values,
                                ensure_ascii=False,allow_nan=False)+'\n')

    def emit_trials(items):
        for item in items:
            events.write(json.dumps({**item,'wall_logged':wall()},ensure_ascii=False,allow_nan=False)+'\n')

    def status(running=True):
        helper_status=read_json_optional(folder/'network/status.json') if reader else None
        return dict(running=running,state=state,monitor_pid=os.getpid(),**game_process_status(reader,game,state),
                    started=metadata['started'],updated=wall(),elapsed_seconds=round(time.perf_counter()-start,2),
                    max_hours=args.hours,deadline=metadata['deadline'],samples=samples,world_samples=world_samples,
                    attachments=attach_count,stop_file=str(folder/'STOP'),sample_file=str(logger.path) if logger.stream else None,
                    target_interval_ms=args.interval_ms,
                    observed_interval_ms=dict(min=min(intervals),mean=sum(intervals)/len(intervals),max=max(intervals)) if intervals else None,
                    read_duration_ms=dict(mean=sum(durations)/len(durations),max=max(durations)) if durations else None,
                    read_errors=reader.read_errors if reader else None,
                    copydog_symbols_available=sorted(symbols),
                    copydog_build_id=trials.build_id,automatic_trace=(latest_row or {}).get('automatic',{}).get('trace'),
                    diagnostic_parry_enabled=(latest_row or {}).get('automatic',{}).get('diagnostic_parry_enabled'),
                    **trials.status(),network_snapshot_id=net_id if reader else None,network_helper=helper_status,
                    display_latency=last_display if reader else None,
                    remote_players=[dict(slot=x['slot'],generation=x['generation'],animation=x['animation'],
                                         distance=x.get('distance_to_self'),latency=x.get('latency'))
                                    for x in ((latest_row or {}).get('others') or []) if x],
                    stop_reason=None if running else stop_reason,error=error)

    metadata=dict(schema_version=3,started=wall(),monitor_pid=os.getpid(),max_hours=args.hours,
                  deadline=datetime.datetime.fromtimestamp(time.time()+args.hours*3600,ZONE).isoformat(timespec='seconds'),
                  target_sample_interval_ms=args.interval_ms,rotation_seconds=args.rotate_seconds,
                  game_detection='DarkSoulsIII.exe name and live process',process_rights='SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ',
                  scope='No target writes, injection, remote calls, input automation, active probes or integrity checks',
                  time_reference='t_ms/perf_counter relative to monitor start; wall=Asia/Shanghai; native phase and render FPS not assumed',
                  offsets_source='src/copydog.h and previously used read-only monitor; unavailable reads remain null',
                  animation_catalog='animation_frame_audit_20261112/parry_behavior_windows.csv, all decoded parry behaviors',
                  strict_success='Frontal riposte self30201/30202 + same peer70030, within 3s before next parry; no self HP loss through riposte+150ms',
                  collection_scope='All observed remote moves, weapons, handedness, parry tools and weapon parries; no restriction to three tested weapon IDs',
                  optimization_target='High reliability for parryable moves around displayed100ms; not a success guarantee',
                  network_sources=['Copydog damage_ack_half estimate, not standard ping',
                                   'Independent DS3ConnectionInfo-style passive UDP estimate, if ETW available',
                                   'DS3ConnectionInfo UI display capture, if tool is present'],
                  automatic_trace='SC3 resident PE data exports; double-read sequence/session; no remote execution or disk-RVA reuse',
                  network_assignment='Endpoints are not silently assigned to character slots',
                  zero_latency_baseline='Offline model only; missing/stale network samples are never zero',
                  trial_count_scopes='trial_counts covers the entire capture; current_process_trial_counts covers one attachment; current_module_trial_counts covers one observed resident DLL run. An absent scope is null.',
                  automatic_origin='A DLL request, an observed animation and a peer reaction are separate evidence. The input origin remains undetermined.')
    atomic_json(folder/'metadata.json',metadata)
    atomic_json(ROOT/'active_session.json',dict(folder=str(folder),monitor_pid=os.getpid(),started=metadata['started'],stop_file=str(folder/'STOP')))
    atomic_json(folder/'latest.json',status())
    event('monitor_started',hours=args.hours)
    print(json.dumps(dict(started=True,folder=str(folder),monitor_pid=os.getpid()),ensure_ascii=False),flush=True)
    try:
        while time.perf_counter()<deadline:
            now=time.perf_counter()
            if (folder/'STOP').exists():
                stop_reason='user_stop_file';break
            if now>=next_network:
                next_network=now+.2
                last_display=read_json_optional(folder/'display_latency/latest.json')
                network=read_json_optional(folder/'network/latest.json')
                helper=read_json_optional(folder/'network/status.json')
                snapshot=dict(helper=helper,data=network)
                if snapshot!=last_network:
                    net_id+=1;last_network=snapshot
                    network_log.write(json.dumps(dict(id=net_id,t_ms=round((now-start)*1000,3),wall=wall(),**snapshot),ensure_ascii=False)+'\n')
            if reader and not reader.alive():
                ended_pid=reader.pid
                ended_scope=trials.identity()
                ended_trials=trials.end_process('game_process_ended')
                reader.close();reader=None;game=None
                game_base=0;symbols={};previous={};latest_row=None
                trace_reader=None;last_sample=None;last_display=None;last_network=None
                state='waiting_for_game';error=None;next_find=now;next_modules=0
                emit_trials(ended_trials)
                logger.close()
                event('game_detached',pid=ended_pid,**ended_scope)
                atomic_json(folder/'latest.json',status())
            if reader is None and now>=next_find:
                next_find=now+1.
                previous_detection=state,(game or {}).get('pid')
                try:
                    reader,game,state=find_game_reader()
                    error=(game or {}).get('telemetry_error')
                    if reader:
                        game_base=game['base'];attach_count+=1;next_modules=0
                        emit_trials(trials.begin_process(reader.pid,attach_count))
                        event('game_attached',pid=reader.pid,base=game_base,path=game['path'])
                except OSError as ex:
                    game=None;state='game_process_scan_unavailable';error=str(ex)
                if previous_detection!=(state,game_process_status(reader,game,state)['game_pid']):
                    next_beat=now
            current_build=None
            if reader and now>=next_modules:
                next_modules=now+1.
                try:
                    module_list=modules(reader.pid)
                except OSError as ex:
                    event('module_enumeration_retry',error=str(ex))
                    symbols={};next_modules=now+.1
                else:
                    dll=next((m for m in module_list if m['name'].casefold()=='copydog.dll'),None)
                    current_symbols={}
                    if dll:
                        try:
                            current_symbols=telemetry_symbols(reader,dll)
                            current_build=resident_build(reader,current_symbols)
                        except Exception as ex:
                            event('copydog_telemetry_unavailable',error=str(ex))
                    old_scope=trials.identity()
                    key=(dll['base'],dll['path'].casefold(),dll['size']) if dll else None
                    emit_trials(trials.observe_module(key,current_build))
                    if old_scope['module_run']!=trials.identity()['module_run']:
                        trace_reader=None
                        if dll:
                            event('copydog_telemetry_symbols',module=dll,names=sorted(current_symbols))
                        else:
                            event('copydog_unloaded',**old_scope)
                    symbols=current_symbols
                    trace_address=symbols.get('CopydogAutoTrace')
                    if trace_address is not None and (trace_reader is None or trace_reader.address!=trace_address):
                        trace_reader=AutoTraceReader(trace_address,dll)
                    elif current_build is not None and trace_address is None:
                        trace_reader=None
            if reader:
                sample_begin=time.perf_counter();t_ms=round((sample_begin-start)*1000,3)
                world=reader.ptr(game_base+0x477fdb8)
                slots=reader.ptr(world+0x40) if world else 0
                me=reader.actor(reader.ptr(world+0x80),0) if world else None
                if me:
                    equiproot=reader.ptr(game_base+0x47572b8)
                    me['equipment']=reader.equipment(reader.ptr(equiproot+0x10)) if equiproot else me['equipment']
                if trace_reader is not None:
                    trace_events=trace_reader.poll(reader)
                    if trace_reader.state in ('ready','entry_changed_retry'):
                        if trials.trace_session_id is not None and trace_reader.session!=trials.trace_session_id and current_build is None:
                            symbols={};next_modules=now
                        emit_trials(trials.observe_trace_session(trace_reader.session,current_build))
                    for trace_event in trace_events:
                        trace_log.write(json.dumps(dict(**trace_event,t_ms=t_ms,wall=wall()) | trials.identity(),ensure_ascii=False,allow_nan=False)+'\n')
                    if trace_reader.state=='unavailable_or_unloaded':
                        symbols={};next_modules=min(next_modules,now+.1)
                automatic=automatic_snapshot(reader,symbols)
                automatic['build_id']=trials.build_id
                if trace_reader is not None:
                    automatic['trace']=trace_reader.snapshot(automatic['tick32'])
                me=context_actor(me,previous.get(0),t_ms,generations)
                others=[]
                for slot in range(1,6):
                    target=reader.actor(reader.ptr(slots+slot*0x38),slot) if slots else None
                    target=context_actor(target,previous.get(slot),t_ms,generations)
                    if target:
                        target['latency']=copydog_latency(automatic,slot)
                        target['passive_endpoint_assignment']='unknown'
                        if me and me['position'] and target['position'] and all(x is not None for x in me['position']+target['position']):
                            target['distance_to_self']=math.dist(me['position'],target['position'])
                            target['height_difference']=target['position'][1]-me['position'][1]
                    others.append(target)
                row=dict(t_ms=t_ms,wall=wall(),pid=reader.pid,self=me,others=others,automatic=automatic,
                         input=foreground_inputs(reader.pid),network_snapshot_id=net_id,
                         display_latency_snapshot_id=(last_display or {}).get('display_snapshot_id'),
                         sampling_interval_ms=round((sample_begin-last_sample)*1000,3) if last_sample else None)
                for actor in [me,*others]:
                    if actor:
                        old=previous.get(actor['slot'])
                        if not old or old['generation']!=actor['generation'] or old['animation']!=actor['animation']:
                            event('actor_animation',slot=actor['slot'],actor=actor['actor'],generation=actor['generation'],
                                  entity_id=actor['entity_id'],animation=actor['animation'],equipment=actor['equipment'],network_snapshot_id=net_id)
                emit_trials(trials.update(row))
                previous={x['slot']:x for x in [me,*others] if x}
                logger.write(row,sample_begin);samples+=1
                if me and me['animation'] is not None:
                    world_samples+=1;state='collecting';error=None
                else:
                    state='waiting_for_character'
                if last_sample:
                    intervals.append((sample_begin-last_sample)*1000)
                last_sample=sample_begin;latest_row=row
                durations.append((time.perf_counter()-sample_begin)*1000)
            if now>=next_beat:
                next_beat=now+2.
                if shutil.disk_usage(folder).free<512*1024*1024:
                    stop_reason='disk_space_below_512MiB';break
                logger.flush();events.flush();network_log.flush();trace_log.flush()
                atomic_json(folder/'latest.json',status())
                print(json.dumps(dict(state=state,samples=samples,**game_process_status(reader,game,state),
                                      elapsed_s=round(now-start,1),trials=trials.tracker.counts)),flush=True)
            spent=time.perf_counter()-now
            time.sleep(max(.001,(args.interval_ms/1000 if reader else .2)-spent))
    except KeyboardInterrupt:
        stop_reason='console_interrupt'
    except Exception as ex:
        error=repr(ex);stop_reason='monitor_error'
        event('monitor_error',error=error)
        raise
    finally:
        ended_scope=trials.identity()
        ended_trials=trials.end_process(stop_reason)
        if reader:
            reader.close();reader=None
        game=None;symbols={};latest_row=None
        (folder/'STOP').touch()
        emit_trials(ended_trials)
        logger.close();event('monitor_stopped',reason=stop_reason,samples=samples,**ended_scope)
        # This is a task-owned marker for the helper, not a game input/action.
        (folder/'network').mkdir(exist_ok=True)
        (folder/'network/STOP').touch()
        state='stopped'
        atomic_json(folder/'latest.json',status(False))
        atomic_json(folder/'summary.json',status(False))
        print(json.dumps(dict(stopped=True,reason=stop_reason,samples=samples)),flush=True)


if __name__=='__main__':
    main()
