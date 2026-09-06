#!/usr/bin/env python3
"""
Analyse and plot Xbee_RangeTest logs written by Tools/xbee_range_log.py.

Groups rows by their distance_m tag and produces, per distance:
  - packet delivery ratio (from sequence-number continuity)
  - RSSI (mean and worst)
  - round-trip time min / p50 / p95 and jitter          (sender logs)
  - one-way inter-arrival jitter                         (receiver logs)
  - MAC retry count                                      (sender logs)

then prints the max distance that meets the pass criterion (PDR >= 95%) and,
if it falls short of the 200 m target, which antenna/radio upgrade that points
to.

USAGE
    python3 Tools/xbee_range_plot.py                       # newest Data/xbee_range_*.csv
    python3 Tools/xbee_range_plot.py Data/xbee_range_*.csv  # merge several runs
    python3 Tools/xbee_range_plot.py --save out.png

Requires: numpy, matplotlib.
"""

import argparse
import glob
import os
import sys

import numpy as np
import matplotlib.pyplot as plt

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(REPO_ROOT, "Data")

PDR_PASS = 0.95
RANGE_TARGET_M = 200.0


def load(paths):
    """Read one or more CSVs, all of the same role. Returns (role, dict of
    column -> np.array)."""
    role = None
    header = None
    rows = []
    skipped = 0
    for path in paths:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if line.startswith("# role="):
                    r = line.split("=", 1)[1].strip()
                    if role and r != role:
                        sys.exit("mixed roles: %s has %s, expected %s" % (path, r, role))
                    role = r
                elif line.startswith("#") or not line:
                    continue
                elif line.startswith("host_iso"):
                    # Every file carries its own header. Only the first one
                    # defines the columns; the rest have to be skipped, not
                    # appended as data - they have the right field count, so
                    # the length check below lets them through and the float()
                    # conversion then dies on "distance_m".
                    if header is None:
                        header = line.split(",")
                    continue
                elif header is not None:
                    parts = line.split(",")
                    if len(parts) == len(header):
                        rows.append(parts)
                    else:
                        skipped += 1
    if not role or not rows:
        sys.exit("no usable rows found")
    cols = {name: [] for name in header}
    for parts in rows:
        # Guard against any other stray non-numeric line surviving the checks
        # above (a truncated row from an interrupted logger, a merged file with
        # a different column set). Drop it rather than crash the whole run.
        try:
            vals = [float(v) for v in parts[1:]]
        except ValueError:
            skipped += 1
            continue
        cols["host_iso"].append(parts[0])
        for name, val in zip(header[1:], vals):
            cols[name].append(val)
    if skipped:
        print("# skipped %d unparseable row(s)" % skipped)
    if not cols["host_iso"]:
        sys.exit("no usable rows found")
    out = {"host_iso": np.array(cols["host_iso"])}
    for name in header[1:]:
        out[name] = np.array(cols[name], dtype=float)
    return role, out


def by_distance(d):
    return sorted(set(d["distance_m"].tolist()))


def pdr_from_seq(seq):
    if seq.size == 0:
        return float("nan")
    expected = seq.max() - seq.min() + 1
    return seq.size / expected if expected > 0 else float("nan")


