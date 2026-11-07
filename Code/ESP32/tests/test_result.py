import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from esp32_uart_test import verify


class ResultTests(unittest.TestCase):
    def setUp(self):
        self.good = dict(test="esp32_stm32_uart_v1", result="PASS", baud=1000000, target=1000,
                         sent=1000, matched=1000, timeouts=0, crc_or_frame_errors=0,
                         bad_status=0, unexpected_seq=0, uart_faults=0, write_errors=0,
                         stm_resets=0, stm_uart_errors=0, stm_uart_errors_start=0,
                         stm_tx_drops=0, stm_tx_drops_start=0)

    def test_complete_pass(self):
        self.assertTrue(verify(self.good))

    def test_failures_cannot_be_hidden_by_pass_label(self):
        for k in ("timeouts", "crc_or_frame_errors", "bad_status", "unexpected_seq", "uart_faults",
                  "write_errors", "stm_resets", "stm_uart_errors", "stm_tx_drops"):
            with self.subTest(field=k):
                self.assertFalse(verify(self.good | {k: 1}))
        self.assertFalse(verify(self.good | {"matched": 999}))
        self.assertFalse(verify(self.good | {"result": "RUNNING"}))

    def test_missing_fields_rejected(self):
        for k in self.good:
            partial = dict(self.good)
            del partial[k]
            with self.subTest(field=k):
                self.assertFalse(verify(partial))


if __name__ == "__main__":
    unittest.main()
