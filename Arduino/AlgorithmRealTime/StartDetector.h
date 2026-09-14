#pragma once
#include <Arduino.h>
#include <math.h>

// ===========================================================================
// StartDetector - causal STA/LTA on the gravity-projected horizontal
// magnitude. On-device port of the StartDetector in Python_Tools/start_detector.py.
//
// This file carries five corrections to the first on-device port (commit
// da47893, folders Arduino/AccelStream/ and Arduino/AlgorithmRealTime/ as they
// stood before the 09-14 consolidation - see git history). Each is recorded at
// the point it happens, marked FIX. Two of them were decided by measurement
// over the 19 captures in Data/ rather than by argument, and one further
// candidate change was REJECTED by the same measurement - see AicPicker.h,
// which deliberately keeps the original approach.
// ===========================================================================

// parameters
static const float DET_STA_MS          = 15.0f;
static const float DET_LTA_MS          = 800.0f;
static const float DET_RATIO_ON        = 6.0f;
static const float DET_RATIO_OFF       = 2.0f;
static const float DET_FLOOR_G         = 0.020f;  // 20 mg
static const float DET_CONFIRM_FLOOR_G = 0.010f;  // 10 mg
static const float DET_CONFIRM_MS      = 15.0f;
static const float DET_BASELINE_TAU_S  = 1.0f;
static const float DET_REST_MS         = 300.0f;
static const float DET_FALSE_START_MS  = 100.0f;
// DET_BLANK_MS is gone: the fixed 1000 ms blanking it named was replaced by
// the arming gate below (DET_MIN_BLANK_MS / DET_QUIET_HOLD_MS). Removing the
// name rather than leaving it unused is deliberate - a stale constant that
// still looks authoritative is how the 3 kHz beep survived a rewrite.
static const float DET_AIC_PRE_MS      = 150.0f;
static const float DET_AIC_POST_MS     = 50.0f;

// ARMING - how the judged window opens. Replaces the fixed 1000 ms blanking
// that used to do this job, and the reason is measured.
//
// At the "set" command the athlete rises into position: a real movement of
// several hundred mg. Nothing may be judged until it is over. The old rule
// gave it a flat 1000 ms and then started judging whether the athlete was
// ready or not. Over the 19 captures in Data/ - which are a real athlete on
// real blocks, board on the back of the block - settling runs to 2.65 s after
// "set", and varies from 1.1 s to 3.3 s between attempts. A clock cannot know
// that; only the signal can.
//
// So: hold for MIN_BLANK, then wait for the athlete to actually go still, then
// arm. The firmware fires "go" a random interval after THAT instant, the way a
// starter holds the gun until the field is steady. Two consequences worth
// knowing: the judged window is trustworthy by construction, and a well-drilled
// athlete gets the gun sooner - set->go median drops from 2.60 s to 2.29 s
// against the old blind random.
//
// DET_SETTLED_MG is the stillness threshold and it IS measured now: a sprinter
// holding set reads 3.1 mg median and 7.0 mg at p95 through the block, against
// a push-off of 1000-3600 mg. 15 mg is ~2x p95. It keeps its old name because
// start_detector.py's parameter is called settled_mg and the two must stay
// recognisably the same knob.
//
static const float DET_SETTLED_MG      = 15.0f;

// How long the signal must stay under that limit, continuously, to count as
// settled rather than as a lull in the middle of settling. Swept over the 19
// captures: at 200 ms with MIN_BLANK below 800 ms the detector latches onto a
// pause DURING the rise and then reports a "false start" one to two seconds
// before "go" - it happened on two captures. At 500 ms and above the athlete
// sometimes never achieves it inside the cap. 200 ms with a 800 ms floor sits
// clear of both failures: 18/18 armed, 0 early, 0 capped.
static const float DET_QUIET_HOLD_MS   = 200.0f;

// The floor. Nothing is judged and no arming may happen before this, no matter
// how still the signal looks - this is the rise into position.
static const float DET_MIN_BLANK_MS    = 800.0f;

