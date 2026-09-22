#!/usr/bin/env python3
"""
sweep_threshold.py - how much the reported reaction time moves when the
detection threshold moves, with the AIC picker and without it.

This is the paper's primary result, and until now the numbers behind it lived
in session scratch. Everything here comes out of one command: the summary
table, the captures the picker cannot save, and the figure.

For every capture in Data/block_starts_*, the detector is run at each
threshold in the sweep, once with the AIC picker and once without, and the
FIRST event in the judged window - the one whose reaction time is the reported
one - is recorded. The shift is measured against the lowest threshold in the
sweep, so both curves start at zero and the threshold-only one walks away from
it.

Where the header carries the board's own arm_t_s (14/09 and 15/09) it is used,
so the arming gate stays fixed and the detection threshold is the only thing
moving.

Note what is being demonstrated: this is the offline detector. The firmware
agrees with it to the microsecond on 49 of the 56 captures that carry a board
verdict; see HANDOFF.md, 2026-09-15.

Usage:
    python3 Arduino/AlgorithmRealTime/Python_Tools/sweep_threshold.py
    python3 Arduino/AlgorithmRealTime/Python_Tools/sweep_threshold.py --step 1
    python3 Arduino/AlgorithmRealTime/Python_Tools/sweep_threshold.py --no-figure
"""

import argparse
import glob
import os
import sys

import numpy as np

import start_detector as sd

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DATA = os.path.join(REPO, "Data")
FIG = os.path.join(REPO, "Docs", "threshold_sweep.pdf")

# One sample at the measured 865.8 Hz. The picker cannot resolve better than
# this, so it is the line "unchanged" is judged against.
SAMPLE_MS = 1.155


def sweep(paths, thresholds):
    """rows: one per capture, each with the reaction time at every threshold,
    with and without the picker. A capture with no event at some threshold is
    returned with None there and handled by the caller."""
    rows = []
    for p in paths:
        rec = dict(path=p, aic={}, thr={})
        for mg in thresholds:
            for use_aic in (True, False):
                r = sd.analyse(p, floor_mg=float(mg), use_aic=use_aic)
                ev = r["events"]
                (rec["aic"] if use_aic else rec["thr"])[mg] = (
                    ev[0]["reaction_ms"] if ev else None)
        rows.append(rec)
        print(f"  {os.path.basename(p)}", end="\r", file=sys.stderr)
    print(" " * 60, end="\r", file=sys.stderr)
    return rows


