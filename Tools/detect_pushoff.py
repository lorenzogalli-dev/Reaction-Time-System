#!/usr/bin/env python3
"""
detect_pushoff.py - offline push-off / movement-onset detector, CSV in, no
hardware needed.

STARTING POINT for tuning, not a validated detector - the constants below
are the same ballpark numbers discussed while designing this (STA ~15ms,
LTA ~0.8s, ratio ~6x, floor ~20mg), never yet checked against a real
on-block push-off. Tune them against real captures once you have some.

Runs the same causal, sample-by-sample logic that's meant to eventually run
on the finish/start unit's firmware: PushOffDetector.update() only ever
sees the current sample plus a short rolling history (for backdating and
noise-confirmation) - never the whole recording, never a future sample.
That's deliberate: whatever tuning works here should behave identically
once ported to C++ on the real device. The one caveat is real-time
latency, not accuracy - see the confirm_ms note below.

Meant so the three of you can develop/tune the algorithm from just
recorded CSVs, without needing to be at the blocks or own the hardware.

Usage:
    python3 Tools/detect_pushoff.py Data/accel_20260907_142712.csv
    python3 Tools/detect_pushoff.py "Data/*.csv" --plot
    python3 Tools/detect_pushoff.py Data/accel_x.csv --sta-ms 15 --lta-ms 800 --ratio 6 --floor-mg 20
"""

import argparse
import glob
import math
import os

import numpy as np
import pandas as pd


