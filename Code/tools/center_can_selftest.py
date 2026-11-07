"""Run the opt-in center CAN wiring test through ST-Link (no UART required).

Build with CENTER_CAN_SELFTEST=ON first. --flash explicitly programs that ELF;
without it, the same test image must already be installed. Never use a leaf ELF.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time


def tool(root, package, executable):
    found = sorted((root / package).glob(f"*/bin/{executable}"), reverse=True)
    if not found:
        raise FileNotFoundError(f"Missing {package}/{executable} in {root}")
    return found[0]


def decode_report(raw):
    # CanTestReport v1 consists solely of uint32_t, with no target-dependent padding.
    fields = [(n, 1) for n in "magic version state start frames_per_port gap_ms ready_mask core_hz fdcan_hz started_ms elapsed_ms".split()]
    fields += [("tx", 5), ("tx_busy", 5), ("rx", 25)]
    fields += [(n, 5) for n in "invalid unexpected sequence_errors rx_drops tx_aborts".split()]
    fields += [(n, 3) for n in "fdcan_psr fdcan_ecr fdcan_error_log".split()]
    fields += [(n, 5) for n in "max_tec max_rec bus_off".split()]
    fields += [(n, 2) for n in "mcp_io_errors mcp_canstat mcp_cnf mcp_eflg mcp_txctrl".split()]
    fields += [("deadline", 1), ("config_error", 1)]
    words = struct.unpack("<" + "I" * sum(n for _, n in fields), raw)
    result, offset = {}, 0
    for name, count in fields:
        value = list(words[offset:offset + count])
        result[name] = value[0] if count == 1 else value
        offset += count
    result["rx"] = [result["rx"][i:i + 5] for i in range(0, 25, 5)]
    if result["magic"] != 0x43414e35 or result["version"] != 1 or result["state"] not in (3, 4):
        raise ValueError("Incomplete or incompatible test report")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bundle-root", type=Path, default=Path(os.environ.get(
        "CUBE_BUNDLE_PATH", str(Path.home() / "AppData/Local/stm32cube/bundles"))))
    parser.add_argument("--frames", type=int, default=1000, choices=range(1, 10001), metavar="1..10000")
    parser.add_argument("--gap-ms", type=int, default=5, choices=range(1, 21), metavar="1..20")
    parser.add_argument("--port", type=int, default=61234)
    parser.add_argument("--flash", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9]{8,64}", args.serial):
        parser.error("Invalid ST-Link serial")
    elf = args.elf.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    binary_path = output / "report.bin"
    if report_path.exists() or binary_path.exists():
        parser.error("Output already contains a report; choose a fresh output directory")
    programmer = tool(args.bundle_root, "programmer", "STM32_Programmer_CLI.exe")
    server = tool(args.bundle_root, "stlink-gdbserver", "ST-LINK_gdbserver.exe")
    gdb = tool(args.bundle_root, "gnu-gdb-for-stm32", "arm-none-eabi-gdb.exe")
    no_window = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    # Verify the test-only symbol BEFORE allowing Flash writes.
    check = subprocess.run([str(gdb), "-nx", "-batch", str(elf), "-ex", "info address g_can_test"],
                           capture_output=True, text=True, creationflags=no_window, timeout=15)
    if check.returncode or 'Symbol "g_can_test"' not in check.stdout:
        raise RuntimeError("ELF is not a center CAN self-test image: " + check.stdout + check.stderr)
    if args.flash:
        with (output / "flash.log").open("w", encoding="utf-8") as log:
            result = subprocess.run([str(programmer), "-c", "port=SWD", f"sn={args.serial}",
                                     "mode=Normal", "freq=1000", "-w", str(elf), "-v", "-rst"],
                                    stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=no_window, timeout=45)
        if result.returncode:
            raise RuntimeError(f"Flash failed; see {output / 'flash.log'}")
    gdb_script = output / "run.gdb"
    gdb_script.write_text(f'''set pagination off
set confirm off
set remotetimeout 10
target extended-remote 127.0.0.1:{args.port}
monitor reset
thbreak BoardApp_Process
continue
if g_can_test.magic != 0x43414e35 || g_can_test.state != 1
  echo Invalid self-test initialization\n
  quit 1
end
set variable g_can_test.frames_per_port = {args.frames}
set variable g_can_test.gap_ms = {args.gap_ms}
hbreak CanSelfTest_Done
set variable g_can_test.start = 1
continue
dump binary memory report.bin &g_can_test ((char *)&g_can_test)+sizeof(g_can_test)
print g_can_test
delete breakpoints
detach
quit
''', encoding="utf-8")
    server_log = output / "gdbserver.log"
    with server_log.open("w", encoding="utf-8") as log:
        proc = subprocess.Popen([str(server), "-d", "-i", args.serial, "-p", str(args.port),
                                 "-cp", str(programmer.parent), "--frequency", "1000"],
                                stdout=log, stderr=subprocess.STDOUT, creationflags=no_window)
        try:
            for _ in range(100):
                if proc.poll() is not None:
                    raise RuntimeError(f"GDB server exited; see {server_log}")
                if "Waiting for debugger connection" in server_log.read_text(errors="replace"):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(f"GDB server not ready; see {server_log}")
            timeout = args.frames * 5 * args.gap_ms / 1000 + 30
            with (output / "gdb.log").open("w", encoding="utf-8") as capture:
                result = subprocess.run([str(gdb), "-nx", "-batch", str(elf), "-x", str(gdb_script)],
                                        stdout=capture, stderr=subprocess.STDOUT,
                                        creationflags=no_window, timeout=timeout, cwd=output)
            if result.returncode or not binary_path.exists():
                raise RuntimeError(f"No valid test report; see {output / 'gdb.log'}")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
    report = decode_report(binary_path.read_bytes())
    report["probe_serial"] = args.serial
    report["elf"] = str(elf)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print("CAN_TEST_RESULT:" + ("PASS" if report["state"] == 3 else "FAIL"))
    return 0 if report["state"] == 3 else 1


if __name__ == "__main__":
    raise SystemExit(main())
