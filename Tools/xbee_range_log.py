#!/usr/bin/env python3
"""
Host logger for Arduino/Xbee_RangeTest.

Tether one of the two range-test boards over USB, run this, and walk the other
board away in measured steps. It transcribes the board's per-packet rows to a
CSV under Data/, tags each row with the distance you are currently at, and
prints a rolling PDR / RSSI / RTT readout so you can decide when to move on.

TIME
----
As everywhere in this repo, the only measurement clock is the board's. The
board prints dt_us / rtt_us from its own micros(); this script only adds a
host wall-clock column (host_iso) for cross-referencing runs. Do not measure
from host_iso.

WHICH BOARD TO TETHER
--------------------
Default: tether the SENDER only. The laptop stays at the start line with it and
the RECEIVER walks away on a power bank - the two boards end up hundreds of
metres apart, so one host cannot reach both (a USB hub does not span 200 m).
That gives uplink PDR, MAC retry count, RTT, and the receiver's RSSI, which the
receiver carries back inside the echo.
Tethering the RECEIVER instead needs a second laptop and a second copy of this
script. It adds the two things the sender log cannot show: one-way downlink PDR
measured at the far end, and clean one-way inter-arrival jitter (dt_us). Without
it, RTT jitter is the only jitter figure available, and it folds both hops plus
the receiver's ATDB turnaround into one number.

USAGE
    python3 Tools/xbee_range_log.py --distance 0
    python3 Tools/xbee_range_log.py --port /dev/cu.usbmodem1101 --distance 25

While running, type a command + Enter:
    d <metres>   set the current distance tag (also resets the board + host
                 counters and starts a new segment)
    <blank>      print stats now
    q            quit

Requires: pyserial.
"""

import argparse
import glob
import os
import queue
import sys
import threading
import time
from datetime import datetime, timezone

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUTDIR = os.path.join(REPO_ROOT, "Data")

SENDER_COLS = ("host_iso,distance_m,seq,unicast,tx_delivery,tx_retries,"
               "echo_ok,rtt_us,rssi_remote_dbm")
RECEIVER_COLS = "host_iso,distance_m,seq,rssi_dbm,dt_us,seq_gap"


def autodetect_port():
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            name = p.device or ""
            if "usbmodem" in name or "usbserial" in name or "ACM" in name:
                return name
    except Exception:
        pass
    for pat in ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pat))
        if found:
            return found[0]
    return None


