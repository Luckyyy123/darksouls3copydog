import math
import struct
import sys
import unittest
from unittest.mock import Mock, call

from native_pose import decode_clock, read_clock


def record(animation=164034000, previous=.2, current=.21, duration=1.8):
    return animation, previous, current, duration


def batch(head, *records):
    data = bytearray(0xc8)
    for offset, values in enumerate(records):
        index = (head + offset) % 10
        struct.pack_into('<ifff', data, 0x20 + index * 16, *values)
    struct.pack_into('<ii', data, 0xc0, (head + len(records)) % 10, head)
    return data


class ClockTests(unittest.TestCase):
    def test_single_record_at_wrap(self):
        result = decode_clock(batch(9, record()))
        self.assertEqual(result['index'], 9)
        self.assertEqual(result['animation'], 164034000)
        self.assertAlmostEqual(result['phase_ms'], 210, places=4)

    def test_zero_phase_is_valid(self):
        self.assertEqual(decode_clock(batch(3, record(previous=0, current=0)))['phase_ms'], 0)

    def test_single_batch_of_two_through_nine_records(self):
        for head in range(10):
            for length in range(2, 10):
                with self.subTest(head=head, length=length):
                    raw = batch(head, *(record(animation=i) for i in range(length)))
                    result = decode_clock(raw)
                    self.assertEqual((result['index'], result['animation']),
                                     ((head + length - 1) % 10, length - 1))
                    self.assertEqual(decode_clock(raw, 0)['index'], head)

    def test_empty_batch_can_contain_old_records(self):
        for head in range(10):
            raw = batch(head, record())
            struct.pack_into('<ii', raw, 0xc0, head, head)
            self.assertIsNone(decode_clock(raw))

    def test_visible_animation_prefers_newest_valid_match(self):
        raw = batch(8, record(14), record(14, .25, .3), record(14, .5, .4), record(26))
        result = decode_clock(raw, 14)
        self.assertEqual(result['index'], 9)
        self.assertAlmostEqual(result['previous_ms'], 250, places=4)
        self.assertAlmostEqual(result['phase_ms'], 300, places=4)
        # Slot 2 is the exclusive tail and still contains a valid older record.
        struct.pack_into('<ifff', raw, 0x20 + 2 * 16, *record(41))
        self.assertEqual(decode_clock(raw, 41)['index'], 1)
        self.assertEqual(decode_clock(raw)['index'], 1)

    def test_skips_malformed_newest_record_and_does_not_splice_slots(self):
        raw = batch(0, record(70), record(71, duration=0))
        self.assertEqual(decode_clock(raw, 71)['animation'], 70)
        raw = batch(0, record(70, duration=0), record(71, current=math.nan))
        self.assertIsNone(decode_clock(raw, 70))

    def test_invalid_input_and_indices(self):
        self.assertIsNone(decode_clock(None))
        self.assertIsNone(decode_clock(bytes(0xc7)))
        for tail, head in ((-1, 9), (0, -1), (10, 9), (0, 10)):
            raw = batch(9, record())
            struct.pack_into('<ii', raw, 0xc0, tail, head)
            self.assertIsNone(decode_clock(raw))

    def test_invalid_or_reversed_phase(self):
        for kwargs in ({'current': math.nan}, {'previous': -1}, {'current': math.inf},
                       {'duration': 0}, {'duration': 601}, {'current': 4}, {'animation': -1},
                       {'previous': .4, 'current': .39}):
            with self.subTest(kwargs=kwargs):
                self.assertIsNone(decode_clock(batch(9, record(**kwargs))))

    def test_memory_snapshot_rechecks_slot_cursors_and_clock_owner(self):
        container, module = 0x1000, 0x2000
        raw = batch(9, record(53), record(54))
        slot_offset = 0x20 + 9 * 16
        slot = raw[slot_offset:slot_offset + 16]
        cursors = raw[0xc0:0xc8]
        for reread, indices, owner in (
                (slot, cursors, module),
                (struct.pack('<ifff', *record(55)), cursors, module),
                (struct.pack('<ifff', *record(53, .25, .3)), cursors, module),
                (slot, struct.pack('<ii', 2, 9), module),
                (slot, struct.pack('<ii', 1, 0), module),
                (slot, cursors, module + 8)):
            with self.subTest(reread=reread, indices=indices, owner=owner):
                reader = Mock()
                reader.ptr.side_effect = [module, owner]
                reader.bytes.side_effect = [raw, reread, indices]
                result = read_clock(reader, container, 53)
                if reread == slot and indices == cursors and owner == module:
                    self.assertEqual(result['animation'], 53)
                    self.assertEqual(reader.bytes.call_args_list, [
                        call(module, 0xc8), call(module + slot_offset, 16), call(module + 0xc0, 8)])
                    self.assertEqual(reader.ptr.call_args_list, [call(container + 0x10)] * 2)
                else:
                    self.assertIsNone(result)

    @unittest.skipUnless(sys.platform == 'win32', 'Windows monitor reader')
    def test_actor_uses_one_visible_animation_sample(self):
        from win_memory import Reader

        actor, container, action, timeact = 0x10000, 0x20000, 0x30000, 0x40000
        roots = bytearray(0x88)
        struct.pack_into('<Q', roots, 0x80, action)
        raw = batch(0, record(53), record(54))
        blocks = {(container, 0x88): roots, (timeact, 0xc8): raw,
                  (timeact + 0x20, 16): raw[0x20:0x30], (timeact + 0xc0, 8): raw[0xc0:0xc8]}
        reader = Mock(spec=Reader)
        pointers = {actor + 0x1f90: container, container + 0x10: timeact}
        reader.ptr.side_effect = lambda addr: pointers.get(addr, 0)
        reader.value.side_effect = lambda addr, fmt: 53 if addr == action + 0xc8 else None
        reader.bytes.side_effect = lambda addr, size: blocks.get((addr, size))
        reader.equipment.return_value = None

        snapshot = Reader.actor(reader, actor, 1)
        self.assertEqual(snapshot['animation'], 53)
        self.assertEqual(snapshot['native_animation_clock']['animation'], 53)
        self.assertEqual(reader.value.call_args_list.count(call(action + 0xc8, '<i')), 1)


if __name__ == '__main__':
    unittest.main()
