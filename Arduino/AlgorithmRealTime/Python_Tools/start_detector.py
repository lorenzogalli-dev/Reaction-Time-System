#!/usr/bin/env python3
"""
start_detector.py - movement-onset / false-start detector for AlgorithmRealTime
captures. GUI by default, CLI with --cli.

Usage:
    python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py                    # GUI, pick a file in it
    python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py Data/x.csv         # GUI, preloaded
    python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py Data/x.csv --cli   # terminal only
    python3 Arduino/AlgorithmRealTime/Python_Tools/start_detector.py "Data/*.csv" --cli # batch

WHAT IT DECIDES ON, AND WHY
---------------------------
Not |a|. The 3-axis vector magnitude is the intuitive choice and it is the
wrong one: horizontal acceleration adds in quadrature with gravity, so a
purely lateral event is under-reported - measured at 4.2x on the sensor
evaluation capture (46 mg against a true 196 mg). The drive out of the blocks
is predominantly horizontal, which is exactly the direction |a| hides.

Not "drop the axis gravity is on", either, which is what the first prototype
did. That is only correct if the board is mounted perfectly flat. Tilt it 20
degrees on the block and part of gravity leaks onto the two "horizontal" axes
while part of the real horizontal drive leaks onto the "vertical" one.

What this does instead is the standard, mount-angle-independent form: estimate
the gravity direction as a VECTOR g_hat from a resting window, then split every
sample into

    a_vert  = a . g_hat                     (along gravity)
    a_horiz = a - (a . g_hat) g_hat         (the plane perpendicular to it)

and run STA/LTA on |a_horiz - baseline|^2. No axis is chosen, no mounting angle
is assumed, and a_vert falls out for free.

STA/LTA itself is the standard seismological trigger: a short-window average of
the signal's energy against a long-window one. The ratio asks "is there much
more energy right now than in the recent background", which adapts to whatever
baseline jitter is present instead of trusting one fixed absolute number.

The detector is CAUSAL - update() sees one sample plus a short rolling history,
never the whole file and never a future sample - so what is tuned here ports to
C++ on the device unchanged, in logic if not in language.

TWO STAGES, AND WHY THE SECOND ONE EXISTS
------------------------------------------
STA/LTA is good at deciding THAT something happened and bad at deciding WHEN it
started, because "when" comes out as a threshold crossing on a rising ramp. On
the 2026-09-08 bench capture that ramp climbs at 1.28 mg/ms against a noise
floor of 1.19 mg (1 sigma), so 1 mg of doubt about the threshold is 0.8 ms of
doubt about the time - and to sit safely clear of the noise the threshold has
to be 5-8 mg, which is already 4-6 ms late. Measured: moving the floor from 10
to 50 mg moved the reported reaction time by 32.5 ms.

That is not measurement error. The detector is perfectly repeatable; it is the
DEFINITION of "onset" that moves with the threshold. Worse, the lateness is not
constant - it depends on how steep the ramp is, so it varies with how explosive
the athlete is and does not cancel out of a calibration.

The standard fix, from seismology, where the problem has exactly this shape (a
wavefront emerging from noise), is two stages: STA/LTA triggers, then a PICKER
refines the onset without any threshold at all. The picker here is Maeda's AIC:
it finds the sample that best splits the window into a "noise" segment and a
"signal" segment. On the same capture it lands on the identical sample for
every floor from 10 to 50 mg - spread 0.0 ms against the threshold's 32.5 ms.

Note what this does and does not prove. It shows the answer stops depending on
tuning. It does NOT show the answer is the true onset - that needs an
independent reference. And AIC is not causal in the STA/LTA sense: it reads
~aic_post_ms of samples after the trigger, so on-device it would add that much
DECISION latency to a live false-start alarm. It does not affect the accuracy
of the reported timestamp. Disable it with use_aic=False / --no-aic to compare.

THE RESTING WINDOW IS NOT FREE, AND v4 IS WHY THIS WORKS
--------------------------------------------------------
g_hat, the baseline and the LTA all need a stretch of settled data before any
of them means anything: the LTA time constant alone is 800 ms. Firmware v4
dumps ~3 s of pre-roll ahead of the "set" beep precisely so that by the instant
the false-start window opens, all three have converged. On a pre-v4 capture
that started at "set" the detector is blind for the first 800 ms of the window
it is supposed to be judging.
"""

import argparse
import glob
import math
import os
import sys

import numpy as np
import pandas as pd

# --- defaults -------------------------------------------------------------
# Ballpark values, NOT yet tuned against a real on-block push-off. floor_mg
# comes from the sensor evaluation (noise floor 0.65 mg 1-sigma, so 20 mg is
# ~30 sigma clear); the time constants are conventional STA/LTA choices. The
# GUI exists to change them against real data - that is the open work.
DEF = dict(
    sta_ms=15.0,
    lta_ms=800.0,
    ratio_on=6.0,
    ratio_off=2.0,
    floor_mg=20.0,
    confirm_ms=15.0,
    confirm_floor_mg=10.0,
    baseline_tau_s=1.0,
    rest_ms=300.0,
    false_start_ms=100.0,
    # The arming gate, mirroring StartDetector.h. blank_ms is now the FLOOR of
    # the arming wait, not a fixed blanking: nothing may arm before it, but
    # arming then waits for quiet_hold_ms of continuous sub-settled_mg signal.
    # Measured on 27 block starts: settling after "set" varies 1.0-3.3 s
    # between attempts, so a fixed blanking cannot know when it is over.
    blank_ms=800.0,
    quiet_hold_ms=200.0,
    arm_cap_ms=4000.0,
    settled_mg=15.0,
    aic_pre_ms=150.0,
    aic_post_ms=50.0,
)


