#!/usr/bin/env python3
"""
pull_captures.py - downloads the starts the board stored on its flash while it
ran on its own (no computer attached), as the same CSVs capture.py writes.

The board replays each stored run through its normal dump ('F'), so nothing
here is a second parser of a second format: it is capture.py's header table
and capture.py's write_csv, fed from a different command.

Files are named <date>_<time>_r<id>_<result>.csv, e.g.
20260929_193512_r00012_0.160s.csv or ..._FS-0.045s.csv. The date is when the
start happened, and the board only knows it if a PC gave it the time (capture.py
and this script do, on connect) since its last power-on. A run recorded on a
power bank after a power cycle is named nodate_r00012_0.160s.csv rather than
given a wrong date. The run id never repeats, so pulling twice just rewrites
the same files, and nothing is lost by pulling before erasing.

Usage:
    python3 Arduino/AlgorithmRealTimeESP32/Python_Tools/pull_captures.py            # list + download
    python3 Arduino/AlgorithmRealTimeESP32/Python_Tools/pull_captures.py --list     # list only
    python3 Arduino/AlgorithmRealTimeESP32/Python_Tools/pull_captures.py --erase    # download, then free the board
"""

import argparse
import os
import sys
import time
from pathlib import Path

import datetime

from capture import HEADER_KEYS, TEXT_KEYS, autodetect_port, result_tag, send_time, write_csv

DEFAULT_OUTDIR = Path(__file__).resolve().parents[3] / "Data" / "board"


def read_line(ser, deadline):
    while time.time() < deadline:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            return line
    raise TimeoutError("board stopped answering")


def list_runs(ser):
    ser.reset_input_buffer()
    ser.write(b"L")
    deadline = time.time() + 5
    runs, free = [], None
    while True:
        line = read_line(ser, deadline)
        if line.startswith("FILES,"):
            free = int(line.split(",")[2])
        elif line.startswith("FILE,"):
            _, rid, n = line.split(",")
            runs.append((int(rid), int(n)))
        elif line == "FILES_END":
            return runs, free


def fetch_all(ser, outdir):
    ser.reset_input_buffer()
    ser.write(b"F")
    saved, failed = [], []
    in_dump, header, rows = False, {}, []
    deadline = time.time() + 10
    while True:
        line = read_line(ser, deadline)
        deadline = time.time() + 10          # per line, not for the whole pull
        if line == "FETCH_END":
            return saved, failed
        if line.startswith("FETCH_FAIL,"):
            failed.append(line.split(",")[1])
            continue
        if line.startswith("DUMP_START"):
            in_dump, header, rows = True, {}, []
            expected = int(line.split(",")[1])
            continue
        if not in_dump:
            continue                          # idle preview rows, banner text
        if line == "DUMP_END":
            in_dump = False
            rid = header.get("board_run_id", 0)
            if len(rows) != expected or not rows:
                failed.append(f"{rid} ({len(rows)}/{expected} rows)")
                continue
            wc = header.get("board_wallclock_unix")
            when = (datetime.datetime.fromtimestamp(wc).strftime("%Y%m%d_%H%M%S")
                    if wc else "nodate")
            path = os.path.join(outdir, f"{when}_r{rid:05d}_{result_tag(header)}.csv")
            warn = write_csv(path, header, rows)
            verdict = header.get("board_verdict", "?")
            rt = header.get("board_reaction_ms")
            rt_s = f"{rt:.1f} ms" if isinstance(rt, float) else ""
            print(f"  r{rid:05d}  {len(rows):5d} rows  {verdict:12s} {rt_s}")
            for w in warn:
                print(f"           !! {w}")
            saved.append(rid)
            continue
        key, _, val = line.partition(",")
        if key in TEXT_KEYS:
            header[TEXT_KEYS[key]] = val.strip()
        elif key in HEADER_KEYS:
            try:
                header[HEADER_KEYS[key]] = float(val) if "." in val else int(val)
            except ValueError:
                pass
        else:
            parts = line.split(",")
            if len(parts) == 4:
                try:
                    rows.append((int(parts[0]), parts[1], parts[2], parts[3]))
                except ValueError:
                    pass


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: autodetected)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--outdir", default=str(DEFAULT_OUTDIR),
                    help="where to write the CSVs (default: Data/board/)")
    ap.add_argument("--list", action="store_true", help="only list what is stored")
    ap.add_argument("--erase", action="store_true",
                    help="after a complete download, erase the runs from the board")
    args = ap.parse_args()

    import serial

    port = args.port or autodetect_port()
    if not port:
        print("No serial port found - plug in the board and switch it on "
              "(hold the button 3 s).", file=sys.stderr)
        sys.exit(1)
    ser = serial.Serial(port, args.baud, timeout=0.2)
    time.sleep(1.0)
    send_time(ser)

    # A board just switched on spends up to ~3 s booting before it answers.
    for attempt in range(3):
        try:
            runs, free = list_runs(ser)
            break
        except TimeoutError:
            if attempt == 2:
                print("The board does not answer - is it switched on?", file=sys.stderr)
                sys.exit(1)
            time.sleep(1.0)
            send_time(ser)
    print(f"{len(runs)} run(s) on the board, {free} free slot(s)")
    if args.list or not runs:
        return

    os.makedirs(args.outdir, exist_ok=True)
    print(f"Downloading to {args.outdir}")
    saved, failed = fetch_all(ser, args.outdir)
    print(f"{len(saved)} saved, {len(failed)} failed")
    for f in failed:
        print(f"  !! run {f} could not be read")

    if args.erase:
        # Only when every listed run arrived whole: an erase after a partial
        # pull is the one way to lose data here.
        if failed or len(saved) != len(runs):
            print("Not erasing: the download was incomplete.")
            sys.exit(1)
        ser.reset_input_buffer()
        ser.write(b"X")
        deadline = time.time() + 10
        while True:
            line = read_line(ser, deadline)
            if line.startswith("ERASED,"):
                print(f"Erased {line.split(',')[1]} run(s) from the board.")
                break


if __name__ == "__main__":
    main()
