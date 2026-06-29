"""S800 智能联网时钟上位机，学生：姜圣基 523170910011。"""

import json
import math
import csv
import queue
import socket
import struct
import subprocess
import threading
import time
import tkinter as tk
from datetime import datetime
from datetime import timedelta, timezone
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import requests
import serial
from serial.tools import list_ports


KEY_NAMES = ("FUNC", "SHIFT", "ADD", "SAVE", "DISP", "SPEED", "FORMAT", "EXT")
SEGMENTS = {
    "0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg", "4": "bcfg",
    "5": "acdfg", "6": "acdefg", "7": "abc", "8": "abcdefg", "9": "abcdfg",
    "A": "abcefg", "B": "cdefg", "C": "adef", "D": "bcdeg", "E": "adefg",
    "F": "aefg", "G": "acdef", "H": "bcefg", "I": "bc", "J": "bcde", "L": "def", "N": "ceg",
    "O": "abcdef", "P": "abefg", "R": "eg", "S": "acdfg", "T": "defg",
    "U": "bcdef", "Y": "bcdfg", "-": "g", "_": "",
}


class SevenSegment(tk.Canvas):
    """单个七段数码管，支持独立小数点。"""

    def __init__(self, master, scale=0.78):
        super().__init__(master, width=int(75 * scale), height=int(125 * scale), bg="#101010",
                         highlightthickness=0)
        self.parts = {}
        def point(value):
            return int(value * scale)

        polygons = {
            "a": (15, 8, 57, 8, 63, 14, 57, 20, 15, 20, 9, 14),
            "b": (60, 17, 66, 23, 66, 55, 60, 61, 54, 55, 54, 23),
            "c": (60, 64, 66, 70, 66, 102, 60, 108, 54, 102, 54, 70),
            "d": (15, 105, 57, 105, 63, 111, 57, 117, 15, 117, 9, 111),
            "e": (12, 64, 18, 70, 18, 102, 12, 108, 6, 102, 6, 70),
            "f": (12, 17, 18, 23, 18, 55, 12, 61, 6, 55, 6, 23),
            "g": (15, 57, 57, 57, 63, 63, 57, 69, 15, 69, 9, 63),
        }
        for name, points in polygons.items():
            scaled = tuple(point(value) for value in points)
            self.parts[name] = self.create_polygon(scaled, fill="#301010", outline="")
        self.parts["dp"] = self.create_oval(
            point(64), point(108), point(73), point(117), fill="#301010", outline=""
        )

    def set_value(self, char, decimal=False, enabled=True):
        active = SEGMENTS.get(char.upper(), "") if enabled else ""
        for name in "abcdefg":
            self.itemconfigure(self.parts[name], fill="#ff3028" if name in active else "#301010")
        self.itemconfigure(self.parts["dp"], fill="#ff3028" if decimal and enabled else "#301010")