def shifts(rec, key, thresholds):
    """Reaction time at each threshold minus the reaction time at the lowest
    one. None if the capture produced no event anywhere in the sweep."""
    v = [rec[key][mg] for mg in thresholds]
    if any(x is None for x in v):
        return None
    return np.array(v) - v[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--step", type=float, default=2.5,
                    help="threshold step in mg (default 2.5)")
    ap.add_argument("--from-mg", type=float, default=10.0)
    ap.add_argument("--to-mg", type=float, default=50.0)
    ap.add_argument("--no-figure", action="store_true")
    ap.add_argument("--out", default=FIG)
    args = ap.parse_args()

    thresholds = np.arange(args.from_mg, args.to_mg + 1e-9, args.step)
    paths = sorted(sum((glob.glob(os.path.join(DATA, d, "*.csv"))
                        for d in sorted(os.listdir(DATA))
                        if d.startswith("block_starts_")), []))
    print(f"{len(paths)} captures, {len(thresholds)} thresholds "
          f"({args.from_mg:g}-{args.to_mg:g} mg, step {args.step:g})")

    rows = sweep(paths, thresholds)

    kept, dropped = [], []
    for rec in rows:
        if shifts(rec, "aic", thresholds) is None:
            dropped.append(rec)
        else:
            kept.append(rec)

    # The captures the picker cannot save are event-SELECTION changes, not
    # onset changes: at a higher threshold the earlier, smaller event is never
    # confirmed, so a later one becomes the first. They are not a failure of
    # the picker - it never sees the event that was missed - but they are not
    # its result either, so they are reported apart rather than averaged in.
    #
    # The split is not a judgement call: the measured shifts are 0, a scatter
    # at one sample (1.14-1.16 ms), then nothing at all until 18 ms. FLIP_MS
    # sits in that gap, and the classification is the same anywhere inside it.
    FLIP_MS = 10.0
    stable = [r for r in kept
              if np.abs(shifts(r, "aic", thresholds)).max() < FLIP_MS]
    flipped = [r for r in kept if r not in stable]

    A = np.array([shifts(r, "aic", thresholds) for r in stable])
    T = np.array([shifts(r, "thr", thresholds) for r in stable])

    print(f"\n{len(paths)} captures, {len(kept)} with an event at every "
          f"threshold, {len(dropped)} with none:")
    for r in dropped:
        print(f"  {os.path.basename(r['path'])}")

    def line(name, M):
        end = M[:, -1]
        allv = np.abs(M).max(axis=1)
        return (f"{name:<14} median {np.median(allv):6.2f}  "
                f"p90 {np.percentile(allv, 90):6.2f}  worst {allv.max():6.2f}  "
                f"| at {args.to_mg:g} mg: median {np.median(end):6.2f}")

    print(f"\nShift over the sweep, {len(stable)} captures where the same "
          f"event stays first:")
    print(" " + line("AIC picker", A))
    print(" " + line("threshold only", T))
    n_ident = sum(1 for r in stable
                  if np.abs(shifts(r, "aic", thresholds)).max() < 1e-9)
    print(f" identical to the sample: AIC {n_ident}/{len(kept)}, "
          f"within one sample ({SAMPLE_MS:.2f} ms): {len(stable)}/{len(kept)}")
    print(f" (identical is grid-dependent: a finer sweep gives more chances to "
          f"land on the next sample, so it falls as --step falls; within one "
          f"sample does not move.)")

    if flipped:
        print(f"\n{len(flipped)} captures where the threshold changes WHICH "
              f"event is first (reaction time in ms):")
        head = "  ".join(f"{mg:>8.0f}" for mg in thresholds[::4])
        print(f"  {'capture':<28}{head}")
        for r in flipped:
            name = os.path.join(os.path.basename(os.path.dirname(r["path"])),
                                os.path.basename(r["path"]))[-28:]
            vals = "  ".join(f"{r['aic'][mg]:8.1f}" for mg in thresholds[::4])
            print(f"  {name:<28}{vals}")

    if args.no_figure:
        return

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(3.4, 2.3))
    # Every band and reference is named on the figure itself, not only in the
    # caption: a reviewer reads the figure detached from the text, and an
    # unexplained shaded area is the first thing they ask about.
    for M, colour, label in (
            (T, "#c44e52", "threshold only"),
            (A, "#2f5d9e", "STA/LTA + AIC picker")):
        med = np.median(M, axis=0)
        lo, hi = np.percentile(M, [25, 75], axis=0)
        flat = np.allclose(lo, 0) and np.allclose(hi, 0)
        ax.fill_between(thresholds, lo, hi, color=colour, alpha=0.18, lw=0)
        ax.plot(thresholds, med, color=colour, lw=1.6,
                label=label + (", IQR 0" if flat else ""))
    ax.axhline(0, color="0.6", lw=0.6, zorder=0)
    ax.axhspan(-SAMPLE_MS, SAMPLE_MS, color="0.85", zorder=0, lw=0)
    ax.annotate(f"$\\pm$1 sample ({SAMPLE_MS:.1f} ms)", xy=(thresholds[-1], 0),
                xytext=(-2, -SAMPLE_MS - 1), textcoords="offset points",
                ha="right", va="top", fontsize=6, color="0.35")
    ax.set_xlabel("detection threshold (mg)")
    ax.set_ylabel(f"shift of reported reaction\n"
                  f"time vs. {thresholds[0]:g} mg (ms)")
    ax.set_xlim(thresholds[0], thresholds[-1])
    leg = ax.legend(frameon=False, fontsize=7, loc="upper left",
                    title="median, interquartile range", alignment="left")
    leg.get_title().set_fontsize(6.5)
    leg.get_title().set_color("0.35")
    ax.tick_params(labelsize=7)
    ax.xaxis.label.set_size(8)
    ax.yaxis.label.set_size(8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout(pad=0.2)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    fig.savefig(args.out)
    print(f"\nfigure -> {os.path.relpath(args.out, REPO)}  "
          f"(median and interquartile range over {len(stable)} captures; "
          f"grey band is one sample)")


if __name__ == "__main__":
    main()
