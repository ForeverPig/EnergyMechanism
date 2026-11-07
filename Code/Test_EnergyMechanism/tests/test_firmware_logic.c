#include "app_config.h"
#include "app_adc_dma.h"
#include "hit_detector.h"
#include "led_controller.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static HitDetector detector;
static HitDetectorConfig detector_config;
static uint16_t raw[HIT_SENSOR_COUNT];
static uint32_t now_ms;
static HitEvent last_event;
static uint32_t event_count;

static void test_adc_dma_packed_scan_halves(void)
{
  uint16_t dma[APP_ADC_DMA_FRAMES * APP_ADC_MAX_RANKS];
  uint16_t out[APP_ADC_DMA_HALF_FRAMES][APP_ADC_MAX_RANKS];
  uint8_t ranks, half;
  uint16_t frame, rank;
  for (ranks = 2u; ranks <= 4u; ranks += 2u) {
    for (frame = 0u; frame < APP_ADC_DMA_FRAMES * APP_ADC_MAX_RANKS; ++frame)
      dma[frame] = 0xA5A5u; /* Unwritten capacity must never be read for ADC3. */
    for (frame = 0u; frame < APP_ADC_DMA_FRAMES; ++frame)
      for (rank = 0u; rank < ranks; ++rank)
        dma[frame * ranks + rank] = (uint16_t)(1000u + frame * 10u + rank);
    for (half = 0u; half < 2u; ++half) {
      for (frame = 0u; frame < APP_ADC_DMA_HALF_FRAMES; ++frame)
        for (rank = 0u; rank < APP_ADC_MAX_RANKS; ++rank) out[frame][rank] = 0xFFFFu;
      AppAdc_CopyDmaHalf(out, dma, ranks, half);
      for (frame = 0u; frame < APP_ADC_DMA_HALF_FRAMES; ++frame) {
        uint16_t source_frame = (uint16_t)(frame + half * APP_ADC_DMA_HALF_FRAMES);
        for (rank = 0u; rank < ranks; ++rank)
          assert(out[frame][rank] == 1000u + source_frame * 10u + rank);
        for (rank = ranks; rank < APP_ADC_MAX_RANKS; ++rank)
          assert(out[frame][rank] == 0xFFFFu);
      }
    }
  }
}

static void test_adc3_stable_press_can_complete_calibration(void)
{
  uint16_t dma[APP_ADC_DMA_FRAMES * APP_ADC_MAX_RANKS] = {0};
  uint16_t out[APP_ADC_DMA_HALF_FRAMES][APP_ADC_MAX_RANKS];
  uint16_t frame;
  uint32_t samples = 0u;
  HitEvent event;
  uint8_t ready, half;
  HitDetector_Init(&detector);
  HitDetector_DefaultConfig(&detector_config);
  for (frame = 0u; frame < HIT_SENSOR_COUNT; ++frame) raw[frame] = 10u;
  for (frame = 0u; frame < APP_ADC_DMA_FRAMES; ++frame) {
    dma[frame * 2u] = 1500u;
    dma[frame * 2u + 1u] = 1900u;
  }
  /* Reproduce both DMA callbacks repeatedly, then feed the detector.
   * A four-rank source stride injects zero blocks and prevents calibration. */
  while (samples < APP_CALIBRATION_MS * 10u + APP_ADC_DMA_FRAMES) {
    for (half = 0u; half < 2u; ++half) {
      AppAdc_CopyDmaHalf(out, dma, 2u, half);
      for (frame = 0u; frame < APP_ADC_DMA_HALF_FRAMES; ++frame) {
        raw[8] = out[frame][0];
        raw[9] = out[frame][1];
        HitDetector_Process(&detector, &detector_config, raw, samples / 10u,
                            &event, &ready);
        assert(!ready);
        ++samples;
      }
    }
  }
  assert(HitDetector_GetCalibrationStatus(&detector) == HIT_CALIBRATION_READY);
  assert(detector.baseline[8] == 1500 && detector.baseline[9] == 1900);
}

static uint8_t step(void)
{
  HitEvent event;
  uint8_t ready;
  HitDetector_Process(&detector, &detector_config, raw, now_ms, &event, &ready);
  if (ready) { last_event = event; ++event_count; }
  return ready;
}

static void reset_detector(void)
{
  uint16_t i;
  HitDetector_Init(&detector);
  HitDetector_DefaultConfig(&detector_config);
  for (i = 0u; i < HIT_SENSOR_COUNT; ++i) raw[i] = 1000u;
  now_ms = 0u;
  event_count = 0u;
  for (i = 0u; i < 5000u; ++i) {
    (void)step();
    if ((i % 10u) == 9u) ++now_ms;
  }
  assert(HitDetector_GetCalibrationStatus(&detector) == HIT_CALIBRATION_READY);
}