// The ceiling. An athlete who never settles because they are already starting
// would otherwise never arm, and the mechanism would disable itself in exactly
// the case it exists to catch. On the cap we arm anyway, fire, and ANNOTATE the
// verdict - we never refuse to give one.
static const float DET_ARM_CAP_MS      = 4000.0f;

struct Vec3 {
  float x, y, z;
};

struct DetectedEvent {
  uint32_t t_us;
  uint32_t trigger_t_us;
  float horiz_mg;
  float ratio;
  float aic_moved_ms;
  bool aic_ok;
  float reaction_ms;
  const char* verdict;
  Vec3 b_h;
};

class StartDetector {
public:
  float odr_hz;
  float a_sta, a_lta, a_base;
  uint16_t confirm_n, rest_n, warmup_n;

  Vec3 g_hat;
  Vec3 b_h;
  bool baseline_frozen;
  float sta, lta;
  bool armed;
  bool candidate;
  uint16_t confirm_count;
  uint32_t sample_count;

  // FIX 1 (measured). The backdating history.
  //
  // The original used a fixed 32-slot buffer; the Python it is a port of sizes
  // it confirm_n + 5, which at 833 Hz is 18. The buffer is scanned on
  // confirmation for the first sample above the floor, and that is its entire
  // job: to undo the latency the confirmation window just cost, and nothing
  // more. A longer buffer lets the backdate reach past the confirmation into
  // an unrelated earlier excursion - one above the floor but below ratio_on,
  // such as a slow drift or a settling wobble.
  //
  // Measured over Data/: with the 32-slot buffer, accel_20260909_121238.csv
  // reports 183.79 ms instead of 299.28 ms. A 115 ms error on one capture in
  // 17, in the direction that turns a valid start into a false one.
  //
  // HIST_MAX is only the storage ceiling; hist_cap is what is actually used.
  static const int HIST_MAX = 48;
  struct HistSample {
    uint32_t t_us;
    float horiz;
  } hist[HIST_MAX];
  int hist_cap;
  int hist_head;
  int hist_len;

  static const int MAX_EVENTS = 6;
  DetectedEvent events[MAX_EVENTS];
  int num_events;

  // FIX 2. Nothing below may fail silently.
  //
  // The original returned early, with no output, both when the pre-roll was
  // too short and when the gravity estimate failed. g_hat then stayed {0,0,0}
  // from reset(), which does not disable the detector - it removes the
  // projection, so "horiz" silently becomes a high-pass of the full |a|
  // instead of the horizontal component, and the vertical/horizontal split
  // that the whole algorithm rests on is gone. The Python raises ValueError
  // with the measured |a|. Here the fault is recorded and reported with the
  // result, so a run can never be read as valid when it was not.
  const char* fault;        // nullptr when healthy
  float fault_value;        // the measured |a| when the gravity estimate failed
  bool calibrated;
  uint16_t events_dropped;  // events past MAX_EVENTS, reported rather than lost

  // The arming gate. Tracked causally as the samples arrive: one compare and
  // one max per sample, no stored trace.
  uint32_t set_us;          // 0 until the "set" beep has been stamped
  bool gate_open;           // the judged window is open; "go" may be scheduled
  uint32_t gate_t_us;       // the instant it opened
  bool gate_capped;         // opened on the cap, NOT on measured stillness
  float gate_peak_mg;       // how still the athlete actually was when it opened
  bool quiet_run;           // a run of sub-threshold samples is in progress
  uint32_t quiet_run_t0;
  float quiet_run_peak_mg;

  void begin(float odr, uint32_t set_marker_us) {
    odr_hz = odr;
    float dt = 1.0f / odr_hz;
    a_sta  = 1.0f - expf(-dt / (DET_STA_MS / 1000.0f));
    a_lta  = 1.0f - expf(-dt / (DET_LTA_MS / 1000.0f));
    a_base = 1.0f - expf(-dt / DET_BASELINE_TAU_S);

    confirm_n = (uint16_t)max(1, (int)roundf(DET_CONFIRM_MS / 1000.0f * odr_hz));
    rest_n    = (uint16_t)max(2, (int)roundf(DET_REST_MS / 1000.0f * odr_hz));
    warmup_n  = (uint16_t)max(1, (int)roundf((DET_LTA_MS / 1000.0f) * odr_hz));

    hist_cap = (int)confirm_n + 5;
    if (hist_cap > HIST_MAX) hist_cap = HIST_MAX;   // only reachable above ~2.8 kHz
    if (hist_cap < 2) hist_cap = 2;

    set_us = set_marker_us;
    reset();
  }

