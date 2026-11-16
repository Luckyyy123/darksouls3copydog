import os
import struct
import unittest

if os.name == 'nt':
    from win_memory import SYMBOLS, automatic_snapshot


class ExportBytes:
    def __init__(self, data):
        self.data = data

    def value(self, address, fmt):
        if address not in self.data:
            return None
        fields = struct.unpack(fmt, self.data[address])
        return fields[0] if len(fields) == 1 else fields


@unittest.skipUnless(os.name == 'nt', 'Windows monitor')
class GroundBlastTelemetry(unittest.TestCase):
    def test_previous_dll_exposes_only_nine_projectile_counters(self):
        row = automatic_snapshot(ExportBytes({1: struct.pack('<9Q', *range(1, 10))}),
                                 {'CopydogBulletDiagnostics': 1})
        self.assertEqual(9, row['bullet_diagnostics']['published'])
        self.assertNotIn('ground_blast', row)

    def test_empty_and_partial_exports(self):
        export = {'CopydogAreaDiagnostics': 1, 'CopydogLastAreaThreat': 2}
        self.assertTrue(set(export) <= SYMBOLS)
        for detail in (None, struct.pack('<Q', 0)):
            memory = {1: bytes(32)}
            if detail is not None:
                memory[2] = detail
            row = automatic_snapshot(ExportBytes(memory), export)
            self.assertEqual(dict(estimated_floor=0, anchor_fuse=0, observed_area=0, consumed=0),
                             row['ground_blast'])
        row = automatic_snapshot(ExportBytes({2: struct.pack('<Q', 1)}), export)
        self.assertNotIn('ground_blast', row)

    def test_sources_id_and_milliseconds(self):
        for raw, label, bullet_id, eta in ((1, 'estimated_floor', 860, 18),
                                            (2, 'anchor_fuse', 705, 99),
                                            (3, 'observed_area', 861, 0),
                                            (4, 'observed_age_unknown', 704, 0),
                                            (17, None, 0xf1234567, 0xffff)):
            with self.subTest(source=raw, eta=eta):
                data = {1: struct.pack('<4Q', 2, 7, 11, 13),
                        2: struct.pack('<Q', (bullet_id << 32) | (raw << 16) | eta)}
                row = automatic_snapshot(ExportBytes(data),
                                         {'CopydogAreaDiagnostics': 1, 'CopydogLastAreaThreat': 2})
                self.assertEqual(dict(estimated_floor=2, anchor_fuse=7, observed_area=11, consumed=13,
                                      last_id=bullet_id, eta_ms=eta, source=label), row['ground_blast'])


if __name__ == '__main__':
    unittest.main()
