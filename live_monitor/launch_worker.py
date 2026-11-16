"""Start one capture worker with private standard streams and publish its Windows creation time."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys


def main():
    folder,role,program,*arguments=sys.argv[1:]
    if role not in ('monitor','network','display','action_names','ranged'):
        raise ValueError('Unknown capture worker')
    session=Path(folder)
    get_times=ctypes.WinDLL('kernel32',use_last_error=True).GetProcessTimes
    get_times.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
    get_times.restype=wintypes.BOOL
    with (session/(role+'.stdout.log')).open('wb') as stdout, (session/(role+'.stderr.log')).open('wb') as stderr:
        # close_fds restricts inheritance to these three standard streams on Windows.
        child=subprocess.Popen([program,*arguments],stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr,
                               close_fds=True,creationflags=subprocess.CREATE_NO_WINDOW)
        times=[wintypes.FILETIME() for _ in range(4)]
        # The handle returned by CreateProcess still identifies this exact child after a rapid exit.
        if not get_times(int(child._handle),*(ctypes.byref(stamp) for stamp in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        created=(times[0].dwHighDateTime<<32)|times[0].dwLowDateTime
        print(json.dumps(dict(pid=child.pid,started_filetime=str(created))))


if __name__=='__main__':
    main()
