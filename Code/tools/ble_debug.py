"""Windows BLE bench console. Explicit connection only; never flashes firmware."""
from __future__ import annotations
import argparse
import asyncio
import contextlib
import json
import struct
import time
import v2_protocol as v2


class Session:
    def __init__(self, client, on_event=print):
        self.client = client
        self.on_event = on_event
        self.seq = self.transfer = self.boot = self.revision = self.token = 0
        self.pending = {}
        self.assemblers = {uuid: v2.Reassembly() for uuid in (v2.CONTROL, v2.EVENT, v2.PRESSURE)}
        self.write_lock = asyncio.Lock()
        self.limit = asyncio.Semaphore(2)

    async def start_notifications(self):
        # Closing a Windows GATT client need not close the physical BLE link.
        # Force CONTROL off before enabling it to request a fresh business session.
        # Bleak deliberately prohibits direct CCCD writes. A temporary listener
        # allows stop_notify() to send the required disable via the public API.
        await self.client.start_notify(v2.CONTROL, lambda *_: None)
        await self.client.stop_notify(v2.CONTROL)
        for uuid in (v2.CONTROL, v2.EVENT, v2.PRESSURE):
            await self.client.start_notify(uuid, lambda _, data, uuid=uuid: self.notification(uuid, data))

    async def stop_notifications(self):
        if self.client.is_connected:
            # CONTROL last: retires the session and releases its observer lease.
            for uuid in (v2.PRESSURE, v2.EVENT, v2.CONTROL):
                with contextlib.suppress(Exception):
                    await self.client.stop_notify(uuid)

    def notification(self, uuid, data):
        packet = self.assemblers[uuid].feed(bytes(data))
        if packet is None:
            return
        try:
            decoded = v2.describe(packet)
        except ValueError as exc:
            self.on_event({"decode_error": str(exc)})
            return
        boot = decoded.get("boot")
        if boot is not None:
            if self.boot and boot != self.boot:
                self.token = 0
            if boot != self.boot or decoded.get("revision", self.revision) >= self.revision:
                self.boot = boot
                self.revision = decoded.get("revision", 0)
        if decoded.get("type") == 3 and not decoded.get("link"):
            self.token = 0
        if decoded.get("type") == 0x10 and (decoded.get("mode") != 5 or not decoded.get("selected")):
            self.token = 0
        seq = packet[1]
        pending = self.pending.get(seq)
        if pending:
            op, args, future, replies = pending
            key = (packet[0], decoded.get("slot", 0))
            replies[key] = decoded
            terminal = (packet[0] == 2 and (packet[2][1] != 0 or op == v2.QUERY)) or (
                packet[0] == 3 and op == v2.HELLO) or (packet[0] == 0x10 and op in (v2.GET_STATE, v2.QUERY))
            if op == v2.GET_LEAVES:
                end = replies.get((2, 0))
                slots = {item["slot"] for item in replies.values() if item["type"] == 0x11}
                wanted = {args[0]} if args[0] else set(range(1, 6))
                terminal = bool(end and (end["phase"] != "COMPLETED" or slots == wanted))
            if packet[0] == 2 and packet[2][1:3] == b"\x01\x00":
                if op == v2.OBSERVE_ENTER:
                    self.token = decoded["token"]
                elif op == v2.OBSERVE_LEAVE:
                    self.token = 0
            if terminal and not future.done():
                future.set_result(list(replies.values()))
        else:
            self.on_event(decoded)

    async def send_frame(self, frame):
        async with self.write_lock:
            self.transfer = (self.transfer + 1) & 0xffff
            for fragment in v2.fragments(frame, self.transfer):
                await self.client.write_gatt_char(v2.COMMAND, fragment, response=True)

    async def request(self, op, args=b"", *, retry_interval=1.5):
        async with self.limit:
            if self.seq >= 65535:
                raise RuntimeError("request sequence exhausted; reconnect before further commands")
            self.seq += 1
            seq = self.seq
            frame = v2.request(seq, op, self.boot, self.revision, args)
            future = asyncio.get_running_loop().create_future()
            self.pending[seq] = (op, args, future, {})
            try:
                for attempt in range(3):
                    # All retries retain seq/content; only transfer_id changes.
                    await self.send_frame(frame)
                    try:
                        return await asyncio.wait_for(asyncio.shield(future), retry_interval if attempt < 2 else 4.5)
                    except asyncio.TimeoutError:
                        pass
                raise TimeoutError(f"request {seq} result UNKNOWN; use query {seq} or retry the same request, not a new action")
            finally:
                self.pending.pop(seq, None)

    async def renew(self):
        while True:
            await asyncio.sleep(1)
            token = self.token
            if token:
                try:
                    replies = await self.request(v2.OBSERVE_RENEW, struct.pack("<I", token))
                    if any(p.get("code") != "OK" for p in replies):
                        self.token = 0
                        self.on_event({"observer_lease": "expired", "reply": replies})
                except Exception as exc:
                    self.token = 0
                    self.on_event({"observer_lease": str(exc)})


