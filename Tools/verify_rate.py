#!/usr/bin/env python3
"""
verify_rate.py - CLI-only check of AccelStream.ino's real recorded rate.

No GUI, no matplotlib: connects, arms a full-rate recording ('r'), waits a
fixed window, stops it ('S'), reads the DUMP_START/.../DUMP_END burst, and
reports whether the achieved sample rate and data integrity are what the
firmware promises (833 Hz, no dropped/corrupted rows, DROPPED=0).

Usage:
    python3 Tools/verify_rate.py                  # autodetect port, 3 s
    python3 Tools/verify_rate.py --seconds 5
    python3 Tools/verify_rate.py --port /dev/cu.usbmodem1101

Only one program can hold the serial port at a time - close the Arduino
Serial Monitor (or accel_live.py) before running this.
"""

import argparse
import glob
import math
import statistics
import sys
import time


def autodetect_port():
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            name = p.device or ""
            if "usbmodem" in name or "usbserial" in name or "ACM" in name:
                return name
    except Exception:
        pass
    for pattern in ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/ttyACM*"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: autodetected)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=3.0,
                     help="how long to record before stopping")
    ap.add_argument("--target-hz", type=float, default=833.0,
                     help="expected firmware ODR, for the pass/fail check")
    ap.add_argument("--range", type=float, default=16.0,
                     help="full-scale range in g; must match ACCEL_RANGE_G in the firmware")
    args = ap.parse_args()

    import serial  # imported here so --help works without pyserial installed

    port = args.port or autodetect_port()
    if not port:
        print("No serial port found - plug in the board.", file=sys.stderr)
        sys.exit(1)

    print(f"Opening {port} @ {args.baud} baud...")
    ser = serial.Serial(port, args.baud, timeout=0.2)
    time.sleep(2.0)  # let the board finish its boot/reset before we touch it
    ser.reset_input_buffer()

    # Drain and print whatever idle-preview/banner text shows up first, so
    # you can see "IMU OK - accel 833 Hz (data-ready on INT1)" go by -
    # confirms the ODR register was accepted, not silently downgraded to the
    # library's 104 Hz default.
    print("--- board output (1s warm-up) ---")
    t_end = time.time() + 1.0
    while time.time() < t_end:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print("[board]", line)
    print("----------------------------------")

    print(f"Arming full-rate recording for {args.seconds:.1f}s...")
    ser.reset_input_buffer()
    ser.write(b"r")
    time.sleep(args.seconds)
    ser.write(b"S")  # 'S' stops and dumps; lowercase 's' is the "set" marker

    rows = []
    expected = None
    dropped = None
    started = False
    deadline = time.time() + 20.0  # generous: dump of a few seconds' data can take a few seconds to transmit
    while time.time() < deadline:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue
        if line.startswith("DUMP_START"):
            started = True
            parts = line.split(",")
            expected = int(parts[1]) if len(parts) > 1 and parts[1].strip().isdigit() else None
            print(f"[board] {line}")
            continue
        if line == "DUMP_END":
            print("[board] DUMP_END")
            break
        if line.startswith(("ON,", "SET,", "GO,")):
            # Marker header lines - always present, 0 when unused. Not junk.
            print(f"[board] {line}")
            continue
        if line.startswith("DROPPED,"):
            val = line.split(",", 1)[1].strip()
            dropped = int(val) if val.isdigit() else None
            print(f"[board] {line}")
            continue
        if started:
            parts = line.split(",")
            if len(parts) == 4:
                try:
                    rows.append((int(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])))
                except ValueError:
                    print(f"!! malformed row (skipped): {line!r}")
            else:
                print(f"!! unexpected line during dump (skipped): {line!r}")
        else:
            print("[board]", line)
    ser.close()

    n = len(rows)
    print()
    print(f"Received {n} rows" + (f" (board says it buffered {expected})" if expected is not None else ""))

    ok = True
    if dropped is None:
        print("!! board reported no DROPPED count - firmware older than v3?")
    elif dropped:
        print(f"!! board recovered {dropped} sample(s) via the data-ready watchdog - "
              f"their timestamps are only good to ~3 sample periods, not clean data")
        ok = False
    else:
        print("data-ready watchdog: 0 recovered samples (clean)")

    if expected is not None and n != expected:
        print(f"!! ROW COUNT MISMATCH: received {n}, expected {expected} - some rows were lost in transit")
        ok = False

    if n < 10:
        print("Not enough rows received to evaluate rate/integrity.")
        sys.exit(1)

    t = [r[0] for r in rows]
    dt = [t[i + 1] - t[i] for i in range(len(t) - 1)]
    mean_dt = statistics.mean(dt)
    median_dt = statistics.median(dt)
    stdev_dt = statistics.stdev(dt)
    min_dt, max_dt = min(dt), max(dt)
    rate = 1e6 / median_dt

    print(f"\ndt (us):  mean={mean_dt:.1f}  median={median_dt:.1f}  std={stdev_dt:.1f}  "
          f"min={min_dt:.1f}  max={max_dt:.1f}")
    print(f"=> effective rate: {rate:.1f} Hz  (target ~{args.target_hz:.0f} Hz)")

    rate_ok = abs(rate - args.target_hz) / args.target_hz < 0.05  # within 5%
    if not rate_ok:
        ok = False

    non_positive = sum(1 for d in dt if d <= 0)
    if non_positive:
        print(f"!! {non_positive} non-positive/duplicate timestamps (t_us not strictly increasing)")
        ok = False

    gap_thresh = 3 * median_dt
    gaps = [d for d in dt if d > gap_thresh]
    print(f"gaps (> 3x median dt = {gap_thresh:.0f}us): {len(gaps)}"
          + (f"  worst={max(gaps):.0f}us" if gaps else ""))
    if gaps:
        ok = False

    # Quick sanity on the values themselves, not just timing: resting
    # magnitude should sit near 1 g if the board was still at some point,
    # and nothing should be pinned at the +/-range clip rails.
    mags = [math.sqrt(x * x + y * y + z * z) for _, x, y, z in rows]
    xs = [r[1] for r in rows]
    ys = [r[2] for r in rows]
    zs = [r[3] for r in rows]
    clipped = sum(1 for v in xs + ys + zs if abs(v) >= 0.98 * args.range)
    print(f"\n|a|: min={min(mags):.3f}g  max={max(mags):.3f}g  mean={statistics.mean(mags):.3f}g")
    print(f"x range [{min(xs):.3f}, {max(xs):.3f}]g  y range [{min(ys):.3f}, {max(ys):.3f}]g  "
          f"z range [{min(zs):.3f}, {max(zs):.3f}]g")
    if clipped:
        print(f"!! {clipped} samples near the +/-{args.range:.0f}g clip rail - real event may be clipped, raise the range")
        ok = False

    print("\n" + ("PASS" if ok else "FAIL"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
