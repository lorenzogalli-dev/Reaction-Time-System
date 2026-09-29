#!/usr/bin/env python3
"""Logger for EspNowLrTest: one laptop per board, Mac or Windows.

Reads the board over USB, writes every line to a CSV with the laptop's own
timestamp in front, and prints live figures so you can tell in the field
whether the link is alive without waiting for the analysis.

    python3 logger.py A          # laptop at the start, ESP-A
    python3 logger.py B          # laptop walking, ESP-B
    py logger.py B               # same, on Windows

Requires pyserial:  pip install pyserial   (Windows: py -m pip install pyserial)

Commands, typed and followed by Enter (on laptop B):
    50          start a station at 50 m (the window lasts --station-s seconds)
    (empty)     end the station early / "we are walking"
    n <text>    free note in the log, e.g.  n Marco crouched over A
    ?           ask the board who it is
    ap on       B brings up the phone access point (reboots the board)
    ap off      ...and turns it off again
    q           quit
"""
import argparse
import datetime as dt
import os
import queue
import statistics
import sys
import threading
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as e:
    # Not always "missing": the unrelated PyPI package called "serial" installs
    # a module with the same name and shadows pyserial.
    sys.exit(f"cannot import pyserial ({e!r}) with {sys.executable}\n"
             f"  serial found at: {getattr(sys.modules.get('serial'), '__file__', 'nowhere')}\n"
             "  fix: pip uninstall serial  then  pip install --force-reinstall pyserial")

ESPRESSIF_VID = 0x303A
PKTS_PER_SLOT = 100          # must match app.cpp
MODES = ["LR250", "LR500", "11b1M"]


