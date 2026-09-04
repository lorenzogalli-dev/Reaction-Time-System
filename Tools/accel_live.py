#!/usr/bin/env python3
"""
Live viewer + recorder for Arduino/AccelStream/AccelStream.ino.

Clean rewrite paired with the simplified no-BLE firmware: reads
"t_us,x_g,y_g,z_g" CSV lines over serial, plots them live, and records to
CSV + a PNG overview on request. t_us is a real per-sample micros() reading
taken on the board, not a nominal index * period.

Usage:
    python3 tools/accel_live.py                    # autodetect the port
    python3 tools/accel_live.py --port /dev/cu.usbmodem1101
    python3 tools/accel_live.py --simulate          # no hardware, UI only

Controls: on-screen buttons, or keys r (record), s (stop), p (snapshot PNG).

Requires: pyserial, matplotlib, numpy.
"""

import argparse
import collections
import glob
import math
import os
import sys
import threading
import time
from datetime import datetime, timezone

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Button

CMD_START = b"r"
CMD_STOP = b"s"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUTDIR = os.path.join(REPO_ROOT, "data")


# ---------------------------------------------------------------------------
# Data sources
# ---------------------------------------------------------------------------

class SerialSource:
    """Reads the serial port in chunks and returns complete text lines."""

    def __init__(self, port, baud):
        import serial  # imported here: --simulate doesn't need pyserial
        self.port = port
        # On macOS, opening a native-USB CDC port at a custom baud (921600
        # isn't a standard termios rate) goes through an ioctl that can fail
        # transiently right after the port appears: OSError Errno 83
        # "Device error". Not a board fault - it clears on retry.
        last_exc = None
        for _ in range(5):
            try:
                self._ser = serial.Serial(port, baud, timeout=0.05)
                break
            except OSError as exc:
                last_exc = exc
                time.sleep(0.3)
        else:
            raise last_exc
        self._tail = ""
        self._ser.reset_input_buffer()
        self._ser.write(CMD_START)

    def read_lines(self):
        n = self._ser.in_waiting
        chunk = self._ser.read(n if n else 1)
        if not chunk:
            return []
        text = self._tail + chunk.decode("utf-8", errors="ignore")
        parts = text.split("\n")
        self._tail = parts.pop()
        return parts

    def close(self):
        try:
            self._ser.write(CMD_STOP)
            self._ser.flush()
        except Exception:
            pass
        try:
            self._ser.close()
        except Exception:
            pass


class SimulatedSource:
    """Generates the same text the firmware would, for testing the UI."""

    def __init__(self, odr_hz, accel_range_g):
        self.port = "simulated"
        self._odr = odr_hz
        self._range = accel_range_g
        self._t0 = time.monotonic()
        self._emitted = 0
        self._banner = [
            "IMU OK - accel %d Hz, +/-%d g" % (odr_hz, accel_range_g),
            "Ready. Serial: 'r' start CSV, 's' stop, 'p' one reading.",
        ]

    def read_lines(self):
        if self._banner:
            return [self._banner.pop(0)]
        time.sleep(0.02)
        due = int((time.monotonic() - self._t0) * self._odr)
        out = []
        while self._emitted < due:
            k = self._emitted
            t_s = k / self._odr
            # Background: gravity on Z plus noise, with a push-off-like
            # transient every 5 s so a peak is visible on screen.
            phase = t_s % 5.0
            burst = math.exp(-((phase - 2.0) ** 2) / 0.002) * 3.0 if phase < 3 else 0.0
            ax = 0.01 * math.sin(2 * math.pi * 3 * t_s) + np.random.normal(0, 0.004)
            ay = 0.01 * math.cos(2 * math.pi * 2 * t_s) + np.random.normal(0, 0.004)
            az = 1.0 + burst + np.random.normal(0, 0.004)
            lim = float(self._range)
            ax, ay, az = (max(-lim, min(lim, v)) for v in (ax, ay, az))
            t_us = int(t_s * 1e6)
            out.append("%d,%.4f,%.4f,%.4f" % (t_us, ax, ay, az))
            self._emitted += 1
        return out

    def close(self):
        pass


# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

class LiveApp:
    def __init__(self, args):
        self.args = args
        self.odr = args.odr
        self.accel_range = args.range
        self.window_s = args.window

        maxlen = int(self.window_s * self.odr)
        self.t_buf = collections.deque(maxlen=maxlen)
        self.x_buf = collections.deque(maxlen=maxlen)
        self.y_buf = collections.deque(maxlen=maxlen)
        self.z_buf = collections.deque(maxlen=maxlen)
        self.m_buf = collections.deque(maxlen=maxlen)

        self.lock = threading.Lock()
        self.running = True
        self.source = None
        self.status = "starting..."

        self.recording = False
        self.csv_file = None
        self.csv_path = None
        self.rec_t0_us = None
        self.rec_rows = 0
        self.rec_peak = 0.0
        self.rec_clipped = 0
        self.rec_data = []

        self.total_samples = 0
        self.host_t0 = time.monotonic()
        self.last_t_us = None
        self.gap_count = 0

        self._build_ui()

    # -- source ---------------------------------------------------------

    def connect(self):
        try:
            if self.args.simulate:
                self.source = SimulatedSource(self.odr, self.accel_range)
            else:
                port = self.args.port or autodetect_port()
                if port is None:
                    self.status = "no port found - plug in the board and restart"
                    print("[accel_live] %s" % self.status, file=sys.stderr)
                    return
                self.source = SerialSource(port, self.args.baud)
            self.status = "connected to %s" % self.source.port
            print("[accel_live] %s" % self.status, file=sys.stderr)
        except Exception as exc:
            self.status = "connection failed: %s" % exc
            print("[accel_live] %s" % self.status, file=sys.stderr)
            self.source = None
            return
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while self.running and self.source is not None:
            try:
                lines = self.source.read_lines()
            except Exception as exc:
                # A closed-port error during a deliberate shutdown is
                # expected (the port is closed out from under this thread),
                # not a real failure - only report it if we're still
                # supposed to be running.
                if self.running:
                    self.status = "read error: %s" % exc
                    print("[accel_live] %s" % self.status, file=sys.stderr)
                return
            for line in lines:
                self._handle_line(line.strip())

    def _handle_line(self, line):
        if not line:
            return
        parts = line.split(",")
        if len(parts) != 4:
            self.status = line  # firmware banner/diagnostic text
            return
        try:
            t_us = int(parts[0])
            ax, ay, az = float(parts[1]), float(parts[2]), float(parts[3])
        except ValueError:
            self.status = line
            return

        mag = math.sqrt(ax * ax + ay * ay + az * az)
        host_iso = datetime.now(timezone.utc).isoformat()

        if self.last_t_us is not None:
            # A gap much larger than the nominal period means missed
            # samples - the firmware doesn't buffer them (no FIFO here), so
            # a slow host read can genuinely lose data between samples.
            if (t_us - self.last_t_us) > 3 * (1e6 / self.odr):
                self.gap_count += 1
        self.last_t_us = t_us
        self.total_samples += 1
        if self.total_samples == 1:
            print("[accel_live] first sample: x=%.3f y=%.3f z=%.3f g" % (ax, ay, az),
                  file=sys.stderr)

        t_s = t_us / 1e6
        with self.lock:
            self.t_buf.append(t_s)
            self.x_buf.append(ax)
            self.y_buf.append(ay)
            self.z_buf.append(az)
            self.m_buf.append(mag)

            if self.recording and self.csv_file is not None:
                if self.rec_t0_us is None:
                    self.rec_t0_us = t_us
                t_rel = (t_us - self.rec_t0_us) / 1e6
                self.csv_file.write("%.6f,%d,%.4f,%.4f,%.4f,%s\n"
                                     % (t_rel, t_us, ax, ay, az, host_iso))
                self.rec_rows += 1
                self.rec_data.append((t_rel, ax, ay, az, mag))
                if mag > self.rec_peak:
                    self.rec_peak = mag
                if max(abs(ax), abs(ay), abs(az)) >= 0.95 * self.accel_range:
                    self.rec_clipped += 1

    # -- recording --------------------------------------------------------

    def start_recording(self, _event=None):
        if self.recording:
            return
        os.makedirs(self.args.outdir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.csv_path = os.path.join(self.args.outdir, "accel_%s.csv" % stamp)
        f = open(self.csv_path, "w", encoding="utf-8")
        f.write("# accel_live capture\n")
        f.write("# started_utc: %s\n" % datetime.now(timezone.utc).isoformat())
        f.write("# source: %s\n" % (self.source.port if self.source else "?"))
        f.write("# odr_hz_nominal: %d\n" % self.odr)
        f.write("# accel_range_g: %d\n" % self.accel_range)
        f.write("# t_s is relative to the first sample of this recording;\n")
        f.write("# t_us is the board's raw micros() reading for that sample.\n")
        f.write("# host_iso is host arrival time, for cross-reference only.\n")
        f.write("t_s,t_us,x_g,y_g,z_g,host_iso\n")

        with self.lock:
            self.csv_file = f
            self.rec_t0_us = None
            self.rec_rows = 0
            self.rec_peak = 0.0
            self.rec_clipped = 0
            self.rec_data = []
            self.recording = True
        self.status = "recording -> %s" % os.path.basename(self.csv_path)

        # Make the recording state impossible to miss: the button itself
        # turns red and its label changes, instead of relying on the small
        # status text at the top of the window.
        if self._saved_flash_timer is not None:
            self._saved_flash_timer.stop()
            self._saved_flash_timer = None
        self.record_btn.label.set_text("● Recording...")
        self.record_btn.color = self.RECORDING_COLOR
        self.record_btn.hovercolor = self.RECORDING_HOVER
        self.record_btn.ax.set_facecolor(self.RECORDING_COLOR)
        self.fig.canvas.draw_idle()

    def stop_recording(self, _event=None):
        if not self.recording:
            return
        with self.lock:
            self.recording = False
            f, self.csv_file = self.csv_file, None
            rows, path = self.rec_rows, self.csv_path
            data = list(self.rec_data)
        if f is not None:
            f.close()

        png = None
        if data:
            png = os.path.splitext(path)[0] + ".png"
            self._save_overview(data, png)

        self.status = "saved %d rows to %s%s" % (
            rows, os.path.basename(path),
            " (+ %s)" % os.path.basename(png) if png else "")

        # Flash the record button green with a clear "Saved" label for 1.5 s
        # so a saved recording is visually obvious, then revert to idle.
        self.record_btn.label.set_text("✓ Saved")
        self.record_btn.color = self.SAVED_COLOR
        self.record_btn.hovercolor = self.SAVED_COLOR
        self.record_btn.ax.set_facecolor(self.SAVED_COLOR)
        self.fig.canvas.draw_idle()

        def _revert():
            self.record_btn.label.set_text("Record (r)")
            self.record_btn.color = self.IDLE_COLOR
            self.record_btn.hovercolor = self.IDLE_HOVER
            self.record_btn.ax.set_facecolor(self.IDLE_COLOR)
            self.fig.canvas.draw_idle()
            self._saved_flash_timer = None

        self._saved_flash_timer = self.fig.canvas.new_timer(interval=1500)
        self._saved_flash_timer.single_shot = True
        self._saved_flash_timer.add_callback(_revert)
        self._saved_flash_timer.start()

    def _save_overview(self, data, png_path):
        # One panel per axis plus the combined magnitude, so a fast, small
        # event on a single axis (e.g. a mostly-horizontal push-off) isn't
        # hidden by the other two traces overlapping it - see BLEtest.ino's
        # own note that vector magnitude alone under-reports a lateral tap.
        arr = np.asarray(data, dtype=float)
        t, ax_, ay_, az_, mag = arr[:, 0], arr[:, 1], arr[:, 2], arr[:, 3], arr[:, 4]

        fig, (a1, a2, a3, a4) = plt.subplots(4, 1, figsize=(12, 11), sharex=True)
        fig.suptitle("accel_live - %s (%d samples, %.2f s)"
                      % (os.path.basename(png_path), len(t), t[-1] - t[0]))

        for ax_plot, series, color, label in (
            (a1, ax_, "r", "X"),
            (a2, ay_, "g", "Y"),
            (a3, az_, "b", "Z"),
        ):
            ax_plot.plot(t, series, color=color, lw=0.8, label=label)
            ax_plot.axhline(self.accel_range, color="k", ls=":", lw=0.8)
            ax_plot.axhline(-self.accel_range, color="k", ls=":", lw=0.8)
            ax_plot.set_ylabel("%s [g]" % label)
            ax_plot.grid(True, ls="--", alpha=0.5)
            ax_plot.legend(loc="upper right", fontsize=8)

        a4.plot(t, mag, "m-", lw=0.9, label="|a|")
        a4.axhline(mag.max(), color="0.5", ls="--", lw=0.8,
                   label="peak %.3f g" % mag.max())
        a4.set_xlabel("Time since recording start [s] (board clock)")
        a4.set_ylabel("Magnitude [g]")
        a4.grid(True, ls="--", alpha=0.5)
        a4.legend(loc="upper right", fontsize=8)

        fig.tight_layout()
        fig.savefig(png_path, dpi=140)
        plt.close(fig)

    def snapshot(self, _event=None):
        os.makedirs(self.args.outdir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(self.args.outdir, "accel_snapshot_%s.png" % stamp)
        self.fig.savefig(path, dpi=140)
        self.status = "snapshot saved to %s" % os.path.basename(path)

        label = self.snapshot_btn.label
        original = label.get_text()
        label.set_text("✓ Saved")
        self.snapshot_btn.ax.set_facecolor(self.SAVED_COLOR)
        self.fig.canvas.draw_idle()

        def _revert():
            label.set_text(original)
            self.snapshot_btn.ax.set_facecolor(self.IDLE_COLOR)
            self.fig.canvas.draw_idle()

        t = self.fig.canvas.new_timer(interval=900)
        t.single_shot = True
        t.add_callback(_revert)
        t.start()

    # -- UI -----------------------------------------------------------------

    def _build_ui(self):
        self.fig, (self.ax_accel, self.ax_mag) = plt.subplots(
            2, 1, figsize=(12, 7.5), sharex=True)
        self.fig.canvas.manager.set_window_title("accel_live")
        self.fig.subplots_adjust(bottom=0.17, top=0.86, hspace=0.12)

        (self.l_x,) = self.ax_accel.plot([], [], "r-", lw=0.8, label="X")
        (self.l_y,) = self.ax_accel.plot([], [], "g-", lw=0.8, label="Y")
        (self.l_z,) = self.ax_accel.plot([], [], "b-", lw=0.8, label="Z")
        self.ax_accel.set_ylabel("Accel [g]")
        self.ax_accel.grid(True, ls="--", alpha=0.5)
        self.ax_accel.legend(loc="upper left", fontsize=8, ncol=3)

        (self.l_m,) = self.ax_mag.plot([], [], "m-", lw=1.0, label="|a|")
        self.ax_mag.set_xlabel("Time [s] (board clock)")
        self.ax_mag.set_ylabel("Magnitude [g]")
        self.ax_mag.grid(True, ls="--", alpha=0.5)

        self.txt_status = self.fig.text(0.012, 0.965, "", fontsize=9.5,
                                         family="monospace", weight="bold")
        self.txt_stats = self.fig.text(0.012, 0.925, "", fontsize=9,
                                        family="monospace", va="top")

        self.IDLE_COLOR = "0.85"
        self.IDLE_HOVER = "0.95"
        self.RECORDING_COLOR = "#e74c3c"
        self.RECORDING_HOVER = "#ff6b5b"
        self.SAVED_COLOR = "#2ecc71"
        self._saved_flash_timer = None

        self.buttons = []
        specs = [
            ("Record (r)", self.start_recording),
            ("Stop (s)", self.stop_recording),
            ("Snapshot PNG (p)", self.snapshot),
        ]
        for i, (label, cb) in enumerate(specs):
            axb = self.fig.add_axes([0.012 + i * 0.155, 0.03, 0.145, 0.055])
            b = Button(axb, label, color=self.IDLE_COLOR, hovercolor=self.IDLE_HOVER)
            b.on_clicked(cb)
            self.buttons.append(b)
        self.record_btn, self.stop_btn, self.snapshot_btn = self.buttons

        self.fig.canvas.mpl_connect("key_press_event", self._on_key)
        self.fig.canvas.mpl_connect("close_event", lambda _e: self.shutdown())

    def _on_key(self, event):
        if event.key == "r":
            self.start_recording()
        elif event.key == "s":
            self.stop_recording()
        elif event.key == "p":
            self.snapshot()

    def _refresh(self):
        with self.lock:
            if not self.t_buf:
                return
            step = self.args.plot_decimation
            t = list(self.t_buf)[::step]
            x = list(self.x_buf)[::step]
            y = list(self.y_buf)[::step]
            z = list(self.z_buf)[::step]
            m = list(self.m_buf)[::step]
            t_span = (self.t_buf[0], self.t_buf[-1])
            n_win = len(self.t_buf)
            recording = self.recording
            rec_rows, rec_peak, rec_clipped = self.rec_rows, self.rec_peak, self.rec_clipped

        self.l_x.set_data(t, x)
        self.l_y.set_data(t, y)
        self.l_z.set_data(t, z)
        self.l_m.set_data(t, m)

        t_end = t_span[1]
        self.ax_accel.set_xlim(max(0.0, t_end - self.window_s), t_end + 0.05)
        if x:
            lo = min(min(x), min(y), min(z))
            hi = max(max(x), max(y), max(z))
            pad = max(0.1, 0.1 * (hi - lo))
            self.ax_accel.set_ylim(lo - pad, hi + pad)
            self.ax_mag.set_ylim(min(m) - 0.1, max(m) + 0.2)

        span = t_span[1] - t_span[0]
        board_rate = (n_win - 1) / span if span > 0 else 0.0
        host_rate = self.total_samples / max(1e-6, time.monotonic() - self.host_t0)

        state = "REC" if recording else "---"
        self.txt_status.set_text("[%s] %s" % (state, self.status))
        stats = ("samples %-9d  board rate %7.1f Hz  host rate %7.1f Hz  "
                 "gaps %-4d  range +/-%d g"
                 % (self.total_samples, board_rate, host_rate, self.gap_count,
                    self.accel_range))
        if recording:
            stats += "\n              rows %-9d  peak %6.3f g  clipped %d" % (
                rec_rows, rec_peak, rec_clipped)
        if rec_clipped:
            stats += "   <<< CLIPPING: raise the range"
        self.txt_stats.set_text(stats)

        # Without this, updated line/text data sits unrendered until some
        # unrelated event (a button click, a window resize) happens to flush
        # it - the plot looks frozen even though data is arriving correctly.
        self.fig.canvas.draw_idle()

    def run(self):
        self.connect()
        timer = self.fig.canvas.new_timer(interval=self.args.refresh_ms)
        timer.add_callback(self._refresh)
        timer.start()
        plt.show()

    def shutdown(self):
        self.running = False
        if self.recording:
            self.stop_recording()
        if self.source is not None:
            self.source.close()
            self.source = None


def autodetect_port():
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            name = (p.device or "")
            if "usbmodem" in name or "usbserial" in name or "ACM" in name:
                return name
    except Exception:
        pass
    for pattern in ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: autodetected)")
    ap.add_argument("--baud", type=int, default=921600, help="must match the firmware")
    ap.add_argument("--odr", type=int, default=416, help="nominal accelerometer ODR")
    ap.add_argument("--range", type=int, default=8,
                     help="full-scale range in g; must match the firmware")
    ap.add_argument("--window", type=float, default=3.0, help="seconds visible on screen")
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR, help="where CSV/PNG files go")
    ap.add_argument("--refresh-ms", type=int, default=200, help="redraw period")
    ap.add_argument("--plot-decimation", type=int, default=2,
                     help="1 point every N in the plot; the CSV stays full-rate")
    ap.add_argument("--simulate", action="store_true",
                     help="fake data, to try the UI without a board")
    args = ap.parse_args(argv)

    app = LiveApp(args)
    try:
        app.run()
    finally:
        app.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