static void pulse(uint8_t channel, uint16_t amplitude, uint16_t width)
{
  uint16_t i;
  for (i = 0u; i < width; ++i) {
    raw[channel] = (uint16_t)(1000u + amplitude);
    (void)step();
    if ((i % 10u) == 9u) ++now_ms;
  }
  raw[channel] = 1000u;
  for (i = 0u; i < 45u; ++i) {
    (void)step();
    if ((i % 10u) == 9u) ++now_ms;
  }
}

static uint8_t drain_event(HitEvent *last)
{
  uint8_t found = 0u;
  uint16_t i;
  for (i = 0u; i < 100u; ++i) {
    HitEvent event;
    uint8_t ready;
    HitDetector_Process(&detector, &detector_config, raw, now_ms,
                        &event, &ready);
    if (ready) { *last = event; found = 1u; }
    if ((i % 10u) == 9u) ++now_ms;
  }
  return found;
}

static void test_valid_multi_ring_hit_selects_one_ring(void)
{
  HitEvent event;
  uint16_t i;
  uint8_t emitted = 0u;
  reset_detector();
  for (i = 0u; i < 12u; ++i) {
    raw[0] = 1300u;
    raw[1] = 1300u;
    (void)step();
    if ((i % 10u) == 9u) ++now_ms;
  }
  raw[0] = raw[1] = 1000u;
  for (i = 0u; i < 60u; ++i) {
    HitEvent current;
    uint8_t ready;
    HitDetector_Process(&detector, &detector_config, raw, now_ms,
                        &current, &ready);
    if (ready) { event = current; ++emitted; }
    if ((i % 10u) == 9u) ++now_ms;
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
  for (ch = 0u; ch < 10u; ++ch) {
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
  for (i = 0u; i < 35u; ++i) {
    raw[0] = (uint16_t)(1000u + i * 10u);
    (void)step();
  }
  raw[0] = 1000u;
  for (i = 0u; i < 60u; ++i) (void)step();
  assert(!drain_event(&event));

  reset_detector();
  raw[0] = 1400u;
  for (i = 0u; i < APP_HIT_MAX_WIDTH_SAMPLES + 50u; ++i) (void)step();
  raw[0] = 1000u;
  for (i = 0u; i < 60u; ++i) (void)step();
  assert(!drain_event(&event));
}

static void test_slow_baseline_drift_is_tracked(void)
{
  HitEvent event;
  HitSensorSnapshot snapshot[HIT_SENSOR_COUNT];
  uint32_t i;
  reset_detector();
  for (i = 0u; i < 20000u; ++i) {
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
    for (i = 0u; i < 850u; ++i) {
      (void)step();
      if ((i % 10u) == 9u) ++now_ms;
    }
  }
  event_count = 0u;
  pulse(1u, 300u, 12u);
  assert(event_count == 1u);
  event = last_event;
  assert(event.ring == 7u);
}

static void test_led_mapping_and_brightness(void)
{
  LedController c;
  uint16_t led;
  uint8_t grb[LED_CONTROLLER_MAX_LEDS][3];
  LedController_Init(&c);
  assert(APP_LED_COUNT == 320u && APP_LED_RING_LED_COUNT == 30u);
  assert(LedController_Update(&c, grb));
  assert(grb[0][0] == 0u && grb[0][1] == 255u && grb[0][2] == 0u);
  assert(grb[4][1] == 255u && grb[13][1] == 255u && grb[14][1] == 255u);
  assert(LedController_SetRingColor(&c, 9u, 0u, 0u, 255u));
  assert(LedController_Update(&c, grb));
  assert(grb[4][0] == 0u && grb[4][1] == 0u && grb[4][2] == 255u);
  assert(grb[3][1] == 255u && grb[14][1] == 255u);
  assert(LedController_SetRingBrightness(&c, 10u, 0u));
  assert(LedController_SetRingBrightness(&c, 8u, 0u));
  assert(LedController_Update(&c, grb));
  assert(grb[3][1] == 0u);
  assert(grb[4][2] == 255u && grb[13][2] == 255u);
  assert(grb[14][1] == 0u && grb[29][1] == 0u);
  assert(grb[30][1] == 0u && grb[319][1] == 0u);
  LedController_StartAnimation(&c, 0u);
  LedController_ProcessAnimation(&c, 14000u);
  LedController_SetSolidColor(&c, 255u, 0u, 0u);
  assert(LedController_Update(&c, grb));
  for (led = 0u; led < APP_LED_COUNT; ++led)
    assert(grb[led][0] == 0u && grb[led][1] == 255u && grb[led][2] == 0u);
  LedController_ProcessAnimation(&c, 100000u);
  assert(!LedController_Update(&c, grb));
}

static void assert_led_animation_frame(const uint8_t grb[LED_CONTROLLER_MAX_LEDS][3],
                                        uint16_t step)
{
  uint16_t led;
  for (led = 0u; led < APP_LED_COUNT; ++led) {
    uint8_t lit = 0u;
    if (step < 10u) {
      switch (step % 5u) {
        case 0u: lit = (uint8_t)(led < 30u); break;
        case 1u: lit = (uint8_t)(led >= 14u && led < 30u); break;
        case 2u: lit = (uint8_t)(led >= 4u && led < 14u); break;
        case 3u: lit = (uint8_t)(led < 4u); break;
        case 4u: lit = (uint8_t)(led == 0u || led == 2u || led == 4u ||
                                led == 9u || led == 14u || led == 22u); break;
        default: assert(0); break;
      }
    } else if (step < 12u) {
      lit = 1u;
    } else {
      uint16_t first = (uint16_t)((step - 12u) * 8u);
      lit = (uint8_t)(led >= first && led < first + 8u);
    }
    assert(grb[led][0] == 0u);
    assert(grb[led][1] == (lit && (step < 5u || step == 10u) ? 255u : 0u));
    assert(grb[led][2] == (lit && !(step < 5u || step == 10u) ? 255u : 0u));
  }
}

static void test_led_animation_320_complete_sequence(void)
{
  LedController c;
  uint8_t grb[LED_CONTROLLER_MAX_LEDS][3];
  uint16_t step;
  LedController_Init(&c);
  LedController_StartAnimation(&c, 1000u);
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 0u);
  for (step = 1u; step <= 104u; ++step) {
    uint32_t due = 1000u + (uint32_t)step * 1000u;
    LedController_ProcessAnimation(&c, due - 1u);
    assert(!LedController_Update(&c, grb));
    LedController_ProcessAnimation(&c, due);
    assert(LedController_Update(&c, grb));
    assert_led_animation_frame((const uint8_t (*)[3])grb, step % 52u);
  }
}

static void test_led_animation_wraparound_and_pending_frame(void)
{
  LedController c;
  uint8_t grb[LED_CONTROLLER_MAX_LEDS][3];
  uint32_t start = UINT32_MAX - 1000u;
  LedController_Init(&c);
  LedController_StartAnimation(&c, start);
  assert(LedController_Update(&c, grb));
  LedController_ProcessAnimation(&c, start + 999u);
  assert(!LedController_Update(&c, grb));
  LedController_ProcessAnimation(&c, start + 2000u); /* Late: skip to ring9. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 2u);
  LedController_ProcessAnimation(&c, start + 4000u);
  /* Pending animation changes leave the last rendered/DMA frame untouched. */
  assert_led_animation_frame((const uint8_t (*)[3])grb, 2u);
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 4u);
  LedController_SetGlobalBrightness(&c, 64u);
  assert(LedController_SetRingColor(&c, 9u, 0u, 0u, 255u));
  assert(LedController_Update(&c, grb));
  assert(grb[4][2] == 64u && grb[9][2] == 64u);
  assert(grb[0][1] == 64u && grb[2][1] == 64u);
  assert(grb[14][1] == 64u && grb[22][1] == 64u);
  assert(grb[1][1] == 0u && grb[5][2] == 0u && grb[29][1] == 0u);
  LedController_ProcessAnimation(&c, start + 5000u);
  assert(LedController_Update(&c, grb));
  assert(grb[1][2] == 64u && grb[5][2] == 64u && grb[29][2] == 64u);
  assert(grb[1][1] == 0u && grb[5][1] == 0u && grb[29][1] == 0u);
  LedController_ProcessAnimation(&c, start + 10000u);
  assert(LedController_Update(&c, grb));
  assert(grb[0][1] == 64u && grb[319][1] == 64u);
  LedController_ProcessAnimation(&c, start + 11000u);
  assert(LedController_Update(&c, grb));
  assert(grb[0][2] == 64u && grb[319][2] == 64u);
  LedController_SetGlobalBrightness(&c, 255u);
  LedController_ProcessAnimation(&c, start + 14000u); /* Late: group3. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 14u);
  LedController_ProcessAnimation(&c, start + 25000u);
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 25u);
  LedController_ProcessAnimation(&c, start + 43000u); /* Group32 ends at LED256. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 43u);
  LedController_ProcessAnimation(&c, start + 44000u); /* Group33 starts at LED257. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 44u);
  LedController_ProcessAnimation(&c, start + 51000u); /* Last group: LEDs313..320. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 51u);
  LedController_ProcessAnimation(&c, start + 52000u);
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 0u);
  LedController_ProcessAnimation(&c, start + 104000u); /* Skip a complete cycle. */
  assert(LedController_Update(&c, grb));
  assert_led_animation_frame((const uint8_t (*)[3])grb, 0u);
}

int main(void)
{
  test_adc_dma_packed_scan_halves();
  test_adc3_stable_press_can_complete_calibration();
  test_valid_multi_ring_hit_selects_one_ring();
  test_single_sample_noise_is_rejected();
  test_slow_press_and_long_hold_are_rejected();
  test_slow_baseline_drift_is_tracked();
  test_lockout_allows_a_later_hit_after_release();
  test_led_mapping_and_brightness();
  test_led_animation_320_complete_sequence();
  test_led_animation_wraparound_and_pending_frame();
  puts("firmware logic checks passed");
  return 0;
}