async def run(args):
    from bleak import BleakClient, BleakScanner
    if args.scan:
        for device in await BleakScanner.discover(timeout=5, service_uuids=[v2.SERVICE]):
            print(device.address, device.name)
        return
    device = await BleakScanner.find_device_by_address(args.address, timeout=10)
    if device is None:
        raise RuntimeError("Device not found; run --scan and verify the gateway firmware is running")
    async with BleakClient(device, disconnected_callback=lambda _: print("BLE disconnected; press Enter to exit.")) as client:
        last_pressure = 0.0

        def event(data):
            nonlocal last_pressure
            if args.quiet:
                return
            if data.get("type") == 0x12:
                if time.monotonic()-last_pressure < .5:
                    return
                last_pressure = time.monotonic()
            print(json.dumps(data, ensure_ascii=False))

        session = Session(client, event)
        info = bytes(await client.read_gatt_char(v2.INFO))
        if len(info) != 16 or info[:6] != b"EMB2\x02\x00" or int.from_bytes(info[12:16], "little") < 2:
            raise RuntimeError("Incompatible INFO characteristic")
        await session.start_notifications()
        print(json.dumps(await session.request(v2.HELLO, b"\x00\x02\x02"), ensure_ascii=False))
        print("Commands: state | leaves [0..5] | debug | idle | stop | watch 1..5 | leave | cal 1..5 | query N | quit")
        renew = asyncio.create_task(session.renew())
        try:
            while client.is_connected:
                line = (await asyncio.to_thread(input, "> ")).strip().split()
                if not line:
                    continue
                name = line[0].lower()
                if name in ("quit", "exit"):
                    break
                try:
                    simple = {"state": v2.GET_STATE, "debug": v2.ENTER_DEBUG, "idle": v2.EXIT_DEBUG, "stop": v2.STOP}
                    if name in simple:
                        result = await session.request(simple[name])
                    elif name in ("leaves", "watch", "cal"):
                        slot = int(line[1]) if len(line) > 1 else 0
                        if not (0 if name == "leaves" else 1) <= slot <= 5:
                            raise ValueError("invalid leaf slot")
                        op = {"leaves": v2.GET_LEAVES, "watch": v2.OBSERVE_ENTER, "cal": v2.CALIBRATE}[name]
                        result = await session.request(op, bytes([slot]))
                    elif name == "leave":
                        result = await session.request(v2.OBSERVE_LEAVE, struct.pack("<I", session.token))
                    elif name == "query" and len(line) == 2:
                        result = await session.request(v2.QUERY, struct.pack("<H", int(line[1])))
                    else:
                        raise ValueError("unknown command")
                    print(json.dumps(result, ensure_ascii=False, indent=2))
                except Exception as exc:
                    print(str(exc))
        finally:
            renew.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await renew
            if session.token and client.is_connected:
                with contextlib.suppress(Exception):
                    await asyncio.wait_for(session.request(v2.OBSERVE_LEAVE, struct.pack("<I", session.token)), 1)
            await session.stop_notifications()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--scan", action="store_true")
    choice.add_argument("--address")
    parser.add_argument("--quiet", action="store_true", help="hide unsolicited telemetry")
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except (KeyboardInterrupt, EOFError):
        pass


if __name__ == "__main__":
    main()
