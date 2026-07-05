import unittest

from qhw_admission import (
    AdmissionContext,
    AdmissionError,
    THREAD_SAFE,
    THREAD_USER,
)


class AdmissionContextTests(unittest.TestCase):
    def test_default_context(self):
        with AdmissionContext() as ctx:
            self.assertEqual(ctx.threading, THREAD_USER)
            self.assertEqual(ctx.last_error, "")

    def test_thread_safe_context(self):
        with AdmissionContext(threading=THREAD_SAFE) as ctx:
            self.assertEqual(ctx.threading, THREAD_SAFE)

    def test_invalid_threading(self):
        with self.assertRaises(AdmissionError):
            AdmissionContext(threading=99)

    def test_closed_context(self):
        ctx = AdmissionContext()
        ctx.close()
        with self.assertRaises(AdmissionError):
            _ = ctx.threading


if __name__ == "__main__":
    unittest.main()
