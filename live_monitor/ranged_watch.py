"""Annotate the existing action-name journal for later review. No process or input access."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import time

from ranged_review import RangedReview

ROOT = Path(__file__).resolve().parent
SOURCE = "action_names.jsonl"


def clock():
    return datetime.now(timezone.utc)


def atomic(path, obj):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    os.replace(tmp, path)


def complete_rows(stream, limit=2048):
    for _ in range(limit):
        start = stream.tell()
        line = stream.readline()
        if not line.endswith(b"\n"):
            stream.seek(start)
            return
        try:
            value = json.loads(line)
        except (UnicodeError, ValueError):
            value = None
        yield start, value


def watch(session):
    import msvcrt
    session = session.resolve()
    if session.parent != (ROOT/"sessions").resolve():
        raise ValueError("Expected one existing monitor session")
    meta = json.loads((session/"metadata.json").read_text(encoding="utf-8-sig"))
    deadline = datetime.fromisoformat(meta["deadline"])
    if deadline.tzinfo is None or clock()>=deadline or (session/"STOP").exists():
        raise ValueError("Session expired or already stopped")
    lock = (session/"ranged_watch.lock").open("a+b")
    lock.seek(0)
    try:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        lock.close()
        raise
    with lock:
        follow_journal(session,meta,deadline)


def follow_journal(session,meta,deadline):
    status_path = session/"ranged_status.json"
    out = session/"ranged_review.jsonl"
    count = Counter()
    high = -1
    if out.exists():
        with out.open("rb") as old:
            for raw in old:
                record = json.loads(raw)
                high = max(high, record["input_offset"])
                count[record.get("stage","unknown")] += 1
    consumed = errors = cursor = 0
    stop = None
    infile = None
    parser = RangedReview()
    def status(running):
        atomic(status_path, dict(running=running,pid=os.getpid(),updated=clock().isoformat(),
               deadline=meta["deadline"],source=SOURCE,source_open=infile is not None,
               source_lines_read=consumed,source_byte_offset=cursor,invalid_rows=errors,
               output_events=sum(count.values()),by_stage=count,stop_reason=stop,
               evidence="Action-name observations only; no verified projectile spawn, spell ID or hit source."))
    try:
        with out.open("a", encoding="utf-8", buffering=1) as dest:
            next_status = 0.
            while True:
                if not infile and (session/SOURCE).is_file():
                    infile = (session/SOURCE).open("rb")
                if infile:
                    for offset,row in complete_rows(infile):
                        consumed+=1
                        if not isinstance(row,dict):
                            errors+=1
                            continue
                        obs=parser.consume(row,SOURCE)
                        if obs is not None and offset>high:
                            rec={**obs, "input_offset":offset}
                            if isinstance(row.get("wall"),str):
                                rec["wall"]=row["wall"]
                            if type(row.get("perf_counter_ns")) is int:
                                rec["recorded_perf_counter_ns"]=row["perf_counter_ns"]
                            dest.write(json.dumps(rec,ensure_ascii=False,allow_nan=False)+"\n")
                            count[rec.get("stage","unknown")]+=1
                            high=offset
                    cursor=infile.tell()
                if (session/"STOP").exists():
                    stop="session_stop_file"
                elif clock()>=deadline:
                    stop="deadline"
                elif time.monotonic()>=next_status and shutil.disk_usage(session).free<512*1024*1024:
                    stop="low_disk"
                if stop:
                    break
                if time.monotonic()>=next_status:
                    status(True)
                    next_status=time.monotonic()+2
                time.sleep(.25)
    except BaseException as exc:
        stop=type(exc).__name__
        raise
    finally:
        if infile:
            infile.close()
            infile=None
        status(False)


if __name__=="__main__":
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--session",type=Path,required=True)
    watch(cli.parse_args().session)