  void reset() {
    g_hat = {0.0f, 0.0f, 0.0f};
    b_h   = {0.0f, 0.0f, 0.0f};
    baseline_frozen = false;
    sta = 0.0f;
    lta = 1e-9f;
    armed = true;
    candidate = false;
    confirm_count = 0;
    sample_count = 0;
    hist_head = 0;
    hist_len = 0;
    num_events = 0;
    fault = nullptr;
    fault_value = 0.0f;
    calibrated = false;
    events_dropped = 0;
    gate_open = false;
    gate_t_us = 0;
    gate_capped = false;
    gate_peak_mg = 0.0f;
    quiet_run = false;
    quiet_run_t0 = 0;
    quiet_run_peak_mg = 0.0f;
  }

  // FIX 4. Gravity from running sums rather than a copied array.
  //
  // The original took three float[300] by value from the caller, which meant a
  // 3.6 KB stack frame and a silent min(rest_n, 300) truncation - fine at
  // 833 Hz, where rest_n is 250, and a quietly shortened calibration window at
  // any higher ODR. The caller now accumulates over the ring in place, so
  // there is no array, no stack cost and no ceiling.
  bool calibrateGravity(float sum_x, float sum_y, float sum_z, uint32_t count) {
    if (count < rest_n) {
      fault = "pre-roll too short for the gravity estimate";
      fault_value = (float)count;
      return false;
    }
    float sx = sum_x / (float)count;
    float sy = sum_y / (float)count;
    float sz = sum_z / (float)count;
    float n = sqrtf(sx * sx + sy * sy + sz * sz);
    if (n < 0.1f) {
      // Same test and same threshold as the Python, which raises here with the
      // message "the board was not still, or gravity is not in this data".
      fault = "resting window is not gravity: board was moving";
      fault_value = n;
      return false;
    }
    g_hat = {sx / n, sy / n, sz / n};
    b_h = {0.0f, 0.0f, 0.0f};
    calibrated = true;
    return true;
  }

