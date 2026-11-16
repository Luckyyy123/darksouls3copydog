import datetime
import json
from contextlib import ExitStack
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import capture_action_names
import monitor
from parry_status import game_status_line
import win_memory


class GamePresenceTests(unittest.TestCase):
    def test_no_unique_pid_never_opens_telemetry(self):
        with patch.object(win_memory,'game_pids',side_effect=[[],[17,22]]), patch.object(win_memory,'Reader') as open_reader:
            empty=win_memory.find_game_reader()
            ambiguous=win_memory.find_game_reader()
        open_reader.assert_not_called()
        self.assertEqual(win_memory.game_process_status(*empty),dict(game_pid=None,game_running=False,telemetry_attached=False))
        self.assertEqual(win_memory.game_process_status(*ambiguous),dict(game_pid=None,game_running=True,telemetry_attached=False))
        self.assertIn('多个',game_status_line(dict(state=ambiguous[2],**win_memory.game_process_status(*ambiguous))))

    def test_process_exits_during_module_lookup(self):
        reader=Mock(pid=44)
        reader.alive.return_value=False
        with patch.object(win_memory,'game_pids',return_value=[44]), patch.object(win_memory,'Reader',return_value=reader), \
                patch.object(win_memory,'modules',return_value=[]):
            self.assertEqual(win_memory.find_game_reader(),(None,None,'waiting_for_game'))
        reader.close.assert_called_once_with()

    def test_failed_snapshot_is_distinct_from_an_empty_snapshot(self):
        kernel=Mock()
        kernel.CreateToolhelp32Snapshot.return_value=win_memory.INVALID_HANDLE
        with patch.object(win_memory,'K',kernel),patch.object(win_memory.C,'get_last_error',return_value=5):
            self.assertEqual(win_memory.game_pids(),[])
            with self.assertRaises(OSError):
                win_memory.find_game_reader()
        self.assertIsNone(win_memory.game_process_status(None,None,'game_process_scan_unavailable')['game_running'])
        self.assertIn('旧记录',game_status_line(dict(state='waiting_for_game',game_pid=None)))

    def run_fake_capture(self,sidecar):
        # At 2 the first process exits without ever granting telemetry. The next
        # process becomes readable after its EXE module is enumerated.
        frames=[(41,'denied'),(41,'denied'),(None,'absent'),(52,'module'),
                (52,'module_error'),(52,'ready'),(None,'absent'),(63,'ready'),(None,'scan_error')]
        step=0;statuses=[];closed=[]
        class Reader:
            read_errors=0
            def __init__(self,pid):
                self.pid=pid
                if frames[step][1]=='denied':
                    raise PermissionError('fixture access denied')
            def alive(self):
                return frames[step][0]==self.pid
            def close(self):
                closed.append(self.pid)
            def ptr(self,address):
                return 0
            def actor(self,address,slot):
                return None
        def modules(pid):
            mode=frames[step][1]
            if mode=='module_error':
                raise OSError('fixture module enumeration unavailable')
            if mode=='module':
                return []
            return [dict(name='DarkSoulsIII.exe',base=0x10000,size=0x900,path='fixture.exe')]
        def discover(*,require_snapshot=False):
            if frames[step][1]=='scan_error':
                raise OSError('fixture process snapshot unavailable')
            return [frames[step][0]] if frames[step][0] else []
        save=monitor.atomic_json
        status_file='action_names_status.json' if sidecar else 'latest.json'
        with tempfile.TemporaryDirectory() as directory, ExitStack() as patches:
            root=Path(directory);folder=root/'capture';folder.mkdir()
            def record(path,data):
                save(path,data)
                if path.name==status_file:
                    statuses.append((step,data))
            def advance(_):
                nonlocal step
                step+=1
                if step==len(frames):
                    (folder/'STOP').touch()
            clock=SimpleNamespace(perf_counter=lambda:step*1.01,perf_counter_ns=lambda:step*1010000000,
                                  time=lambda:1700000000,sleep=advance)
            args=SimpleNamespace(hours=1,interval_ms=5,rotate_seconds=60)
            patches.enter_context(patch.multiple(win_memory,Reader=Reader,modules=modules,
                        game_pids=discover,
                        K=SimpleNamespace(GetTickCount64=lambda:step*1010)))
            patches.enter_context(patch.multiple(monitor,ROOT=root,time=clock,atomic_json=record,
                        modules=modules,automatic_snapshot=lambda *_:dict(tick32=step*1010),foreground_inputs=lambda *_:{}))
            patches.enter_context(patch.object(capture_action_names,'time',clock))
            patches.enter_context(patch('builtins.print'))
            with ExitStack() as cleanup:
                if sidecar:
                    deadline=datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(hours=1)
                    capture_action_names.run_capture(args,folder,dict(deadline=deadline.isoformat()),deadline,cleanup)
                else:
                    monitor.run_capture(args,folder,cleanup)
            latest=json.loads((folder/status_file).read_text(encoding='utf-8'))
            self.assertFalse(latest['running'])
            self.assertIsNone(latest['game_pid'])
            self.assertIsNone(latest['game_running'])
            self.assertFalse(latest['telemetry_attached'])
            self.assertEqual(closed,[52,52,52,63])
        return statuses

    def test_pending_telemetry_exit_and_next_pid_in_both_status_files(self):
        for sidecar in (False,True):
            with self.subTest(sidecar=sidecar):
                statuses=self.run_fake_capture(sidecar)
                values={(i,s['game_pid'],s['game_running'],s['telemetry_attached'],s['state']) for i,s in statuses}
                for expected in [(0,41,True,False,'read_access_unavailable'),(2,None,False,False,'waiting_for_game'),
                                 (3,52,True,False,'waiting_for_game_module'),(5,52,True,True,'waiting_for_character'),
                                 (6,None,False,False,'waiting_for_game'),(7,63,True,True,'waiting_for_character'),
                                 (8,None,None,False,'game_process_scan_unavailable')]:
                    self.assertIn(expected,values)
                for _,s in statuses:
                    if s['state']=='read_access_unavailable':
                        self.assertIn('denied',s['error'])
                        self.assertIn('游戏已启动，PID 41',game_status_line(s))
                    if s['state']=='waiting_for_game_module':
                        self.assertIn('游戏主模块',game_status_line(s))


if __name__=='__main__':
    unittest.main()
