"""Exercise the shipping Windows launcher and readers against two ordinary child processes."""
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

from trial_labels import DEFAULT_PARRY_CSV, load_parry_ids

HERE = Path(__file__).resolve().parent
WINDOWS = Path(os.environ.get("SystemRoot", "C:/Windows"))
SHELL = WINDOWS / "System32/WindowsPowerShell/v1.0/powershell.exe"
CSC = WINDOWS / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
FIXTURE = r'''
using System;
using System.IO;
using System.Threading;
class Fixture {
    static string Arg(string[] args, string name) { return args[Array.IndexOf(args, name) + 1]; }
    static int Main(string[] args) {
        if (args[0]=="--game") { Console.ReadLine(); return int.Parse(args[1]); }
        var folder=Arg(args,"--output"); Directory.CreateDirectory(folder);
        var status=Path.Combine(folder,"status.json");
        File.WriteAllText(status,"{\"running\":true}");
        while (!File.Exists(Arg(args,"--stop-file"))) Thread.Sleep(20);
        Thread.Sleep(650);
        File.WriteAllText(status,"{\"running\":false}");
        return 0;
    }
}
'''
DISPLAY_FIXTURE = r'''
param([string]$Session, [string]$Deadline)
$folder=Join-Path $Session 'display_latency'
[IO.Directory]::CreateDirectory($folder) | Out-Null
[IO.File]::WriteAllText((Join-Path $folder 'sidecar_pid.json'), (@{pid=$PID} | ConvertTo-Json))
while (-not [IO.File]::Exists((Join-Path $Session 'STOP'))) { Start-Sleep -Milliseconds 20 }
[IO.File]::WriteAllText((Join-Path $folder 'latest.json'), '{"running":false}')
'''
READER_FIXTURE = '''
import ctypes as C
from ctypes import wintypes as W
import json, os
from pathlib import Path
import win_memory_impl as impl
impl.GAME_EXE_NAME = {name!r}
getinfo = impl.K.GetHandleInformation
getinfo.argtypes = [impl.HANDLE, C.POINTER(W.DWORD)]
original = impl.Reader.close
def checked_close(reader):
    h = reader.handle
    original(reader)
    if h:
        flags = W.DWORD()
        ok = getinfo(h,C.byref(flags))
        entry = dict(pid=reader.pid, closed=not ok and C.get_last_error()==6)
        with Path(__file__).with_name('closed-'+str(os.getpid())+'.jsonl').open('a') as f:
            f.write(json.dumps(entry)+'\\n')
impl.Reader.close = checked_close
from win_memory_impl import *
'''


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def wait_until(check, seconds=8):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            value=check()
            if value:
                return value
        except (OSError,ValueError):
            pass
        time.sleep(.03)
    raise AssertionError(f"Timed out waiting for {check}")


