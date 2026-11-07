#include "hit_detector.h"
#include "app_config.h"
#include <string.h>

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) b = a;
  return b;
}

static uint16_t filtered_sample(HitDetector *d, uint8_t ch, uint16_t raw)
{
  uint8_t i = d->filter_index[ch];
  d->raw[ch] = raw;
  d->filtered[ch][i] = raw;
  d->filter_index[ch] = (uint8_t)((i + 1u) % 3u);
  if (d->filter_count[ch] < 3u) ++d->filter_count[ch];
  if (d->filter_count[ch] < 3u) return raw;
  return median3(d->filtered[ch][0], d->filtered[ch][1], d->filtered[ch][2]);
}

void HitDetector_DefaultConfig(HitDetectorConfig *c)
{
  c->calibration_ms = APP_CALIBRATION_MS;
  c->calibration_max_range = APP_CALIBRATION_MAX_RANGE;
  c->start_delta = APP_HIT_START_DELTA;
  c->min_peak = APP_HIT_MIN_PEAK;
  c->rise_delta = APP_HIT_RISE_DELTA;
  c->release_delta = APP_HIT_RELEASE_DELTA;
  c->min_width_samples = APP_HIT_MIN_WIDTH_SAMPLES;
  c->max_width_samples = APP_HIT_MAX_WIDTH_SAMPLES;
  c->release_stable_samples = APP_HIT_RELEASE_STABLE_MS * 10u;
  c->group_samples = APP_HIT_GROUP_MS * 10u;
  c->lockout_samples = APP_HIT_LOCKOUT_MS * 10u;
  c->baseline_track_shift = APP_BASELINE_TRACK_SHIFT;
}

void HitDetector_Init(HitDetector *d)
{
  memset(d, 0, sizeof(*d));
  HitDetector_Reset(d);
}

void HitDetector_Reset(HitDetector *d)
{
  uint8_t i;
  memset(d, 0, sizeof(*d));
  d->calibration_status = HIT_CALIBRATION_RUNNING;
  for (i = 0u; i < HIT_SENSOR_COUNT; ++i) {
    d->state[i] = HIT_STATE_CALIBRATING;
    d->calibration_min[i] = 4095u;
  }
}

static void reset_calibration(HitDetector *d)
{
  uint8_t i;
  d->calibration_samples = 0u;
  for (i = 0u; i < HIT_SENSOR_COUNT; ++i) {
    d->calibration_sum[i] = 0u;
    d->calibration_min[i] = 4095u;
    d->calibration_max[i] = 0u;
  }
}

