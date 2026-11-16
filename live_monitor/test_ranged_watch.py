import io
import unittest

from ranged_watch import complete_rows


class JournalTests(unittest.TestCase):
    def test_partial_record_is_not_lost_or_counted_twice(self):
        f=io.BytesIO(b'{"n":1}\n{"n":')
        self.assertEqual(list(complete_rows(f)),[(0,{"n":1})])
        pending=f.tell()
        f.seek(0,2)
        f.write(b'2}\n')
        f.seek(pending)
        self.assertEqual(list(complete_rows(f)),[(8,{"n":2})])
        self.assertEqual(list(complete_rows(f)),[])

    def test_bad_record_does_not_discard_the_next_observation(self):
        f=io.BytesIO(b'oops\n{"n":3}\n')
        self.assertEqual(list(complete_rows(f)),[(0,None),(5,{"n":3})])
