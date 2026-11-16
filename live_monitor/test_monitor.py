import ctypes
from contextlib import ExitStack
import gzip
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import monitor
from monitor import RotatingSamples, ScopedTrials, atomic_json, elapsed32, copydog_latency, context_actor
from trial_labels import TrialTracker
from win_memory import Reader, modules
from read_samples import iter_samples
from collections import defaultdict


class MonitorTests(unittest.TestCase):
    def test_read_only_reader_own_known_memory(self):
        value=ctypes.c_int32(123456)
        reader=Reader(os.getpid())
        try:
            self.assertEqual(reader.value(ctypes.addressof(value),'<i'),123456)
            self.assertTrue(reader.alive())
            self.assertIsNone(reader.value(0,'<i'))
            self.assertIsNone(reader.value(1,'<i'))
        finally:
            reader.close()

    def test_current_process_module_enumeration(self):
        result=modules(os.getpid())
        self.assertTrue(result)
        self.assertTrue(any(m['name'].casefold().endswith('.exe') for m in result))

    def test_tick_wrap(self):
        self.assertEqual(elapsed32(10,0xfffffff0),26)

    def test_latency_zero_and_default_are_not_network_baseline(self):
        for value in (0,256):
            row=copydog_latency(dict(latency_raw=[value]*5,last_outgoing_hit_tick32=[0]*5,tick32=100),1)
            self.assertTrue(row['initial_or_ambiguous_value'])
            self.assertFalse(row['usable_for_zero_latency_comparison'])
            self.assertIsNone(row['sample_age_ms'])

    def test_latency_age_is_send_age_not_sample_age(self):
        row=copydog_latency(dict(latency_raw=[70]*5,last_outgoing_hit_tick32=[950]*5,tick32=1000),2)
        self.assertEqual(row['last_outgoing_hit_age_ms'],50)
        self.assertIsNone(row['sample_age_ms'])
        self.assertEqual(row['copydog_damage_ack_half_ms'],70)

    def test_generation_changes_on_reused_address(self):
        generations=defaultdict(int)
        first=context_actor(dict(slot=1,actor=100,entity_id=50,animation=1),None,0,generations)
        second=context_actor(dict(slot=1,actor=100,entity_id=51,animation=1),first,20,generations)
        self.assertEqual(second['generation'],first['generation']+1)
        self.assertEqual(second['observed_animation_age_ms'],0)

    def test_animation_clock_is_observation_only(self):
        generations=defaultdict(int)
        first=context_actor(dict(slot=1,actor=100,entity_id=50,animation=1),None,10,generations)
        second=context_actor(dict(slot=1,actor=100,entity_id=50,animation=1),first,30,generations)
        self.assertEqual(second['observed_animation_age_ms'],20)
        self.assertEqual(second['animation_origin_quality'],'first_observation_not_native_phase')

    def test_rotation_preserves_closed_gzip_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);writer=RotatingSamples(root,1)
            writer.write(dict(n=1),0);writer.write(dict(n=2),2);writer.close()
            files=sorted(root.glob('*.jsonl.gz'))
            self.assertEqual(len(files),2)
            values=[]
            for path in files:
                with gzip.open(path,'rt',encoding='utf-8') as stream:
                    values.extend(json.loads(line) for line in stream)
            self.assertEqual(values,[dict(n=1),dict(n=2)])

    def test_atomic_status_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'latest.json'
            atomic_json(path,dict(state='waiting'));atomic_json(path,dict(state='collecting'))
            self.assertEqual(json.loads(path.read_text(encoding='utf-8'))['state'],'collecting')
            self.assertFalse(path.with_suffix('.json.tmp').exists())

    def test_read_samples_while_gzip_is_still_open(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);writer=RotatingSamples(root)
            try:
                writer.write(dict(t_ms=10),0);writer.flush()
                self.assertEqual(list(iter_samples(root)),[dict(t_ms=10)])
                writer.write(dict(t_ms=20),.1);writer.flush()
                self.assertEqual(list(iter_samples(root)),[dict(t_ms=10),dict(t_ms=20)])
            finally:
                writer.close()

    def test_trial_counts_for_process_without_dll_and_dll_replacement(self):
        trials=ScopedTrials(TrialTracker({48032100}))
        key=(0x1000,'copydog.dll',0x3000)
        def sample(t,animation=0,hp=100):
            trace=dict(state='ready',session_id=trials.trace_session_id) if trials.trace_session_id else None
            return trials.update(dict(pid=75,t_ms=t,self=dict(actor=4,animation=animation,hp=hp),
                                      others=[],automatic=dict(build_id=trials.build_id,trace=trace)))

        trials.begin_process(75,1)
        sample(0)
        sample(5,48032100)
        failed,=sample(10,48032100,99)
        self.assertIsNone(failed['module_run'])
        self.assertIsNone(trials.status()['current_module_trial_counts'])
        self.assertEqual(trials.status()['current_process_trial_counts']['failed'],1)
        trials.observe_module(key,None)
        first_run=trials.identity()['module_run']
        trials.observe_module(key,'A')
        trials.observe_module(key,None)
        trials.observe_trace_session(9)
        self.assertEqual(trials.identity()['module_run'],first_run)
        self.assertEqual(trials.identity()['build_id'],'A')
        self.assertEqual(trials.status()['current_module_trial_counts']['failed'],0)
        sample(20)
        sample(25,48032100)
        closed,=trials.observe_module(key,'B')
        self.assertEqual((closed['build_id'],closed['trace_session_id'],closed['module_run']),('A',9,first_run))
        self.assertEqual(closed['reason'],'copydog_module_changed')
        self.assertIsNone(trials.identity()['trace_session_id'])
        self.assertEqual(trials.status()['current_module_trial_counts']['incomplete'],0)
        second_run=trials.identity()['module_run']
        sample(30)
        sample(35,48032100)
        unloaded,=trials.observe_module(None,None)
        self.assertEqual((unloaded['build_id'],unloaded['module_run']),('B',second_run))
        self.assertEqual(trials.status()['current_process_trial_counts'],dict(success=0,failed=1,incomplete=2))
        self.assertIsNone(trials.status()['current_module_trial_counts'])
        trials.observe_module(key,'B')
        self.assertGreater(trials.identity()['module_run'],second_run)
        previous=trials.status()
        trials.end_process('game_process_ended')
        trials.begin_process(75,2)
        self.assertEqual(trials.status()['current_process_trial_counts'],dict(success=0,failed=0,incomplete=0))
        self.assertEqual(previous['trial_counts'],trials.status()['trial_counts'])

    def test_later_identity_read_cannot_claim_earlier_trial(self):
        trials=ScopedTrials(TrialTracker({48032100}))
        key=(0x1000,'copydog.dll',0x3000)
        trials.begin_process(75,1)
        trials.observe_module(key,None)
        for t,anim in ((0,0),(5,48032100)):
            trials.update(dict(t_ms=t,pid=75,self=dict(actor=8,animation=anim,hp=100),others=[]))
        trials.observe_module(key,'A')
        trials.observe_trace_session(9)
        old_run=trials.identity()['module_run']
        result,=trials.observe_trace_session(10)
        self.assertIsNone(result['trace_session_id'])
        self.assertIsNone(result['start_copydog_build_id'])
        self.assertEqual(result['module_run'],old_run)
        self.assertIsNone(trials.identity()['build_id'])
        self.assertEqual(trials.identity()['trace_session_id'],10)
        new_run=trials.identity()['module_run']
        trials.observe_module(key,'B')
        self.assertEqual(trials.identity()['module_run'],new_run)
        self.assertEqual(trials.status()['current_module_trial_counts']['incomplete'],0)

    def test_capture_settles_trials_at_trace_change_process_exit_and_stop(self):
        frames=[(101,11,0),(101,11,48032100),(101,12,0),(101,12,48032100),
                (None,None,0),(202,11,0),(202,11,48032100)]
        step=0
        observed_status=[]
        closed_pids=[]
        class FakeReader:
            read_errors=0
            def __init__(self,pid):
                self.pid=pid
            def alive(self):
                return self.pid==frames[step][0]
            def close(self):
                closed_pids.append(self.pid)
            def ptr(self,address):
                return {0x10000+0x477fdb8:0x5000,0x5000+0x80:0x6000}.get(address,0)
            def actor(self,address,slot):
                if address:
                    return dict(slot=slot,actor=address,entity_id=10,animation=frames[step][2],hp=100,
                                position=[0,0,0],equipment={})
        class FakeTrace:
            def __init__(self,address,dll):
                self.address=address
                self.session=None
                self.state='awaiting_header'
            def poll(self,reader):
                session=frames[step][1]
                new=session!=self.session
                self.session=session
                self.state='ready'
                return [dict(kind='auto_trace_session',session_id=session)] if new else []
            def snapshot(self,tick32):
                return dict(state=self.state,session_id=self.session)
        def find_game():
            pid=frames[step][0]
            if pid is None:
                return None,None,'waiting_for_game'
            return FakeReader(pid),dict(base=0x10000,path='fixture.exe'),'collecting'
        def module_list(pid):
            if step==1:
                raise OSError('fixture transient enumeration failure')
            return [dict(name='copydog.dll',path='fixture/copydog.dll',base=0x9000,size=0x1000)]
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root=Path(directory)
            folder=root/'capture'
            folder.mkdir()
            def advance(wait):
                nonlocal step
                step+=1
                if step==len(frames):
                    (folder/'STOP').touch()
            def record_status(path,data):
                atomic_json(path,data)
                if path.name=='latest.json':
                    observed_status.append(data)
            clock=SimpleNamespace(perf_counter=lambda:step*1.01,time=lambda:1700000000,sleep=advance)
            stack.enter_context(patch.multiple(monitor,ROOT=root,time=clock,atomic_json=record_status,
                      find_game_reader=find_game,modules=module_list,telemetry_symbols=lambda *_:{'CopydogAutoTrace':0x9100},
                      resident_build=lambda *_:'fixture-build',AutoTraceReader=FakeTrace,
                      automatic_snapshot=lambda *_:dict(tick32=step*1010),foreground_inputs=lambda *_:{},
                      TrialTracker=lambda:TrialTracker({48032100})))
            stack.enter_context(patch('builtins.print'))
            with ExitStack() as cleanup:
                monitor.run_capture(SimpleNamespace(hours=1,interval_ms=5,rotate_seconds=60),folder,cleanup)
            events=[json.loads(line) for line in (folder/'events.jsonl').read_text(encoding='utf-8').splitlines()]
            results=[e for e in events if e['kind']=='trial_result']
            self.assertEqual([(e['trial_id'],e['game_pid'],e['game_attachment'],e['module_run'],e['trace_session_id'],e['reason'])
                              for e in results],[(1,101,1,1,11,'copydog_run_session_changed'),
                                                 (2,101,1,2,12,'game_process_ended'),(3,202,2,3,11,'user_stop_file')])
            for kind in ('parry_animation_start','trial_result'):
                self.assertTrue(all(e['build_id']=='fixture-build' for e in events if e['kind']==kind))
            detached,=[e for e in events if e['kind']=='game_detached']
            self.assertEqual((detached['pid'],detached['game_pid'],detached['trace_session_id']),(101,101,12))
            self.assertTrue(any(s['game_pid']==101 and s['trace_session_id']==12 and
                                s['current_process_trial_counts']['incomplete']==1 and
                                s['current_module_trial_counts']['incomplete']==0 for s in observed_status))
            self.assertTrue(any(s['state']=='waiting_for_game' and s['game_pid'] is None and
                                s['trial_counts']['incomplete']==2 and s['current_process_trial_counts'] is None and
                                s['current_module_trial_counts'] is None for s in observed_status))
            summary=json.loads((folder/'summary.json').read_text(encoding='utf-8'))
            self.assertEqual(summary['trial_counts'],dict(success=0,failed=0,incomplete=3))
            self.assertIsNone(summary['current_process_trial_counts'])
            self.assertIsNone(summary['current_module_trial_counts'])
            self.assertEqual(closed_pids,[101,202])
            self.assertTrue((folder/'network/STOP').exists())


if __name__=='__main__':
    unittest.main()