  // update the detector with a new sample (t_us, x, y, z) in G units
  void update(uint32_t t_us, float x, float y, float z) {
    if (!calibrated) return;   // never run the projection against a null g_hat

    // Horizontal Acceleration
    float a_vert = x * g_hat.x + y * g_hat.y + z * g_hat.z;
    Vec3 h_vec = { x - a_vert * g_hat.x, y - a_vert * g_hat.y, z - a_vert * g_hat.z };

    if (!baseline_frozen) {
      b_h.x += a_base * (h_vec.x - b_h.x);
      b_h.y += a_base * (h_vec.y - b_h.y);
      b_h.z += a_base * (h_vec.z - b_h.z);
    }

    Vec3 d = { h_vec.x - b_h.x, h_vec.y - b_h.y, h_vec.z - b_h.z };
    float horiz = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    // STA/LTA
    float energy = horiz * horiz;
    sta += a_sta * (energy - sta);
    lta += a_lta * (energy - lta);
    float ratio = (lta > 0.0f) ? (sta / lta) : 0.0f;
    sample_count++;

    // Buffer storage
    hist_head = (hist_head + 1) % hist_cap;
    hist[hist_head] = { t_us, horiz };
    if (hist_len < hist_cap) hist_len++;

    if (sample_count <= warmup_n) return;

    // Call order matters and must match start_detector.py exactly: the arming
    // check runs AFTER this sample has entered the history, not before. With
    // it before, the gate opens one sample earlier than the Python's and the
    // backdated onset lands one sample period (1.2 ms at 833 Hz) away.
    trackArming(t_us, horiz);

    // Nothing is judged before the gate opens. The EMAs above keep running
    // throughout, so the detector is warm the instant it does.
    if (!gate_open) return;

    if (!candidate) {
      if (!armed) {
        if (ratio < DET_RATIO_OFF) {
          armed = true;
          baseline_frozen = false;
        }
        return;
      }
      if (horiz > DET_FLOOR_G && ratio > DET_RATIO_ON) {
        candidate = true;
        baseline_frozen = true;
        confirm_count = 1;
      }
      return;
    }

    // Confirmation window
    if (horiz > DET_CONFIRM_FLOOR_G) {
      confirm_count++;
    } else {
      candidate = false;
      confirm_count = 0;
      baseline_frozen = false;
      return;
    }

    if (confirm_count < confirm_n) return;

    // Backdate to the first sample in the rolling window that crossed the
    // floor: the confirmation window costs decision latency, never timestamp
    // accuracy. hist_cap bounds how far back this may reach - see FIX 1.
    uint32_t ev_t = hist[hist_head].t_us;
    float ev_h = hist[hist_head].horiz;
    for (int i = 0; i < hist_len; i++) {
      int idx = (hist_head - hist_len + 1 + i + hist_cap) % hist_cap;
      if (hist[idx].horiz > DET_FLOOR_G) {
        ev_t = hist[idx].t_us;
        ev_h = hist[idx].horiz;
        break;
      }
    }

    if (num_events < MAX_EVENTS) {
      events[num_events].t_us = ev_t;
      events[num_events].trigger_t_us = ev_t;
      events[num_events].horiz_mg = ev_h * 1000.0f;
      events[num_events].ratio = ratio;
      events[num_events].aic_ok = false;
      events[num_events].aic_moved_ms = 0.0f;
      events[num_events].reaction_ms = 0.0f;
      events[num_events].verdict = "unclassified";
      events[num_events].b_h = b_h;
      num_events++;
    } else {
      events_dropped++;   // reported, not swallowed
    }

    candidate = false;
    confirm_count = 0;
    armed = false;
  }

  // How still the athlete was at the moment the gun was armed, in mg. Reported
  // with every verdict: it is the evidence behind the arming decision, and the
  // number to look at first when a start is disputed.
  float armedAtMg() const { return gate_peak_mg; }

private:
  // Open the judged window on measured stillness, or on the cap.
  void trackArming(uint32_t t_us, float horiz) {
    if (set_us == 0 || gate_open) return;

    // Signed differences throughout, so the gate survives the ~71 minute
    // micros() wrap the way the rest of the firmware does.
    if ((int32_t)(t_us - (set_us + (uint32_t)(DET_MIN_BLANK_MS * 1000.0f))) < 0) {
      return;   // the rise into position: too early to arm on anything
    }

    float mg = horiz * 1000.0f;
    if (mg < DET_SETTLED_MG) {
      if (!quiet_run) {
        quiet_run = true;
        quiet_run_t0 = t_us;
        quiet_run_peak_mg = mg;
      } else if (mg > quiet_run_peak_mg) {
        quiet_run_peak_mg = mg;
      }
      if ((int32_t)(t_us - (quiet_run_t0 + (uint32_t)(DET_QUIET_HOLD_MS * 1000.0f))) >= 0) {
        openGate(t_us, quiet_run_peak_mg, false);
        return;
      }
    } else {
      quiet_run = false;   // a lull, not a hold
    }

    if ((int32_t)(t_us - (set_us + (uint32_t)(DET_ARM_CAP_MS * 1000.0f))) >= 0) {
      openGate(t_us, mg, true);
    }
  }

  void openGate(uint32_t t_us, float peak_mg, bool capped) {
    gate_open = true;
    gate_t_us = t_us;
    gate_capped = capped;
    gate_peak_mg = peak_mg;
    // Ready immediately: the hold that just opened the gate is itself proof
    // the ratio has fallen back, so there is nothing to wait for.
    armed = true;
    candidate = false;
    confirm_count = 0;
    baseline_frozen = false;
  }
};
