"""Start the finite ESP32-to-STM32 UART test over the ESP32 native USB console."""
import argparse
import json
from pathlib import Path
import time
import serial


def verify(report):
    counters = ("baud", "target", "sent", "matched", "timeouts", "crc_or_frame_errors", "bad_status",
                "unexpected_seq", "uart_faults", "write_errors", "stm_resets", "stm_uart_errors",
                "stm_uart_errors_start", "stm_tx_drops", "stm_tx_drops_start")
    return (all(type(report.get(k)) is int and report[k] >= 0 for k in counters)
            and report.get("test") == "esp32_stm32_uart_v1" and report.get("result") == "PASS"
            and report.get("baud") == 1000000 and report.get("target") == 1000
            and report.get("sent") == report.get("matched") == 1000
            and all(report.get(k) == 0 for k in ("timeouts", "crc_or_frame_errors", "bad_status",
                    "unexpected_seq", "uart_faults", "write_errors", "stm_resets"))
            and report.get("stm_uart_errors") == report.get("stm_uart_errors_start")
            and report.get("stm_tx_drops") == report.get("stm_tx_drops_start"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report_file = args.output / "report.json"
    if report_file.exists():
        ap.error("Choose a fresh output directory; report.json already exists")
    ser = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=1)
    ser.dtr = False
    ser.rts = False
    ser.port = args.port
    ser.open()
    with ser, (args.output / "usb-console.txt").open("w", encoding="utf-8") as log:
        def packet():
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                log.write(line + "\n")
                log.flush()
                try:
                    p = json.loads(line)
                    if isinstance(p, dict) and p.get("test") == "esp32_stm32_uart_v1":
                        return p
                except json.JSONDecodeError:
                    pass
            return None

        ser.reset_input_buffer()
        ser.write(b"status\n")
        deadline = time.monotonic() + 5
        ready = None
        while time.monotonic() < deadline:
            ready = packet()
            if ready:
                break
        if not ready:
            raise RuntimeError("No ESP32 test console response; check firmware, USB and COM port")
        if ready["result"] == "RUNNING":
            raise RuntimeError("Another test is already running; use its console to stop it first")
        ser.write(b"run\n")
        started, completed = False, None
        deadline, next_status = time.monotonic() + 45, time.monotonic() + 1
        while time.monotonic() < deadline:
            p = packet()
            if p:
                if p["result"] == "RUNNING":
                    started = True
                elif started and p["result"] in ("PASS", "FAIL", "STOPPED"):
                    completed = p
                    break
            if time.monotonic() >= next_status:
                ser.write(b"status\n")
                next_status = time.monotonic() + 1
        if completed is None:
            ser.write(b"stop\n")
            raise TimeoutError("No complete test result within 45 seconds; stop requested")
        completed["usb_port"] = args.port
        report_file.write_text(json.dumps(completed, indent=2), encoding="utf-8")
        print(json.dumps(completed, indent=2))
        if not verify(completed):
            raise RuntimeError("ESP32/STM32 UART test FAIL; see report and console log")
        print("UART_TEST_RESULT:PASS")


if __name__ == "__main__":
    main()
