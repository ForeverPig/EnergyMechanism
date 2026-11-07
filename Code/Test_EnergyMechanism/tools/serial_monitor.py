"""STM32 energy-mechanism USB CDC monitor. Run: python tools/serial_monitor.py.

Only reads the serial port. All Tk operations run on the main thread.
Python 3.9+, Tkinter, pyserial 3.5. No plotting package is required.
"""

import argparse
import csv
import json
import math
import queue
import random
import re
import sys
import threading
import time
from collections import deque
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional, Tuple
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parent / "_deps"))
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        serial = None
        list_ports = None

STATES = ("校准中", "空闲", "候选", "等待释放", "锁定")
CALIBRATION = ("校准中", "就绪", "波动过大")
COLORS = ("#38bdf8", "#a78bfa", "#34d399", "#fbbf24", "#fb7185",
          "#22d3ee", "#f472b6", "#a3e635", "#fb923c", "#818cf8")
BG, PANEL, FG, MUTED = "#101827", "#182438", "#e6edf7", "#93a6bf"


@dataclass(frozen=True)
class Sensor:
    ring: int
    raw: int
    baseline: int
    delta: int
    peak: int
    state: int


@dataclass(frozen=True)
class Snapshot:
    timestamp: int
    calibration: int
    dma_drops: int
    adc_errors: int
    debug_drops: int
    sensors: Tuple[Sensor, ...]


@dataclass(frozen=True)
class Hit:
    ring: int
    peak: int
    timestamp: int


class ProtocolParser:
    """Publish snapshots only when all ten distinct rings have arrived.

    A fresh S line discards any previous partial frame. E lines can appear
    between sensor rows without breaking the pending snapshot.
    """

    def __init__(self):
        self.header = None
        self.rows = {}
        self.bad_lines = 0
        self.incomplete_frames = 0

    def reset(self):
        if self.header is not None:
            self.incomplete_frames += 1
        self.header, self.rows = None, {}

    def feed_line(self, line: str):
        line = line.strip()
        if not line:
            return None
        try:
            fields = line.split(",")
            if fields[0] == "E":
                if len(fields) != 4:
                    raise ValueError("event field count")
                ring, peak, timestamp = map(int, fields[1:])
                if not (1 <= ring <= 10 and 0 <= peak <= 4095 and
                        0 <= timestamp <= 0xFFFFFFFF):
                    raise ValueError("event range")
                return Hit(ring, peak, timestamp)
            if fields[0] == "S":
                if len(fields) != 6:
                    raise ValueError("header field count")
                values = tuple(map(int, fields[1:]))
                if not (values[1] in range(3) and
                        all(0 <= v <= 0xFFFFFFFF for v in values)):
                    raise ValueError("header range")
                self.reset()
                self.header = values
                return None
            if self.header is None:
                return None  # Connecting in the middle of a frame is normal.
            if len(fields) != 6:
                raise ValueError("sensor field count")
            ring, raw, baseline, delta, peak, state = map(int, fields)
            if not (1 <= ring <= 10 and 0 <= raw <= 4095 and
                    0 <= baseline <= 4095 and -4095 <= delta <= 4095 and
                    0 <= peak <= 4095 and state in range(5)):
                raise ValueError("sensor range")
            if ring in self.rows:
                raise ValueError("duplicate sensor")
            self.rows[ring] = Sensor(ring, raw, baseline, delta, peak, state)
            if len(self.rows) == 10:
                result = Snapshot(*self.header, tuple(self.rows[r] for r in range(1, 11)))
                self.header, self.rows = None, {}
                return result
        except (ValueError, TypeError):
            self.bad_lines += 1
            # An invalid event does not invalidate an otherwise valid snapshot.
            if not line.startswith("E,"):
                self.reset()
        return None


