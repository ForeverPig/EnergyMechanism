#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_SENSOR_COUNT                 10u
#define APP_SENSOR_SAMPLE_HZ             10000u
#define APP_SAMPLE_PERIOD_US             100u
#define APP_ADC_DMA_FRAMES               32u
#define APP_ADC_DMA_HALF_FRAMES          16u
#define APP_ADC_MAX_RANKS                 4u
#define APP_ADC_QUEUE_DEPTH              4u
#define APP_CALIBRATION_MS                500u
#define APP_CALIBRATION_MAX_RANGE         120u
#define APP_HIT_START_DELTA                80u
#define APP_HIT_MIN_PEAK                  160u
#define APP_HIT_RISE_DELTA                 30u
#define APP_HIT_RELEASE_DELTA              40u
#define APP_HIT_MIN_WIDTH_SAMPLES            3u
#define APP_HIT_MAX_WIDTH_SAMPLES          600u
#define APP_HIT_RELEASE_STABLE_MS            2u
#define APP_HIT_GROUP_MS                     3u
#define APP_HIT_LOCKOUT_MS                  80u
#define APP_BASELINE_TRACK_SHIFT            12u

#define APP_LED_RING_COUNT                   3u
#define APP_LED_RING10_COUNT                  4u
#define APP_LED_RING9_COUNT                  10u
#define APP_LED_RING8_COUNT                  16u
#define APP_LED_ANIMATION_PERIOD_MS         1000u
#define APP_LED_CONTINUOUS_REFRESH              1u
#define APP_LED_SOLID_MODE                      1u
#define APP_LED_ANIMATION_COLOR_COUNT          2u
#define APP_LED_RING_LED_COUNT (APP_LED_RING10_COUNT + APP_LED_RING9_COUNT + \
                                APP_LED_RING8_COUNT)
#define APP_LED_COUNT                        320u
#define APP_LED_GROUP_SIZE                     8u
#define APP_LED_GROUP_COUNT ((APP_LED_COUNT + APP_LED_GROUP_SIZE - 1u) / APP_LED_GROUP_SIZE)
#if APP_LED_GROUP_SIZE == 0u || APP_LED_RING_LED_COUNT > APP_LED_COUNT
#error "Invalid LED strip or group layout"
#endif
#define APP_LED_GLOBAL_BRIGHTNESS          255u
#define APP_LED_PWM_TIMER_HZ         160000000u
#define APP_LED_PWM_HZ                 800000u
#define APP_LED_PWM_PERIOD_TICKS            200u
#define APP_LED_T0H_TICKS                     56u
#define APP_LED_T1H_TICKS                    112u
#define APP_LED_RESET_SLOTS                  256u

/* Logical ring IDs for physical networks ADC1..ADC10. */
static const uint8_t APP_SENSOR_RING[APP_SENSOR_COUNT] = {
  6u, 7u, 8u, 9u, 10u, 5u, 4u, 3u, 2u, 1u
};

/* LEDs are daisy-chained from the center outward: 10 -> 9 -> 8.
 * The animation visits this table in reverse order (outer -> inner). */
typedef struct {
  uint8_t ring;
  uint16_t led_count;
} AppLedRingConfig;

static const AppLedRingConfig APP_LED_RINGS[APP_LED_RING_COUNT] = {
  {10u, APP_LED_RING10_COUNT},
  {9u, APP_LED_RING9_COUNT},
  {8u, APP_LED_RING8_COUNT}
};

/* One-based LED positions in the WS2812 chain for the line pattern. */
static const uint16_t APP_LED_LINE_LEDS[] = {1u, 3u, 5u, 10u, 15u, 23u};
#define APP_LED_LINE_LED_COUNT (sizeof(APP_LED_LINE_LEDS) / sizeof(APP_LED_LINE_LEDS[0]))

/* Run the complete pattern sequence once in each RGB color. */
static const uint8_t APP_LED_ANIMATION_COLORS[APP_LED_ANIMATION_COLOR_COUNT][3] = {
  {255u, 0u, 0u},
  {0u, 0u, 255u}
};

/* The appended eight-pixel chase uses blue; one group at a time. */
static const uint8_t APP_LED_GROUP_COLOR[3] = {0u, 0u, 255u};
static const uint8_t APP_LED_SOLID_COLOR[3] = {255u, 0u, 0u};

#endif