class S800ClockApp:
    def __init__(self, root):
        self.root = root
        self.root.title("S800 智能联网时钟 - JIANGSJ")
        self.root.geometry("1180x820")
        try:
            self.root.state("zoomed")
        except tk.TclError:
            self.root.attributes("-fullscreen", True)
        self.serial = None
        self.rx_queue = queue.Queue()
        self.stop_reader = threading.Event()
        self.last_ports = []
        self.ping_sent_at = None
        self.last_rx_at = 0.0
        self.heartbeat_timed_out = False
        self.next_tx_time = 0.0
        self.last_weather_temp = None
        self.last_weather_label = None
        self.suppress_next_user2_event = False
        self.next_auto_mode_check = 0.0
        self.last_auto_mode_sent = None
        self.event_file = Path(__file__).with_name("s800_event_log.jsonl")
        self.event_records = self.load_event_records()
        self.dashboard_window = None
        self.display_text = "________"
        self.dp_value = 0
        self.led_value = 0

        self.port_var = tk.StringVar()
        self.connection_var = tk.StringVar(value="连接：未连接")
        self.format_var = tk.StringVar(value="FORMAT：LEFT")
        self.mode_var = tk.StringVar(value="MODE：DAY")
        self.alarm_var = tk.StringVar(value="ALARM：OFF")
        self.alarm_detail_var = tk.StringVar(value="闹钟：07:30:00 / OFF")
        self.last_command_var = tk.StringVar(value="命令：--")
        self.latency_var = tk.StringVar(value="延迟：-- ms")
        self.time_var = tk.StringVar(value="12 34 56")
        self.date_var = tk.StringVar(value="2026 06 04")
        self.alarm_time_var = tk.StringVar(value="07 30 00")
        self.message_var = tk.StringVar(value="Hello.S800")
        self.beep_var = tk.StringVar(value="500")
        self.combo_var = tk.StringVar(value="日期：YEAR MONTH DATE")
        self.weather_var = tk.StringVar(value="天气：--")
        self.lat_var = tk.StringVar(value="31.1120")
        self.lon_var = tk.StringVar(value="121.3817")
        self.focus_var = tk.StringVar(value="FOCUS: OFF")
        self.focus_minutes_var = tk.StringVar(value="40")
        self.focus_count_var = tk.StringVar(value="Done: 0")
        self.focus_remaining_var = tk.StringVar(value="Left: --")
        self.flip_var = tk.StringVar(value="FLIP：OFF")
        self.auto_mode_enabled_var = tk.BooleanVar(value=True)
        self.auto_mode_var = tk.StringVar(value="AUTO：ON")
        self.event_count_var = tk.StringVar(value=f"记录：{len(self.event_records)}")

        self.digits, self.led_canvases, self.key_buttons = [], [], {}
        self.build_ui()
        self.refresh_ports()
        self.poll_rx()
        self.periodic_tasks()

    def build_ui(self):
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(3, weight=1)

        top = ttk.LabelFrame(self.root, text="串口", padding=8)
        top.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 4))
        ttk.Label(top, text="串口：").grid(row=0, column=0, sticky="w", padx=2, pady=2)
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=18, state="readonly")
        self.port_box.grid(row=0, column=1, sticky="w", padx=2, pady=2)
        ttk.Button(top, text="刷新", command=self.refresh_ports).grid(row=0, column=2, padx=4, pady=2)
        ttk.Button(top, text="连接", command=self.connect).grid(row=0, column=3, padx=4, pady=2)
        ttk.Button(top, text="断开", command=self.disconnect).grid(row=0, column=4, padx=4, pady=2)

        mirror = ttk.LabelFrame(self.root, text="数字孪生镜像", padding=4)
        mirror.grid(row=1, column=0, sticky="ew", padx=8, pady=4)
        digit_frame = tk.Frame(mirror, bg="#101010")
        digit_frame.pack(fill="x", ipady=1)
        for _ in range(8):
            digit = SevenSegment(digit_frame)
            digit.pack(side="left", expand=True, pady=1)
            self.digits.append(digit)

        info_bar = tk.Frame(mirror, bg="#f5f5f5", padx=8, pady=3)
        info_bar.pack(fill="x", pady=(3, 1))
        ttk.Label(info_bar, textvariable=self.alarm_detail_var,
                  font=("Microsoft YaHei UI", 11, "bold")).pack(side="left", padx=(4, 20))
        ttk.Label(info_bar, textvariable=self.weather_var).pack(side="left", padx=(0, 20))
        ttk.Label(info_bar, textvariable=self.focus_var).pack(side="left", padx=(0, 20))
        ttk.Label(info_bar, textvariable=self.mode_var).pack(side="left", padx=(0, 20))
        ttk.Label(info_bar, textvariable=self.flip_var).pack(side="left", padx=(0, 20))
        ttk.Label(info_bar, text="LED2 闹钟启用 | LED3 正在响铃 | LED4 对时成功").pack(side="left")

        led_frame = ttk.Frame(mirror)
        led_frame.pack(fill="x", pady=(2, 0))
        for index in range(8):
            canvas = tk.Canvas(led_frame, width=80, height=44, highlightthickness=0)
            canvas.create_oval(29, 3, 51, 25, fill="#303030", tags="lamp")
            canvas.create_text(40, 38, text=f"LED{index + 1}", anchor="center")
            canvas.pack(side="left", expand=True)
            self.led_canvases.append(canvas)

        key_frame = ttk.LabelFrame(self.root, text="虚拟按键", padding=8)
        key_frame.grid(row=2, column=0, sticky="ew", padx=8, pady=4)
        for index, name in enumerate(KEY_NAMES + ("USER1", "USER2")):
            button = ttk.Button(key_frame, text=name, command=lambda n=name: self.send(f"*SET:KEY {n}"))
            button.grid(row=0, column=index, sticky="ew", padx=3, pady=3)
            self.key_buttons[name] = button
            key_frame.columnconfigure(index, weight=1)

        bottom = ttk.PanedWindow(self.root, orient="horizontal")
        bottom.grid(row=3, column=0, sticky="nsew", padx=8, pady=4)
        left_panel = ttk.Frame(bottom, padding=(0, 0, 6, 0))
        right_panel = ttk.Frame(bottom, padding=(6, 0, 0, 0))
        bottom.add(left_panel, weight=1)
        bottom.add(right_panel, weight=1)

        controls = ttk.LabelFrame(left_panel, text="控制面板", padding=3)
        controls.pack(fill="both", expand=True)

        clock_box = ttk.LabelFrame(controls, text="时间 / 日期 / 闹钟", padding=2)
        clock_box.pack(fill="x", pady=0)
        ttk.Label(clock_box, text="时间").pack(side="left")
        ttk.Entry(clock_box, textvariable=self.time_var, width=9).pack(side="left", padx=3)
        ttk.Button(clock_box, text="设置时间", command=self.set_time).pack(side="left", padx=3)
        ttk.Label(clock_box, text="日期").pack(side="left", padx=(8, 0))
        ttk.Entry(clock_box, textvariable=self.date_var, width=10).pack(side="left", padx=3)
        ttk.Button(clock_box, text="设置日期", command=self.set_date).pack(side="left", padx=3)
        ttk.Label(clock_box, text="闹钟").pack(side="left", padx=(8, 0))
        ttk.Entry(clock_box, textvariable=self.alarm_time_var, width=9).pack(side="left", padx=3)
        ttk.Button(clock_box, text="设置闹钟", command=self.set_alarm).pack(side="left", padx=3)
        ttk.Button(clock_box, text="关闭闹钟", command=self.close_alarm).pack(side="left", padx=3)
        ttk.Button(clock_box, text="复位", command=self.reset_board).pack(side="left", padx=3)

        display_box = ttk.LabelFrame(controls, text="显示 / 方向 / 模式", padding=2)
        display_box.pack(fill="x", pady=0)
        for text, command in (
            ("显示 ON", "*SET:DISPLAY ON"), ("显示 OFF", "*SET:DISPLAY OFF"),
            ("LEFT", "*SET:FORMAT LEFT"), ("RIGHT", "*SET:FORMAT RIGHT"),
            ("DAY", "*SET:MODE DAY"), ("NIGHT", "*SET:MODE NIGHT"),
        ):
            ttk.Button(display_box, text=text, command=lambda c=command: self.send(c)).pack(side="left", padx=2)
        ttk.Checkbutton(display_box, text="自动昼夜", variable=self.auto_mode_enabled_var,
                        command=self.on_auto_mode_toggle).pack(side="left", padx=(12, 2))
        ttk.Label(display_box, textvariable=self.auto_mode_var).pack(side="left", padx=2)

        query_box = ttk.LabelFrame(controls, text="状态查询", padding=2)
        query_box.pack(fill="x", pady=0)
        for text, command in (
            ("GET TIME", "*GET:TIME"), ("GET DATE", "*GET:DATE"),
            ("GET ALARM", "*GET:ALARM"), ("GET DISPLAY", "*GET:DISPLAY"),
            ("GET FORMAT", "*GET:FORMAT"), ("GET FLIP", "*GET:FLIP"),
            ("GET MODE", "*GET:MODE"),
        ):
            ttk.Button(query_box, text=text, command=lambda c=command: self.send(c)).pack(side="left", padx=2)

        message_box = ttk.LabelFrame(controls, text="消息 / 蜂鸣", padding=2)
        message_box.pack(fill="x", pady=0)
        ttk.Label(message_box, text="消息").pack(side="left")
        ttk.Entry(message_box, textvariable=self.message_var, width=22).pack(side="left", padx=3)
        ttk.Button(message_box, text="发送消息", command=lambda: self.send("*SET:MSG " + self.message_var.get())).pack(side="left", padx=3)
        ttk.Button(message_box, text="返回时钟", command=lambda: self.send("*SET:MSG")).pack(side="left", padx=3)
        ttk.Label(message_box, text="蜂鸣").pack(side="left", padx=(12, 0))
        ttk.Entry(message_box, textvariable=self.beep_var, width=7).pack(side="left", padx=3)
        ttk.Button(message_box, text="蜂鸣 ms", command=lambda: self.send("*SET:BEEP " + self.beep_var.get())).pack(side="left", padx=3)

        ext_box = ttk.LabelFrame(controls, text="扩展功能", padding=2)
        ext_box.pack(fill="x", pady=0)
        ttk.Button(ext_box, text="网络对时", command=self.sync_ntp).pack(side="left", padx=3)
        ttk.Button(ext_box, text="获取天气", command=self.fetch_weather).pack(side="left", padx=3)
        ttk.Label(ext_box, text="纬度").pack(side="left", padx=(12, 2))
        ttk.Entry(ext_box, textvariable=self.lat_var, width=9).pack(side="left", padx=2)
        ttk.Label(ext_box, text="经度").pack(side="left", padx=(8, 2))
        ttk.Entry(ext_box, textvariable=self.lon_var, width=9).pack(side="left", padx=2)
        ttk.Button(ext_box, text="语音播报", command=self.speak_status).pack(side="left", padx=3)

        music_box = ttk.LabelFrame(controls, text="音乐播放", padding=2)
        music_box.pack(fill="x", pady=0)
        ttk.Button(music_box, text="起风了", command=lambda: self.send("*SET:MUSIC WIND")).pack(side="left", padx=3)
        ttk.Button(music_box, text="停止音乐", command=lambda: self.send("*SET:MUSIC STOP")).pack(side="left", padx=3)

        focus_box = ttk.LabelFrame(controls, text="专注模式", padding=2)
        focus_box.pack(fill="x", pady=0)
        ttk.Label(focus_box, text="时长").pack(side="left", padx=(0, 2))
        self.focus_minutes_entry = ttk.Entry(focus_box, textvariable=self.focus_minutes_var, width=4)
        self.focus_minutes_entry.pack(side="left", padx=3)
        ttk.Button(focus_box, text="设置", command=self.set_focus_minutes).pack(side="left", padx=2)
        ttk.Button(focus_box, text="开始", command=self.start_focus).pack(side="left", padx=2)
        ttk.Button(focus_box, text="关闭", command=self.stop_focus).pack(side="left", padx=2)
        ttk.Label(focus_box, textvariable=self.focus_count_var).pack(side="left", padx=(8, 4))
        ttk.Label(focus_box, textvariable=self.focus_remaining_var).pack(side="left")

        compact_row = ttk.Frame(controls)
        compact_row.pack(fill="x", pady=0)
        compact_row.columnconfigure(0, weight=1)
        compact_row.columnconfigure(1, weight=1)

        data_box = ttk.LabelFrame(compact_row, text="数据看板", padding=2)
        data_box.grid(row=0, column=0, sticky="ew", padx=(0, 3))
        ttk.Button(data_box, text="打开看板", command=self.show_dashboard).pack(side="left", padx=3)
        ttk.Button(data_box, text="导出 CSV", command=self.export_event_csv).pack(side="left", padx=3)
        ttk.Button(data_box, text="清空记录", command=self.clear_event_records).pack(side="left", padx=3)
        ttk.Label(data_box, textvariable=self.event_count_var).pack(side="left", padx=(10, 0))

        demos = ttk.LabelFrame(compact_row, text="协议演示", padding=2)
        demos.grid(row=0, column=1, sticky="ew", padx=(3, 0))
        combos = ("日期：YEAR MONTH DATE", "日期：YEAR DATE", "日期：MONTH DATE", "时间：HOUR MIN SEC")
        ttk.Combobox(demos, values=combos, textvariable=self.combo_var, width=24, state="readonly").pack(side="left")
        ttk.Button(demos, text="发送参数组合", command=self.send_combo).pack(side="left", padx=3)
        ttk.Button(demos, text="缩写演示", command=lambda: self.send("*SET:TIME HOUR MIN SEC 12 30 45")).pack(side="left", padx=3)
        ttk.Button(demos, text="大小写混合演示", command=lambda: self.send("*sEt:FoRmAt rIgHt")).pack(side="left", padx=3)

        command_box = ttk.LabelFrame(right_panel, text="串口命令与收发日志", padding=8)
        command_box.pack(fill="both", expand=True)
        command_line = ttk.Frame(command_box)
        command_line.pack(fill="x")
        self.command_entry = ttk.Entry(command_line)
        self.command_entry.insert(0, "*GET:TIME")
        self.command_entry.pack(side="left", fill="x", expand=True)
        self.command_entry.bind("<Return>", lambda _event: self.send_command())
        ttk.Button(command_line, text="发送", command=self.send_command).pack(side="left", padx=3)
        ttk.Button(command_line, text="导出日志", command=self.export_log).pack(side="left", padx=3)
        ttk.Button(command_line, text="清空日志", command=self.clear_log).pack(side="left", padx=3)
        self.log = tk.Text(command_box, height=24, state="disabled", font=("Consolas", 10), wrap="none")
        self.log.pack(fill="both", expand=True, pady=(5, 0))
        for tag, color in (("tx", "#1565c0"), ("response", "#2e7d32"), ("event", "#6a1b9a"),
                           ("error", "#d32f2f"), ("info", "#555555")):
            self.log.tag_configure(tag, foreground=color)

        status = ttk.Frame(self.root, padding=8)
        status.grid(row=4, column=0, sticky="ew")
        for variable in (self.connection_var, self.format_var, self.mode_var,
                         self.auto_mode_var, self.alarm_var, self.focus_var,
                         self.latency_var, self.last_command_var):
            ttk.Label(status, textvariable=variable).pack(side="left", padx=8)

        self.render_display()
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def refresh_ports(self):
        ports = [port.device for port in list_ports.comports()]
        self.port_box["values"] = ports
        if ports != self.last_ports:
            self.last_ports = ports
            if ports and self.port_var.get() not in ports:
                self.port_var.set(ports[0])

    def connect(self):
        self.disconnect()
        if not self.port_var.get():
            messagebox.showwarning("串口", "没有检测到可用 COM 口。")
            return
        try:
            self.serial = serial.Serial(self.port_var.get(), 115200, timeout=0.2)
        except (serial.SerialException, OSError) as exc:
            self.report_error("串口连接失败", exc, popup=True)
            return
        self.stop_reader.clear()
        self.last_rx_at = time.monotonic()
        self.heartbeat_timed_out = False
        self.connection_var.set(f"连接：{self.port_var.get()} @115200")
        self.append_log(f"已连接 {self.port_var.get()}", "info")
        self.record_event("对时", f"串口连接 {self.port_var.get()}")
        threading.Thread(target=self.read_serial, daemon=True).start()
        for command in ("*GET:FORMAT", "*GET:FLIP", "*GET:ALARM", "*GET:MODE", "*GET:FOCUS", "*PING"):
            self.send(command)
        self.root.after(800, lambda: self.apply_auto_day_night(force=True))

    def disconnect(self):
        self.stop_reader.set()
        current, self.serial = self.serial, None
        if current:
            try:
                current.close()
            except (serial.SerialException, OSError):
                pass
        self.connection_var.set("连接：未连接")
        self.latency_var.set("延迟：-- ms")

        self.focus_var.set("FOCUS: OFF")
        self.focus_remaining_var.set("Left: --")
        self.flip_var.set("FLIP：OFF")

    def close(self):
        self.disconnect()
        self.root.destroy()

    def send(self, command):
        command = command.strip()
        if not command:
            return
        if len(command.encode("ascii", errors="replace")) > 64:
            self.report_error("命令过长", "单帧最大长度为 64 字节。", popup=True)
            return
        current = self.serial
        if not current or not current.is_open:
            self.report_error("发送失败", "请先连接串口。")
            return
        now = time.monotonic()
        delay_ms = max(0, int((self.next_tx_time - now) * 1000))
        self.next_tx_time = max(now, self.next_tx_time) + 0.25
        if delay_ms > 0:
            self.root.after(delay_ms, lambda c=command: self.write_command(c))
        else:
            self.write_command(command)

    def write_command(self, command):
        current = self.serial
        if not current or not current.is_open:
            return
        try:
            current.write((command + "\r\n").encode("ascii"))
            if command.upper() == "*PING":
                self.ping_sent_at = time.monotonic()
            self.append_log(command, "tx", "TX")
        except (serial.SerialException, OSError) as exc:
            self.report_error("串口写入失败", exc)
            self.disconnect()

    def send_command(self):
        self.send(self.command_entry.get())

    def set_time(self):
        values = self.time_var.get().replace(":", " ").split()
        if len(values) == 3:
            self.send(f"*SET:TIME HOUR MIN SEC {' '.join(values)}")
        else:
            self.report_error("参数错误", "时间格式应为 HH MM SS。")

    def set_date(self):
        values = self.date_var.get().replace("-", " ").split()
        if len(values) == 3:
            self.send(f"*SET:DATE YEAR MONTH DATE {' '.join(values)}")
        else:
            self.report_error("参数错误", "日期格式应为 YYYY MM DD。")

    def set_alarm(self):
        values = self.alarm_time_var.get().replace(":", " ").split()
        if len(values) == 3:
            self.set_alarm_status("ON", values)
            self.record_event("闹钟", "设置 " + ":".join(values))
            self.send(f"*SET:ALARM HOUR MIN SEC {' '.join(values)}")
        else:
            self.report_error("参数错误", "闹钟格式应为 HH MM SS。")

    def close_alarm(self):
        self.set_alarm_status("OFF")
        self.record_event("闹钟", "关闭")
        self.send("*SET:ALARM OFF")

    def reset_board(self):
        self.set_alarm_status("OFF", ("12", "01", "00"))
        self.format_var.set("FORMAT：LEFT")
        self.mode_var.set("MODE：DAY")
        self.flip_var.set("FLIP：OFF")
        self.focus_var.set("FOCUS: OFF")
        self.focus_count_var.set("Done: 0")
        self.focus_remaining_var.set("Left: --")
        self.send("*RST")

    def set_focus_minutes(self):
        try:
            minutes = int(self.focus_minutes_var.get())
        except ValueError:
            self.report_error("Focus param", "Duration must be an integer.", popup=True)
            return False
        if minutes < 1 or minutes > 99:
            self.report_error("Focus param", "Duration must be between 1 and 99.", popup=True)
            return False
        self.send(f"*SET:FOCUS {minutes}")
        self.record_event("专注", f"设置 {minutes} 分钟")
        return True

    def start_focus(self):
        if self.set_focus_minutes():
            self.root.after(120, lambda: self.send("*FOCUS:START"))
            self.record_event("专注", "开始")

    def stop_focus(self):
        self.send("*FOCUS:STOP")
        self.record_event("专注", "关闭")

    def build_speech_text(self):
        now = datetime.now()
        text = f"现在时间 {now.hour} 点 {now.minute} 分"
        if self.last_weather_temp is None or not self.last_weather_label:
            return text + "。天气信息暂未获取。"
        weather_map = {
            "SUN": "晴",
            "RAIN": "雨",
            "CLOUD": "多云",
            "HOT": "炎热",
        }
        weather_text = weather_map.get(self.last_weather_label, self.last_weather_label)
        return f"{text}。当前天气 {weather_text}，温度 {self.last_weather_temp} 摄氏度。"

    def speak_status(self):
        text = self.build_speech_text()
        threading.Thread(target=self.speak_text_worker, args=(text,), daemon=True).start()

    def speak_text_worker(self, text):
        escaped = text.replace("'", "''")
        script = (
            "Add-Type -AssemblyName System.Speech; "
            "$speak = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
            f"$speak.Speak('{escaped}');"
        )
        try:
            subprocess.run(
                ["powershell", "-NoProfile", "-Command", script],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=True,
                timeout=30,
            )
            self.root.after(0, lambda: self.last_command_var.set("命令：语音播报"))
        except (OSError, subprocess.SubprocessError) as exc:
            self.root.after(0, lambda: self.report_error("语音播报失败", exc, popup=True))

    def on_auto_mode_toggle(self):
        if self.auto_mode_enabled_var.get():
            self.last_auto_mode_sent = None
            self.auto_mode_var.set("AUTO：ON")
            self.apply_auto_day_night(force=True)
        else:
            self.auto_mode_var.set("AUTO：OFF")

    def apply_auto_day_night(self, force=False):
        if not self.auto_mode_enabled_var.get():
            self.auto_mode_var.set("AUTO：OFF")
            return
        try:
            lat = float(self.lat_var.get())
            lon = float(self.lon_var.get())
            now = datetime.now(timezone(timedelta(hours=8)))
            sunrise, sunset = self.sun_times_minutes(now, lat, lon)
        except (ValueError, ZeroDivisionError) as exc:
            self.auto_mode_var.set("AUTO：ERR")
            if force:
                self.report_error("自动昼夜失败", exc)
            return

        current = now.hour * 60 + now.minute + now.second / 60.0
        target = "DAY" if sunrise <= current < sunset else "NIGHT"
        self.auto_mode_var.set(
            f"AUTO：{target} {self.format_minutes(sunrise)}-{self.format_minutes(sunset)}"
        )
        if not self.serial or not self.serial.is_open:
            return
        if force or target != self.last_auto_mode_sent:
            self.send(f"*SET:MODE {target}")
            self.record_event("模式", f"自动昼夜 {target}")
            self.last_auto_mode_sent = target

    def sun_times_minutes(self, now, lat, lon):
        if not -66.0 <= lat <= 66.0:
            raise ValueError("自动昼夜仅支持非极昼极夜地区")
        if not -180.0 <= lon <= 180.0:
            raise ValueError("经度范围应为 -180 到 180")
        day = now.timetuple().tm_yday
        gamma = 2.0 * math.pi / 365.0 * (day - 1)
        eq_time = 229.18 * (
            0.000075 + 0.001868 * math.cos(gamma) -
            0.032077 * math.sin(gamma) - 0.014615 * math.cos(2 * gamma) -
            0.040849 * math.sin(2 * gamma)
        )
        decl = (
            0.006918 - 0.399912 * math.cos(gamma) +
            0.070257 * math.sin(gamma) - 0.006758 * math.cos(2 * gamma) +
            0.000907 * math.sin(2 * gamma) - 0.002697 * math.cos(3 * gamma) +
            0.00148 * math.sin(3 * gamma)
        )
        lat_rad = math.radians(lat)
        zenith = math.radians(90.833)
        hour_angle_arg = (
            math.cos(zenith) / (math.cos(lat_rad) * math.cos(decl)) -
            math.tan(lat_rad) * math.tan(decl)
        )
        hour_angle_arg = max(-1.0, min(1.0, hour_angle_arg))
        hour_angle = math.degrees(math.acos(hour_angle_arg))
        solar_noon = 720.0 - 4.0 * lon - eq_time + 8.0 * 60.0
        sunrise = (solar_noon - hour_angle * 4.0) % 1440.0
        sunset = (solar_noon + hour_angle * 4.0) % 1440.0
        return sunrise, sunset

    def format_minutes(self, minutes):
        total = int(round(minutes)) % 1440
        return f"{total // 60:02d}:{total % 60:02d}"

    def sync_ntp(self):
        threading.Thread(target=self.sync_ntp_worker, daemon=True).start()

    def sync_ntp_worker(self):
        try:
            ntp_time = self.query_ntp_time()
            now = datetime.fromtimestamp(ntp_time, tz=timezone.utc) + timedelta(hours=8)
            date_cmd = f"*SET:DATE YEAR MONTH DATE {now.year:04d} {now.month:02d} {now.day:02d}"
            time_cmd = f"*SET:TIME HOUR MIN SEC {now.hour:02d} {now.minute:02d} {now.second:02d}"
            self.root.after(0, lambda: self.send(date_cmd))
            self.root.after(80, lambda: self.send(time_cmd))
            self.root.after(160, lambda: self.send("*SET:SYNC OK"))
            self.root.after(0, lambda: self.last_command_var.set("命令：NTP 对时完成"))
            self.root.after(0, lambda: self.record_event("对时", "NTP 对时完成"))
        except (OSError, TimeoutError, ValueError) as exc:
            self.root.after(0, lambda: self.report_error("NTP 对时失败", exc, popup=True))

    def query_ntp_time(self):
        packet = b"\x1b" + 47 * b"\0"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(4.0)
            sock.sendto(packet, ("pool.ntp.org", 123))
            data, _addr = sock.recvfrom(48)
        if len(data) < 48:
            raise ValueError("NTP 数据长度不足")
        seconds = struct.unpack("!12I", data)[10]
        return seconds - 2208988800

    def fetch_weather(self, show_after_update=False, speak_after_update=False):
        threading.Thread(target=lambda: self.fetch_weather_worker(show_after_update, speak_after_update),
                         daemon=True).start()

    def show_weather_on_board(self):
        self.suppress_next_user2_event = True
        self.send("*SET:KEY USER2")

    def fetch_weather_worker(self, show_after_update=False, speak_after_update=False):
        try:
            lat = float(self.lat_var.get())
            lon = float(self.lon_var.get())
            response = requests.get(
                "https://api.met.no/weatherapi/locationforecast/2.0/compact",
                params={
                    "lat": lat,
                    "lon": lon,
                },
                headers={"User-Agent": "S800-NetworkClock/1.0 contact:none"},
                timeout=8,
                proxies={"http": None, "https": None},
            )
            response.raise_for_status()
            payload = response.json()
            current = payload["properties"]["timeseries"][0]["data"]
            temp = int(round(float(current["instant"]["details"]["air_temperature"])))
            summary = current.get("next_1_hours") or current.get("next_6_hours") or current.get("next_12_hours") or {}
            symbol = summary.get("summary", {}).get("symbol_code", "")
            label = self.weather_label(symbol, temp)
            temp = max(0, min(99, temp))
            self.last_weather_temp = temp
            self.last_weather_label = label
            command = f"*SET:WEATHER {temp:02d} {label}"
            self.root.after(0, lambda: self.send(command))
            self.root.after(0, lambda: self.weather_var.set(f"天气：{temp:02d}C / {label}"))
            self.root.after(0, lambda: self.record_event("天气", f"{temp:02d}C / {label}"))
            if show_after_update:
                self.root.after(200, self.show_weather_on_board)
            if speak_after_update:
                self.root.after(300, self.speak_status)
        except (requests.RequestException, OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
            self.root.after(0, lambda: self.report_error("天气获取失败", exc, popup=True))

    def weather_label(self, code, temp):
        if temp >= 30:
            return "HOT"
        if isinstance(code, str):
            lowered = code.lower()
            if "rain" in lowered or "sleet" in lowered or "snow" in lowered or "thunder" in lowered:
                return "RAIN"
            if "clear" in lowered or "fair" in lowered:
                return "SUN"
            return "CLOUD"
        if code in (0, 1):
            return "SUN"
        if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82, 95, 96, 99):
            return "RAIN"
        return "CLOUD"

    def load_event_records(self):
        records = []
        if not self.event_file.exists():
            return records
        try:
            with self.event_file.open("r", encoding="utf-8") as source:
                for line in source:
                    try:
                        item = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if {"time", "type", "detail"} <= set(item):
                        records.append(item)
        except OSError:
            return records
        return records[-2000:]

    def record_event(self, event_type, detail):
        item = {
            "time": datetime.now().isoformat(timespec="seconds"),
            "type": event_type,
            "detail": str(detail),
        }
        self.event_records.append(item)
        self.event_records = self.event_records[-2000:]
        self.event_count_var.set(f"记录：{len(self.event_records)}")
        try:
            with self.event_file.open("a", encoding="utf-8") as output:
                output.write(json.dumps(item, ensure_ascii=False) + "\n")
        except OSError as exc:
            self.append_log(f"事件记录失败：{exc}", "error", "ERR")

    def event_type_counts(self):
        names = ("闹钟", "相等", "音乐", "对时", "按键", "模式", "天气", "专注", "错误")
        counts = {name: 0 for name in names}
        for item in self.event_records:
            name = item.get("type", "其他")
            counts[name] = counts.get(name, 0) + 1
        return counts

    def hourly_event_counts(self):
        counts = [0] * 24
        for item in self.event_records:
            try:
                hour = datetime.fromisoformat(item["time"]).hour
            except (ValueError, KeyError):
                continue
            counts[hour] += 1
        return counts

    def export_event_csv(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8-sig") as output:
                writer = csv.DictWriter(output, fieldnames=("time", "type", "detail"))
                writer.writeheader()
                writer.writerows(self.event_records)
            self.append_log(f"事件记录已导出：{path}", "info")
        except OSError as exc:
            self.report_error("事件导出失败", exc, popup=True)

    def clear_event_records(self):
        if not messagebox.askyesno("清空记录", "确定清空所有数据看板事件记录吗？"):
            return
        self.event_records.clear()
        self.event_count_var.set("记录：0")
        try:
            self.event_file.write_text("", encoding="utf-8")
        except OSError as exc:
            self.report_error("清空记录失败", exc, popup=True)
        self.refresh_dashboard()

    def show_dashboard(self):
        if self.dashboard_window and self.dashboard_window.winfo_exists():
            self.dashboard_window.lift()
            self.refresh_dashboard()
            return
        win = tk.Toplevel(self.root)
        win.title("S800 数据可视化看板")
        win.geometry("980x620")
        win.minsize(860, 520)
        win.columnconfigure(0, weight=3)
        win.columnconfigure(1, weight=2)
        win.rowconfigure(1, weight=1)
        win.protocol("WM_DELETE_WINDOW", lambda: self.close_dashboard())
        self.dashboard_window = win

        header = ttk.Frame(win, padding=8)
        header.grid(row=0, column=0, columnspan=2, sticky="ew")
        ttk.Label(header, text="事件统计 / 持久化记录", font=("Microsoft YaHei UI", 12, "bold")).pack(side="left")
        ttk.Label(header, textvariable=self.event_count_var).pack(side="left", padx=16)
        ttk.Button(header, text="刷新", command=self.refresh_dashboard).pack(side="right", padx=4)
        ttk.Button(header, text="导出 CSV", command=self.export_event_csv).pack(side="right", padx=4)

        charts = ttk.LabelFrame(win, text="图表", padding=8)
        charts.grid(row=1, column=0, sticky="nsew", padx=(8, 4), pady=(0, 8))
        charts.rowconfigure(0, weight=1)
        charts.rowconfigure(1, weight=1)
        charts.columnconfigure(0, weight=1)
        self.event_type_canvas = tk.Canvas(charts, height=230, bg="white", highlightthickness=1,
                                           highlightbackground="#d0d0d0")
        self.event_type_canvas.grid(row=0, column=0, sticky="nsew", pady=(0, 8))
        self.hour_canvas = tk.Canvas(charts, height=190, bg="white", highlightthickness=1,
                                     highlightbackground="#d0d0d0")
        self.hour_canvas.grid(row=1, column=0, sticky="nsew")

        side = ttk.LabelFrame(win, text="最近事件", padding=8)
        side.grid(row=1, column=1, sticky="nsew", padx=(4, 8), pady=(0, 8))
        side.rowconfigure(1, weight=1)
        side.columnconfigure(0, weight=1)
        self.summary_label = ttk.Label(side, text="", justify="left")
        self.summary_label.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        columns = ("time", "type", "detail")
        self.event_tree = ttk.Treeview(side, columns=columns, show="headings", height=12)
        self.event_tree.heading("time", text="时间")
        self.event_tree.heading("type", text="类型")
        self.event_tree.heading("detail", text="详情")
        self.event_tree.column("time", width=135, stretch=False)
        self.event_tree.column("type", width=55, stretch=False)
        self.event_tree.column("detail", width=220, stretch=True)
        scrollbar = ttk.Scrollbar(side, orient="vertical", command=self.event_tree.yview)
        self.event_tree.configure(yscrollcommand=scrollbar.set)
        self.event_tree.grid(row=1, column=0, sticky="nsew")
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.refresh_dashboard()

    def close_dashboard(self):
        if self.dashboard_window and self.dashboard_window.winfo_exists():
            self.dashboard_window.destroy()
        self.dashboard_window = None

    def refresh_dashboard(self):
        if not self.dashboard_window or not self.dashboard_window.winfo_exists():
            return
        self.draw_type_chart()
        self.draw_hour_chart()
        self.update_event_table()

    def draw_type_chart(self):
        canvas = self.event_type_canvas
        canvas.delete("all")
        counts = self.event_type_counts()
        items = [(key, value) for key, value in counts.items() if value > 0]
        if not items:
            canvas.create_text(20, 20, anchor="nw", text="暂无事件记录")
            return
        canvas.update_idletasks()
        width = max(canvas.winfo_width(), 500)
        height = max(canvas.winfo_height(), 200)
        left, top, bottom = 58, 28, height - 38
        max_count = max(value for _key, value in items)
        canvas.create_text(left, 10, anchor="w", text="事件类型统计")
        bar_width = max(22, int((width - left - 30) / max(len(items), 1) * 0.55))
        gap = max(16, int((width - left - 30) / max(len(items), 1) * 0.45))
        colors = ("#4e79a7", "#59a14f", "#f28e2b", "#e15759", "#76b7b2", "#edc948", "#af7aa1")
        for index, (name, value) in enumerate(items):
            x0 = left + index * (bar_width + gap)
            bar_height = int((bottom - top) * value / max_count)
            y0 = bottom - bar_height
            canvas.create_rectangle(x0, y0, x0 + bar_width, bottom, fill=colors[index % len(colors)], outline="")
            canvas.create_text(x0 + bar_width / 2, y0 - 10, text=str(value))
            canvas.create_text(x0 + bar_width / 2, bottom + 16, text=name)
        canvas.create_line(left - 8, bottom, width - 20, bottom, fill="#999999")

    def draw_hour_chart(self):
        canvas = self.hour_canvas
        canvas.delete("all")
        counts = self.hourly_event_counts()
        if not any(counts):
            canvas.create_text(20, 20, anchor="nw", text="暂无按小时统计数据")
            return
        canvas.update_idletasks()
        width = max(canvas.winfo_width(), 500)
        height = max(canvas.winfo_height(), 170)
        left, top, bottom = 46, 28, height - 34
        max_count = max(counts)
        canvas.create_text(left, 10, anchor="w", text="24 小时事件分布")
        usable = width - left - 24
        bar_width = max(6, int(usable / 24 * 0.65))
        step = usable / 24
        for hour, value in enumerate(counts):
            x0 = left + hour * step
            bar_height = int((bottom - top) * value / max_count) if max_count else 0
            canvas.create_rectangle(x0, bottom - bar_height, x0 + bar_width, bottom,
                                    fill="#4e79a7", outline="")
            if hour % 3 == 0:
                canvas.create_text(x0 + bar_width / 2, bottom + 12, text=f"{hour:02d}", font=("Arial", 8))
        canvas.create_line(left - 8, bottom, width - 20, bottom, fill="#999999")

    def update_event_table(self):
        for row in self.event_tree.get_children():
            self.event_tree.delete(row)
        for item in reversed(self.event_records[-80:]):
            display_time = item.get("time", "").replace("T", " ")
            self.event_tree.insert("", "end", values=(display_time, item.get("type", ""), item.get("detail", "")))
        counts = self.event_type_counts()
        summary = "总记录：{}\n闹钟：{}  相等：{}  音乐：{}  对时：{}\n按键：{}  模式：{}  天气：{}  专注：{}  错误：{}".format(
            len(self.event_records), counts.get("闹钟", 0), counts.get("相等", 0),
            counts.get("音乐", 0), counts.get("对时", 0), counts.get("按键", 0),
            counts.get("模式", 0), counts.get("天气", 0), counts.get("专注", 0),
            counts.get("错误", 0)
        )
        self.summary_label.configure(text=summary)

    def send_combo(self):
        commands = {
            "日期：YEAR MONTH DATE": "*SET:DATE YEAR MONTH DATE 2026 06 04",
            "日期：YEAR DATE": "*SET:DATE YEAR DATE 2026 04",
            "日期：MONTH DATE": "*SET:DATE MONTH DATE 06 04",
            "时间：HOUR MIN SEC": "*SET:TIME HOUR MIN SEC 12 30 45",
        }
        command = commands[self.combo_var.get()]
        self.send(command)
        if ":DATE" in command:
            self.root.after(700, lambda: self.send("*GET:DATE"))
        elif ":TIME" in command:
            self.root.after(700, lambda: self.send("*GET:TIME"))

    def read_serial(self):
        while not self.stop_reader.is_set():
            current = self.serial
            if not current or not current.is_open:
                return
            try:
                line = current.readline().decode("ascii", errors="replace").strip()
                if line:
                    self.rx_queue.put(("line", line))
            except (serial.SerialException, OSError) as exc:
                self.rx_queue.put(("disconnect", str(exc)))
                return
            except Exception as exc:
                self.rx_queue.put(("reader_error", repr(exc)))
                return

    def poll_rx(self):
        while not self.rx_queue.empty():
            kind, data = self.rx_queue.get_nowait()
            if kind == "disconnect":
                self.report_error("串口已断开", data)
                self.disconnect()
            elif kind == "reader_error":
                self.report_error("串口读取线程异常", data)
                self.disconnect()
            else:
                self.last_rx_at = time.monotonic()
                if self.heartbeat_timed_out:
                    self.heartbeat_timed_out = False
                    if self.serial and self.serial.is_open:
                        self.connection_var.set(f"连接：{self.port_var.get()} @115200")
                self.parse_line(data)
        self.root.after(40, self.poll_rx)

    def parse_line(self, line):
        upper = line.upper()
        tag = "event" if upper.startswith(("#EVT:", "*EVT:", "#PONG", "STATE,")) else "response"
        if upper.startswith(("ERROR", "ERR")):
            tag = "error"
            self.last_command_var.set("命令：" + upper)
        self.append_log(line, tag, "RX")
        try:
            if upper.startswith(("#EVT:DISP ", "*EVT:DISP ")):
                payload = line.split(" ", 1)[1]
                text, dp = payload.rsplit(" ", 1)
                if len(text) != 8:
                    raise ValueError("DISP 文本必须正好为 8 字符")
                self.display_text, self.dp_value = text, int(dp, 16)
                self.render_display()
            elif upper.startswith(("#EVT:LED ", "*EVT:LED ")):
                self.set_led(int(line.rsplit(" ", 1)[1], 16))
            elif upper.startswith(("#EVT:MODE ", "*EVT:MODE ")):
                mode = upper.rsplit(" ", 1)[1]
                self.mode_var.set("MODE：" + mode)
                self.record_event("模式", mode)
            elif upper.startswith(("#EVT:FLIP ", "*EVT:FLIP ")):
                self.flip_var.set("FLIP：" + upper.rsplit(" ", 1)[1])
            elif upper.startswith(("#EVT:FOCUS ", "*EVT:FOCUS ")):
                self.parse_focus_status(line.split(" ", 1)[1])
            elif upper.startswith(("#EVT:EQUAL ", "*EVT:EQUAL ")):
                self.record_event("相等", line.split(" ", 1)[1])
            elif upper.startswith(("#EVT:MUSIC ", "*EVT:MUSIC ")):
                self.record_event("音乐", upper.rsplit(" ", 1)[1])
            elif upper in ("#EVT:ALARM", "*EVT:ALARM"):
                self.set_alarm_status("RINGING")
                self.record_event("闹钟", "响铃")
            elif upper in ("#EVT:ALARM_ON", "*EVT:ALARM_ON"):
                self.set_alarm_status("ON")
                self.record_event("闹钟", "启用")
            elif upper in ("#EVT:ALARM_OFF", "*EVT:ALARM_OFF"):
                self.set_alarm_status("OFF")
                self.record_event("闹钟", "关闭")
            elif upper.startswith(("#EVT:KEY ", "*EVT:KEY ")):
                key_name = upper.rsplit(" ", 1)[1]
                self.record_event("按键", key_name)
                self.flash_key(key_name)
                if key_name == "USER1":
                    self.sync_ntp()
                elif key_name == "USER2":
                    if self.suppress_next_user2_event:
                        self.suppress_next_user2_event = False
                    else:
                        self.fetch_weather(show_after_update=True, speak_after_update=True)
                    return
                    if self.weather_var.get() == "天气：--":
                        self.fetch_weather(show_after_update=True, speak_after_update=True)
                    else:
                        self.speak_status()
            elif upper.startswith("#PONG"):
                if self.ping_sent_at is not None:
                    delay = (time.monotonic() - self.ping_sent_at) * 1000
                    self.latency_var.set(f"延迟：{delay:.0f} ms")
                    self.ping_sent_at = None
            elif upper.startswith("STATE,"):
                self.parse_legacy_state(line)
            elif upper.startswith("OK RIGHT"):
                self.format_var.set("FORMAT：RIGHT")
            elif upper.startswith("OK LEFT"):
                self.format_var.set("FORMAT：LEFT")
            elif upper.startswith("OK FLIP "):
                self.flip_var.set("FLIP：" + upper.rsplit(" ", 1)[1])
            elif upper.startswith("OK NIGHT"):
                self.mode_var.set("MODE：NIGHT")
                self.record_event("模式", "NIGHT")
            elif upper.startswith("OK DAY"):
                self.mode_var.set("MODE：DAY")
                self.record_event("模式", "DAY")
            elif upper.startswith("OK FOCUS "):
                self.parse_focus_status(line[3:])
            elif self.parse_alarm_response(upper):
                pass
            elif upper.startswith("OK"):
                self.last_command_var.set("命令：OK")
            elif upper.startswith("ERROR"):
                self.last_command_var.set("命令：" + upper)
        except (ValueError, IndexError) as exc:
            self.report_error("解析异常", f"{line} | {exc}")

    def parse_legacy_state(self, line):
        fields = {}
        for item in line.split(",")[1:]:
            if "=" in item:
                key, value = item.split("=", 1)
                fields[key.upper()] = value
        if "LED" in fields:
            self.set_led(int(fields["LED"], 16))
        if "FORMAT" in fields:
            self.format_var.set("FORMAT：" + fields["FORMAT"].upper())
        if "EN" in fields:
            state = "RINGING" if fields.get("RING") == "1" else ("ON" if fields["EN"] == "1" else "OFF")
            alarm_values = fields.get("ALARM", "").replace(":", " ").split()
            self.set_alarm_status(state, alarm_values if len(alarm_values) == 3 else None)

    def parse_alarm_response(self, upper):
        parts = upper.split()
        if len(parts) == 5 and parts[0] == "OK" and parts[4] in ("ON", "OFF"):
            try:
                numbers = [f"{int(part):02d}" for part in parts[1:4]]
            except ValueError:
                return False
            self.set_alarm_status(parts[4], numbers)
            return True
        return False

    def parse_focus_status(self, text):
        parts = text.strip().split()
        if len(parts) == 5 and parts[0].upper() == "FOCUS":
            state = parts[1].upper()
            duration = int(parts[2])
            remaining = int(parts[3])
            completed = int(parts[4])
        elif len(parts) == 4:
            state = parts[0].upper()
            duration = int(parts[1])
            remaining = int(parts[2])
            completed = int(parts[3])
        else:
            raise ValueError("Invalid focus status")
        if self.root.focus_get() is not self.focus_minutes_entry:
            self.focus_minutes_var.set(str(duration))
        self.focus_count_var.set(f"Done: {completed}")
        if state == "OFF" and remaining == 0:
            self.focus_remaining_var.set("Left: --")
            self.focus_var.set("FOCUS: OFF")
        else:
            self.focus_remaining_var.set(
                f"Left: {remaining // 60:02d}:{remaining % 60:02d}"
            )
            self.focus_var.set(
                f"FOCUS: {state} {remaining // 60:02d}:{remaining % 60:02d}"
            )
        if state == "DONE":
            self.focus_var.set(f"FOCUS: DONE {completed}")
            self.last_command_var.set("Command: Focus finished")
            self.record_event("专注", f"完成 {completed}")

    def set_alarm_status(self, status, values=None):
        if values and len(values) == 3:
            try:
                numbers = [f"{int(part):02d}" for part in values]
                self.alarm_time_var.set(" ".join(numbers))
            except ValueError:
                numbers = values
            alarm_text = ":".join(numbers)
        else:
            alarm_text = self.alarm_time_var.get().replace(" ", ":")
        self.alarm_var.set("ALARM：" + status)
        self.alarm_detail_var.set(f"闹钟：{alarm_text} / {status}")

    def render_display(self):
        for index, digit in enumerate(self.digits):
            digit.set_value(self.display_text[index], bool(self.dp_value & (1 << index)))

    def set_led(self, value):
        self.led_value = value & 0xFF
        for index, canvas in enumerate(self.led_canvases):
            canvas.itemconfigure("lamp", fill="#ff3030" if value & (1 << index) else "#303030")

    def flash_key(self, name):
        button = self.key_buttons.get(name)
        if button:
            button.state(["pressed"])
            self.root.after(180, lambda: button.state(["!pressed"]))

    def periodic_tasks(self):
        self.refresh_ports()
        now = time.monotonic()
        if now >= self.next_auto_mode_check:
            self.next_auto_mode_check = now + 60.0
            self.apply_auto_day_night()
        if self.serial and self.serial.is_open:
            idle = time.monotonic() - self.last_rx_at if self.last_rx_at else 0
            if idle > 3.0 and now >= self.next_tx_time:
                self.send("*PING")
            if (self.last_rx_at and
                    idle > 5.0 and
                    not self.heartbeat_timed_out):
                self.heartbeat_timed_out = True
                self.connection_var.set("连接：心跳超时")
                self.append_log("超过 5 秒未收到板端数据", "error", "SYS")
        self.root.after(1000, self.periodic_tasks)

    def append_log(self, text, tag="info", direction="SYS"):
        stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log.configure(state="normal")
        self.log.insert("end", f"[{stamp}] [{direction}] {text}\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def report_error(self, title, detail, popup=False):
        self.append_log(f"{title}: {detail}", "error", "ERR")
        self.record_event("错误", f"{title}: {detail}")
        if popup:
            messagebox.showerror(title, str(detail))

    def clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def export_log(self):
        path = filedialog.asksaveasfilename(defaultextension=".txt",
                                            filetypes=[("Text", "*.txt"), ("All files", "*.*")])
        if path:
            try:
                with open(path, "w", encoding="utf-8") as output:
                    output.write(self.log.get("1.0", "end-1c"))
                self.append_log(f"日志已导出：{path}", "info")
            except OSError as exc:
                self.report_error("日志导出失败", exc, popup=True)


if __name__ == "__main__":
    window = tk.Tk()
    S800ClockApp(window)
    window.mainloop()
