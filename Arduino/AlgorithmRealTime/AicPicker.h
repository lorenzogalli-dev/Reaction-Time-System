#pragma once
#include "StartDetector.h"

inline bool refineOnsetAIC(
    uint32_t trigger_t_us, uint32_t not_before_us,
    const uint32_t* recT, const int16_t* recX, const int16_t* recY, const int16_t* recZ,
    uint32_t recWritten, uint32_t maxSamples, float scaleG,
    const Vec3& g_hat, const Vec3& b_h,
    uint32_t* refined_t_us, float* moved_ms)
{
  uint32_t pre_us  = (uint32_t)(DET_AIC_PRE_MS * 1000.0f);
  uint32_t post_us = (uint32_t)(DET_AIC_POST_MS * 1000.0f);
  uint32_t start_us = (trigger_t_us > pre_us) ? (trigger_t_us - pre_us) : 0;
  if (not_before_us > 0 && start_us < not_before_us) start_us = not_before_us;
  uint32_t end_us = trigger_t_us + post_us;

  static const int MAX_AIC = 256;
  float sig[MAX_AIC];
  uint32_t t_sig[MAX_AIC];
  int n = 0;

  uint32_t oldest = (recWritten > maxSamples) ? (recWritten - maxSamples) : 0;
  for (uint32_t i = oldest; i < recWritten && n < MAX_AIC; i++) {
    uint32_t slot = i % maxSamples;
    uint32_t t = recT[slot];
    if (t >= start_us && t <= end_us) {
      float x = (float)recX[slot] * scaleG;
      float y = (float)recY[slot] * scaleG;
      float z = (float)recZ[slot] * scaleG;

      float a_vert = x * g_hat.x + y * g_hat.y + z * g_hat.z;
      Vec3 h_vec = { x - a_vert * g_hat.x, y - a_vert * g_hat.y, z - a_vert * g_hat.z };
      Vec3 d = { h_vec.x - b_h.x, h_vec.y - b_h.y, h_vec.z - b_h.z };
      sig[n] = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
      t_sig[n] = t;
      n++;
    }
  }

  if (n < 20) return false;

  // Contrast Filter
  int q = (n / 4 < 4) ? 4 : (n / 4);
  float s_h = 0, sq_h = 0;
  for (int i = 0; i < q; i++) { s_h += sig[i]; sq_h += sig[i] * sig[i]; }
  float v_head = (sq_h / q) - (s_h / q) * (s_h / q);

  float s_t = 0, sq_t = 0;
  for (int i = n - q; i < n; i++) { s_t += sig[i]; sq_t += sig[i] * sig[i]; }
  float v_tail = (sq_t / q) - (s_t / q) * (s_t / q);

  if (v_head <= 1e-9f || v_tail <= 1e-9f || (v_tail / v_head) <= 16.0f) {
    return false;
  }

  // Cumulative sums for AIC
  static float c1[MAX_AIC], c2[MAX_AIC];
  c1[0] = sig[0];
  c2[0] = sig[0] * sig[0];
  for (int i = 1; i < n; i++) {
    c1[i] = c1[i - 1] + sig[i];
    c2[i] = c2[i - 1] + sig[i] * sig[i];
  }
  float tot1 = c1[n - 1], tot2 = c2[n - 1];

  float min_aic = 1e30f;
  int best_k = -1;

  for (int k = 5; k <= n - 5; k++) {
    float n1 = (float)k;
    float v1 = c2[k - 1] / n1 - (c1[k - 1] / n1) * (c1[k - 1] / n1);
    float n2 = (float)(n - k);
    float v2 = (tot2 - c2[k - 1]) / n2 - ((tot1 - c1[k - 1]) / n2) * ((tot1 - c1[k - 1]) / n2);

    if (v1 > 1e-9f && v2 > 1e-9f) {
      float aic_val = n1 * logf(v1) + (n2 - 1.0f) * logf(v2);
      if (aic_val < min_aic) {
        min_aic = aic_val;
        best_k = k;
      }
    }
  }

  if (best_k >= 10 && best_k <= (n - 10)) {
    *refined_t_us = t_sig[best_k];
    *moved_ms = ((int32_t)(*refined_t_us - trigger_t_us)) / 1000.0f;
    return true;
  }
  return false;
}

// Event exact time
inline void classifyEvent(DetectedEvent* ev, uint32_t set_us, uint32_t go_us) {
  if (go_us == 0) {
    ev->verdict = "movement";
    return;
  }
  if (set_us != 0 && ev->t_us < set_us) {
    ev->verdict = "pre-set (settling)";
    return;
  }
  if (set_us != 0 && (ev->t_us - set_us) < (uint32_t)(DET_BLANK_MS * 1000.0f)) {
    ev->verdict = "rise into set (not judged)";
    return;
  }

  float rt = ((int32_t)(ev->t_us - go_us)) / 1000.0f;
  ev->reaction_ms = rt;

  if (ev->t_us < go_us) {
    ev->verdict = "FALSE START (moved before go)";
  } else if (rt < DET_FALSE_START_MS) {
    ev->verdict = "FALSE START (reacted < 100ms)";
  } else {
    ev->verdict = "valid start";
  }
}