#pragma once
#include "StartDetector.h"

// ===========================================================================
// Maeda's AIC onset picker, second stage. STA/LTA is good at deciding THAT
// something happened and bad at deciding WHEN, because "when" comes out as a
// threshold crossing on a rising ramp. AIC finds the split that best divides
// the window into a noise segment and a signal segment, with no threshold at
// all:
//
//     AIC(k) = k*log(var(sig[:k])) + (n-k-1)*log(var(sig[k:]))
//
// ---------------------------------------------------------------------------
// TWO THINGS DELIBERATELY LEFT ALONE, both decided by measurement over the 19
// captures in Data/ rather than by argument.
//
// 1. The signal this runs on. It recomputes the horizontal magnitude from the
//    raw ring using the baseline FROZEN at the event, where the Python runs on
//    the detector's own live trace (whose baseline is still adapting through
//    the pre-window). That looked like a divergence worth removing. Measured,
//    it is not one: over 64 comparisons - 16 captures x 4 absolute floors -
//    the two agree to 0.000000 ms, every time. Not "closely": identically.
//
//    There is also an argument that the frozen reference is the better input,
//    since AIC's model is two stationary segments and a baseline chasing the
//    onset slightly flattens the very transition being looked for. Either way
//    it costs nothing here and would have cost ~9 KB of stored trace to
//    change, so it stays.
//
// 2. Sub-sample interpolation was tried and REJECTED. Fitting a parabola to
//    AIC(k-1..k+1) and taking its vertex is the standard way to beat sample
//    quantisation, and on paper it should have helped: at 833 Hz one sample is
//    1.2 ms. Measured, it made stability WORSE. Across floors of 10-50 mg the
//    plain pick moves 0.000 ms on all 16 captures; the interpolated pick moves
//    0.060 ms on average and up to 0.253 ms, because the window shifts with
//    the trigger and drags the vertex with it. It traded an exactly stable
//    answer for a marginally unstable one, so it is not here.
//
//    The remaining uncertainty is therefore NOT in this estimator. It is the
//    sample period (1.2 ms at 833 Hz) and the timestamp jitter (334 us), both
//    properties of the sampling. The only lever that moves them is a higher
//    ODR, and both are already far below the unmeasured buzzer latency that
//    dominates the budget.
// ===========================================================================

// Window pre+post is 200 ms; at 833 Hz that is 167 samples. The ceiling below
// covers up to ~1.28 kHz. Above that the window would be truncated, which the
// original did silently - it now refuses to pick instead. See FIX below.
static const int MAX_AIC = 256;

// Scratch, static rather than stack: this runs once per event from the tail
// state, single-threaded and non-reentrant, and four of these on the stack
// would be 4 KB.
static float    aic_sig[MAX_AIC];
static uint32_t aic_t[MAX_AIC];
// double, not float, and this is not gold-plating. The per-segment variance is
// computed as E[x^2] - E[x]^2, which is the textbook case of catastrophic
// cancellation: for a quiet segment the two terms agree to several digits and
// float32 keeps only ~7. Near a shallow AIC minimum that noise decides which
// sample wins: this code once landed one sample (1.2 ms at 833 Hz) away from
// start_detector.py on two of the 09-11 captures for exactly that reason.
// 2 KB more static RAM for a deterministic answer that matches the reference.
static double   aic_c1[MAX_AIC], aic_c2[MAX_AIC];