def summarise(role, d):
    dists = by_distance(d)
    table = []
    for dist in dists:
        m = d["distance_m"] == dist
        seq = d["seq"][m]
        row = {"distance_m": dist, "rows": int(m.sum()), "pdr": pdr_from_seq(seq)}
        if role == "sender":
            uni = m & (d["unicast"] == 1)
            exp = (seq.max() - seq.min() + 1) if seq.size else 0
            # uplink_pdr is a unicast-only figure: only unicast transmits get a
            # 0x8B delivery status, so the denominator has to be the unicast
            # sequence span too. Using the full span would count the handful of
            # pre-discovery broadcast pings as uplink losses.
            useq = d["seq"][uni]
            uexp = (useq.max() - useq.min() + 1) if useq.size else 0
            row["uplink_pdr"] = (np.sum(d["tx_delivery"][uni] == 0) / uexp) if uexp else float("nan")
            row["rt_pdr"] = (np.sum(d["echo_ok"][m] == 1) / exp) if exp else float("nan")
            ret = d["tx_retries"][uni & (d["tx_retries"] >= 0)]
            row["retries_mean"] = float(ret.mean()) if ret.size else float("nan")
            row["retries_max"] = float(ret.max()) if ret.size else float("nan")
            rtt = d["rtt_us"][m & (d["echo_ok"] == 1) & (d["rtt_us"] >= 0)]
            row["rtt_min"] = float(rtt.min()) if rtt.size else float("nan")
            row["rtt_p50"] = float(np.percentile(rtt, 50)) if rtt.size else float("nan")
            row["rtt_p95"] = float(np.percentile(rtt, 95)) if rtt.size else float("nan")
            row["rtt_jitter"] = row["rtt_p95"] - row["rtt_p50"]
            rr = d["rssi_remote_dbm"][m & (d["rssi_remote_dbm"] != 0)]
            row["rssi_mean"] = float(rr.mean()) if rr.size else float("nan")
            # negative dBm: the weakest signal is the most negative -> min()
            row["rssi_worst"] = float(rr.min()) if rr.size else float("nan")
        else:
            rssi = d["rssi_dbm"][m & (d["rssi_dbm"] != 0)]
            row["rssi_mean"] = float(rssi.mean()) if rssi.size else float("nan")
            row["rssi_worst"] = float(rssi.min()) if rssi.size else float("nan")
            dt = d["dt_us"][m & (d["dt_us"] > 0)]
            row["dt_p50"] = float(np.percentile(dt, 50)) if dt.size else float("nan")
            row["dt_jitter"] = (float(np.percentile(dt, 95) - np.percentile(dt, 50))
                                if dt.size else float("nan"))
        table.append(row)
    return table


def verdict(role, table):
    key = "uplink_pdr" if role == "sender" else "pdr"
    good = [r["distance_m"] for r in table if r.get(key, r["pdr"]) >= PDR_PASS]
    max_good = max(good) if good else 0.0
    print("\n=== verdict ===")
    print("pass criterion: %s >= %.0f%%" % (key, PDR_PASS * 100))
    print("max distance meeting it: %.0f m" % max_good)
    if max_good >= RANGE_TARGET_M:
        print("-> clears the %.0f m target with the stock PCB-antenna S2C." % RANGE_TARGET_M)
    else:
        print("-> does NOT clear %.0f m with the stock PCB-antenna S2C." % RANGE_TARGET_M)
        print("   This is a useful result, not a blocked task. Recommended next step:")
        if max_good >= 0.5 * RANGE_TARGET_M:
            print("   try the wire-whip / U.FL antenna variant (e.g. XB24CZ7WIT) - the")
            print("   PCB trace is the weakest antenna in the S2C line.")
        else:
            print("   move to XBee-PRO S2C (+18 dBm) and/or an external antenna; the")
            print("   gap is too large for an antenna swap alone.")
    # jitter trend note
    jkey = "rtt_jitter" if role == "sender" else "dt_jitter"
    js = [(r["distance_m"], r[jkey]) for r in table if not np.isnan(r.get(jkey, float("nan")))]
    if len(js) >= 2 and js[-1][1] > 2 * js[0][1]:
        print("   note: %s roughly %.1fx higher at %.0f m than at %.0f m - rising"
              % (jkey, js[-1][1] / js[0][1], js[-1][0], js[0][0]))
        print("   jitter is the leading indicator for clock-sync degradation.")