class SerialReader(threading.Thread):
    def __init__(self, port: str, baudrate: int):
        super().__init__(daemon=True, name="USB-CDC-reader")
        self.port, self.baudrate = port, baudrate
        self.stop_event = threading.Event()
        self.snapshots = queue.Queue(maxsize=128)
        self.hits = queue.Queue(maxsize=256)
        self.statuses = queue.Queue(maxsize=16)
        self.parser = ProtocolParser()
        self.host_drops = 0

    def put(self, channel, value):
        try:
            channel.put_nowait(value)
        except queue.Full:
            try:
                channel.get_nowait()
            except queue.Empty:
                pass
            try:
                channel.put_nowait(value)
            except queue.Full:
                pass
            if channel is not self.statuses:
                self.host_drops += 1

    def run(self):
        if serial is None:
            self.put(self.statuses, "缺少 pyserial，请安装 tools/requirements.txt")
            return
        while not self.stop_event.is_set():
            try:
                self.put(self.statuses, "正在连接 " + self.port)
                with serial.Serial(self.port, self.baudrate, timeout=0.2) as stream:
                    self.parser.reset()
                    self.put(self.statuses, "已连接 " + self.port)
                    pending = bytearray()
                    while not self.stop_event.is_set():
                        chunk = stream.read(min(4096, max(1, stream.in_waiting)))
                        pending.extend(chunk)
                        while b"\n" in pending:
                            line, _, rest = pending.partition(b"\n")
                            pending = bytearray(rest)
                            if len(line) > 1024:
                                self.parser.bad_lines += 1
                                self.parser.reset()
                                continue
                            try:
                                decoded = line.decode("ascii")
                            except UnicodeDecodeError:
                                self.parser.bad_lines += 1
                                self.parser.reset()
                                continue
                            item = self.parser.feed_line(decoded)
                            if item is not None:
                                self.put(self.hits if isinstance(item, Hit) else self.snapshots,
                                         (time.monotonic(), item))
                        if len(pending) > 1024:
                            pending.clear()
                            self.parser.bad_lines += 1
                            self.parser.reset()
            except (serial.SerialException, OSError, ValueError) as exc:
                self.parser.reset()
                self.put(self.statuses, f"连接失败/断开：{exc}；2 秒后重试")
                self.stop_event.wait(2.0)
        self.put(self.statuses, "已断开")


def thresholds():
    result = {"APP_HIT_START_DELTA": 80, "APP_HIT_MIN_PEAK": 160}
    try:
        source = (Path(__file__).resolve().parent.parent / "Core/Inc/app_config.h").read_text(encoding="utf-8-sig")
        for key in result:
            match = re.search(r"^\s*#define\s+" + key + r"\s+(\d+)", source, re.MULTILINE)
            if match:
                result[key] = int(match.group(1))
    except OSError:
        pass
    return tuple(result.values())


def sensor_mapping():
    """Local source wiring metadata only; never remap received ring IDs.

    Firmware already applies APP_SENSOR_RING to both S rows and E events.
    ADC1..ADC10 here are net names, not the three ADC peripheral instances.
    """
    try:
        source = (Path(__file__).resolve().parent.parent / "Core/Inc/app_config.h").read_text(encoding="utf-8-sig")
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.DOTALL)
        match = re.search(r"\bAPP_SENSOR_RING\s*\[[^\]]+\]\s*=\s*\{([^}]+)\}", source)
        if match:
            rings = tuple(int(value) for value in re.findall(r"\b(\d+)[uUlL]*\b", match.group(1)))
            if len(rings) == 10 and set(rings) == set(range(1, 11)):
                return rings
    except (OSError, ValueError):
        pass
    return ()


