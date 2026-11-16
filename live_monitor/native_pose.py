"""Observed DS3 native inputs. No action dispatch and no invented missing phase."""
import math
import struct

MAX_BONES = 256
RING_COUNT = 10
CLOCK_RECORDS = 0x20
CLOCK_RECORD = struct.Struct('<ifff')
CLOCK_INDICES = CLOCK_RECORDS + RING_COUNT * CLOCK_RECORD.size
CLOCK_CURSORS = struct.Struct('<ii')
CLOCK_SIZE = CLOCK_INDICES + CLOCK_CURSORS.size


def decode_clock(raw, visible_animation=None):
    if raw is None or len(raw) < CLOCK_SIZE:
        return None
    tail, head = CLOCK_CURSORS.unpack_from(raw, CLOCK_INDICES)
    if not (0 <= head < RING_COUNT and 0 <= tail < RING_COUNT):
        return None

    # The producer publishes an exclusive tail. Equal cursors leave no consumable slot.
    newest = None
    for offset in range((tail - head) % RING_COUNT):
        index = (tail - 1 - offset) % RING_COUNT
        animation, previous, current, duration = CLOCK_RECORD.unpack_from(
            raw, CLOCK_RECORDS + index * CLOCK_RECORD.size)
        if (animation < 0 or not all(math.isfinite(v) for v in (previous, current, duration))
                or not 0 < duration <= 600 or not 0 <= previous <= current <= duration + .1):
            continue
        sample = dict(animation=animation, previous_ms=previous*1000,
                      phase_ms=current*1000, duration_ms=duration*1000, index=index)
        if visible_animation is None or animation == visible_animation:
            return sample
        if newest is None:
            newest = sample
    return newest


def read_clock(reader, container, visible_animation=None):
    module = reader.ptr(container + 0x10) if container else 0
    if not module:
        return None
    raw = reader.bytes(module, CLOCK_SIZE)
    result = decode_clock(raw, visible_animation)
    if result is None:
        return None
    # The selected slot may be reused while the numeric cursors wrap back.
    slot = CLOCK_RECORDS + result['index'] * CLOCK_RECORD.size
    if reader.bytes(module + slot, CLOCK_RECORD.size) != raw[slot:slot + CLOCK_RECORD.size]:
        return None
    if reader.bytes(module + CLOCK_INDICES, CLOCK_CURSORS.size) != raw[CLOCK_INDICES:CLOCK_SIZE]:
        return None
    if reader.ptr(container + 0x10) != module:
        return None
    return result


def read_pose(reader, container):
    behavior = reader.ptr(container + 0x28) if container else 0
    owner = reader.ptr(behavior + 0x28) if behavior else 0
    if not owner:
        return None
    header = reader.bytes(owner + 0x38, 0x28)
    if not header:
        return None
    skeleton, local, local_count, local_capacity, model, count, capacity = struct.unpack('<QQIIQII', header)
    capacity &= 0x3fffffff
    local_capacity &= 0x3fffffff
    if (not skeleton or not local or not model or count != local_count
            or not 0 < count <= MAX_BONES or not count <= capacity <= MAX_BONES*4
            or not count <= local_capacity <= MAX_BONES*4):
        return None
    raw = reader.bytes(model, count*48)
    if raw is None:
        return None
    values = struct.unpack('<'+'f'*(count*12), raw)
    if not all(math.isfinite(v) and abs(v) < 1e6 for v in values):
        return None
    bones = [values[i:i+12] for i in range(0, len(values), 12)]
    for bone in bones:
        norm = sum(v*v for v in bone[4:8])
        if not .9 <= norm <= 1.1 or any(abs(v) > 100 for v in bone[8:11]):
            return None
    if reader.bytes(owner + 0x38, 0x28) != header or reader.ptr(behavior + 0x28) != owner:
        return None
    # This is a checked copy, not a game-thread lock or a native hurtbox claim.
    return dict(skeleton=skeleton, owner=owner, model_transforms=bones)