class StartDetector:
    """Causal STA/LTA on gravity-projected horizontal magnitude.

    Feed samples in time order with update(t_s, x, y, z); it returns an event
    time the instant one is confirmed, else None. Unlike the first prototype
    it RE-ARMS: after an event it waits for the ratio to fall back under
    ratio_off before it will trigger again, so one capture can legitimately
    contain a settling twitch, a false start and a real push-off rather than
    only ever reporting whichever came first.
    """

    def __init__(self, odr_hz, sta_ms=DEF["sta_ms"], lta_ms=DEF["lta_ms"],
                 ratio_on=DEF["ratio_on"], ratio_off=DEF["ratio_off"],
                 floor_mg=DEF["floor_mg"], confirm_ms=DEF["confirm_ms"],
                 confirm_floor_mg=DEF["confirm_floor_mg"],
                 baseline_tau_s=DEF["baseline_tau_s"], rest_ms=DEF["rest_ms"],
                 warmup_s=None, set_t=None, blank_ms=DEF["blank_ms"],
                 quiet_hold_ms=DEF["quiet_hold_ms"], arm_cap_ms=DEF["arm_cap_ms"],
                 settled_mg=DEF["settled_mg"]):
        self.odr_hz = float(odr_hz)
        dt = 1.0 / self.odr_hz
        # EMA coefficients from time constants, so the tuning stays in
        # milliseconds and does not silently change meaning when the firmware
        # rate does. (It has changed once already: 977 Hz free-running in v2,
        # 833 Hz nominal / 863 Hz real from v3.)
        self.a_sta = 1.0 - math.exp(-dt / (sta_ms / 1000.0))
        self.a_lta = 1.0 - math.exp(-dt / (lta_ms / 1000.0))
        self.a_base = 1.0 - math.exp(-dt / baseline_tau_s)
        self.ratio_on = ratio_on
        self.ratio_off = ratio_off
        self.floor = floor_mg / 1000.0
        self.confirm_floor = confirm_floor_mg / 1000.0
        self.confirm_n = max(1, int(round(confirm_ms / 1000.0 * self.odr_hz)))
        self.rest_n = max(2, int(round(rest_ms / 1000.0 * self.odr_hz)))
        # Until the LTA has seen one full time constant of real samples it is
        # still climbing away from its seed, so the ratio is spuriously huge
        # and every file's opening samples look like an infinite-ratio event.
        # Gate triggering, not the EMA updates themselves.
        warm = warmup_s if warmup_s is not None else lta_ms / 1000.0
        self.warmup_n = max(1, int(round(warm * self.odr_hz)))

        self._rest = []
        self.g_hat = None
        self.b_h = None      # slowly-adapting horizontal baseline vector
        self.sta = 0.0
        self.lta = 1e-9
        self._n = 0
        self._hist = []      # (t, horiz), long enough to backdate through
        self._hist_max = self.confirm_n + 5
        # --- the arming gate, mirroring StartDetector.h -------------------
        # Nothing is judged until the athlete has been MEASURED still. set_t is
        # None when the caller has no "set" marker (a free capture), and then
        # the gate is open from the start and this reduces to the old
        # behaviour.
        self.set_t = set_t
        self.min_blank_s = blank_ms / 1000.0
        self.quiet_hold_s = quiet_hold_ms / 1000.0
        self.arm_cap_s = arm_cap_ms / 1000.0
        self.settled = settled_mg / 1000.0
        self.gate_open = set_t is None
        self.gate_t = None
        self.gate_capped = False
        self.gate_peak_mg = 0.0
        self._quiet_t0 = None
        self._quiet_peak = 0.0
        self.forced_arm_t = None   # set by analyse() when the CSV carries ARM
        self.forced_arm_peak_mg = None

        self.armed = True         # ratio has fallen back; a new event may open
        self.candidate = False
        self.confirm_count = 0
        self.baseline_frozen = False

        # Per-sample traces, for plotting only - the decision never reads them.
        self.tr_t, self.tr_horiz, self.tr_vert, self.tr_ratio = [], [], [], []
        self.events = []     # (t_s, horiz_g, ratio)

    def _track_arming(self, t_s, horiz):
        """Open the judged window on measured stillness, or on the cap.

        This is what replaced the fixed blanking, and the reason is in the
        data: at the "set" command the athlete rises into position - several
        hundred mg, lasting up to 2.65 s - and a clock cannot know when that is
        over. A signal can. The firmware then fires "go" a random interval
        after this instant, the way a starter holds the gun until the field is
        steady, so the judged window is trustworthy by construction.
        """
        if self.gate_open:
            return
        # The board already decided. Open at its instant, not ours.
        if self.forced_arm_t is not None:
            if t_s >= self.forced_arm_t:
                peak = (self.forced_arm_peak_mg if self.forced_arm_peak_mg is not None
                        else horiz * 1000.0)
                self._open_gate(t_s, peak, False)
            return
        if self.set_t is None:
            return
        if t_s < self.set_t + self.min_blank_s:
            return                       # the rise into position

        if horiz < self.settled:
            if self._quiet_t0 is None:
                self._quiet_t0, self._quiet_peak = t_s, horiz
            else:
                self._quiet_peak = max(self._quiet_peak, horiz)
            if t_s - self._quiet_t0 >= self.quiet_hold_s:
                self._open_gate(t_s, self._quiet_peak * 1000.0, False)
                return
        else:
            self._quiet_t0 = None        # a lull, not a hold

        # An athlete who never settles because they are already starting would
        # otherwise never arm, and the mechanism would disable itself in
        # exactly the case it exists to catch.
        if t_s >= self.set_t + self.arm_cap_s:
            self._open_gate(t_s, horiz * 1000.0, True)

    def _open_gate(self, t_s, peak_mg, capped):
        self.gate_open = True
        self.gate_t = t_s
        self.gate_capped = capped
        self.gate_peak_mg = peak_mg
        # Ready immediately: the hold that just opened the gate is itself proof
        # the ratio has fallen back, so there is nothing to wait for.
        self.armed = True
        self.candidate = False
        self.confirm_count = 0
        self.baseline_frozen = False

    def update(self, t_s, x, y, z):
        a = np.array((x, y, z), dtype=float)

        if self.g_hat is None:
            self._rest.append(a)
            if len(self._rest) < self.rest_n:
                return None
            # Gravity as a direction, not an axis - see the module docstring.
            g = np.mean(self._rest, axis=0)
            n = np.linalg.norm(g)
            if n < 0.1:
                raise ValueError(
                    "resting window averages |a| = %.3f g: the board was not "
                    "still, or gravity is not in this data" % n)
            self.g_hat = g / n
            self.b_h = np.zeros(3)
            self._rest = None
            return None

        horiz_vec = a - float(a @ self.g_hat) * self.g_hat
        if not self.baseline_frozen:
            # Frozen the instant a candidate opens, so a real movement cannot
            # drag its own reference along with it and hide itself.
            self.b_h = self.b_h + self.a_base * (horiz_vec - self.b_h)
        d = horiz_vec - self.b_h
        horiz = float(np.linalg.norm(d))
        vert = float(a @ self.g_hat)

        self.sta += self.a_sta * (horiz * horiz - self.sta)
        self.lta += self.a_lta * (horiz * horiz - self.lta)
        ratio = self.sta / self.lta if self.lta > 0 else 0.0
        self._n += 1

        self.tr_t.append(t_s)
        self.tr_horiz.append(horiz)
        self.tr_vert.append(vert)
        self.tr_ratio.append(ratio)

        self._hist.append((t_s, horiz))
        if len(self._hist) > self._hist_max:
            self._hist.pop(0)

        if self._n <= self.warmup_n:
            return None

        self._track_arming(t_s, horiz)
        # The EMAs above keep running throughout, so the detector is warm the
        # instant the gate opens.
        if not self.gate_open:
            return None

        if not self.candidate:
            if not self.armed:
                # Hysteresis: hold off until the signal has actually calmed
                # down, otherwise the tail of one event re-triggers on itself.
                if ratio < self.ratio_off:
                    self.armed = True
                    self.baseline_frozen = False
                return None
            if horiz > self.floor and ratio > self.ratio_on:
                self.candidate = True
                self.baseline_frozen = True
                self.confirm_count = 1
            return None

        # A candidate is open: it has to hold up for confirm_n samples.
        if horiz > self.confirm_floor:
            self.confirm_count += 1
        else:
            self.candidate = False
            self.confirm_count = 0
            self.baseline_frozen = False
            return None

        if self.confirm_count < self.confirm_n:
            return None

        # Confirmed. Report the first raw sample in the rolling window that
        # crossed the floor, not the later instant confirmation finished - the
        # confirm window costs decision latency, never timestamp accuracy.
        ev_t, ev_h = self._hist[-1]
        for t_i, h_i in self._hist:
            if h_i > self.floor:
                ev_t, ev_h = t_i, h_i
                break
        self.events.append((ev_t, ev_h, ratio))
        self.candidate = False
        self.confirm_count = 0
        self.armed = False
        return ev_t