class PushOffDetector:
    """Causal, one-sample-at-a-time push-off detector.

    Feed samples in order via update(t_s, x, y, z). Internally tracks:
      - a slow-adapting baseline (x0, y0, z0) - the "resting position" -
        frozen the instant a candidate event starts, so real motion can't
        drag its own reference out from under it.
      - horiz = |(the two non-gravity axes) - baseline|. Whichever axis is
        dominated by gravity at rest is excluded, same reasoning as the
        earlier sensor evaluation (a purely lateral event can be
        under-reported 4x by the full 3-axis vector magnitude) - but which
        physical axis that is depends on how the board is mounted/held,
        confirmed by two real captures on 2026-09-07 where gravity sat on Y,
        not Z. Auto-detected from the first ~20ms of samples by default
        (whichever axis averages closest to +/-1g); pass gravity_axis
        explicitly ('x'/'y'/'z') to override if a capture's resting window
        is too short or noisy for that to be reliable.
      - STA/LTA: fast vs. slow moving average of horiz^2. A ratio spike
        means "much more energy right now than the recent background",
        adapting to whatever baseline jitter is present instead of relying
        on one fixed absolute number - this is what would make it robust to
        pre-start fidgeting, in principle. Unverified against real fidgeting
        - that's exactly what a real block capture would tell you.

    On confirmation, the reported event time is backdated to the first raw
    sample that crossed `floor_g` - not the (later) instant the ratio
    crossed threshold or the confirmation finished. The confirm window
    itself costs real decision latency (confirm_ms) if this were ever used
    to make a live decision on-device (e.g. lighting a "go detected" LED) -
    it does NOT affect the reported timestamp's accuracy, only how soon the
    device could act on it.
    """

    def __init__(self, odr_hz, sta_ms=15.0, lta_ms=800.0, ratio_threshold=6.0,
                 floor_g=0.020, confirm_ms=15.0, confirm_floor_g=0.010,
                 baseline_alpha=0.01, warmup_s=None, gravity_axis="auto",
                 earliest_trigger_t=None):
        self.odr_hz = odr_hz
        # Baseline/STA/LTA still update on every sample regardless (so
        # warmup isn't disturbed) - this only suppresses *triggering* on
        # anything before this time. None = no restriction, detect the
        # first qualifying movement anywhere in the file (which is what you
        # want for real false-start detection; a real system needs to catch
        # movement before "go" too, not just after it).
        self.earliest_trigger_t = earliest_trigger_t
        self.gravity_axis = gravity_axis  # 'x', 'y', 'z', or 'auto'
        # ~20ms of samples, averaged, to pick the gravity axis and seed the
        # baseline - more robust than trusting a single first sample.
        self._axis_detect_n = max(5, int(round(0.020 * odr_hz)))
        self._axis_detect_buf = []
        dt = 1.0 / odr_hz
        # EMA smoothing per sample: alpha = dt / time_constant. Uses the
        # nominal dt (1/odr_hz), not each sample's real dt - a fine
        # simplification given how tight the measured jitter is on this
        # firmware (std ~11us on a ~1ms period); revisit if that changes.
        self.alpha_sta = dt / (sta_ms / 1000.0)
        self.alpha_lta = dt / (lta_ms / 1000.0)
        self.alpha_baseline = baseline_alpha
        self.ratio_threshold = ratio_threshold
        self.floor_g = floor_g
        self.confirm_n = max(1, int(round((confirm_ms / 1000.0) * odr_hz)))
        self.confirm_floor_g = confirm_floor_g
        # LTA starts with no real background noise "seen" yet, so STA/LTA
        # is meaningless (spuriously huge) until it's had at least one full
        # LTA time constant of real samples to converge on. Without this
        # gate, the very first few noisy samples of any recording look like
        # an infinite-ratio "event" - caught by testing against a real
        # capture, not a hypothetical.
        self.warmup_n = max(1, int(round((warmup_s if warmup_s is not None else lta_ms / 1000.0) * odr_hz)))

        self.bx = None
        self.by = None
        self.bz = None
        self.sta = 0.0
        self.lta = 1e-6  # avoid div-by-zero before any real motion is seen
        self._n = 0
        self._hist_t = []
        self._hist_horiz = []
        self._hist_maxlen = self.confirm_n + 5

        self.armed = True          # baseline still tracking, no candidate yet
        self.candidate_seen = False
        self.confirm_count = 0
        self.triggered = False
        self.event_t = None
        self.event_horiz_g = None

    def update(self, t_s, x, y, z):
        """Feed one new sample, in time order. Returns the event time (s)
        the moment an event is confirmed, else None."""
        if self.triggered:
            return None

        if self.bx is None:
            # Still averaging the opening window to pick the gravity axis
            # (if "auto") and seed the baseline - see the class docstring.
            self._axis_detect_buf.append((x, y, z))
            if len(self._axis_detect_buf) < self._axis_detect_n:
                return None
            xs, ys, zs = zip(*self._axis_detect_buf)
            self.bx, self.by, self.bz = sum(xs) / len(xs), sum(ys) / len(ys), sum(zs) / len(zs)
            if self.gravity_axis == "auto":
                means = {"x": self.bx, "y": self.by, "z": self.bz}
                self.gravity_axis = max(means, key=lambda a: abs(means[a]))
            self._axis_detect_buf = None
            return None
        elif self.armed:
            self.bx += self.alpha_baseline * (x - self.bx)
            self.by += self.alpha_baseline * (y - self.by)
            self.bz += self.alpha_baseline * (z - self.bz)

        deltas = {"x": x - self.bx, "y": y - self.by, "z": z - self.bz}
        del deltas[self.gravity_axis]
        horiz = math.hypot(*deltas.values())
        self.sta += self.alpha_sta * (horiz * horiz - self.sta)
        self.lta += self.alpha_lta * (horiz * horiz - self.lta)

        self._hist_t.append(t_s)
        self._hist_horiz.append(horiz)
        if len(self._hist_t) > self._hist_maxlen:
            self._hist_t.pop(0)
            self._hist_horiz.pop(0)

        self._n += 1
        ratio = self.sta / self.lta if self.lta > 0 else 0.0

        if not self.candidate_seen:
            too_early = self.earliest_trigger_t is not None and t_s < self.earliest_trigger_t
            if (not too_early and self._n > self.warmup_n
                    and horiz > self.floor_g and ratio > self.ratio_threshold):
                self.armed = False  # freeze baseline
                self.candidate_seen = True
                self.confirm_count = 1
            return None

        # A candidate is open: keep counting consecutive support.
        if horiz > self.confirm_floor_g:
            self.confirm_count += 1
        else:
            # Dropped back below the confirm floor before enough
            # consecutive support - treat as noise, reopen for a new one.
            self.candidate_seen = False
            self.confirm_count = 0
            self.armed = True
            return None

        if self.confirm_count >= self.confirm_n:
            # Confirmed - backdate to the first raw sample (oldest to
            # newest in the rolling window) that crossed floor_g.
            for t_i, h_i in zip(self._hist_t, self._hist_horiz):
                if h_i > self.floor_g:
                    self.event_t, self.event_horiz_g = t_i, h_i
                    break
            else:
                self.event_t, self.event_horiz_g = self._hist_t[-1], self._hist_horiz[-1]
            self.triggered = True
            return self.event_t
        return None