inline bool refineOnsetAIC(
    uint32_t trigger_t_us, uint32_t not_before_us,
    const uint32_t* recT, const int16_t* recX, const int16_t* recY, const int16_t* recZ,
    uint32_t recWritten, uint32_t maxSamples, float scaleG,
    const Vec3& g_hat, const Vec3& b_h,
    uint32_t* refined_t_us, float* moved_ms, const char** why)
{
  *why = nullptr;
  uint32_t pre_us  = (uint32_t)(DET_AIC_PRE_MS * 1000.0f);
  uint32_t post_us = (uint32_t)(DET_AIC_POST_MS * 1000.0f);
  uint32_t start_us = (trigger_t_us > pre_us) ? (trigger_t_us - pre_us) : 0;
  // Bound how far back the window may reach, normally to the previous event's
  // onset. Without it two events closer together than pre_ms share a window,
  // and AIC quite correctly reports the largest transition in it - which is
  // the EARLIER event, so the second gets backdated onto the first.
  if (not_before_us > 0 && (int32_t)(start_us - not_before_us) < 0) start_us = not_before_us;
  uint32_t end_us = trigger_t_us + post_us;

  uint32_t oldest = (recWritten > maxSamples) ? (recWritten - maxSamples) : 0;

  // FIX. Count the window before filling it. The original collected samples
  // with `n < MAX_AIC` in the loop condition, so a window larger than the
  // buffer was silently cut off at its EARLY end - AIC would then be handed a
  // window with the onset near or past its edge and would report whatever it
  // found. At 833 Hz this cannot happen; at a raised ODR it would, quietly.
  uint32_t in_window = 0;
  for (uint32_t i = oldest; i < recWritten; i++) {
    uint32_t t = recT[i % maxSamples];
    if ((int32_t)(t - start_us) >= 0 && (int32_t)(t - end_us) <= 0) in_window++;
  }
  if (in_window > (uint32_t)MAX_AIC) {
    *why = "AIC window larger than the buffer (raise MAX_AIC for this ODR)";
    return false;
  }

  int n = 0;
  for (uint32_t i = oldest; i < recWritten && n < MAX_AIC; i++) {
    uint32_t slot = i % maxSamples;
    uint32_t t = recT[slot];
    if ((int32_t)(t - start_us) >= 0 && (int32_t)(t - end_us) <= 0) {
      float x = (float)recX[slot] * scaleG;
      float y = (float)recY[slot] * scaleG;
      float z = (float)recZ[slot] * scaleG;

      float a_vert = x * g_hat.x + y * g_hat.y + z * g_hat.z;
      Vec3 h_vec = { x - a_vert * g_hat.x, y - a_vert * g_hat.y, z - a_vert * g_hat.z };
      Vec3 d = { h_vec.x - b_h.x, h_vec.y - b_h.y, h_vec.z - b_h.z };
      aic_sig[n] = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
      aic_t[n] = t;
      n++;
    }
  }

  if (n < 20) { *why = "AIC: too few samples in the window"; return false; }

  // Contrast filter. AIC assumes the window really does contain a quiet part
  // followed by an active one. For an event that fires in the middle of
  // movement already in progress there is no such split, and AIC dutifully
  // reports the largest variance change inside a uniformly active window,
  // which is meaningless - it dragged one event 83 ms backwards on
  // Data/data_before_080926/accel_20260908_233329.csv. So require the window
  // to look like what AIC expects before trusting it.
  int q = (n / 4 < 4) ? 4 : (n / 4);
  double s_h = 0, sq_h = 0;
  for (int i = 0; i < q; i++) { s_h += aic_sig[i]; sq_h += aic_sig[i] * aic_sig[i]; }
  double v_head = (sq_h / q) - (s_h / q) * (s_h / q);

  double s_t = 0, sq_t = 0;
  for (int i = n - q; i < n; i++) { s_t += aic_sig[i]; sq_t += aic_sig[i] * aic_sig[i]; }
  double v_tail = (sq_t / q) - (s_t / q) * (s_t / q);

  if (v_head <= 1e-9 || v_tail <= 1e-9 || (v_tail / v_head) <= 16.0) {
    *why = "AIC: no clear onset, kept the threshold";
    return false;
  }

  // Cumulative moments let every split's variance cost O(1), so the whole
  // curve is one pass instead of n slices.
  aic_c1[0] = aic_sig[0];
  aic_c2[0] = aic_sig[0] * aic_sig[0];
  for (int i = 1; i < n; i++) {
    aic_c1[i] = aic_c1[i - 1] + aic_sig[i];
    aic_c2[i] = aic_c2[i - 1] + aic_sig[i] * aic_sig[i];
  }
  double tot1 = aic_c1[n - 1], tot2 = aic_c2[n - 1];

  double min_aic = 1e30;
  int best_k = -1;

  // Range and guard both mirror start_detector.py's aic_pick EXACTLY. Both
  // were wrong here until 2026-09-14, and only the quiet 09-11 captures
  // exposed them:
  //   - the Python sweeps np.arange(5, n - 5), whose last k is n - 6. The
  //     `k <= n - 5` written here tried one split more than the reference.
  //   - the Python's guard is `v > 0`; the 1e-9 floor written here discarded
  //     splits the Python accepts. On a quiet capture - the 09-11 set holds
  //     set at ~5 mg - the segment variances are small enough for that floor
  //     to bite, and near a shallow minimum it moved the pick by one sample.
  // Neither shows up as a crash or an obviously wrong number. Both move the
  // onset by 1.2 ms, quietly, on some captures and not others.
  for (int k = 5; k < n - 5; k++) {
    double n1 = (double)k;
    double v1 = aic_c2[k - 1] / n1 - (aic_c1[k - 1] / n1) * (aic_c1[k - 1] / n1);
    double n2 = (double)(n - k);
    double v2 = (tot2 - aic_c2[k - 1]) / n2 - ((tot1 - aic_c1[k - 1]) / n2) * ((tot1 - aic_c1[k - 1]) / n2);

    if (v1 > 0.0 && v2 > 0.0) {
      double aic_val = n1 * log(v1) + ((double)(n - k) - 1.0) * log(v2);
      if (aic_val < min_aic) {
        min_aic = aic_val;
        best_k = k;
      }
    }
  }

  // A minimum against a window edge means there was no clear transition
  // inside it, so keep the caller's own estimate rather than trust this one.
  if (best_k >= 10 && best_k <= (n - 10)) {
    *refined_t_us = aic_t[best_k];
    *moved_ms = ((int32_t)(*refined_t_us - trigger_t_us)) / 1000.0f;
    return true;
  }
  *why = "AIC: minimum against the window edge, kept the threshold";
  return false;
}


// THE VERDICT. One rule, one outcome, and a reaction time always reported.
//
// The blanking cases this function used to carry - "pre-set (settling)" and
// "rise into set (not judged)" - are gone, and not because they stopped
// mattering: the arming gate in StartDetector.h means no event can exist
// before the athlete has been measured still, so there is nothing left for
// them to describe. What used to be a verdict is now a precondition.
//
// The two false-start cases are one comparison. Moving before the gun and
// reacting in under 100 ms are the same fault - a start that cannot have been
// a response to the gun - and World Athletics treats them as one.
//
// Every comparison is a signed difference, so this stays correct across the
// ~71 minute micros() wrap. Data/accel_20260909_133119.csv is a capture that
// straddles it, so this is not theoretical: it is one file in nineteen.
inline void classifyEvent(DetectedEvent* ev, uint32_t set_us, uint32_t go_us) {
  (void)set_us;
  if (go_us == 0) {
    ev->verdict = "movement (no go marker)";
    return;
  }
  float rt = ((int32_t)(ev->t_us - go_us)) / 1000.0f;
  ev->reaction_ms = rt;
  ev->verdict = (rt < DET_FALSE_START_MS) ? "FALSE START" : "valid start";
}