def aic_pick(sig, t):
    """Maeda's AIC picker. For each candidate split k, the window is modelled
    as two segments with different variances; the best split minimises

        AIC(k) = k*log(var(sig[:k])) + (n-k-1)*log(var(sig[k:]))

    No threshold and no tunable constant - which is the whole point, since a
    threshold is what made the onset estimate depend on tuning in the first
    place. Returns (t_pick, ok); ok is False when the minimum lands against a
    window edge, which means there was no clear transition inside the window
    and the caller should keep its own estimate rather than trust this one.
    """
    n = len(sig)
    if n < 20:
        return None, False
    aic = np.full(n, np.inf)
    # Cumulative moments let every split's variance be computed in O(1), so the
    # whole curve costs one pass instead of n slices.
    c1 = np.cumsum(sig)
    c2 = np.cumsum(sig * sig)
    tot1, tot2 = c1[-1], c2[-1]
    k = np.arange(5, n - 5)
    n1 = k
    v1 = c2[k - 1] / n1 - (c1[k - 1] / n1) ** 2
    n2 = n - k
    v2 = (tot2 - c2[k - 1]) / n2 - ((tot1 - c1[k - 1]) / n2) ** 2
    good = (v1 > 0) & (v2 > 0)
    aic[k[good]] = (n1[good] * np.log(v1[good])
                    + (n - k[good] - 1) * np.log(v2[good]))
    if not np.isfinite(aic).any():
        return None, False
    i = int(np.argmin(aic))
    return float(t[i]), bool(10 <= i <= n - 10)


