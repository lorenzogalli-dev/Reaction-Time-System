#pragma once
#include <Arduino.h>
#include <math.h>

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
static const float DET_BLANK_MS        = 1000.0f;
static const float DET_AIC_PRE_MS      = 150.0f;
static const float DET_AIC_POST_MS     = 50.0f;

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

  // history
  static const int HIST_MAX = 32;
  struct HistSample {
    uint32_t t_us;
    float horiz;
  } hist[HIST_MAX];
  int hist_head;
  int hist_len;

  static const int MAX_EVENTS = 6;
  DetectedEvent events[MAX_EVENTS];
  int num_events;

  void begin(float odr) {
    odr_hz = odr;
    float dt = 1.0f / odr_hz;
    a_sta  = 1.0f - expf(-dt / (DET_STA_MS / 1000.0f));
    a_lta  = 1.0f - expf(-dt / (DET_LTA_MS / 1000.0f));
    a_base = 1.0f - expf(-dt / DET_BASELINE_TAU_S);

    confirm_n = (uint16_t)max(1, (int)roundf(DET_CONFIRM_MS / 1000.0f * odr_hz));
    rest_n    = (uint16_t)max(2, (int)roundf(DET_REST_MS / 1000.0f * odr_hz));
    warmup_n  = (uint16_t)max(1, (int)roundf((DET_LTA_MS / 1000.0f) * odr_hz));

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
  }

  bool calibrateGravity(const float* xs, const float* ys, const float* zs, uint32_t count) {
    if (count < rest_n) return false;
    float sx = 0, sy = 0, sz = 0;
    for (uint32_t i = 0; i < rest_n; i++) {
      sx += xs[i]; sy += ys[i]; sz += zs[i];
    }
    sx /= rest_n; sy /= rest_n; sz /= rest_n;
    float n = sqrtf(sx * sx + sy * sy + sz * sz);
    if (n < 0.1f) return false;

    g_hat = {sx / n, sy / n, sz / n};
    b_h = {0.0f, 0.0f, 0.0f};
    return true;
  }

  // update the detector with a new sample (t_us, x, y, z) in G units
  void update(uint32_t t_us, float x, float y, float z) {
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
    hist_head = (hist_head + 1) % HIST_MAX;
    hist[hist_head] = { t_us, horiz };
    if (hist_len < HIST_MAX) hist_len++;

    if (sample_count <= warmup_n) return;

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

    // Go back to when the ratio first exceeded DET_RATIO_ON, or to the last sample above DET_FLOOR_G if that is later.
    uint32_t ev_t = hist[hist_head].t_us;
    float ev_h = hist[hist_head].horiz;
    for (int i = 0; i < hist_len; i++) {
      int idx = (hist_head - hist_len + 1 + i + HIST_MAX) % HIST_MAX;
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
    }

    candidate = false;
    confirm_count = 0;
    armed = false;
  }
};