def percentile(sorted_vals, q):
    """Linear-interpolated percentile of an already-sorted list. Avoids a
    numpy dependency in the logger - the plot script does the real analysis."""
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    pos = (len(sorted_vals) - 1) * q
    lo = int(pos)
    frac = pos - lo
    hi = min(lo + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


class Segment:
    """Accumulators for one distance point, reset on every 'd' command."""

    def __init__(self, distance_m):
        self.distance_m = distance_m
        self.t0 = time.monotonic()
        self.rows = 0
        self.first_seq = None
        self.last_seq = None
        # sender
        self.uplink_ok = 0
        self.retries = []
        self.echo_ok = 0
        self.rtt_us = []
        self.rssi_remote = []
        # receiver
        self.rssi = []
        self.dt_us = []

    def add_sender(self, seq, unicast, tx_delivery, tx_retries, echo_ok, rtt_us, rssi_remote):
        self.rows += 1
        self._seq(seq)
        if unicast == 1 and tx_delivery == 0:
            self.uplink_ok += 1
        if unicast == 1 and tx_retries >= 0:
            self.retries.append(tx_retries)
        if echo_ok == 1:
            self.echo_ok += 1
            if rtt_us >= 0:
                self.rtt_us.append(rtt_us)
            if rssi_remote != 0:
                self.rssi_remote.append(rssi_remote)

    def add_receiver(self, seq, rssi_dbm, dt_us, seq_gap):
        self.rows += 1
        self._seq(seq)
        if rssi_dbm != 0:
            self.rssi.append(rssi_dbm)
        if dt_us > 0:
            self.dt_us.append(dt_us)

    def _seq(self, seq):
        if self.first_seq is None:
            self.first_seq = seq
        self.last_seq = seq

    def expected(self):
        if self.first_seq is None:
            return 0
        return self.last_seq - self.first_seq + 1

    def summary(self, role):
        exp = self.expected()
        if exp <= 0:
            return "distance %s m  waiting for packets..." % self.distance_m
        if role == "sender":
            rtt = sorted(self.rtt_us)
            rt_pdr = 100.0 * self.echo_ok / exp
            ul_pdr = 100.0 * self.uplink_ok / exp
            mean_ret = (sum(self.retries) / len(self.retries)) if self.retries else float("nan")
            return ("distance %-4s m  rows %-5d  uplink-PDR %5.1f%%  rt-PDR %5.1f%%  "
                    "retries~%.2f  rtt_us p50 %.0f p95 %.0f min %.0f"
                    % (self.distance_m, self.rows, ul_pdr, rt_pdr, mean_ret,
                       percentile(rtt, 0.5), percentile(rtt, 0.95),
                       rtt[0] if rtt else float("nan")))
        else:
            pdr = 100.0 * self.rows / exp
            rssi_mean = (sum(self.rssi) / len(self.rssi)) if self.rssi else float("nan")
            # RSSI is negative dBm, so the weakest signal is the most negative:
            # min(), not max(). Matches the firmware's rssiMin.
            rssi_worst = min(self.rssi) if self.rssi else float("nan")
            dt = sorted(self.dt_us)
            jit = percentile(dt, 0.95) - percentile(dt, 0.5) if dt else float("nan")
            return ("distance %-4s m  rows %-5d  PDR %5.1f%%  rssi_dbm mean %.1f worst %.0f  "
                    "dt_us p50 %.0f jitter(p95-p50) %.0f"
                    % (self.distance_m, self.rows, pdr, rssi_mean, rssi_worst,
                       percentile(dt, 0.5), jit))


class Logger:
    def __init__(self, args):
        self.args = args
        self.role = None            # 'sender' | 'receiver', learned from the first data row
        self.csv = None
        self.csv_path = None
        self.seg = Segment(args.distance)
        self.distance = args.distance
        self.cmd_q = queue.Queue()
        self.running = True
        self.ser = None

    # -- serial -----------------------------------------------------------

    def open_serial(self):
        import serial
        port = self.args.port or autodetect_port()
        if not port:
            print("no serial port found - connect the board", file=sys.stderr)
            sys.exit(1)
        self.ser = serial.Serial(port, self.args.baud, timeout=0.1)
        print("# reading %s @ %d" % (port, self.args.baud))

    def _reader(self):
        buf = ""
        while self.running:
            try:
                n = self.ser.in_waiting
                chunk = self.ser.read(n if n else 1)
            except Exception as exc:
                print("# serial read error: %s" % exc, file=sys.stderr)
                return
            if not chunk:
                continue
            buf += chunk.decode("utf-8", errors="ignore")
            parts = buf.split("\n")
            buf = parts.pop()
            for line in parts:
                self._handle(line.strip())

    def _handle(self, line):
        if not line:
            return
        if line.startswith("#"):
            print(line)
            return
        parts = line.split(",")
        try:
            if parts[0] == "S":
                vals = [int(x) for x in parts[1:8]]
                self._ensure_csv("sender")
                self.seg.add_sender(*vals)
                self._write(vals)
            elif parts[0] == "R":
                vals = [int(x) for x in parts[1:5]]
                self._ensure_csv("receiver")
                self.seg.add_receiver(*vals)
                self._write(vals)
        except (ValueError, IndexError):
            print("# unparsed: %s" % line)

    # -- csv ------------------------------------------------------------

    def _ensure_csv(self, role):
        if self.csv is not None:
            return
        self.role = role
        os.makedirs(self.args.outdir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.csv_path = os.path.join(self.args.outdir, "xbee_range_%s.csv" % stamp)
        self.csv = open(self.csv_path, "w", encoding="utf-8")
        self.csv.write("# xbee range test\n")
        self.csv.write("# role=%s\n" % role)
        self.csv.write("# started_utc: %s\n" % datetime.now(timezone.utc).isoformat())
        self.csv.write("# dt_us / rtt_us are the board's micros(); host_iso is arrival time only\n")
        self.csv.write((SENDER_COLS if role == "sender" else RECEIVER_COLS) + "\n")
        self.csv.flush()
        print("# logging -> %s" % self.csv_path)

    def _write(self, vals):
        host_iso = datetime.now(timezone.utc).isoformat()
        self.csv.write("%s,%s,%s\n" % (host_iso, self.distance,
                                       ",".join(str(v) for v in vals)))
        self.csv.flush()

    # -- commands -----------------------------------------------------

    def _stdin(self):
        for line in sys.stdin:
            self.cmd_q.put(line.strip())
        self.cmd_q.put("q")

    def _apply_cmd(self, cmd):
        if cmd == "q":
            self.running = False
        elif cmd.startswith("d "):
            try:
                self.distance = float(cmd[2:].strip())
            except ValueError:
                print("# bad distance")
                return
            if self.ser:
                try:
                    self.ser.write(b"r")   # reset the board's counters too
                except Exception:
                    pass
            self.seg = Segment(self.distance)
            print("# --- new segment: %s m ---" % self.distance)
        elif cmd == "":
            print(self.seg.summary(self.role or "sender"))
        else:
            print("# commands: 'd <metres>', blank = stats, 'q' = quit")

    # -- run --------------------------------------------------------

    def run(self):
        self.open_serial()
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._stdin, daemon=True).start()
        last_print = 0.0
        while self.running:
            try:
                cmd = self.cmd_q.get(timeout=0.25)
                self._apply_cmd(cmd)
            except queue.Empty:
                pass
            now = time.monotonic()
            if self.role and now - last_print > 2.0:
                last_print = now
                print(self.seg.summary(self.role))
        if self.csv:
            self.csv.close()
        if self.ser:
            self.ser.close()
        print("# done -> %s" % self.csv_path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detected)")
    ap.add_argument("--baud", type=int, default=115200,
                    help="USB serial baud - matches Serial.begin() in the sketch")
    ap.add_argument("--distance", type=float, default=0.0,
                    help="distance tag to start at, in metres")
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR, help="where the CSV goes")
    args = ap.parse_args(argv)

    Logger(args).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