void HitDetector_Process(HitDetector *d, const HitDetectorConfig *c,
                         const uint16_t raw[HIT_SENSOR_COUNT],
                         uint32_t timestamp_ms, HitEvent *event,
                         uint8_t *event_ready)
{
  uint8_t ch;
  uint8_t any_active = 0u;
  uint32_t now = d->sample_index++;
  *event_ready = 0u;

  if (d->calibration_status != HIT_CALIBRATION_READY) {
    uint8_t unstable = 0u;
    for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch) {
      uint16_t v = filtered_sample(d, ch, raw[ch]);
      if (v < d->calibration_min[ch]) d->calibration_min[ch] = v;
      if (v > d->calibration_max[ch]) d->calibration_max[ch] = v;
      d->calibration_sum[ch] += v;
      if ((uint16_t)(d->calibration_max[ch] - d->calibration_min[ch]) >
          c->calibration_max_range) unstable = 1u;
    }
    if (unstable) {
      reset_calibration(d);
      d->calibration_status = HIT_CALIBRATION_UNSTABLE;
      return;
    }
    d->calibration_status = HIT_CALIBRATION_RUNNING;
    if (++d->calibration_samples >= (uint32_t)c->calibration_ms * 10u) {
      for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch) {
        d->baseline[ch] = (int32_t)(d->calibration_sum[ch] /
                                     d->calibration_samples);
        d->delta[ch] = 0;
        d->state[ch] = HIT_STATE_IDLE;
      }
      d->calibration_status = HIT_CALIBRATION_READY;
    }
    return;
  }

  for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch) {
    uint16_t v = filtered_sample(d, ch, raw[ch]);
    int16_t delta_2_samples_ago = d->delta_history[ch][1];
    int32_t delta = (int32_t)v - d->baseline[ch];
    if (delta > 32767) delta = 32767;
    if (delta < -32768) delta = -32768;
    d->delta[ch] = (int16_t)delta;
    d->delta_history[ch][1] = d->delta_history[ch][0];
    d->delta_history[ch][0] = d->delta[ch];

    switch ((HitDetectorState)d->state[ch]) {
      case HIT_STATE_IDLE:
        if (d->delta[ch] >= (int16_t)c->start_delta) {
          d->state[ch] = HIT_STATE_CANDIDATE;
          d->onset_sample[ch] = now;
          d->onset_delta[ch] = (int16_t)(d->delta[ch] - delta_2_samples_ago);
          d->peak[ch] = (uint16_t)d->delta[ch];
          d->state_start[ch] = timestamp_ms;
          if (!d->group_active) {
            d->group_active = 1u;
            d->group_start = now;
            d->group_valid = 0u;
          }
        } else if (d->delta[ch] < (int16_t)c->release_delta) {
          int32_t error = (int32_t)v - d->baseline[ch];
          int32_t step = (int32_t)(1UL << c->baseline_track_shift);
          d->baseline_track_accum[ch] += error;
          if (d->baseline_track_accum[ch] >= step) {
            ++d->baseline[ch];
            d->baseline_track_accum[ch] -= step;
          } else if (d->baseline_track_accum[ch] <= -step) {
            --d->baseline[ch];
            d->baseline_track_accum[ch] += step;
          }
        }
        break;

      case HIT_STATE_CANDIDATE:
        if (d->delta[ch] > (int16_t)d->peak[ch])
          d->peak[ch] = (uint16_t)d->delta[ch];
        if (d->delta[ch] < (int16_t)c->release_delta) {
          if (d->release_start[ch] == 0u) d->release_start[ch] = now;
          if ((now - d->release_start[ch]) >= c->release_stable_samples) {
            uint32_t width = d->release_start[ch] - d->onset_sample[ch];
            uint8_t rise_ok = (uint8_t)(d->onset_delta[ch] >=
                                        (int16_t)c->rise_delta);
            if (width >= c->min_width_samples &&
                width <= c->max_width_samples &&
                d->peak[ch] >= c->min_peak && rise_ok) {
              uint8_t ring = APP_SENSOR_RING[ch];
              if (!d->group_valid || d->peak[ch] > d->group_peak ||
                  (d->peak[ch] == d->group_peak && ring > d->group_ring)) {
                d->group_valid = 1u;
                d->group_peak = d->peak[ch];
                d->group_ring = ring;
                d->group_timestamp = d->state_start[ch];
              }
            }
            d->state[ch] = HIT_STATE_IDLE;
            d->release_start[ch] = 0u;
          }
        } else {
          d->release_start[ch] = 0u;
          if ((now - d->onset_sample[ch]) > c->max_width_samples) {
            d->state[ch] = HIT_STATE_WAIT_RELEASE;
            d->release_start[ch] = 0u;
          }
        }
        break;

      case HIT_STATE_WAIT_RELEASE:
        if (d->delta[ch] < (int16_t)c->release_delta) {
          if (d->release_start[ch] == 0u) d->release_start[ch] = now;
          if ((now - d->release_start[ch]) >= c->release_stable_samples) {
            d->state[ch] = HIT_STATE_IDLE;
            d->release_start[ch] = 0u;
          }
        } else {
          d->release_start[ch] = 0u;
        }
        break;

      case HIT_STATE_LOCKED:
      default:
        break;
    }
    if (d->state[ch] == HIT_STATE_CANDIDATE ||
        d->state[ch] == HIT_STATE_WAIT_RELEASE) any_active = 1u;
  }

  if (d->group_active && !any_active &&
      (now - d->group_start) >= c->group_samples) {
    if (d->group_valid && (int32_t)(now - d->lockout_until) >= 0) {
      event->ring = d->group_ring;
      event->peak = d->group_peak;
      event->timestamp_ms = d->group_timestamp;
      *event_ready = 1u;
      d->lockout_until = now + c->lockout_samples;
      d->locked = 1u;
      d->group_release_start = 0u;
      for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch)
        d->state[ch] = HIT_STATE_LOCKED;
    }
    d->group_active = 0u;
    d->group_valid = 0u;
  }

  if (d->locked) {
    uint8_t all_released = 1u;
    for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch) {
      if (d->delta[ch] >= (int16_t)c->release_delta) all_released = 0u;
    }
    if ((int32_t)(now - d->lockout_until) >= 0 && all_released) {
      if (d->group_release_start == 0u) d->group_release_start = now;
      if ((now - d->group_release_start) >= c->release_stable_samples) {
        for (ch = 0u; ch < HIT_SENSOR_COUNT; ++ch) {
          d->state[ch] = HIT_STATE_IDLE;
        }
        d->locked = 0u;
        d->group_release_start = 0u;
      }
    } else {
      d->group_release_start = 0u;
    }
    return;
  }
}

void HitDetector_GetSnapshot(const HitDetector *d,
                             HitSensorSnapshot out[HIT_SENSOR_COUNT])
{
  uint8_t i;
  for (i = 0u; i < HIT_SENSOR_COUNT; ++i) {
    out[i].raw = d->raw[i];
    out[i].baseline = d->baseline[i];
    out[i].delta = d->delta[i];
    out[i].peak = d->peak[i];
    out[i].state = d->state[i];
  }
}

uint8_t HitDetector_GetCalibrationStatus(const HitDetector *d)
{
  return d->calibration_status;
}
