#!/usr/bin/env python3
"""Per-station table from one EspNowLrTest walk: the A file and the B file.

    python3 analyse.py measurements/20260929/125103_A.csv measurements/20260929/125113_B.csv

Stations come from the M lines typed on laptop B. The two files share no
clock; they are joined through the sequence number, which is deterministic:
A sends seq n at boot + n * 50 ms, in mode (n // 100) % 3. So the expected
packets of a station are known even when none of them arrived, and delivery
is counted against what A actually sent, not against what B happened to see.

    one-way   A -> B, from B's D lines. The direction the t0 travels.
    round     A -> B -> A, from A's E lines.
    t0        per 5 s slot inside the station: tries until acked, or lost.
"""
import statistics as st
import sys

PKT_S = 0.05
PER_SLOT = 100
MODES = ["LR250", "LR500", "11b1M"]


def rows(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        f = line.rstrip("\r\n").split(",")
        if len(f) < 2:
            continue
        try:
            yield float(f[0]), f[1], f[2:]
        except ValueError:
            continue


def mode_of(seq):
    return (seq // PER_SLOT) % len(MODES)


def main(a_path, b_path):
    D = {}                      # seq -> (host_s, rssi) at B
    marks = []
    for t, k, f in rows(b_path):
        if k == "D":
            D[int(f[1])] = (t, int(f[3]))
        elif k == "M":
            marks.append((t, f[0]))
        elif k == "I" and D:
            print(f"!! B rebooted at {t:.1f} s")

    E, K, X = {}, {}, {}
    boots = 0
    for t, k, f in rows(a_path):
        if k == "E":
            E[int(f[1])] = (t, int(f[6]))          # rssi of B's echo, seen at A
        elif k == "K":
            K[int(f[1])] = int(f[3])
        elif k == "X":
            X[int(f[1])] = int(f[3])
        elif k == "I":
            boots += 1
    if boots > 2:   # the board prints I twice at boot (setup + the logger's "?")
        print("!! A may have rebooted during the run: seq restarts, check by hand")

    # B host time of seq n = off + n * 50 ms; the median shrugs off USB jitter
    off = st.median(t - s * PKT_S for s, (t, _) in D.items())

    stations = []
    for (t0, d), (t1, e) in zip(marks, marks[1:]):
        if d != "-" and e == "-":
            stations.append((d, t0, t1))

    print(f"{'dist':>5} {'mode':>6} {'one-way':>8} {'round':>7} {'RSSI A>B':>9} {'RSSI B>A':>9}  t0 tries per slot")
    for d, t0, t1 in stations:
        lo = int((t0 - off) / PKT_S) + 1
        hi = int((t1 - off) / PKT_S) - 1
        for m, name in enumerate(MODES):
            seqs = [s for s in range(lo, hi + 1) if mode_of(s) == m]
            got = [s for s in seqs if s in D]
            rt = [s for s in seqs if s in E]
            r_ab = st.median(D[s][1] for s in got) if got else None
            r_ba = st.median(E[s][1] for s in rt) if rt else None
            slots = sorted({s // PER_SLOT for s in seqs
                            if s % PER_SLOT == 0})           # slots that start inside
            tries = []
            for sl in slots:
                tries.append(str(K[sl]) if sl in K else ("LOST" if sl in X else "?"))
            fmt = lambda r: f"{r:6.0f} dBm" if r is not None else "        --"
            print(f"{d:>5} {name:>6} {100 * len(got) / len(seqs):7.1f}% "
                  f"{100 * len(rt) / len(seqs):6.1f}% {fmt(r_ab)} {fmt(r_ba)}  {' '.join(tries)}")
        print()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
