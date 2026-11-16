"""Stream complete rows from sealed or still-open gzip capture parts."""
import argparse
import json
from pathlib import Path
import zlib


def iter_samples(folder):
    for path in sorted(Path(folder).glob('samples_*.jsonl.gz')):
        decoder=zlib.decompressobj(16+zlib.MAX_WBITS)
        pending=b''
        with path.open('rb') as stream:
            while chunk:=stream.read(65536):
                pending+=decoder.decompress(chunk)
                rows=pending.split(b'\n');pending=rows.pop()
                for row in rows:
                    if row:
                        yield json.loads(row)
        # A live part may have neither its final row nor gzip footer yet.
        # Only newline-terminated rows are exposed, without calling it corrupt.


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path)
    args=parser.parse_args()
    count=0;last=None
    for row in iter_samples(args.folder):
        count+=1;last=row.get('t_ms')
    print(json.dumps(dict(rows=count,last_t_ms=last)))