class Monitor:
    def __init__(self, root, port="COM3", baudrate=115200, demo=False):
        self.root, self.baudrate, self.demo = root, baudrate, demo
        self.reader = None
        self.latest = None
        self.history = deque(maxlen=1200)
        self.events = deque(maxlen=300)
        self.hit_counts = [0] * 10
        self.hit_until = [0.0] * 10
        self.selected = 10
        self.last_data = 0.0
        self.record_file = None
        self.record_writer = None
        self.last_flush = 0.0
        self.start_time = time.monotonic()
        self.received_frames = 0
        self.closing = False
        self.start_delta, self.min_peak = thresholds()
        self.sensor_rings = sensor_mapping()
        self.ring_networks = {ring: f"ADC{network}" for network, ring in enumerate(self.sensor_rings, 1)}
        root.title("能量机关 · 十环 USB ADC 实时监控")
        root.configure(bg=BG)
        width = min(1420, root.winfo_screenwidth() - 60)
        height = min(970, root.winfo_screenheight() - 100)
        root.geometry(f"{width}x{height}")
        root.minsize(1050, 750)
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.port = tk.StringVar(value=port)
        self.status = tk.StringVar(value="尚未连接")
        self.stats = tk.StringVar(value="等待完整的十路 ADC 快照…")
        self.details = tk.StringVar(value="点击环或表格可选择通道")
        self.fixed_range = tk.BooleanVar(value=False)
        self.window_seconds = tk.StringVar(value="15")
        self.hit_summary = tk.StringVar(value="等待击打事件")
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=FG, font=("Microsoft YaHei UI", 10))
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 18, "bold"))
        style.configure("Muted.TLabel", foreground=MUTED)
        style.configure("TButton", font=("Microsoft YaHei UI", 10), padding=(10, 5))
        style.configure("TCheckbutton", background=BG, foreground=FG)
        style.map("TCheckbutton", background=[("active", BG)])
        style.configure("Treeview", background=PANEL, fieldbackground=PANEL, foreground=FG,
                        rowheight=25, font=("Microsoft YaHei UI", 9))
        style.configure("Treeview.Heading", background="#26374e", foreground=FG,
                        font=("Microsoft YaHei UI", 9, "bold"))
        style.map("Treeview", background=[("selected", "#365473")])
        outer = ttk.Frame(root, padding=16)
        outer.pack(fill="both", expand=True)
        title = ttk.Frame(outer)
        title.pack(fill="x")
        ttk.Label(title, text="十环实时监控", style="Title.TLabel").pack(side="left")
        ttk.Label(title, textvariable=self.hit_summary).pack(side="right")
        controls = ttk.Frame(outer, padding=(0, 12, 0, 8))
        controls.pack(fill="x")
        ttk.Label(controls, text="USB 串口").pack(side="left")
        self.port_box = ttk.Combobox(controls, textvariable=self.port, width=10)
        self.port_box.pack(side="left", padx=8)
        ttk.Button(controls, text="刷新", command=self.refresh_ports).pack(side="left")
        self.connect_button = ttk.Button(controls, text="连接", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=8)
        self.record_button = ttk.Button(controls, text="保存 CSV…", command=self.toggle_record)
        self.record_button.pack(side="left", padx=8)
        ttk.Label(controls, text="曲线窗口 / 秒").pack(side="left", padx=(16, 4))
        ttk.Combobox(controls, textvariable=self.window_seconds, values=("5", "15", "30", "60"),
                     state="readonly", width=4).pack(side="left")
        ttk.Checkbutton(controls, text="ADC 固定范围 0～4095", variable=self.fixed_range).pack(side="left", padx=12)
        ttk.Label(outer, textvariable=self.stats, style="Muted.TLabel").pack(fill="x", pady=(0, 10))
        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=0, minsize=455)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)
        left = ttk.Frame(body)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 16))
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)
        ttk.Label(left, text="环形位置 · 击打后高亮 2 秒").grid(row=0, column=0, sticky="w")
        self.ring_canvas = tk.Canvas(left, bg=PANEL, highlightthickness=0, height=330)
        self.ring_canvas.grid(row=1, column=0, sticky="nsew", pady=8)
        self.ring_canvas.bind("<Button-1>", self.pick_ring)
        mapping_text = ("源码 ADC1～10 → " + ",".join(map(str, self.sensor_rings)) + " 环"
                        if self.sensor_rings else "未读取到源码 ADC 网络映射")
        ttk.Label(left, text="报文环号直接显示；击打点为示意位置。\n" + mapping_text,
                  wraplength=450, style="Muted.TLabel").grid(row=2, column=0, sticky="w")
        self.table = ttk.Treeview(left, columns=("ring", "network", "raw", "base", "delta", "peak", "state", "hits"),
                                  show="headings", height=10, selectmode="browse")
        for col, label, width in zip(self.table["columns"],
                                    ("环", "ADC网络", "ADC raw", "基线", "Δ", "峰值", "状态", "命中"),
                                    (30, 58, 64, 56, 45, 45, 70, 40)):
            self.table.heading(col, text=label)
            self.table.column(col, width=width, anchor="center", stretch=True)
        for ring in range(10, 0, -1):
            self.table.insert("", "end", iid=str(ring),
                              values=(ring, self.ring_networks.get(ring, "—"), "—", "—", "—", "—", "—", 0))
            self.table.tag_configure(str(ring), foreground=COLORS[ring - 1])
            self.table.item(str(ring), tags=(str(ring),))
        self.table.grid(row=3, column=0, sticky="ew", pady=(12, 8))
        self.table.bind("<<TreeviewSelect>>", self.pick_table)
        ttk.Label(left, textvariable=self.details, wraplength=450, style="Muted.TLabel").grid(row=4, column=0, sticky="w")
        right = ttk.Frame(body)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(2, weight=3)
        right.rowconfigure(4, weight=2)
        ttk.Label(right, text="ADC 原始值 · 十路快照曲线").grid(row=0, column=0, sticky="w")
        legend = ttk.Frame(right)
        legend.grid(row=1, column=0, sticky="ew", pady=6)
        for ring, color in enumerate(COLORS, 1):
            tk.Label(legend, text=f"● {ring}环", bg=BG, fg=color,
                     font=("Microsoft YaHei UI", 9)).pack(side="left", padx=4)
        self.raw_canvas = tk.Canvas(right, bg=PANEL, highlightthickness=0, height=230)
        self.raw_canvas.grid(row=2, column=0, sticky="nsew")
        ttk.Label(right, text=f"基线差值 Δ · 虚线为源码阈值 {self.start_delta}/{self.min_peak}；圆点为事件峰值").grid(row=3, column=0, sticky="w", pady=(12, 6))
        self.delta_canvas = tk.Canvas(right, bg=PANEL, highlightthickness=0, height=180)
        self.delta_canvas.grid(row=4, column=0, sticky="nsew")
        ttk.Label(right, text="击打记录 · 最新事件在最上方").grid(row=5, column=0, sticky="w", pady=(12, 6))
        self.event_table = ttk.Treeview(right, columns=("pc", "ring", "peak", "time"), show="headings", height=6)
        for col, label in zip(self.event_table["columns"], ("接收时间", "击打环", "事件峰值 / ADC", "MCU 时间 / ms")):
            self.event_table.heading(col, text=label)
            self.event_table.column(col, width=110, anchor="center")
        self.event_table.grid(row=6, column=0, sticky="ew")
        footer = ttk.Frame(outer, padding=(0, 10, 0, 0))
        footer.pack(fill="x")
        ttk.Label(footer, textvariable=self.status).pack(side="left")
        ttk.Label(footer, text="快照约 20 Hz · 原始采集 10 kHz · 时间轴为 PC 接收时间", style="Muted.TLabel").pack(side="right")
        self.refresh_ports()
        if demo:
            self.status.set("演示模式 · 模拟数据")
            self.connect_button.configure(state="disabled")
            self.demo_tick()
        else:
            self.connect()
        self.root.after(50, self.poll)
        self.root.after(100, self.redraw)

    def refresh_ports(self):
        self.port_box["values"] = tuple(p.device for p in list_ports.comports()) if list_ports else ()

    def connect(self):
        if serial is None:
            self.status.set("缺少 pyserial，运行 python -m pip install -r tools/requirements.txt")
            return
        port = self.port.get().strip()
        if not port:
            return
        self.reader = SerialReader(port, self.baudrate)
        self.reader.start()
        self.connect_button.configure(text="断开")
        self.port_box.configure(state="disabled")

    def toggle_connection(self):
        if self.reader is None:
            self.connect()
        else:
            self.reader.stop_event.set()
            self.connect_button.configure(state="disabled")
            self.status.set("正在断开…")
            self.finish_disconnect()

    def finish_disconnect(self):
        if self.reader and self.reader.is_alive():
            self.root.after(50, self.finish_disconnect)
            return
        self.reader = None
        self.port_box.configure(state="normal")
        self.connect_button.configure(text="连接", state="normal")
        self.status.set("已断开")

    def toggle_record(self):
        if self.record_file:
            self.stop_record()
            return
        path = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")],
                                           initialfile=datetime.now().strftime("energy_%Y%m%d_%H%M%S.csv"))
        if path:
            try:
                self.record_file = open(path, "w", newline="", encoding="utf-8-sig")
                self.record_writer = csv.writer(self.record_file)
                self.record_writer.writerow(("type", "pc_time", "mcu_ms", "ring", "raw", "baseline", "delta", "peak", "state", "calibration", "dma_drops", "adc_errors", "debug_drops"))
                self.record_button.configure(text="停止保存 CSV")
            except OSError as exc:
                self.stop_record()
                messagebox.showerror("无法保存", str(exc))

    def stop_record(self):
        if self.record_file:
            try:
                self.record_file.close()
            except OSError:
                pass
        self.record_file = self.record_writer = None
        self.record_button.configure(text="保存 CSV…")

    def record(self, item):
        if self.record_writer is None:
            return
        try:
            pc_time = datetime.now().isoformat(timespec="milliseconds")
            if isinstance(item, Hit):
                self.record_writer.writerow(("E", pc_time, item.timestamp, item.ring, "", "", "", item.peak, "", "", "", "", ""))
            else:
                for sensor in item.sensors:
                    self.record_writer.writerow(("S", pc_time, item.timestamp, sensor.ring, sensor.raw,
                                                 sensor.baseline, sensor.delta, sensor.peak, sensor.state,
                                                 item.calibration, item.dma_drops, item.adc_errors, item.debug_drops))
            if time.monotonic() - self.last_flush > 1:
                self.record_file.flush()
                self.last_flush = time.monotonic()
        except OSError as exc:
            self.stop_record()
            self.status.set("CSV 保存失败：" + str(exc))

    def receive_snapshot(self, received, snapshot):
        self.latest = snapshot
        self.history.append((received, snapshot))
        self.last_data = received
        self.received_frames += 1
        self.record(snapshot)

    def receive_hit(self, received, hit):
        self.events.append((received, hit))
        self.hit_counts[hit.ring - 1] += 1
        self.hit_until[hit.ring - 1] = time.monotonic() + 2.0
        self.hit_summary.set(f"最近击打：{hit.ring} 环   峰值 {hit.peak}   MCU {hit.timestamp} ms")
        self.event_table.insert("", 0, values=(datetime.now().strftime("%H:%M:%S.%f")[:-3], hit.ring, hit.peak, hit.timestamp))
        children = self.event_table.get_children()
        if len(children) > 100:
            self.event_table.delete(*children[100:])
        self.record(hit)

    def poll(self):
        if self.closing:
            return
        if self.reader:
            for channel, handler in ((self.reader.statuses, self.status.set),
                                     (self.reader.snapshots, self.receive_snapshot),
                                     (self.reader.hits, self.receive_hit)):
                for _ in range(256):
                    try:
                        item = channel.get_nowait()
                    except queue.Empty:
                        break
                    if channel is self.reader.statuses:
                        handler(item)
                    else:
                        handler(*item)
        if self.latest:
            s = self.latest
            age = time.monotonic() - self.last_data
            stale = f" · 已 {age:.1f}s 无数据" if age > 1 else ""
            host = ""
            if self.reader:
                p = self.reader.parser
                host = f" · PC丢包 {self.reader.host_drops} / 非法行 {p.bad_lines} / 不完整帧 {p.incomplete_frames}"
            self.stats.set(f"MCU {s.timestamp} ms · {CALIBRATION[s.calibration]} · 已收 {self.received_frames} 帧 · DMA丢块 {s.dma_drops} / ADC错误 {s.adc_errors} / 调试丢帧 {s.debug_drops}" + host + stale)
            for sensor in s.sensors:
                self.table.item(str(sensor.ring), values=(sensor.ring, self.ring_networks.get(sensor.ring, "—"),
                                                         sensor.raw, sensor.baseline,
                                                         sensor.delta, sensor.peak, STATES[sensor.state],
                                                         self.hit_counts[sensor.ring - 1]))
            sensor = s.sensors[self.selected - 1]
            network = self.ring_networks.get(sensor.ring, "未知")
            self.details.set(f"当前 {sensor.ring} 环 / 源码网络 {network}：raw={sensor.raw}  baseline={sensor.baseline}\nΔ={sensor.delta}  最近候选峰值={sensor.peak}  状态={STATES[sensor.state]}")
        self.root.after(50, self.poll)

    def pick_table(self, _event):
        selected = self.table.selection()
        if selected:
            self.selected = int(selected[0])

    def pick_ring(self, event):
        width, height = self.ring_canvas.winfo_width(), self.ring_canvas.winfo_height()
        step = max(1, (min(width, height) / 2 - 15) / 10)
        distance = math.hypot(event.x - width / 2, event.y - height / 2)
        if distance <= step * 10:
            self.selected = min(10, max(1, 11 - math.ceil(distance / step)))
            self.table.selection_set(str(self.selected))

    def draw_rings(self):
        canvas = self.ring_canvas
        canvas.delete("all")
        width, height = canvas.winfo_width(), canvas.winfo_height()
        cx, cy = width / 2, height / 2
        step = max(1, (min(width, height) / 2 - 15) / 10)
        now = time.monotonic()
        for ring in range(1, 11):
            radius = (11 - ring) * step
            hit = self.hit_until[ring - 1] > now
            color = COLORS[ring - 1]
            raw = self.latest.sensors[ring - 1].raw if self.latest else 0
            intensity = 0.12 + 0.25 * raw / 4095
            rgb = tuple(int(int(color[i:i + 2], 16) * intensity) for i in (1, 3, 5))
            fill = "#%02x%02x%02x" % rgb
            outline = "#ff9d4d" if hit else (FG if self.selected == ring else color)
            canvas.create_oval(cx - radius, cy - radius, cx + radius, cy + radius,
                               fill="#774323" if hit else fill, outline=outline,
                               width=3 if hit or self.selected == ring else 1)
        for ring in range(1, 11):
            radius = (11 - ring) * step
            canvas.create_text(cx - radius + step / 2 if ring != 10 else cx, cy,
                               text=str(ring), fill=FG, font=("Segoe UI", 9, "bold"))
            if self.hit_until[ring - 1] > now:
                x = cx + radius - step / 2
                canvas.create_oval(x - 5, cy - 5, x + 5, cy + 5, fill="#ffb261", outline="white", width=2)

    def draw_plot(self, canvas, is_delta):
        canvas.delete("all")
        width, height = canvas.winfo_width(), canvas.winfo_height()
        x0, y0, x1, y1 = 58, 20, width - 18, height - 32
        if x1 <= x0 or y1 <= y0:
            return
        span = float(self.window_seconds.get())
        now = time.monotonic()
        samples = [(t, s) for t, s in self.history if now - span <= t <= now]
        events = [(t, e) for t, e in self.events if now - span <= t <= now]
        values = [getattr(sensor, "delta" if is_delta else "raw")
                  for _, frame in samples for sensor in frame.sensors]
        if is_delta:
            values.extend(hit.peak for _, hit in events)
            low, high = min(values + [-20]), max(values + [self.min_peak + 20])
            padding = max(15, (high - low) * 0.08)
            low, high = low - padding, high + padding
        elif self.fixed_range.get() or not values:
            low, high = 0, 4095
        else:
            low, high = min(values), max(values)
            padding = max(32, (high - low) * 0.1)
            low, high = max(0, low - padding), min(4095, high + padding)
        def px(t):
            return x0 + (t - (now - span)) / span * (x1 - x0)
        def py(value):
            return y1 - (value - low) / max(1, high - low) * (y1 - y0)
        for i in range(5):
            value = low + i / 4 * (high - low)
            y = py(value)
            canvas.create_line(x0, y, x1, y, fill="#2a3a51")
            canvas.create_text(x0 - 8, y, text=f"{value:.0f}", fill=MUTED, anchor="e", font=("Segoe UI", 9))
        for i in range(5):
            x = x0 + i / 4 * (x1 - x0)
            canvas.create_line(x, y0, x, y1, fill="#223149")
            canvas.create_text(x, y1 + 15, text=f"{-span + i * span / 4:g}s", fill=MUTED, font=("Segoe UI", 9))
        if is_delta:
            for threshold in (0, self.start_delta, self.min_peak):
                y = py(threshold)
                canvas.create_line(x0, y, x1, y, fill="#9a835a" if threshold else MUTED, dash=(5, 4))
                canvas.create_text(x1 - 3, y - 7, text=str(threshold), fill="#dac08c", anchor="e", font=("Segoe UI", 9))
        # Draw the selected ring last so its curve remains visible.
        order = [ring for ring in range(1, 11) if ring != self.selected] + [self.selected]
        for ring in order:
            coords = []
            previous = None
            for t, snapshot in samples:
                if previous is not None and t - previous > 0.5:
                    if len(coords) >= 4:
                        canvas.create_line(*coords, fill=COLORS[ring - 1], width=2 if ring == self.selected else 1)
                    coords = []  # Do not connect across unplug/reconnect gaps.
                sensor = snapshot.sensors[ring - 1]
                coords.extend((px(t), py(sensor.delta if is_delta else sensor.raw)))
                previous = t
            if len(coords) >= 4:
                canvas.create_line(*coords, fill=COLORS[ring - 1], width=2 if ring == self.selected else 1)
        if is_delta:
            for t, hit in events:
                x, y = px(t), py(hit.peak)
                canvas.create_oval(x - 4, y - 4, x + 4, y + 4, fill=COLORS[hit.ring - 1], outline=FG)
                canvas.create_text(x, y - 13, text=f"{hit.ring}环", fill=FG, font=("Microsoft YaHei UI", 9))
        if not samples:
            canvas.create_text((x0 + x1) / 2, (y0 + y1) / 2, text="等待串口数据…", fill=MUTED)

    def redraw(self):
        if self.closing:
            return
        self.draw_rings()
        self.draw_plot(self.raw_canvas, False)
        self.draw_plot(self.delta_canvas, True)
        self.root.after(100, self.redraw)

    def demo_tick(self):
        if self.closing:
            return
        now = time.monotonic()
        elapsed = now - self.start_time
        timestamp = int(elapsed * 1000) & 0xFFFFFFFF
        ring = int(elapsed // 3) % 10 + 1
        phase = elapsed % 3
        pulse = max(0, 1 - abs(phase - 1) / 0.12)
        sensors = []
        for index in range(1, 11):
            base = 35 + index * 12
            delta = random.randint(-4, 4) + int(550 * pulse if index == ring else 0)
            sensors.append(Sensor(index, base + delta, base, delta, 550 if index == ring else 0,
                                  2 if index == ring and pulse else 1))
        self.receive_snapshot(now, Snapshot(timestamp, 1, 0, 0, 0, tuple(sensors)))
        cycle = int(elapsed // 3)
        if phase > 1.12 and getattr(self, "demo_last_hit", -1) != cycle:
            self.demo_last_hit = cycle
            self.receive_hit(now, Hit(ring, 550, timestamp))
        self.root.after(50, self.demo_tick)

    def close(self):
        if self.closing:
            return
        self.closing = True
        self.stop_record()
        if self.reader:
            self.reader.stop_event.set()
        self.finish_close()

    def finish_close(self):
        if self.reader and self.reader.is_alive():
            self.root.after(50, self.finish_close)
        else:
            self.root.destroy()


def capture(port, baudrate, seconds):
    """Headless real-port diagnostic, useful before launching the GUI."""
    reader = SerialReader(port, baudrate)
    reader.start()
    frames, hits, latest, status = 0, 0, None, ""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for channel in (reader.statuses, reader.snapshots, reader.hits):
            while True:
                try:
                    item = channel.get_nowait()
                except queue.Empty:
                    break
                if channel is reader.statuses:
                    status = item
                elif channel is reader.snapshots:
                    frames += 1
                    latest = item[1]
                else:
                    hits += 1
        time.sleep(0.02)
    reader.stop_event.set()
    reader.join(timeout=2)
    print(json.dumps({"port": port, "status": status, "frames": frames, "hits": hits,
                      "bad_lines": reader.parser.bad_lines,
                      "incomplete_frames": reader.parser.incomplete_frames,
                      "host_drops": reader.host_drops,
                      "latest": asdict(latest) if latest else None}, ensure_ascii=False, indent=2))
    return 0 if frames else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baudrate", type=int, default=115200, help="USB CDC ignores physical UART baudrate")
    parser.add_argument("--capture", type=float, metavar="SECONDS", help="Read actual serial data without a GUI")
    parser.add_argument("--demo", action="store_true", help="Show simulated data; do not open a serial port")
    parser.add_argument("--smoke-test", action="store_true", help="Construct/draw GUI with demo data, then close")
    args = parser.parse_args()
    if args.capture is not None:
        if args.capture <= 0:
            parser.error("--capture must be positive")
        return capture(args.port, args.baudrate, args.capture)
    root = tk.Tk()
    failed = []
    def callback_error(exc_type, exc_value, traceback):
        import traceback as traceback_module
        failed.append(str(exc_value))
        traceback_module.print_exception(exc_type, exc_value, traceback)
    root.report_callback_exception = callback_error
    app = Monitor(root, args.port, args.baudrate, args.demo or args.smoke_test)
    if args.smoke_test:
        root.after(1500, app.close)
    root.mainloop()
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
