"""Protocol and bounded-reader tests; no real port is opened."""
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import serial_monitor as monitor


def frame(timestamp=100, raw=120):
    return (f"S,{timestamp},1,0,0,0\r\n" + "".join(
        f"{ring},{raw},100,{raw - 100},240,1\r\n" for ring in range(1, 11)))


class ParserTests(unittest.TestCase):
    def setUp(self):
        self.parser = monitor.ProtocolParser()

    def feed(self, text):
        return [value for line in text.splitlines()
                if (value := self.parser.feed_line(line)) is not None]

    def test_complete_frame_and_signed_delta(self):
        messages = self.feed(frame(4294967295, 90) + frame(0, 95))
        self.assertEqual([m.timestamp for m in messages], [4294967295, 0])
        self.assertEqual([s.ring for s in messages[0].sensors], list(range(1, 11)))
        self.assertEqual(messages[0].sensors[3].delta, -10)

    def test_event_can_interrupt_snapshot(self):
        text = frame().replace("6,120", "E,9,420,99\r\n6,120")
        messages = self.feed(text)
        self.assertEqual(messages[0], monitor.Hit(9, 420, 99))
        self.assertIsInstance(messages[1], monitor.Snapshot)

    def test_swapped_firmware_ring_ids_are_not_remapped_twice(self):
        # Actual wire order is ADC1..ADC10, with the swapped logical ring IDs.
        # Give each physical input distinct data to detect any double swap.
        wire_rings = (6, 7, 8, 9, 10, 5, 4, 3, 2, 1)
        text = "S,100,1,0,0,0\n"
        for network, ring in enumerate(wire_rings, 1):
            value = network * 100
            text += f"{ring},{value},10,{value - 10},{value},1\n"
        messages = self.feed(text + "E,10,490,99\nE,1,990,100\n")
        snapshot = messages[0]
        self.assertEqual([s.ring for s in snapshot.sensors], list(range(1, 11)))
        self.assertEqual(snapshot.sensors[9].raw, 500)  # ADC5 now labels ring10.
        self.assertEqual(snapshot.sensors[4].raw, 600)  # ADC6 now labels ring5.
        self.assertEqual(snapshot.sensors[0].raw, 1000) # ADC10 now labels ring1.
        self.assertEqual(snapshot.sensors[3].raw, 700)  # ADC7 now labels ring4.
        self.assertEqual(snapshot.sensors[1].raw, 900)  # ADC9 now labels ring2.
        self.assertEqual(messages[1:], [monitor.Hit(10, 490, 99), monitor.Hit(1, 990, 100)])

    def test_partial_snapshot_is_never_published(self):
        self.feed("S,1,1,0,0,0\n1,120,100,20,200,1\n")
        messages = self.feed(frame(2))
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].timestamp, 2)
        self.assertEqual(self.parser.incomplete_frames, 1)

    def test_duplicate_ring_invalidates_frame(self):
        text = frame().replace("2,120,100,20,240,1", "1,120,100,20,240,1")
        self.assertEqual(self.feed(text), [])
        self.assertEqual(self.parser.bad_lines, 1)
        self.assertEqual(len(self.feed(frame(3))), 1)

    def test_invalid_event_does_not_erase_valid_frame(self):
        text = frame().replace("3,120", "E,11,420,99\r\n3,120")
        self.assertEqual(len(self.feed(text)), 1)
        self.assertEqual(self.parser.bad_lines, 1)

    def test_bad_header_and_out_of_range_adc(self):
        self.feed("S,2,8,0,0,0\n")
        self.assertEqual(self.parser.bad_lines, 1)
        self.assertEqual(self.feed(frame().replace("5,120", "5,4096")), [])
        self.assertEqual(self.parser.bad_lines, 2)

    def test_connection_reset_discards_partial_frame(self):
        self.feed("S,1,1,0,0,0\n1,120,100,20,240,1\n")
        self.parser.reset()
        self.assertIsNone(self.parser.feed_line("2,120,100,20,240,1"))
        self.assertEqual(len(self.feed(frame(3))), 1)


@unittest.skipIf(monitor.serial is None, "pyserial is not installed")
class ReaderTests(unittest.TestCase):
    def read_bytes(self, data, chunk_size):
        reader = monitor.SerialReader("FAKE", 115200)
        chunks = [data[i:i + chunk_size] for i in range(0, len(data), chunk_size)]
        class Stream:
            in_waiting = 4096
            def __enter__(self):
                return self
            def __exit__(self, *_args):
                return False
            def read(self, _size):
                if chunks:
                    return chunks.pop(0)
                reader.stop_event.set()
                return b""
        with patch.object(monitor.serial, "Serial", return_value=Stream()):
            reader.start()
            reader.join(3)
        self.assertFalse(reader.is_alive())
        return reader

    def test_fragmented_lines_and_event(self):
        reader = self.read_bytes((frame() + "E,10,270,100\r\n").encode(), 7)
        self.assertEqual(reader.snapshots.get_nowait()[1].timestamp, 100)
        self.assertEqual(reader.hits.get_nowait()[1], monitor.Hit(10, 270, 100))
        self.assertEqual(reader.parser.bad_lines, 0)

    def test_queue_overflow_keeps_latest_and_events_separate(self):
        data = ("".join(frame(i) for i in range(200)) + "E,4,380,199\n").encode()
        reader = self.read_bytes(data, 4000)
        self.assertEqual(reader.snapshots.qsize(), 128)
        self.assertEqual(reader.host_drops, 72)
        self.assertEqual(reader.hits.qsize(), 1)
        values = []
        while not reader.snapshots.empty():
            values.append(reader.snapshots.get_nowait()[1].timestamp)
        self.assertEqual((values[0], values[-1]), (72, 199))

    def test_invalid_encoding_and_oversized_line_resynchronize(self):
        reader = self.read_bytes(b"\xff\n" + b"x" * 1100 + b"\n" + frame(123).encode(), 100)
        self.assertGreaterEqual(reader.parser.bad_lines, 2)
        self.assertEqual(reader.snapshots.get_nowait()[1].timestamp, 123)


if __name__ == "__main__":
    unittest.main()
