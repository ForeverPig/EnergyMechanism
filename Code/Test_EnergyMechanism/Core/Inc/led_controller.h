#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>
#include "app_config.h"

#define LED_CONTROLLER_MAX_LEDS APP_LED_COUNT
#define LED_CONTROLLER_MAX_RINGS APP_LED_RING_COUNT

typedef struct {
  uint8_t ring;
  uint16_t first_led;
  uint16_t led_count;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness;
} LedRingState;

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness;
} LedPixel;

typedef struct {
  LedRingState rings[LED_CONTROLLER_MAX_RINGS];
  LedPixel pixels[LED_CONTROLLER_MAX_LEDS];
  uint8_t pixel_enabled[LED_CONTROLLER_MAX_LEDS];
  uint8_t global_brightness;
  uint8_t dirty;
  /* Ring rounds, full-strip color frames, then consecutive pixel groups. */
  uint16_t animation_step;
  uint8_t animation_active;
  uint8_t strip_mode;
  uint8_t strip_color[3];
  uint32_t animation_last_ms;
} LedController;

void LedController_Init(LedController *controller);
/* Fill the complete strip and stop any running animation. */
void LedController_SetSolidColor(LedController *controller,
                                  uint8_t red, uint8_t green, uint8_t blue);
void LedController_StartAnimation(LedController *controller, uint32_t now_ms);
void LedController_ProcessAnimation(LedController *controller, uint32_t now_ms);
uint8_t LedController_SetRingColor(LedController *controller, uint8_t ring,
                                   uint8_t red, uint8_t green, uint8_t blue);
uint8_t LedController_SetRingBrightness(LedController *controller,
                                        uint8_t ring, uint8_t brightness);
void LedController_SetGlobalBrightness(LedController *controller,
                                       uint8_t brightness);
uint8_t LedController_Update(LedController *controller,
                             uint8_t out_grb[LED_CONTROLLER_MAX_LEDS][3]);

#endif