@unittest.skipUnless(os.name=="nt" and CSC.is_file(),"Windows process fixtures need .NET Framework csc")
class MonitorLifecycleTests(unittest.TestCase):
    def test_pid_switch_stop_and_stale_identity(self):
        with tempfile.TemporaryDirectory(prefix="copydog PID test ") as temp:
            root=Path(temp)/"project"/"live_monitor"
            root.mkdir(parents=True)
            source=root.parent/"src"
            source.mkdir()
            scripts=root.parent/"scripts"
            scripts.mkdir()
            shutil.copy(HERE.parent/"scripts/Toolchain.ps1",scripts)
            shutil.copy(HERE.parent/"src/parry_animations.h",source)
            for name in ("Start-Monitor.ps1","Stop-Monitor.ps1","session_control.ps1","launch_worker.py","monitor.py",
                         "capture_action_names.py","auto_trace.py","native_pose.py","ranged_watch.py",
                         "ranged_review.py","trial_labels.py"):
                shutil.copy(HERE/name,root)
            game=root/("CopydogFixture"+uuid.uuid4().hex[:12]+".exe")
            shutil.copy(HERE/"win_memory.py",root/"win_memory_impl.py")
            (root/"win_memory.py").write_text(READER_FIXTURE.format(name=game.name.lower()),encoding="utf-8")
            (root/"capture_display_latency.ps1").write_text(DISPLAY_FIXTURE,encoding="ascii")
            fixture=root/"Fixture.cs"
            fixture.write_text(FIXTURE,encoding="ascii")
            subprocess.run([str(CSC),"/nologo","/target:exe","/out:"+str(game),str(fixture)],
                           check=True,capture_output=True,timeout=20)
            helper=root/"latency_etw/bin/Release/net10.0-windows/LatencyEtw.exe"
            helper.parent.mkdir(parents=True)
            shutil.copy(game,helper)
            env=dict(os.environ,COPYDOG_PYTHON=sys.executable,PYTHONDONTWRITEBYTECODE="1")

            def control(script,*args,check=True):
                # The long-lived workers must not inherit these caller-owned stdout/stderr pipes.
                result=subprocess.run([str(SHELL),"-NoProfile","-ExecutionPolicy","Bypass","-File",
                                       str(root/script),*args],cwd=root,env=env,stdin=subprocess.DEVNULL,
                                       capture_output=True,text=True,errors="replace",timeout=30)
                if check:
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                return result

            games=[]
            session=None
            def fixture_game(exit_code):
                proc=subprocess.Popen([str(game),"--game",str(exit_code)],stdin=subprocess.PIPE,
                                      creationflags=subprocess.CREATE_NO_WINDOW)
                games.append(proc)
                return proc

            def finish_game(proc):
                if proc.poll() is None:
                    proc.stdin.write(b"exit\n");proc.stdin.flush()
                proc.wait(timeout=5)

            try:
                control("Start-Monitor.ps1","-Hours","0.04","-IntervalMs","12")
                info=read(root/"active_session.json")
                session=Path(info["folder"])
                manifest=read(session/"workers.json")
                self.assertEqual(set(manifest),{"monitor","network","display","action_names","ranged"})
                self.assertTrue(all(w.get("started_filetime") for w in manifest.values()))
                self.assertTrue(all(not info.get(role+"_startup_error") for role in manifest))
                dupe=control("Start-Monitor.ps1","-Hours","0.04",check=False)
                self.assertNotEqual(dupe.returncode,0)
                self.assertEqual(read(root/"active_session.json")["folder"],str(session))

                def states():
                    return [read(session/"latest.json"),read(session/"action_names_status.json")]
                def attached(proc):
                    return all(s["game_pid"]==proc.pid and s["samples"]>0 for s in states())
                def released(proc):
                    for name in ("monitor","action_names"):
                        records=(root/f"closed-{manifest[name]['pid']}.jsonl").read_text().splitlines()
                        if not any(json.loads(line)==dict(pid=proc.pid,closed=True) for line in records):
                            return False
                    return True

                first=fixture_game(259)
                wait_until(lambda: attached(first))
                finish_game(first)
                self.assertEqual(first.returncode,259)
                wait_until(lambda: all(s["state"]=="waiting_for_game" and s["game_pid"] is None for s in states()))
                wait_until(lambda: released(first))
                second=fixture_game(0)
                self.assertNotEqual(second.pid,first.pid)
                wait_until(lambda: attached(second))
                stopped=json.loads(control("Stop-Monitor.ps1","-WaitSeconds","12").stdout)
                self.assertTrue(stopped["stopped"])
                self.assertEqual(set(stopped["workers"].values()),{"exited"})
                self.assertIsNone(second.poll())
                self.assertTrue(released(second))
                self.assertTrue(all(not s["running"] and s["game_pid"] is None for s in states()))
                self.assertFalse(read(session/"network/status.json")["running"])
                self.assertFalse(read(session/"display_latency/latest.json")["running"])
                self.assertFalse(read(session/"ranged_status.json")["source_open"])
                self.assertEqual(read(session/"summary.json")["attachments"],2)
                streams=sorted(session.glob("samples_*.jsonl.gz"))
                captured=[]
                for stream in streams:
                    with gzip.open(stream,"rt",encoding="utf-8") as rows:
                        captured.append({json.loads(row)["pid"] for row in rows})
                self.assertEqual(captured,[{first.pid},{second.pid}])

                # A recorded PID can now name an unrelated live process.
                manifest["monitor"]={"pid":second.pid,"started_filetime":"1"}
                (session/"workers.json").write_text(json.dumps(manifest),encoding="utf-8")
                repeated=json.loads(control("Stop-Monitor.ps1","-WaitSeconds","2").stdout)
                self.assertTrue(repeated["stopped"])
                self.assertEqual(repeated["workers"]["monitor"],"pid_reused")
                self.assertIsNone(second.poll())
            finally:
                if (root/"active_session.json").is_file() and (
                    session is None or not (session/"STOP").is_file()):
                    control("Stop-Monitor.ps1",check=False)
                for proc in games:
                    finish_game(proc)
                    proc.stdin.close()

    @unittest.skipUnless(DEFAULT_PARRY_CSV.is_file(),"full local animation audit required for cross-check")
    def test_packaged_parry_ids_match_local_audit(self):
        import trial_labels
        from unittest.mock import patch
        with patch.object(trial_labels,"DEFAULT_PARRY_CSV",HERE/"missing-fixture.csv"):
            packaged=load_parry_ids()
        self.assertEqual(packaged,load_parry_ids(DEFAULT_PARRY_CSV))


if __name__=="__main__":
    unittest.main()