def mode_of(seq):
    return (seq // PKTS_PER_SLOT) % len(MODES)


def find_port():
    ports = list(list_ports.comports())
    esp = [p for p in ports if p.vid == ESPRESSIF_VID]
    if len(esp) == 1:
        return esp[0].device
    if not ports:
        return None
    cands = esp or ports
    print("Which port?")
    for i, p in enumerate(cands):
        print(f"  {i}: {p.device}  {p.description}")
    return cands[int(input("> ").strip() or 0)].device


def open_port(dev):
    # DTR and RTS low BEFORE opening: on the C3's USB-serial, pyserial's
    # defaults can reset the chip or park it in the bootloader.
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = dev, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


class Station:
    """Live counts for the current station, per mode."""

    def __init__(self, dist, role):
        self.dist, self.role, self.t0 = dist, role, time.monotonic()
        self.seqs = {m: [] for m in range(len(MODES))}
        self.rssi = {m: [] for m in range(len(MODES))}
        self.t0_tries = {m: [] for m in range(len(MODES))}
        self.t0_lost = {m: 0 for m in range(len(MODES))}

    def add(self, f):
        k = f[0]
        try:
            if k == "D" and self.role == "B":           # D,ms,seq,mode,rssi,...
                seq, m = int(f[2]), int(f[3])
                self.seqs[m].append(seq)
                self.rssi[m].append(int(f[4]))
            elif k == "E" and self.role == "A":         # E,ms,seq,mode,delay,off,rssiB,rssiA
                seq, m = int(f[2]), int(f[3])
                self.seqs[m].append(seq)
                self.rssi[m].append(int(f[7]))
            elif k == "K" and self.role == "A":         # K,ms,id,mode,tries,delay
                self.t0_tries[int(f[3])].append(int(f[4]))
            elif k == "X" and self.role == "A":         # X,ms,id,mode,tries
                self.t0_lost[int(f[3])] += 1
        except (IndexError, ValueError):
            pass

    def summary(self):
        el = time.monotonic() - self.t0
        parts = [f"[{self.dist} m  {el:4.0f}s]" if self.dist != "live" else "[last 15 s]"]
        for m, name in enumerate(MODES):
            s = self.seqs[m]
            if not s:
                parts.append(f"{name}: --")
                continue
            lo, hi = min(s), max(s)
            exp = sum(1 for q in range(lo, hi + 1) if mode_of(q) == m)
            pdr = 100.0 * len(set(s)) / exp if exp else 0
            r = statistics.median(self.rssi[m])
            txt = f"{name}: {pdr:5.1f}% {r:4.0f}dBm"
            if self.role == "A":
                tr = self.t0_tries[m]
                txt += f" t0 ok {len(tr)}/{len(tr) + self.t0_lost[m]}"
                if tr:
                    txt += f" max {max(tr)} try"
            parts.append(txt)
        return "  |  ".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("role", choices=["A", "B"], help="which board is plugged into this laptop")
    ap.add_argument("--port", help="serial port (found automatically if omitted)")
    ap.add_argument("--station-s", type=float, default=30.0, help="length of a station window")
    ap.add_argument("--outdir", default=str(Path(__file__).resolve().parent / "measurements"
                                            / dt.date.today().strftime("%Y%m%d")))
    a = ap.parse_args()

    dev = a.port or find_port()
    if not dev:
        sys.exit("No serial port found. Is the ESP plugged in?")
    os.makedirs(a.outdir, exist_ok=True)
    path = Path(a.outdir) / f"{dt.datetime.now():%H%M%S}_{a.role}.csv"
    out = open(path, "w", buffering=1, newline="")
    out.write(f"# EspNowLrTest role={a.role} port={dev} started={dt.datetime.now().isoformat()}\n")
    out.write("# host_s,<board line>   host_s = seconds since the logger started\n")
    print(f"port {dev}  ->  {path}")

    cmds = queue.Queue()
    threading.Thread(target=lambda: [cmds.put(l.rstrip("\r\n")) for l in sys.stdin], daemon=True).start()

    T0 = time.monotonic()
    host = lambda: time.monotonic() - T0
    st = None
    last_print = 0.0
    last_rx = time.monotonic()
    s = None
    buf = b""

    while True:
        # --- the port: open it, and reopen it if the cable is wiggled or the board reboots
        if s is None:
            try:
                s = open_port(dev)
                s.write(b"?\n")
                print(f"{host():8.1f}  connected to {dev}")
            except (serial.SerialException, OSError):
                time.sleep(1.0)
                if a.port is None:
                    dev = find_port() or dev
                continue

        # --- commands from the keyboard
        while not cmds.empty():
            c = cmds.get().strip()
            if c == "q":
                out.write(f"{host():.3f},#,quit\n")
                out.close()
                print(f"saved {path}")
                return
            if c.startswith("n "):
                out.write(f"{host():.3f},N,{c[2:].replace(',', ';')}\n")
                print(f"{host():8.1f}  note saved")
            elif c.startswith("ap ") or c == "?":
                s.write((c + "\n").encode())
                out.write(f"{host():.3f},#,cmd {c}\n")
            elif c == "":
                if st and st.dist != "live":
                    print(f"{host():8.1f}  END  {st.summary()}")
                    out.write(f"{host():.3f},M,-\n")
                st = None
            else:
                try:
                    d = float(c)
                except ValueError:
                    print("?  a distance in metres, empty, 'n <note>', 'ap on', 'ap off' or 'q'")
                    continue
                if st and st.dist != "live":
                    print(f"{host():8.1f}  END  {st.summary()}")
                st = Station(f"{d:g}", a.role)
                out.write(f"{host():.3f},M,{d:g}\n")
                print(f"{host():8.1f}  STATION {d:g} m, stand still for {a.station_s:.0f} s")

        # --- board lines
        try:
            chunk = s.read(4096)
        except (serial.SerialException, OSError):
            print(f"{host():8.1f}  PORT LOST, retrying...")
            out.write(f"{host():.3f},#,port lost\n")
            s = None
            continue
        if chunk:
            last_rx = time.monotonic()
            buf += chunk
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode(errors="replace").strip()
                if not line:
                    continue
                out.write(f"{host():.3f},{line}\n")
                f = line.split(",")
                if f[0] in ("I", "#"):
                    print(f"{host():8.1f}  {line}")
                    # The role lives in the board's flash: whatever it was,
                    # it becomes the one this laptop was started with.
                    if f[0] == "I" and len(f) > 1 and f[1] != a.role:
                        print(f"{host():8.1f}  board is role {f[1]}, setting {a.role} (it reboots)")
                        s.write(f"role {a.role}\n".encode())
                elif st:
                    st.add(f)

        # --- live output
        now = time.monotonic()
        # Laptop A sits alone at the blocks and nobody types distances there:
        # it shows a rolling 15 s window instead (one pass over all three modes).
        if a.role == "A" and st is None:
            st = Station("live", a.role)
        if a.role == "A" and st.dist == "live" and now - st.t0 >= 15.0:
            print(f"{host():8.1f}  {st.summary()}")
            st = Station("live", a.role)
            continue
        if st and st.dist != "live" and now - st.t0 >= a.station_s:
            print(f"{host():8.1f}  DONE \a {st.summary()}")
            out.write(f"{host():.3f},M,-\n")
            st = None
        if now - last_print >= 2.0:
            last_print = now
            if st and st.dist != "live":
                print(f"{host():8.1f}  {st.summary()}")
            if now - last_rx > 3.0:
                print(f"{host():8.1f}  !! no data from the board for {now - last_rx:.0f} s")


if __name__ == "__main__":
    main()
