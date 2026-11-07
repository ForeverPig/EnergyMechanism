import asyncio
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import v2_protocol as v2
from ble_debug import Session


class CodecTests(unittest.TestCase):
    def test_max_frame_crc_timeout(self):
        frame = v2.encode(1, 123, bytes(range(184)))
        parts = list(v2.fragments(frame, 5))
        self.assertEqual((len(frame), len(parts), len(parts[-1])), (194, 13, 6))
        r = v2.Reassembly()
        for part in parts[:-1]:
            self.assertIsNone(r.feed(part, 0))
        self.assertEqual(r.feed(parts[-1], .9), (1, 123, bytes(range(184))))
        r.feed(parts[0], 1)
        self.assertIsNone(r.feed(parts[1], 2.1))
        corrupt = bytearray(frame)
        corrupt[-1] ^= 1
        with self.assertRaises(ValueError):
            v2.decode(corrupt)
        with self.assertRaises(ValueError):
            v2.encode(1, 1, bytes(185))

    def test_ring_and_motor_signed(self):
        p = bytearray(125)
        p[12] = 5
        struct.pack_into("<BBHHhH", p, 25, 10, 1, 1200, 1500, -300, 400)
        d = v2.describe((0x12, 0, bytes(p)))
        self.assertEqual(d["values"][0]["delta"], -300)
        self.assertEqual(d["slot"], 5)
        p = bytearray(22)
        struct.pack_into("<hh", p, 12, -1234, -100)
        self.assertEqual(v2.describe((0x14, 0, p))["rotor_rpm"], -1234)

    def test_invalid_fragments_do_not_mix(self):
        frame = v2.request(7, 2, 99, 1)
        a, b = list(v2.fragments(frame, 4)), list(v2.fragments(frame, 5))
        r = v2.Reassembly()
        r.feed(a[0], 0)
        self.assertIsNone(r.feed(b[1], .1))
        for p in b[:-1]:
            self.assertIsNone(r.feed(p, .2))
        self.assertEqual(r.feed(b[-1], .2)[1], 7)


class ClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_retry_preserves_request_and_acceptance_is_not_completion(self):
        class Fake:
            def __init__(self):
                self.r = v2.Reassembly()
                self.requests = []
                self.session = None

            async def write_gatt_char(self, uuid, data, response):
                self_response = self.r.feed(data)
                self_outer.assertEqual(uuid, v2.COMMAND)
                self_outer.assertTrue(response)
                if self_response:
                    self.requests.append(self_response)
                    if len(self.requests) == 2:
                        payload = bytearray(36)
                        payload[0] = v2.CALIBRATE
                        for part in v2.fragments(v2.encode(2, self_response[1], payload), 10):
                            self.session.notification(v2.CONTROL, part)
                        asyncio.get_running_loop().call_later(.002, self.complete, self_response[1], payload)

            def complete(self, seq, payload):
                payload[1] = 1
                for part in v2.fragments(v2.encode(2, seq, payload), 11):
                    self.session.notification(v2.CONTROL, part)
        self_outer = self
        fake = Fake()
        session = Session(fake, lambda _: None)
        fake.session = session
        result = await session.request(v2.CALIBRATE, b"\x01", retry_interval=.01)
        self.assertEqual(len(fake.requests), 2)
        self.assertEqual(fake.requests[0], fake.requests[1])
        self.assertEqual(result[-1]["phase"], "COMPLETED")


if __name__ == "__main__":
    unittest.main()
