"""Bounded read-only native pose/control observations; fields remain candidates."""
import argparse
import json
import math
from pathlib import Path
import struct
import time

from win_memory import Reader, game_pids, modules


def array(reader, address, stride, maximum):
    header = reader.bytes(address, 16)
    if header is None:
        return None
    pointer, count, capacity = struct.unpack('<QII', header)
    capacity &= 0x3fffffff
    if not 0 < count <= maximum or not count <= capacity <= maximum * 4:
        return None
    return reader.bytes(pointer, count * stride)


def sample(reader, base):
    world = reader.ptr(base + 0x477fdb8)
    actor = reader.ptr(world + 0x80) if world else 0
    container = reader.ptr(actor + 0x1f90) if actor else 0
    behavior = reader.ptr(container + 0x28) if container else 0
    pose_owner = reader.ptr(behavior + 0x28) if behavior else 0
    mediator = reader.ptr(behavior + 0x18) if behavior else 0
    if not actor or not pose_owner or not mediator:
        return None
    buffers = {}
    for offset in (0x40, 0x50):
        raw = array(reader, pose_owner + offset, 48, 256)
        if raw is None:
            continue
        values = struct.unpack('<' + 'f' * (len(raw) // 4), raw)
        if all(math.isfinite(v) and abs(v) < 1e6 for v in values):
            buffers[hex(offset)] = [values[i:i+12] for i in range(0, len(values), 12)]
    layers = []
    for offset in (0xd0, 0x2b0):
        skeleton = reader.ptr(mediator + offset)
        pointers = array(reader, skeleton + 0x20, 8, 64) if skeleton else None
        controls = []
        if pointers:
            for (pointer,) in struct.iter_unpack('<Q', pointers):
                raw = reader.bytes(pointer, 0x90)
                if raw:
                    controls.append(dict(address=pointer, raw=raw.hex(),
                                         time_candidate_s=struct.unpack_from('<f', raw, 0x10)[0],
                                         weight_candidate=struct.unpack_from('<f', raw, 0x14)[0],
                                         speed_candidate=struct.unpack_from('<f', raw, 0x50)[0]))
        layers.append(dict(offset=hex(offset), skeleton=skeleton, controls=controls))
    # Reject observations crossing a character/pose replacement.
    if reader.ptr(world + 0x80) != actor or reader.ptr(behavior + 0x28) != pose_owner:
        return None
    return dict(actor=reader.actor(actor, 0), pose_owner=pose_owner,
                buffers=buffers, layers=layers)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inventory', type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 0 < args.seconds <= 120:
        parser.error('seconds must be in (0,120]')
    pids = game_pids()
    if len(pids) != 1:
        raise RuntimeError('Expected exactly one DarkSoulsIII.exe')
    module = next(m for m in modules(pids[0]) if m['name'].casefold() == 'darksoulsiii.exe')
    reader = Reader(pids[0])
    inventory = json.loads(args.inventory.read_text()) if args.inventory else []
    candidates = [n for n in inventory if any(tag in n['type'] for tag in
                  ('FrpgDefaultAnimationControl', 'hkbClipGenerator', 'SprjChrTimeActModule'))][:64]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    rows = valid = 0
    try:
        with args.output.open('w', encoding='utf-8') as stream:
            while reader.alive() and time.perf_counter() - start < args.seconds:
                row = sample(reader, module['base'])
                if row:
                    valid += 1
                    row['clock_candidates'] = [dict(address=n['address'], type=n['type'],
                        raw=(reader.bytes(n['address'], 0x100) or b'').hex()) for n in candidates]
                stream.write(json.dumps(dict(t_ms=(time.perf_counter()-start)*1000,
                                             observation=row), allow_nan=False) + '\n')
                rows += 1
                time.sleep(.02)
    finally:
        reader.close()
    print(json.dumps(dict(rows=rows, valid=valid, output=str(args.output))))


if __name__ == '__main__':
    main()
