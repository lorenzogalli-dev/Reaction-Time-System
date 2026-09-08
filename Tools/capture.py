#!/usr/bin/env python3
"""
capture.py - writes to disk what AccelStream.ino v4 dumps. Nothing else.

This is deliberately dumb. Through v3 the host owned the recording: it held
the state, it sent the o/s/g markers on a human's keypress, and it decided
when a capture began and ended. That put a person's reaction time inside the
measurement of a person's reaction time - on Data/accel_20260907_160108.csv
the "go" marker lands ~90 ms after the movement it was meant to mark.

In v4 the firmware owns all of it: the button, the randomised delays, the
three beeps, the marker timestamps and the recorded window. This script opens
the port, prints the board's progress lines so you can follow a run, and
writes each dump to a CSV. It makes no timing decisions, so it cannot corrupt
any. Keep it that way - anything that needs to *decide* something belongs in
the firmware (if it is about when) or in start_detector.py (if it is about
what the data means).

Usage:
    python3 Tools/capture.py                 # autodetect port, run until Ctrl-C
    python3 Tools/capture.py --port /dev/cu.usbmodem101 --outdir Data
"""

import argparse
import datetime
import os
import sys
import threading
import time


def autodetect_port():
    from serial.tools import list_ports
    for p in list_ports.comports():
        # The XIAO enumerates as a USB CDC device; on macOS that is
        # cu.usbmodem*, on Linux ttyACM*.
        if "usbmodem" in p.device or "ttyACM" in p.device:
            return p.device
    return None


# Header lines the firmware emits between DUMP_START and the rows. Value is
# the comment key written into the CSV.
HEADER_KEYS = {
    "ON": "on_t_us",
    "SET": "set_t_us",
    "GO": "go_t_us",
    "PREROLL": "preroll_samples",
    "TRUNCATED": "truncated",
    "DROPPED": "dropped",
    "CLOCKSTEP": "clockstep_us",
}


def write_csv(path, header, rows):
    """Write one dump. Markers go in the leading comment block, not appended
    after the rows the way accel_live.py had to do it - the firmware now sends
    them *before* the data, so there is no reason to keep the append hack that
    already cost one bug (a reader that stopped at the first non-comment line
    and silently found no markers at all)."""
    t0 = rows[0][0]
    on_us = header.get("on_t_us", 0)
    set_us = header.get("set_t_us", 0)
    go_us = header.get("go_t_us", 0)

    with open(path, "w", encoding="utf-8") as f:
        f.write("# AccelStream v4 capture\n")
        f.write(f"# written_utc: {datetime.datetime.now(datetime.timezone.utc).isoformat()}\n")
        f.write("# t_us is the board's raw micros() reading for that sample.\n")
        f.write("# t_s is relative to the first row of this file.\n")
        f.write("# on/set/go are the firmware's own beep timestamps, on the same\n")
        f.write("# micros() clock as every sample - no host or keypress latency.\n")
        for key in ("on_t_us", "set_t_us", "go_t_us"):
            f.write(f"# {key}: {header.get(key, 0)}\n")
            us = header.get(key, 0)
            if us:
                f.write(f"# {key[:-5]}_t_s: {(us - t0) / 1e6:.6f}\n")
        for key in ("preroll_samples", "truncated", "dropped", "clockstep_us"):
            if key in header:
                f.write(f"# {key}: {header[key]}\n")
        f.write("t_s,t_us,x_g,y_g,z_g\n")
        for t_us, x, y, z in rows:
            f.write(f"{(t_us - t0) / 1e6:.6f},{t_us},{x},{y},{z}\n")

    # Anything other than a clean run is worth saying out loud rather than
    # leaving in a comment line nobody reads.
    warn = []
    if header.get("clockstep_us", 0) > 100:
        warn.append(f"micros() resolves only ~{header['clockstep_us']} us - "
                    "wrong Arduino core, timestamps are ~1 ms granular")
    if header.get("dropped", 0):
        warn.append(f"{header['dropped']} sample(s) recovered by the data-ready watchdog")
    if header.get("truncated", 0):
        warn.append("ring wrapped: less pre-set history than intended "
                    f"({header.get('preroll_samples', '?')} samples)")
    if not go_us:
        warn.append("no 'go' marker in this dump")
    return warn


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: autodetected)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--outdir", default="Data", help="where to write the CSVs")
    args = ap.parse_args()

    import serial

    port = args.port or autodetect_port()
    if not port:
        print("No serial port found - plug in the board.", file=sys.stderr)
        sys.exit(1)
    os.makedirs(args.outdir, exist_ok=True)

    print(f"Opening {port} @ {args.baud} baud...")
    ser = serial.Serial(port, args.baud, timeout=0.2)
    time.sleep(2.0)  # let the board finish its boot/reset before reading
    ser.reset_input_buffer()
    # Only one process can hold the serial port, and this one has to hold it
    # for the whole session to catch a dump the moment it arrives. So typed
    # keys are relayed straight through to the board - a passthrough, not a
    # feature: this script still decides nothing. It is what lets you run the
    # sequence with no button wired yet.
    def relay_stdin():
        for line in sys.stdin:
            cmd = line.strip()
            cmd = "b" if cmd == "" else cmd[0]
            try:
                ser.write(cmd.encode())
            except Exception:                         # port closed on exit
                return
            print(f"[sent] {cmd!r}")

    threading.Thread(target=relay_stdin, daemon=True).start()

    print("Listening. Press the board's button to start a sequence, or press")
    print("Enter here to send 'b' (same thing). 'a'+Enter aborts, 'p'+Enter")
    print("prints one reading. Ctrl-C to quit.\n")

    in_dump = False
    header = {}
    rows = []
    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue

            if line.startswith("DUMP_START"):
                in_dump, header, rows = True, {}, []
                continue

            if not in_dump:
                # Idle preview rows are 4 comma-separated numbers and are pure
                # noise here; everything else is banner or SEQ progress and is
                # exactly what you want to watch during a run.
                if line.count(",") != 3 or line.startswith("SEQ,"):
                    print(f"[board] {line}")
                continue

            if line == "DUMP_END":
                in_dump = False
                if not rows:
                    print("!! dump contained no rows")
                    continue
                stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                path = os.path.join(args.outdir, f"accel_{stamp}.csv")
                warn = write_csv(path, header, rows)
                print(f"\nSaved {len(rows)} rows -> {path}")
                for w in warn:
                    print(f"  !! {w}")
                print()
                continue

            key, _, val = line.partition(",")
            if key in HEADER_KEYS:
                try:
                    header[HEADER_KEYS[key]] = int(val)
                except ValueError:
                    pass
                continue

            parts = line.split(",")
            if len(parts) == 4:
                try:
                    rows.append((int(parts[0]), parts[1], parts[2], parts[3]))
                except ValueError:
                    print(f"!! malformed row skipped: {line!r}")
            else:
                print(f"!! unexpected line inside dump: {line!r}")
    except KeyboardInterrupt:
        print("\nBye.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
