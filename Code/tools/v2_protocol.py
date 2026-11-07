"""Frozen bench V2 codec. Standard library only; see ../PROTOCOL_V2.md."""
from __future__ import annotations
import binascii
import struct
import time

SERVICE = "af510000-7b3d-4b28-9f02-6c6d454d0001"
INFO, COMMAND, CONTROL, EVENT, PRESSURE = [
    f"af51000{i}-7b3d-4b28-9f02-6c6d454d0001" for i in range(1, 6)
]
HELLO, GET_STATE, GET_LEAVES = 1, 2, 3
STOP, ENTER_DEBUG, EXIT_DEBUG = 0x12, 0x13, 0x14
OBSERVE_ENTER, OBSERVE_RENEW, OBSERVE_LEAVE, CALIBRATE, QUERY = 0x20, 0x21, 0x22, 0x23, 0x32
ERRORS = ("OK", "BAD_ARGUMENT", "UNSUPPORTED", "NOT_READY", "WRONG_MODE",
          "NOT_STOPPED", "LEAF_OFFLINE", "BUSY", "RATE_LIMIT", "STALE_STATE",
          "ALREADY_RUNNING", "TIMEOUT", "LINK_DOWN", "REQUEST_CONFLICT",
          "REPLAY_EXPIRED", "CACHE_GAP", "FAULT_ACTIVE")
PHASES = ("ACCEPTED", "COMPLETED", "REJECTED", "FAILED", "UNKNOWN")


def encode(kind: int, seq: int, payload: bytes) -> bytes:
    if len(payload) > 184:
        raise ValueError("BLE payload exceeds 184 bytes")
    body = struct.pack("<BBHH", 2, kind, len(payload), seq) + payload
    return b"\xa5\x5a" + body + struct.pack("<H", binascii.crc_hqx(body, 0xffff))


def decode(frame: bytes) -> tuple[int, int, bytes]:
    if not 10 <= len(frame) <= 194 or frame[:3] != b"\xa5\x5a\x02":
        raise ValueError("invalid frame header")
    size, seq = struct.unpack_from("<HH", frame, 4)
    if size > 184 or len(frame) != size + 10:
        raise ValueError("invalid frame length")
    if struct.unpack_from("<H", frame, len(frame)-2)[0] != binascii.crc_hqx(frame[2:-2], 0xffff):
        raise ValueError("CRC mismatch")
    return frame[3], seq, frame[8:-2]


def fragments(frame: bytes, transfer: int):
    decode(frame)
    count = (len(frame)+15)//16
    for index in range(count):
        yield struct.pack("<HBB", transfer, index, count) + frame[index*16:(index+1)*16]


class Reassembly:
    def __init__(self):
        self.active = False
        self.buffer = bytearray()
        self.transfer = self.index = self.count = 0
        self.started = 0.0
        self.errors = 0

    def feed(self, fragment: bytes, now: float | None = None):
        now = time.monotonic() if now is None else now
        try:
            if not 5 <= len(fragment) <= 20:
                raise ValueError("fragment size")
            transfer, index, count = struct.unpack_from("<HBB", fragment)
            if not 1 <= count <= 13 or index >= count or (index+1 < count and len(fragment) != 20):
                raise ValueError("fragment bounds")
            if self.active and now-self.started > 1:
                self.active = False
                self.errors += 1
            if index == 0:
                if self.active:
                    self.errors += 1
                self.buffer.clear()
                self.active = True
                self.transfer, self.count, self.index, self.started = transfer, count, 0, now
            if not self.active or (transfer, count, index) != (self.transfer, self.count, self.index):
                raise ValueError("fragment sequence")
            self.buffer.extend(fragment[4:])
            self.index += 1
            if len(self.buffer) > 194:
                raise ValueError("fragment overflow")
            if self.index == self.count:
                self.active = False
                return decode(bytes(self.buffer))
        except ValueError:
            self.active = False
            self.errors += 1
        return None


def request(seq: int, op: int, boot: int, revision: int, args: bytes = b"") -> bytes:
    return encode(1, seq, struct.pack("<BQI", op, boot, revision) + args)


def describe(packet: tuple[int, int, bytes]) -> dict:
    kind, seq, p = packet
    d = {"type": kind, "seq": seq}
    u16 = lambda offset: struct.unpack_from("<H", p, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", p, offset)[0]
    u64 = lambda offset: struct.unpack_from("<Q", p, offset)[0]
    if kind == 2 and len(p) == 36:
        d.update(op=p[0], phase=PHASES[p[1]] if p[1] < len(PHASES) else p[1],
                 code=ERRORS[p[2]] if p[2] < len(ERRORS) else p[2], detail=p[3],
                 boot=u64(4), revision=u32(12), operation_id=u32(20),
                 token=u32(24), selection_epoch=u32(28), mode=p[32], selected=p[33])
    elif kind == 3 and len(p) == 56:
        d.update(device=p[:12].hex(), gateway_epoch=u64(12), client_session=u64(20),
                 boot=u64(28), capabilities=u32(36), revision=u32(40), link=bool(p[44]),
                 max_clients=p[45], max_payload=u16(46), lease_ms=u32(52))
    elif kind == 0x10 and len(p) == 48:
        d.update(boot=u64(0), revision=u32(8), state_revision=u32(12), now_ms=u32(16),
                 mode=p[20], selected=p[21], online_leaves=p[22], can_ready=p[23],
                 stopped=bool(p[24]), motor_valid=p[25], motor_moving=p[26], motor_known=p[27],
                 selection_epoch=u32(28), capabilities=u32(32), uart_errors=u32(36),
                 uart_drops=u32(40), lease_ms=u32(44))
    elif kind == 0x11 and len(p) == 44:
        d.update(boot=u64(0), state_revision=u32(8), slot=p[12], online=bool(p[13]),
                 calibration=p[14], uid_valid=bool(p[15]), age_ms=u32(16),
                 adc_errors=u16(20), adc_drops=u16(22), can_drops=u32(24),
                 uid=p[28:40].hex(), batch=u16(40), count=p[42], source_version=p[43])
    elif kind == 0x12 and len(p) == 125:
        values = []
        for offset in range(25, 125, 10):
            ring, state, raw, baseline, delta, peak = struct.unpack_from("<BBHHhH", p, offset)
            values.append(dict(ring=ring, state=state, raw=raw, baseline=baseline, delta=delta, peak=peak))
        d.update(boot=u64(0), selection_epoch=u32(8), slot=p[12], snapshot=u32(13),
                 received_ms=u32(17), sample_ms=u32(21), values=values)
    elif kind == 0x13 and len(p) == 28:
        d.update(boot=u64(0), event=u32(8), slot=p[12], source_version=p[13], leaf_seq=p[14],
                 ring=p[15], peak=u16(16), sample_ms=u32(18), received_ms=u32(22), diagnostic_only=True)
    elif kind == 0x14 and len(p) == 22:
        d.update(boot=u64(0), motor_id=p[8], temperature=p[9], angle_raw=u16(10),
                 rotor_rpm=struct.unpack_from("<h", p, 12)[0],
                 current_raw=struct.unpack_from("<h", p, 14)[0], age_ms=u32(16), online=bool(p[20]))
    else:
        raise ValueError(f"unexpected type/length: {kind}/{len(p)}")
    return d