def print_table(role, table):
    print("\n=== per-distance summary ===")
    if role == "sender":
        print("%6s %6s %8s %8s %8s %9s %9s %9s %8s %8s"
              % ("dist", "rows", "ul_pdr", "rt_pdr", "ret~", "rtt_min", "rtt_p50", "rtt_p95", "rssi", "worst"))
        for r in table:
            print("%6.0f %6d %7.1f%% %7.1f%% %8.2f %9.0f %9.0f %9.0f %8.1f %8.0f"
                  % (r["distance_m"], r["rows"], 100 * r["uplink_pdr"], 100 * r["rt_pdr"],
                     r["retries_mean"], r["rtt_min"], r["rtt_p50"], r["rtt_p95"],
                     r["rssi_mean"], r["rssi_worst"]))
    else:
        print("%6s %6s %8s %8s %8s %9s %10s"
              % ("dist", "rows", "pdr", "rssi", "worst", "dt_p50", "dt_jitter"))
        for r in table:
            print("%6.0f %6d %7.1f%% %8.1f %8.0f %9.0f %10.0f"
                  % (r["distance_m"], r["rows"], 100 * r["pdr"], r["rssi_mean"],
                     r["rssi_worst"], r["dt_p50"], r["dt_jitter"]))


def plot(role, table, save):
    dist = [r["distance_m"] for r in table]
    fig, axs = plt.subplots(3, 1, figsize=(10, 10), sharex=True)

    if role == "sender":
        axs[0].plot(dist, [100 * r["uplink_pdr"] for r in table], "o-", label="uplink PDR")
        axs[0].plot(dist, [100 * r["rt_pdr"] for r in table], "s--", label="round-trip PDR")
    else:
        axs[0].plot(dist, [100 * r["pdr"] for r in table], "o-", label="PDR")
    axs[0].axhline(PDR_PASS * 100, color="r", ls=":", lw=1, label="%.0f%% pass" % (PDR_PASS * 100))
    axs[0].set_ylabel("delivery [%]")
    axs[0].set_ylim(0, 105)
    axs[0].grid(True, ls="--", alpha=0.5)
    axs[0].legend(fontsize=8)

    axs[1].plot(dist, [r["rssi_mean"] for r in table], "o-", label="RSSI mean")
    axs[1].plot(dist, [r["rssi_worst"] for r in table], "v--", label="RSSI worst")
    axs[1].set_ylabel("RSSI [dBm]")
    axs[1].grid(True, ls="--", alpha=0.5)
    axs[1].legend(fontsize=8)

    if role == "sender":
        axs[2].plot(dist, [r["rtt_min"] for r in table], "o-", label="RTT min")
        axs[2].plot(dist, [r["rtt_p50"] for r in table], "s-", label="RTT p50")
        axs[2].plot(dist, [r["rtt_p95"] for r in table], "^--", label="RTT p95")
        axs[2].set_ylabel("RTT [us]")
    else:
        axs[2].plot(dist, [r["dt_p50"] for r in table], "s-", label="inter-arrival p50")
        axs[2].plot(dist, [r["dt_jitter"] for r in table], "^--", label="jitter (p95-p50)")
        axs[2].set_ylabel("inter-arrival [us]")
    axs[2].set_xlabel("distance [m]")
    axs[2].grid(True, ls="--", alpha=0.5)
    axs[2].legend(fontsize=8)

    fig.suptitle("XBee S2C range test - %s log" % role)
    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=140)
        print("\nsaved %s" % save)
    else:
        plt.show()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="*", help="log files (default: newest Data/xbee_range_*.csv)")
    ap.add_argument("--save", help="write the figure here instead of showing it")
    args = ap.parse_args(argv)

    paths = args.csv
    if not paths:
        found = sorted(glob.glob(os.path.join(DATA_DIR, "xbee_range_*.csv")))
        if not found:
            sys.exit("no Data/xbee_range_*.csv found - run Tools/xbee_range_log.py first")
        paths = [found[-1]]

    role, d = load(paths)
    table = summarise(role, d)
    print_table(role, table)
    verdict(role, table)
    plot(role, table, args.save)
    return 0


if __name__ == "__main__":
    sys.exit(main())