def read_go_t_s(path):
    """Pull the '# go_t_s: <value>' comment line accel_live.py writes when a
    go-marker button press was captured during the recording, if present.
    accel_live.py appends it *after* the data rows (it only knows the
    marker once the on-device buffer dump finishes), so this has to scan
    the whole file, not just the leading comment block."""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("# go_t_s:"):
                try:
                    return float(line.split(":", 1)[1].strip())
                except ValueError:
                    return None
    return None


def run_csv(path, odr_hz, **detector_kwargs):
    df = pd.read_csv(path, comment="#")
    t = df["t_s"].values if "t_s" in df.columns else df["t_us"].values / 1e6
    x, y, z = df["x_g"].values, df["y_g"].values, df["z_g"].values

    det = PushOffDetector(odr_hz=odr_hz, **detector_kwargs)
    for i in range(len(t)):
        det.update(t[i], x[i], y[i], z[i])
        if det.triggered:
            break
    return det, (t, x, y, z)


def _save_plot(path, t, x, y, z, event_t, go_t_s=None, zoom_ms=120.0,
                gravity_axis=None, baseline=None, floor_g=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    mag = np.sqrt(x ** 2 + y ** 2 + z ** 2)
    # The plain 3-axis |a| above is NOT what the detector decides on - it's
    # dominated by a ~1g gravity offset, so a real 20-60mg onset looks
    # visually flat on it (confirmed against a real capture: |a| crept from
    # 1.00g to 0.93g, imperceptible on a 0-4g axis, while horiz over the
    # same samples was already rising cleanly through the floor). horiz -
    # gravity axis excluded, baseline subtracted - is the actual signal the
    # threshold/backdating logic looks at, so that's what has to be plotted
    # to verify a placement, not |a|.
    horiz_full = None
    if gravity_axis is not None and baseline is not None:
        bx, by, bz = baseline
        deltas_full = {"x": x - bx, "y": y - by, "z": z - bz}
        del deltas_full[gravity_axis]
        d1, d2 = deltas_full.values()
        horiz_full = np.hypot(d1, d2)

    zoom_center = event_t if event_t is not None else go_t_s
    n_rows = (6 if horiz_full is not None else 4) if zoom_center is not None else (3 if horiz_full is not None else 2)
    fig, axes = plt.subplots(n_rows, 1, figsize=(12, n_rows * 2.75))
    full_rows = axes[:3] if horiz_full is not None else axes[:2]
    a1, a2 = full_rows[0], full_rows[1]
    a1.sharex(a2)

    a1.plot(t, x, "r-", lw=0.7, label="X")
    a1.plot(t, y, "g-", lw=0.7, label="Y")
    a1.plot(t, z, "b-", lw=0.7, label="Z")
    if go_t_s is not None:
        a1.axvline(go_t_s, color="0.4", ls=":", lw=1.4, label="go marker")
    if event_t is not None:
        a1.axvline(event_t, color="k", ls="--", lw=1.2, label="detected event")
    a1.set_ylabel("Accel [g]")
    a1.legend(loc="upper right", fontsize=8, ncol=4)
    a1.grid(True, ls="--", alpha=0.5)

    a2.plot(t, mag, "m-", lw=0.9, label="|a| (NOT the decision signal - see below)")
    if go_t_s is not None:
        a2.axvline(go_t_s, color="0.4", ls=":", lw=1.4)
    if event_t is not None:
        a2.axvline(event_t, color="k", ls="--", lw=1.2)
    a2.set_xlabel("Time [s] (full recording)")
    a2.set_ylabel("Magnitude [g]")
    a2.grid(True, ls="--", alpha=0.5)
    a2.legend(loc="upper right", fontsize=8)

    if horiz_full is not None:
        a2b = full_rows[2]
        a2b.sharex(a2)
        a2b.plot(t, horiz_full * 1000, color="darkorange", lw=0.9, label="horiz (mg) - actual decision signal")
        if floor_g is not None:
            a2b.axhline(floor_g * 1000, color="0.5", ls="-.", lw=1.0, label=f"floor ({floor_g*1000:.0f}mg)")
        if go_t_s is not None:
            a2b.axvline(go_t_s, color="0.4", ls=":", lw=1.4)
        if event_t is not None:
            a2b.axvline(event_t, color="k", ls="--", lw=1.2)
        a2b.set_xlabel("Time [s] (full recording)")
        a2b.set_ylabel("horiz [mg]")
        a2b.grid(True, ls="--", alpha=0.5)
        a2b.legend(loc="upper right", fontsize=8)

    if zoom_center is not None:
        # Zoomed-in set, individual samples marked (not just a line) so you
        # can see exactly which sample the marker landed on - a static
        # full-recording PNG doesn't have enough pixels per sample for that.
        zoom_rows = axes[3:] if horiz_full is not None else axes[2:]
        a3, a4 = zoom_rows[0], zoom_rows[1]
        a3.sharex(a4)
        half = (zoom_ms / 1000.0) / 2.0
        lo, hi = zoom_center - half, zoom_center + half
        mask = (t >= lo) & (t <= hi)
        for ax_plot, series, color, label in ((a3, x, "r", "X"), (a3, y, "g", "Y"), (a3, z, "b", "Z")):
            ax_plot.plot(t[mask], series[mask], color=color, marker="o", ms=3, lw=0.8, label=label)
        if go_t_s is not None and lo <= go_t_s <= hi:
            a3.axvline(go_t_s, color="0.4", ls=":", lw=1.4, label="go marker")
        if event_t is not None:
            a3.axvline(event_t, color="k", ls="--", lw=1.2, label="detected event")
        a3.set_ylabel("Accel [g]")
        a3.legend(loc="upper right", fontsize=8, ncol=4)
        a3.grid(True, ls="--", alpha=0.5)
        a3.set_title(f"zoomed: {zoom_ms:.0f} ms window around the marker, individual samples shown",
                     fontsize=9)

        a4.plot(t[mask], mag[mask], "m-", marker="o", ms=3, lw=0.8, label="|a| (not the decision signal)")
        if go_t_s is not None and lo <= go_t_s <= hi:
            a4.axvline(go_t_s, color="0.4", ls=":", lw=1.4)
        if event_t is not None:
            a4.axvline(event_t, color="k", ls="--", lw=1.2)
        a4.set_xlabel("Time [s] (zoomed)")
        a4.set_ylabel("Magnitude [g]")
        a4.grid(True, ls="--", alpha=0.5)
        a4.legend(loc="upper right", fontsize=8)

        if horiz_full is not None:
            a5 = zoom_rows[2]
            a5.plot(t[mask], horiz_full[mask] * 1000, color="darkorange", marker="o", ms=3, lw=0.8,
                    label="horiz (mg) - actual decision signal")
            if floor_g is not None:
                a5.axhline(floor_g * 1000, color="0.5", ls="-.", lw=1.0, label=f"floor ({floor_g*1000:.0f}mg)")
            if go_t_s is not None and lo <= go_t_s <= hi:
                a5.axvline(go_t_s, color="0.4", ls=":", lw=1.4)
            if event_t is not None:
                a5.axvline(event_t, color="k", ls="--", lw=1.2)
            a5.set_xlabel("Time [s] (zoomed)")
            a5.set_ylabel("horiz [mg]")
            a5.grid(True, ls="--", alpha=0.5)
            a5.legend(loc="upper right", fontsize=8)

    out = os.path.splitext(path)[0] + "_detect.png"
    title_t = f"{event_t:.4f}s" if event_t is not None else "NONE"
    fig.suptitle(f"detect_pushoff - {os.path.basename(path)} - event @ {title_t}")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)
    print(f"  -> saved {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csvs", nargs="+", help="one or more recorded CSV files (globs OK, quote them)")
    ap.add_argument("--odr", type=float, default=977.5,
                     help="sample rate of the CSV, for the EMA time constants")
    ap.add_argument("--sta-ms", type=float, default=15.0)
    ap.add_argument("--lta-ms", type=float, default=800.0)
    ap.add_argument("--ratio", type=float, default=6.0, help="STA/LTA ratio trigger threshold")
    ap.add_argument("--floor-mg", type=float, default=20.0, help="absolute floor, mg, for arming + backdating")
    ap.add_argument("--confirm-ms", type=float, default=15.0,
                     help="how long the signal must stay above the confirm floor to accept the event")
    ap.add_argument("--confirm-floor-mg", type=float, default=10.0)
    ap.add_argument("--gravity-axis", choices=["auto", "x", "y", "z"], default="auto",
                     help="which axis carries gravity (excluded from the horizontal-plane "
                          "calc) - auto-detected from the first ~20ms by default")
    ap.add_argument("--after-go", action="store_true",
                     help="ignore any movement before the file's own go marker (if present) - "
                          "use to isolate the real push-off from pre-go handling/fidgeting when "
                          "checking a bench capture; a real false-start detector must NOT do "
                          "this, since catching pre-go movement is the whole point")
    ap.add_argument("--plot", action="store_true", help="save a PNG marking the detected instant")
    ap.add_argument("--zoom-ms", type=float, default=120.0,
                     help="width of the zoomed-in panel (individual samples marked) around the event/go marker")
    args = ap.parse_args()

    paths = []
    for pattern in args.csvs:
        matches = sorted(glob.glob(pattern))
        paths.extend(matches if matches else [pattern])

    base_kwargs = dict(sta_ms=args.sta_ms, lta_ms=args.lta_ms, ratio_threshold=args.ratio,
                        floor_g=args.floor_mg / 1000.0, confirm_ms=args.confirm_ms,
                        confirm_floor_g=args.confirm_floor_mg / 1000.0, gravity_axis=args.gravity_axis)

    for path in paths:
        if not os.path.isfile(path):
            print(f"{path}: not found")
            continue
        go_t_s = read_go_t_s(path)
        kwargs = dict(base_kwargs)
        if args.after_go and go_t_s is not None:
            kwargs["earliest_trigger_t"] = go_t_s
        det, (t, x, y, z) = run_csv(path, args.odr, **kwargs)
        if det.triggered:
            msg = (f"{os.path.basename(path)}: event at t={det.event_t:.4f}s "
                   f"(horiz={det.event_horiz_g * 1000:.1f} mg)")
            if go_t_s is not None:
                rt_ms = (det.event_t - go_t_s) * 1000.0
                msg += f"  |  go marker at t={go_t_s:.4f}s  |  reaction time = {rt_ms:.1f} ms"
            print(msg)
        else:
            msg = f"{os.path.basename(path)}: NO EVENT detected"
            if go_t_s is not None:
                msg += f"  (go marker at t={go_t_s:.4f}s)"
            print(msg)

        if args.plot:
            baseline = (det.bx, det.by, det.bz) if det.bx is not None else None
            _save_plot(path, t, x, y, z, det.event_t if det.triggered else None, go_t_s,
                       zoom_ms=args.zoom_ms, gravity_axis=det.gravity_axis, baseline=baseline,
                       floor_g=det.floor_g)


if __name__ == "__main__":
    main()
