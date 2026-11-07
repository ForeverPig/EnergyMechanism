#include "detector_defaults.h"
#include "hit_detector.h"
#include <assert.h>
#include <stdio.h>
static const uint8_t rings[10] = {6, 7, 8, 9, 10, 5, 4, 3, 1, 2};
static HitDetector detector;
static HitDetectorConfig detector_config;
static uint16_t raw[10];
static uint32_t now_ms;
static HitEvent last_event;
static uint32_t event_count;
static uint8_t step(void)
{
  HitEvent event;
  uint8_t ready;
  HitDetector_Process(&detector, &detector_config, raw, now_ms, &event, &ready);
  if (ready)
  {
    last_event = event;
    ++event_count;
  }
  return ready;
}

static void reset_detector(void)
{
  uint16_t i;
  HitDetector_Init(&detector, rings);
  HitDetector_DefaultConfig(&detector_config);
  for (i = 0u; i < HIT_SENSOR_COUNT; ++i)
    raw[i] = 1000u;
  now_ms = 0u;
  event_count = 0u;
  for (i = 0u; i < 5000u; ++i)
  {
    (void)step();
    if ((i % 10u) == 9u)
      ++now_ms;
  }
  assert(HitDetector_GetCalibrationStatus(&detector) == HIT_CALIBRATION_READY);
}

static void pulse(uint8_t channel, uint16_t amplitude, uint16_t width)
{
  uint16_t i;
  for (i = 0u; i < width; ++i)
  {
    raw[channel] = (uint16_t)(1000u + amplitude);
    (void)step();
    if ((i % 10u) == 9u)
      ++now_ms;
  }
  raw[channel] = 1000u;
  for (i = 0u; i < 45u; ++i)
  {
    (void)step();
    if ((i % 10u) == 9u)
      ++now_ms;
  }
}

static uint8_t drain_event(HitEvent *last)
{
  uint8_t found = 0u;
  uint16_t i;
  for (i = 0u; i < 100u; ++i)
  {
    HitEvent event;
    uint8_t ready;
    HitDetector_Process(&detector, &detector_config, raw, now_ms, &event, &ready);
    if (ready)
    {
      *last = event;
      found = 1u;
    }
    if ((i % 10u) == 9u)
      ++now_ms;
  }
  return found;
}

static void test_valid_multi_ring_hit_selects_one_ring(void)
{
  HitEvent event;
  uint16_t i;
  uint8_t emitted = 0u;
  reset_detector();
  for (i = 0u; i < 12u; ++i)
  {
    raw[0] = 1300u;
    raw[1] = 1300u;
    (void)step();
    if ((i % 10u) == 9u)
      ++now_ms;
  }
  raw[0] = raw[1] = 1000u;
  for (i = 0u; i < 60u; ++i)
  {
    HitEvent current;
    uint8_t ready;
    HitDetector_Process(&detector, &detector_config, raw, now_ms, &current, &ready);
    if (ready)
    {
      event = current;
      ++emitted;
    }
    if ((i % 10u) == 9u)
      ++now_ms;
  }
  assert(emitted == 1u);
  assert(event.ring == 7u);
  assert(event.peak >= APP_HIT_MIN_PEAK);
}

static void test_single_sample_noise_is_rejected(void)
{
  HitEvent event;
  uint8_t ch;
  reset_detector();
  for (ch = 0u; ch < 10u; ++ch)
  {
    raw[0] = ch == 4u ? 1500u : 1000u;
    (void)step();
    raw[0] = 1000u;
    (void)step();
  }
  assert(!drain_event(&event));
}

static void test_slow_press_and_long_hold_are_rejected(void)
{
  HitEvent event;
  uint16_t i;
  reset_detector();
  for (i = 0u; i < 35u; ++i)
  {
    raw[0] = (uint16_t)(1000u + i * 10u);
    (void)step();
  }
  raw[0] = 1000u;
  for (i = 0u; i < 60u; ++i)
    (void)step();
  assert(!drain_event(&event));

  reset_detector();
  raw[0] = 1400u;
  for (i = 0u; i < APP_HIT_MAX_WIDTH_SAMPLES + 50u; ++i)
    (void)step();
  raw[0] = 1000u;
  for (i = 0u; i < 60u; ++i)
    (void)step();
  assert(!drain_event(&event));
}

static void test_slow_baseline_drift_is_tracked(void)
{
  HitEvent event;
  HitSensorSnapshot snapshot[HIT_SENSOR_COUNT];
  uint32_t i;
  reset_detector();
  for (i = 0u; i < 20000u; ++i)
  {
    raw[0] = (uint16_t)(1000u + i * 100u / 20000u);
    (void)step();
  }
  HitDetector_GetSnapshot(&detector, snapshot);
  assert(snapshot[0].baseline > 1050);
  assert(!drain_event(&event));
}

static void test_lockout_allows_a_later_hit_after_release(void)
{
  HitEvent event;
  reset_detector();
  pulse(0u, 300u, 12u);
  assert(event_count == 1u);
  event = last_event;
  assert(event.ring == 6u);
  event_count = 0u;
  pulse(1u, 300u, 12u);
  assert(event_count == 0u);
  {
    uint16_t i;
    for (i = 0u; i < 850u; ++i)
    {
      (void)step();
      if ((i % 10u) == 9u)
        ++now_ms;
    }
  }
  event_count = 0u;
  pulse(1u, 300u, 12u);
  assert(event_count == 1u);
  event = last_event;
  assert(event.ring == 7u);
}

int main(void)
{
  test_valid_multi_ring_hit_selects_one_ring();
  test_single_sample_noise_is_rejected();
  test_slow_press_and_long_hold_are_rejected();
  test_slow_baseline_drift_is_tracked();
  test_lockout_allows_a_later_hit_after_release();
  for (unsigned ch = 0; ch < 10; ch++)
  {
    reset_detector();
    pulse(ch, 300, 12);
    assert(event_count == 1 && last_event.ring == rings[ch]);
  }
  reset_detector();
  HitDetector_Reset(&detector);
  for (unsigned i = 0; i < 10; i++)
    assert(detector.rings[i] == rings[i]);
  assert(HitDetector_GetCalibrationStatus(&detector) == HIT_CALIBRATION_RUNNING);
  reset_detector();
  detector.sample_index = 0xfffffff0u;
  now_ms = 0xfffffff0u;
  pulse(0, 300, 12);
  assert(event_count == 1);
  puts("PASS: reference hit/noise/hold/drift/lockout cases, all 10 new-board rings, reset and "
       "rollover");
  return 0;
}