def refine_onset(det, ev_t, pre_ms, post_ms, not_before=None):
    """Re-pick one STA/LTA event's onset with AIC. Returns (t, moved_ms, ok).

    Runs on the horizontal magnitude the detector itself decided on, so the two
    stages never disagree about what the signal is.

    `not_before` bounds how far back the window may reach - normally the
    previous event's onset. Without it, two events closer together than pre_ms
    share a window, and AIC quite correctly reports the largest transition in
    it, which is the EARLIER event: the second event then gets backdated onto
    the first. Seen on Data/accel_20260908_233329.csv, where the third event
    jumped 83 ms back onto the second.
    """
    t = np.asarray(det.tr_t)
    h = np.asarray(det.tr_horiz)
    lo = ev_t - pre_ms / 1000.0
    if not_before is not None:
        lo = max(lo, not_before)
    m = (t >= lo) & (t <= ev_t + post_ms / 1000.0)
    if m.sum() < 20:
        return ev_t, 0.0, False
    seg = h[m]
    # AIC assumes the window really does contain a quiet part followed by an
    # active one. For an event that fires in the middle of movement already in
    # progress - a second push, the far side of a stumble - there is no such
    # split, and AIC dutifully reports the largest variance change it can find
    # inside a uniformly active window, which is meaningless. Seen on
    # Data/accel_20260908_233329.csv, where a third event 200 ms into a
    # 400 mg movement was dragged 83 ms backwards.
    #
    # So require the window to look like what AIC expects before trusting it:
    # its opening quarter must be markedly quieter than its closing quarter.
    q = max(4, len(seg) // 4)
    v_head, v_tail = np.var(seg[:q]), np.var(seg[-q:])
    if not (v_tail > 0 and v_head > 0 and v_tail / v_head > 16.0):
        return ev_t, 0.0, False
    pick, ok = aic_pick(seg, t[m])
    if pick is None or not ok:
        return ev_t, 0.0, False
    return pick, (pick - ev_t) * 1000.0, True


# --- file loading ---------------------------------------------------------

def read_markers(path):
    """Marker comments, wherever they are in the file. v4 writes them in the
    leading block; accel_live.py appended them after the data rows, and one
    reader that stopped at the first non-comment line silently found none at
    all - so scan the whole file and stay compatible with both."""
    out = {}
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.startswith("#"):
                continue
            key, _, val = line[1:].partition(":")
            key, val = key.strip(), val.strip()
            if key == "board_verdict":
                out[key] = val
                continue
            if key in ("on_t_s", "set_t_s", "go_t_s", "arm_t_s", "arm_peak_mg",
                       "arm_capped", "board_reaction_ms", "clockstep_us",
                       "dropped", "truncated", "preroll_samples"):
                try:
                    out[key] = float(val)
                except ValueError:
                    pass
    return out


def load_csv(path):
    df = pd.read_csv(path, comment="#")
    cols = {c.lower(): c for c in df.columns}
    for cand in ("x_g", "x"):
        if cand in cols:
            xc = cols[cand]
            break
    else:
        raise ValueError(f"{path}: no x column among {list(df.columns)}")
    yc = cols.get("y_g", cols.get("y"))
    zc = cols.get("z_g", cols.get("z"))
    if "t_s" in cols:
        t = df[cols["t_s"]].values.astype(float)
    elif "t_us" in cols:
        t = df[cols["t_us"]].values.astype(float) / 1e6
        t -= t[0]
    elif "elapsed_s" in cols:
        t = df[cols["elapsed_s"]].values.astype(float)
    else:
        raise ValueError(f"{path}: no time column among {list(df.columns)}")
    return t, df[xc].values.astype(float), df[yc].values.astype(float), \
        df[zc].values.astype(float), read_markers(path)


def analyse(path, **kw):
    """Run the detector over one file. Returns a dict the GUI and the CLI
    both render."""
    false_start_ms = kw.pop("false_start_ms", DEF["false_start_ms"])
    blank_ms = kw.pop("blank_ms", DEF["blank_ms"])
    settled_mg = kw.pop("settled_mg", DEF["settled_mg"])
    quiet_hold_ms = kw.pop("quiet_hold_ms", DEF["quiet_hold_ms"])
    arm_cap_ms = kw.pop("arm_cap_ms", DEF["arm_cap_ms"])
    aic_pre_ms = kw.pop("aic_pre_ms", DEF["aic_pre_ms"])
    aic_post_ms = kw.pop("aic_post_ms", DEF["aic_post_ms"])
    use_aic = kw.pop("use_aic", True)
    force_rearm = kw.pop("force_rearm", False)
    t, x, y, z, markers = load_csv(path)
    if len(t) < 50:
        raise ValueError(f"{path}: only {len(t)} rows")
    # Derive the rate from the capture rather than assuming one: the firmware's
    # rate has already changed once, and every time constant above is sized
    # from this number, so a stale default mis-sizes them silently instead of
    # failing. Median, so one gap cannot skew it.
    dt = float(np.median(np.diff(t)))
    if not dt > 0:
        raise ValueError(f"{path}: timestamps are not increasing")
    odr = 1.0 / dt

    go = markers.get("go_t_s")
    set_ = markers.get("set_t_s")

    # set_t goes INTO the detector, not just into the classification: the
    # arming gate is causal and decides, sample by sample, when the judged
    # window opens. A capture with no "set" marker arms immediately.
    # If the board wrote down when it armed, USE IT rather than re-deriving it.
    # Re-deriving is not guaranteed to reach the same instant - the board works
    # in float32 and numpy in float64, and on a capture where the signal hovers
    # at the stillness limit a 0.3 mg gap moved the arming by 1.5 s - and more
    # to the point, the board's arming is what actually happened: it is when
    # the gun was scheduled from. Recomputing it would be second-guessing a
    # decision that has already been taken, the same mistake as recomputing
    # "go" instead of reading its timestamp.
    #
    # force_rearm=True re-derives anyway. That is what tuning needs: change a
    # threshold in the panel and see where the gate WOULD have opened.
    board_arm = markers.get("arm_t_s")
    if board_arm is not None and not force_rearm:
        det = StartDetector(odr_hz=odr, set_t=None, **kw)
        det.gate_open = False
        det.set_t = set_
        det.forced_arm_t = board_arm
        # The stillness the BOARD measured when it armed, not this sample's
        # value. Reading the instant but recomputing the number beside it would
        # report two different things about one decision.
        det.forced_arm_peak_mg = markers.get("arm_peak_mg")
    else:
        det = StartDetector(odr_hz=odr, set_t=set_, blank_ms=blank_ms,
                            quiet_hold_ms=quiet_hold_ms, arm_cap_ms=arm_cap_ms,
                            settled_mg=settled_mg, **kw)
    for i in range(len(t)):
        det.update(t[i], x[i], y[i], z[i])

    events = []
    prev_t = None
    for ev_t, ev_h, ratio in det.events:
        trig_t = ev_t
        moved, refined = 0.0, False
        if use_aic:
            ev_t, moved, refined = refine_onset(det, ev_t, aic_pre_ms,
                                                aic_post_ms, not_before=prev_t)
        prev_t = ev_t
        verdict, rt = classify(ev_t, go)
        events.append(dict(t=ev_t, trigger_t=trig_t, aic_moved_ms=moved,
                           aic_ok=refined, horiz_mg=ev_h * 1000.0, ratio=ratio,
                           verdict=verdict, reaction_ms=rt))
    return dict(path=path, odr=odr, t=t, x=x, y=y, z=z, markers=markers,
                det=det, events=events, false_start_ms=false_start_ms,
                blank_ms=blank_ms, use_aic=use_aic,
                armed=dict(t=det.gate_t, capped=bool(markers.get("arm_capped"))
                                                 if board_arm is not None and not force_rearm
                                                 else det.gate_capped,
                           source="board" if (board_arm is not None and not force_rearm)
                                          else "recomputed",
                           peak_mg=det.gate_peak_mg, limit_mg=settled_mg,
                           after_set_ms=None if (det.gate_t is None or set_ is None)
                                        else (det.gate_t - set_) * 1000.0),
                summary=summarise(events, go, det))


def classify(ev_t, go_t):
    """THE VERDICT. One rule, one outcome, a reaction time always reported.

    The "pre-set (settling)" and "rise into set (not judged)" cases this used to
    carry are gone, and not because they stopped mattering: the arming gate
    means no event can exist before the athlete has been measured still, so
    there is nothing left for them to describe. What used to be a verdict is
    now a precondition.

    The two false-start cases are one comparison. Moving before the gun and
    reacting in under 100 ms are the same fault - a start that cannot have been
    a response to the gun - and World Athletics treats them as one.
    """
    if go_t is None:
        return "movement (no go marker)", None
    rt = (ev_t - go_t) * 1000.0
    return ("FALSE START" if rt < DEF["false_start_ms"] else "valid start"), rt


def summarise(events, go_t, det):
    note = ""
    if getattr(det, "gate_capped", False):
        note = ("  |  NOTE: armed on the cap, the athlete never settled - "
                "the verdict stands but is worth reviewing")
    if not events:
        return "No movement in the judged window." + note
    e = events[0]
    if e["reaction_ms"] is None:
        return f"Movement at t={e['t']:.4f}s - no 'go' marker, so no reaction time.{note}"
    return (f"{e['verdict']}  |  t={e['t']:.4f}s  |  reaction {e['reaction_ms']:.1f} ms"
            f"  |  armed at {det.gate_peak_mg:.1f} mg{note}")


# --- CLI ------------------------------------------------------------------

def run_cli(paths, **kw):
    rc = 0
    for path in paths:
        try:
            r = analyse(path, **kw)
        except Exception as exc:                      # noqa: BLE001
            print(f"{os.path.basename(path)}: ERROR - {exc}")
            rc = 1
            continue
        m = r["markers"]
        print(f"\n=== {os.path.basename(path)} ===")
        print(f"  {len(r['t'])} rows, {r['odr']:.1f} Hz effective")
        if m.get("clockstep_us", 0) > 100:
            print(f"  !! CLOCKSTEP {m['clockstep_us']:.0f} us - wrong Arduino core, "
                  f"timestamps are ~1 ms granular, reaction times are not reliable")
            rc = 1
        if m.get("dropped"):
            print(f"  !! DROPPED {m['dropped']:.0f} - watchdog-recovered samples")
        if m.get("truncated"):
            print(f"  !! TRUNCATED - short pre-roll ({m.get('preroll_samples', 0):.0f} samples)")
        for name in ("on_t_s", "set_t_s", "go_t_s"):
            if name in m:
                print(f"  {name} = {m[name]:.4f}s")
        if not r["events"]:
            print("  no events")
        for i, e in enumerate(r["events"], 1):
            rt = "" if e["reaction_ms"] is None else f"  reaction {e['reaction_ms']:+.1f} ms"
            aic = ""
            if r["use_aic"]:
                aic = (f"  [AIC {e['aic_moved_ms']:+.1f} ms]" if e["aic_ok"]
                       else "  [AIC: no clear onset, kept the threshold]")
            print(f"  #{i}: t={e['t']:.4f}s  horiz={e['horiz_mg']:.1f} mg  "
                  f"ratio={e['ratio']:.1f}{aic}  [{e['verdict']}]{rt}")
        print(f"  => {r['summary']}")
    return rc


# --- GUI ------------------------------------------------------------------

def run_gui(initial=None):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    import matplotlib
    matplotlib.use("TkAgg")
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

    FIELDS = [
        ("STA (ms)", "sta_ms"),
        ("LTA (ms)", "lta_ms"),
        ("Ratio ON", "ratio_on"),
        ("Ratio OFF (re-arm)", "ratio_off"),
        ("Floor (mg)", "floor_mg"),
        ("Confirm (ms)", "confirm_ms"),
        ("Confirm floor (mg)", "confirm_floor_mg"),
        ("Baseline tau (s)", "baseline_tau_s"),
        ("Rest window (ms)", "rest_ms"),
        ("False start under (ms)", "false_start_ms"),
        ("Arm: min blank (ms)", "blank_ms"),
        ("Arm: still under (mg)", "settled_mg"),
        ("Arm: hold for (ms)", "quiet_hold_ms"),
        ("Arm: give up after (ms)", "arm_cap_ms"),
        ("AIC window before (ms)", "aic_pre_ms"),
        ("AIC window after (ms)", "aic_post_ms"),
    ]

    class App:
        def __init__(self, root):
            self.root = root
            root.title("Start Detector - STA/LTA on horizontal plane")
            root.geometry("1320x780")
            root.minsize(1040, 640)

            self.path = tk.StringVar(value=initial or "")
            self.status = tk.StringVar(value="Pick a capture to begin.")
            self.vars = {k: tk.StringVar(value=str(DEF[k])) for _, k in FIELDS}
            self.use_aic = tk.BooleanVar(value=True)
            self.result = None
            self._build()
            if initial and os.path.isfile(initial):
                self.process()

        # -- layout
        def _build(self):
            paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
            paned.pack(fill=tk.BOTH, expand=True)

            # The left panel's content (14 parameter rows + a 10-row table) can
            # exceed the window's height depending on screen size/DPI scaling,
            # and a plain packed Frame has no scrolling - content past the
            # bottom was silently clipped, with no way to reach it. Wrap it in
            # a Canvas+Scrollbar instead.
            left_outer = ttk.Frame(paned)
            paned.add(left_outer, weight=0)

            left_canvas = tk.Canvas(left_outer, highlightthickness=0)
            left_vsb = ttk.Scrollbar(left_outer, orient=tk.VERTICAL, command=left_canvas.yview)
            left_canvas.configure(yscrollcommand=left_vsb.set)
            left_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            left_vsb.pack(side=tk.RIGHT, fill=tk.Y)

            left = ttk.Frame(left_canvas, padding=8)
            left_win = left_canvas.create_window((0, 0), window=left, anchor="nw")
            left.bind("<Configure>",
                      lambda e: left_canvas.configure(scrollregion=left_canvas.bbox("all")))
            left_canvas.bind("<Configure>",
                              lambda e: left_canvas.itemconfig(left_win, width=e.width))

            # Mouse wheel scrolls the left panel only while the cursor is over
            # it, so it doesn't hijack scroll events meant for the plot on the
            # right. macOS reports small per-notch deltas (unlike Windows'
            # multiples of 120), so no /120 scaling here.
            def _on_mousewheel(event):
                left_canvas.yview_scroll(int(-1 * event.delta), "units")
            left_canvas.bind("<Enter>",
                              lambda e: left_canvas.bind_all("<MouseWheel>", _on_mousewheel))
            left_canvas.bind("<Leave>",
                              lambda e: left_canvas.unbind_all("<MouseWheel>"))

            box_file = ttk.LabelFrame(left, text="Capture", padding=8)
            box_file.pack(fill=tk.X, pady=(0, 6))
            ttk.Entry(box_file, textvariable=self.path, width=30).pack(
                side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
            ttk.Button(box_file, text="Browse...", command=self.browse).pack(side=tk.RIGHT)

            box_p = ttk.LabelFrame(left, text="Detector parameters", padding=8)
            box_p.pack(fill=tk.X, pady=(0, 6))
            for row, (label, key) in enumerate(FIELDS):
                ttk.Label(box_p, text=label).grid(row=row, column=0, sticky="w", pady=2)
                ttk.Entry(box_p, textvariable=self.vars[key], width=12).grid(
                    row=row, column=1, sticky="e", pady=2)
            ttk.Checkbutton(box_p, text="Refine onset with AIC (2nd stage)",
                            variable=self.use_aic).grid(
                row=len(FIELDS), column=0, columnspan=2, sticky="w", pady=(6, 0))
            ttk.Button(box_p, text="Reset to defaults", command=self.reset).grid(
                row=len(FIELDS) + 1, column=0, columnspan=2, sticky="ew", pady=(6, 0))

            ttk.Button(left, text="▶  Analyse", command=self.process).pack(
                fill=tk.X, pady=(0, 6))

            box_r = ttk.LabelFrame(left, text="Detected events", padding=8)
            box_r.pack(fill=tk.BOTH, expand=True)
            ttk.Label(box_r, textvariable=self.status, wraplength=330,
                      font=("Helvetica", 10, "bold")).pack(anchor="w", pady=(0, 6))
            cols = ("n", "t", "aic", "mg", "ratio", "rt", "verdict")
            self.tree = ttk.Treeview(box_r, columns=cols, show="headings", height=10)
            for c, head, w in zip(cols,
                                  ("#", "t (s)", "ΔAIC", "horiz", "ratio", "react", "verdict"),
                                  (28, 74, 60, 58, 48, 62, 140)):
                self.tree.heading(c, text=head)
                self.tree.column(c, width=w, anchor="w")
            self.tree.pack(fill=tk.BOTH, expand=True)

            right = ttk.Frame(paned)
            paned.add(right, weight=1)
            self.fig, self.axes = plt.subplots(3, 1, figsize=(9, 7), sharex=True)
            self.fig.subplots_adjust(hspace=0.12, left=0.09, right=0.98, top=0.96, bottom=0.07)
            self.canvas = FigureCanvasTkAgg(self.fig, master=right)
            self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
            NavigationToolbar2Tk(self.canvas, right).update()

        def browse(self):
            p = filedialog.askopenfilename(
                title="Open capture", filetypes=[("CSV", "*.csv"), ("All", "*.*")],
                initialdir="Data" if os.path.isdir("Data") else ".")
            if p:
                self.path.set(p)
                self.process()

        def reset(self):
            for _, k in FIELDS:
                self.vars[k].set(str(DEF[k]))
            self.use_aic.set(True)

        # -- run
        def process(self):
            path = self.path.get().strip()
            if not os.path.isfile(path):
                messagebox.showerror("Not found", f"No such file:\n{path}")
                return
            try:
                kw = {k: float(self.vars[k].get()) for _, k in FIELDS}
            except ValueError:
                messagebox.showerror("Bad parameter", "All parameters must be numbers.")
                return
            kw["use_aic"] = self.use_aic.get()
            try:
                self.result = analyse(path, **kw)
            except Exception as exc:                  # noqa: BLE001
                messagebox.showerror("Analysis failed", str(exc))
                return
            self._fill_table()
            self._draw()

        def _fill_table(self):
            r = self.result
            self.tree.delete(*self.tree.get_children())
            for i, e in enumerate(r["events"], 1):
                rt = "" if e["reaction_ms"] is None else f"{e['reaction_ms']:+.0f} ms"
                if not r["use_aic"]:
                    aic = "off"
                elif e["aic_ok"]:
                    aic = f"{e['aic_moved_ms']:+.1f}"
                else:
                    aic = "n/a"
                self.tree.insert("", "end", values=(
                    i, f"{e['t']:.4f}", aic, f"{e['horiz_mg']:.1f} mg",
                    f"{e['ratio']:.1f}", rt, e["verdict"]))
            m = r["markers"]
            warn = []
            if m.get("clockstep_us", 0) > 100:
                warn.append("!! wrong core: micros() is ~1 ms granular")
            if m.get("dropped"):
                warn.append(f"!! {m['dropped']:.0f} watchdog-recovered samples")
            if m.get("truncated"):
                warn.append("!! short pre-roll")
            if r["armed"]["capped"]:
                warn.append("!! armed on the cap: the athlete never settled")
            elif r["armed"]["after_set_ms"] is not None:
                warn.append(f"armed {r['armed']['after_set_ms']:.0f} ms after set "
                            f"at {r['armed']['peak_mg']:.1f} mg")
            self.status.set(f"{r['summary']}\n{r['odr']:.1f} Hz, {len(r['t'])} rows"
                            + ("\n" + "  ".join(warn) if warn else ""))

        # -- plots
        def _draw(self):
            r = self.result
            det, m = r["det"], r["markers"]
            for ax in self.axes:
                ax.clear()

            ax0, ax1, ax2 = self.axes
            ax0.plot(r["t"], r["x"], lw=0.7, label="x")
            ax0.plot(r["t"], r["y"], lw=0.7, label="y")
            ax0.plot(r["t"], r["z"], lw=0.7, label="z")
            ax0.set_ylabel("raw (g)")
            ax0.legend(loc="upper left", fontsize=8, ncol=3)

            # The panel that actually matters: this is the signal the detector
            # decides on. Plotting |a| instead once produced a "the marker is
            # in the wrong place" report that turned out to be the plot, not
            # the placement - |a| crept 1.00 -> 0.93 g, invisible on its axis,
            # while horiz was climbing cleanly through the floor.
            ax1.plot(det.tr_t, np.array(det.tr_horiz) * 1000.0, lw=0.8, color="tab:purple")
            ax1.axhline(float(self.vars["floor_mg"].get()), color="tab:red",
                        ls="--", lw=0.8, label="floor")
            ax1.axhline(float(self.vars["confirm_floor_mg"].get()), color="tab:orange",
                        ls=":", lw=0.8, label="confirm floor")
            ax1.set_ylabel("horiz (mg)")
            ax1.legend(loc="upper left", fontsize=8)

            ax2.plot(det.tr_t, det.tr_ratio, lw=0.8, color="tab:green")
            ax2.axhline(float(self.vars["ratio_on"].get()), color="tab:red", ls="--", lw=0.8)
            ax2.axhline(float(self.vars["ratio_off"].get()), color="tab:blue", ls=":", lw=0.8)
            ax2.set_ylabel("STA/LTA")
            ax2.set_xlabel("t (s)")
            ax2.set_yscale("log")

            # Two shades, because they mean opposite things: grey is the wait
            # for the athlete to settle, which is NOT judged, red is the window
            # in which movement is a false start. The boundary between them is
            # the arming instant, measured from the signal rather than assumed
            # - so where it falls is itself worth looking at.
            set_t, go_t = m.get("set_t_s"), m.get("go_t_s")
            arm_t = r["armed"]["t"]
            if set_t is not None and go_t is not None:
                edge = min(arm_t if arm_t is not None else go_t, go_t)
                for ax in self.axes:
                    ax.axvspan(set_t, edge, color="tab:gray", alpha=0.10)
                    ax.axvspan(edge, go_t, color="tab:red", alpha=0.10)
                    if arm_t is not None:
                        ax.axvline(arm_t, color="tab:orange", lw=1.2, ls="--")
            for name, colour in (("on_t_s", "tab:gray"), ("set_t_s", "tab:blue"),
                                 ("go_t_s", "tab:green")):
                if name in m:
                    for ax in self.axes:
                        ax.axvline(m[name], color=colour, lw=1.2)
                    ax0.text(m[name], ax0.get_ylim()[1], name[:-4].upper(),
                             color=colour, fontsize=8, va="top", ha="left")
            for e in r["events"]:
                # Solid black = the reported onset. The dotted line, when it
                # differs, is where the raw STA/LTA threshold crossing was, so
                # the second stage's correction is visible rather than implied.
                for ax in self.axes:
                    ax.axvline(e["t"], color="black", lw=1.2)
                    if r["use_aic"] and e["aic_ok"] and abs(e["aic_moved_ms"]) > 0.5:
                        ax.axvline(e["trigger_t"], color="black", lw=0.8,
                                   ls=":", alpha=0.55)

            self.fig.suptitle(os.path.basename(r["path"]), fontsize=10)
            self.canvas.draw()

    root = tk.Tk()
    App(root)
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="?", help="capture to open (glob allowed with --cli)")
    ap.add_argument("--cli", action="store_true", help="terminal only, no GUI")
    ap.add_argument("--no-aic", action="store_true",
                    help="skip the AIC onset refinement, report the raw STA/LTA "
                         "threshold crossing (for comparison)")
    for label, key in [("--sta-ms", "sta_ms"), ("--lta-ms", "lta_ms"),
                       ("--ratio-on", "ratio_on"), ("--ratio-off", "ratio_off"),
                       ("--floor-mg", "floor_mg"), ("--confirm-ms", "confirm_ms"),
                       ("--confirm-floor-mg", "confirm_floor_mg"),
                       ("--baseline-tau-s", "baseline_tau_s"),
                       ("--rest-ms", "rest_ms"), ("--false-start-ms", "false_start_ms"),
                       ("--blank-ms", "blank_ms"), ("--settled-mg", "settled_mg"),
                       ("--quiet-hold-ms", "quiet_hold_ms"), ("--arm-cap-ms", "arm_cap_ms"),
                       ("--aic-pre-ms", "aic_pre_ms"), ("--aic-post-ms", "aic_post_ms")]:
        ap.add_argument(label, type=float, default=DEF[key], dest=key)
    args = ap.parse_args()

    kw = {k: getattr(args, k) for k in DEF}
    kw["use_aic"] = not args.no_aic
    if args.cli:
        if not args.csv:
            print("--cli needs a file or glob", file=sys.stderr)
            sys.exit(2)
        paths = sorted(glob.glob(args.csv)) or [args.csv]
        sys.exit(run_cli(paths, **kw))
    run_gui(args.csv)


if __name__ == "__main__":
    main()
