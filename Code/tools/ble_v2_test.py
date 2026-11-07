"""Read-only physical BLE/UART V2 check. Never sends motion, debug or calibration commands."""
from __future__ import annotations
import argparse
import asyncio
import collections
import json
from pathlib import Path
import statistics
import time

import v2_protocol as v2
from ble_debug import Session

READ_ONLY = {v2.HELLO, v2.GET_STATE, v2.GET_LEAVES}


async def test(args, report, log):
    from bleak import BleakClient, BleakScanner
    identity = None
    start_state = end_state = None
    latencies = []
    uart_queries = 0

    for cycle in range(args.reconnects + 1):
        device = await BleakScanner.find_device_by_address(args.address, timeout=10)
        if device is None:
            raise RuntimeError("Target not advertising before connection")
        events = []
        notifications = collections.Counter()
        writes = collections.Counter()
        async with BleakClient(device, timeout=15, winrt={"use_cached_services": False}) as client:
            info = bytes(await client.read_gatt_char(v2.INFO))
            if (len(info) != 16 or info[:6] != b"EMB2\x02\x00" or info[8] != 3
                    or int.from_bytes(info[12:16], "little") < 2):
                raise RuntimeError("Unexpected INFO")
            service = client.services.get_service(v2.SERVICE)
            if service is None or {c.uuid for c in service.characteristics} != {
                    v2.INFO, v2.COMMAND, v2.CONTROL, v2.EVENT, v2.PRESSURE}:
                raise RuntimeError("Incomplete GATT service")

            class MeterSession(Session):
                def notification(self, uuid, data):
                    notifications[uuid] += 1
                    super().notification(uuid, data)

                async def send_frame(self, frame):
                    _, seq, payload = v2.decode(frame)
                    if payload[0] not in READ_ONLY:
                        raise RuntimeError("Test refuses a non-read-only opcode")
                    writes[seq] += 1
                    await super().send_frame(frame)

            session = MeterSession(client, events.append)
            await session.start_notifications()

            async def query(op, payload=b""):
                nonlocal uart_queries
                began = time.perf_counter()
                result = await session.request(op, payload)
                elapsed = (time.perf_counter()-began)*1000
                if any(item.get("code", "OK") != "OK" for item in result):
                    raise RuntimeError(f"Request rejected: {result}")
                if op == v2.GET_STATE:
                    if len(result) != 1 or result[0]["type"] != 0x10:
                        raise RuntimeError("Missing STATE")
                elif op == v2.GET_LEAVES:
                    leaves = [item for item in result if item["type"] == 0x11]
                    final = [item for item in result if item["type"] == 2]
                    if {item["slot"] for item in leaves} != {1,2,3,4,5} or len(final) != 1 or final[0]["phase"] != "COMPLETED":
                        raise RuntimeError("Incomplete leaf batch")
                    if len({item["batch"] for item in leaves}) != 1 or any(item["count"] != 5 for item in leaves):
                        raise RuntimeError("Inconsistent leaf batch")
                if op != v2.HELLO:
                    uart_queries += 1
                    latencies.append(elapsed)
                row = {"cycle":cycle, "op":op, "seq":session.seq, "latency_ms":round(elapsed,3), "reply":result}
                log.write(json.dumps(row)+"\n")
                log.flush()
                return result

            hello = (await query(v2.HELLO,b"\x00\x02\x02"))[0]
            if not hello.get("link") or not hello.get("boot") or not hello.get("gateway_epoch"):
                raise RuntimeError("Center V2 handshake not ready")
            current_identity = (hello["device"],hello["gateway_epoch"],hello["boot"])
            if identity is None:
                identity = current_identity
            elif identity != current_identity:
                raise RuntimeError("Gateway/center restarted during reconnect test")
            state = (await query(v2.GET_STATE))[0]
            if start_state is None:
                start_state = state
            if state["boot"] != identity[2] or state["mode"] != 0 or state["selected"] != 0:
                raise RuntimeError("Unexpected device state")
            if cycle == 0:
                for index in range(args.count):
                    if index % 10 == 9:
                        await query(v2.GET_LEAVES,b"\x00")
                    else:
                        await query(v2.GET_STATE)
                    if (index+1)%50 == 0:
                        print(f"read-only queries {index+1}/{args.count}",flush=True)
                    await asyncio.sleep(.1)
                end_state = (await query(v2.GET_STATE))[0]
            else:
                await query(v2.GET_LEAVES,b"\x00")
                end_state = state
            await asyncio.sleep(.7)
            errors = sum(r.errors for r in session.assemblers.values())
            unexpected = [event for event in events if event.get("seq",0) or "decode_error" in event]
            retries = sum(n-1 for n in writes.values())
            if errors or unexpected or retries:
                raise RuntimeError(f"Framing/response issue: errors={errors}, unexpected={unexpected}, retries={retries}")
            report["connections"].append({
                "cycle":cycle,"session":hello,"info_hex":info.hex(),
                "notifications":dict(notifications),"async_states":sum(e.get("type")==0x10 for e in events),
                "request_count":len(writes),"retries":retries,"reassembly_errors":errors,
                "state":state})
            await session.stop_notifications()
        print(f"connection {cycle+1}/{args.reconnects+1}: passed and disconnected",flush=True)
        await asyncio.sleep(.3)

    sessions = [c["session"]["client_session"] for c in report["connections"]]
    if len(set(sessions)) != len(sessions):
        raise RuntimeError("Client session reused after reconnect")
    if end_state["now_ms"] <= start_state["now_ms"]:
        raise RuntimeError("Center uptime did not advance")
    deltas = {k:end_state[k]-start_state[k] for k in ("uart_errors","uart_drops")}
    report.update(uart_queries=uart_queries, start_state=start_state, end_state=end_state,
                  uart_counter_delta=deltas,
                  latency_ms={"min":round(min(latencies),3),"mean":round(statistics.mean(latencies),3),
                              "max":round(max(latencies),3)},
                  read_only_opcodes=sorted(READ_ONLY))
    if any(deltas.values()):
        raise RuntimeError(f"UART error counters increased: {deltas}")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--address",required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--count",type=int,default=200)
    p.add_argument("--reconnects",type=int,default=2)
    args=p.parse_args()
    if not 1 <= args.count <= 10000 or not 0 <= args.reconnects <= 5:
        p.error("count must be 1..10000; reconnects 0..5")
    args.output.mkdir(parents=True,exist_ok=True)
    path=args.output/"report.json"
    if path.exists():
        p.error("Choose a fresh output directory")
    report={"test":"physical_ble_uart_v2","address":args.address,"count":args.count,
            "reconnects":args.reconnects,"connections":[],"result":"INCOMPLETE"}
    failure=None
    try:
        with (args.output/"transactions.jsonl").open("w",encoding="utf-8") as log:
            asyncio.run(test(args,report,log))
        report["result"]="PASS"
    except Exception as exc:
        report["result"]="FAIL";report["error"]=str(exc);failure=exc
    finally:
        path.write_text(json.dumps(report,indent=2),encoding="utf-8")
        print(json.dumps({k:v for k,v in report.items() if k not in ("connections","start_state","end_state")},indent=2))
    if failure:
        raise failure


if __name__ == "__main__":
    main()
