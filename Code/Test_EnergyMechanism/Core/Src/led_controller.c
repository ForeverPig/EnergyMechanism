#include "led_controller.h"
#include "app_config.h"
#include <string.h>

#define ANIMATION_PATTERN_COUNT (APP_LED_RING_COUNT + 2u)
#define ANIMATION_RING_STEP_COUNT (ANIMATION_PATTERN_COUNT * APP_LED_ANIMATION_COLOR_COUNT)
#define ANIMATION_CHASE_FIRST_STEP (ANIMATION_RING_STEP_COUNT + APP_LED_ANIMATION_COLOR_COUNT)
#define ANIMATION_STEP_COUNT (ANIMATION_CHASE_FIRST_STEP + APP_LED_GROUP_COUNT)

void LedController_Init(LedController *c)
{
  uint8_t i;
  uint16_t offset = 0u;
  memset(c, 0, sizeof(*c));
  memset(c->pixel_enabled, 1, sizeof(c->pixel_enabled));
  c->global_brightness = APP_LED_GLOBAL_BRIGHTNESS;
  for (i = 0u; i < APP_LED_RING_COUNT; ++i) {
    c->rings[i].ring = APP_LED_RINGS[i].ring;
    c->rings[i].first_led = offset;
    c->rings[i].led_count = APP_LED_RINGS[i].led_count;
    c->rings[i].red = 255u;
    c->rings[i].brightness = 255u;
    offset = (uint16_t)(offset + c->rings[i].led_count);
  }
  c->dirty = 1u;
}

void LedController_SetSolidColor(LedController *c,
                                  uint8_t red, uint8_t green, uint8_t blue)
{
  c->animation_active = 0u;
  c->strip_mode = 1u;
  c->strip_color[0] = red;
  c->strip_color[1] = green;
  c->strip_color[2] = blue;
  memset(c->pixel_enabled, 1, sizeof(c->pixel_enabled));
  c->dirty = 1u;
}

static void apply_animation_step(LedController *c)
{
  uint8_t i;
  if (c->animation_step >= ANIMATION_RING_STEP_COUNT) {
    const uint8_t *strip_color;
    c->strip_mode = 1u;
    if (c->animation_step < ANIMATION_CHASE_FIRST_STEP) {
      strip_color = APP_LED_ANIMATION_COLORS[c->animation_step - ANIMATION_RING_STEP_COUNT];
      memset(c->pixel_enabled, 1, sizeof(c->pixel_enabled));
    } else {
      uint16_t led;
      uint16_t first = (uint16_t)((c->animation_step - ANIMATION_CHASE_FIRST_STEP) *
                                   APP_LED_GROUP_SIZE);
      uint16_t end = (uint16_t)(first + APP_LED_GROUP_SIZE);
      if (end > APP_LED_COUNT) end = APP_LED_COUNT;
      strip_color = APP_LED_GROUP_COLOR;
      memset(c->pixel_enabled, 0, sizeof(c->pixel_enabled));
      for (led = first; led < end; ++led) c->pixel_enabled[led] = 1u;
    }
    memcpy(c->strip_color, strip_color, sizeof(c->strip_color));
    c->dirty = 1u;
    return;
  }
  c->strip_mode = 0u;
  uint8_t pattern = (uint8_t)(c->animation_step % ANIMATION_PATTERN_COUNT);
  const uint8_t *color = APP_LED_ANIMATION_COLORS[c->animation_step / ANIMATION_PATTERN_COUNT];
  uint8_t line = (uint8_t)(pattern == APP_LED_RING_COUNT + 1u);
  uint8_t active_ring = 0u;
  if (pattern >= 1u && pattern <= APP_LED_RING_COUNT)
    active_ring = APP_LED_RINGS[APP_LED_RING_COUNT - pattern].ring;

  for (i = 0u; i < APP_LED_RING_COUNT; ++i) {
    (void)LedController_SetRingColor(c, c->rings[i].ring, color[0], color[1], color[2]);
    (void)LedController_SetRingBrightness(c, c->rings[i].ring,
        (active_ring == 0u || c->rings[i].ring == active_ring) ? 255u : 0u);
  }

  if (line) {
    uint16_t position;
    memset(c->pixel_enabled, 0, sizeof(c->pixel_enabled));
    for (position = 0u; position < APP_LED_LINE_LED_COUNT; ++position) {
      uint16_t led = APP_LED_LINE_LEDS[position];
      if (led >= 1u && led <= LED_CONTROLLER_MAX_LEDS)
        c->pixel_enabled[led - 1u] = 1u;
    }
  } else {
    memset(c->pixel_enabled, 1, sizeof(c->pixel_enabled));
  }
  c->dirty = 1u;
}

void LedController_StartAnimation(LedController *c, uint32_t now_ms)
{
  c->animation_step = 0u;
  c->animation_active = 1u;
  c->animation_last_ms = now_ms;
  apply_animation_step(c);
}

void LedController_ProcessAnimation(LedController *c, uint32_t now_ms)
{
  uint32_t elapsed, advances;
  if (!c->animation_active) return;
  elapsed = (uint32_t)(now_ms - c->animation_last_ms);
  if (elapsed < APP_LED_ANIMATION_PERIOD_MS) return;
  advances = elapsed / APP_LED_ANIMATION_PERIOD_MS;
  c->animation_last_ms += advances * APP_LED_ANIMATION_PERIOD_MS;
  c->animation_step = (uint16_t)((c->animation_step +
      advances % ANIMATION_STEP_COUNT) % ANIMATION_STEP_COUNT);
  apply_animation_step(c);
}

uint8_t LedController_SetRingColor(LedController *c, uint8_t ring,
                                   uint8_t red, uint8_t green, uint8_t blue)
{
  uint8_t i;
  for (i = 0u; i < APP_LED_RING_COUNT; ++i) {
    LedRingState *s = &c->rings[i];
    if (s->ring == ring) {
      if (s->red != red || s->green != green || s->blue != blue) {
        s->red = red; s->green = green; s->blue = blue; c->dirty = 1u;
      }
      return 1u;
    }
  }
  return 0u;
}

uint8_t LedController_SetRingBrightness(LedController *c, uint8_t ring,
                                        uint8_t brightness)
{
  uint8_t i;
  for (i = 0u; i < APP_LED_RING_COUNT; ++i) {
    if (c->rings[i].ring == ring) {
      if (c->rings[i].brightness != brightness) {
        c->rings[i].brightness = brightness; c->dirty = 1u;
      }
      return 1u;
    }
  }
  return 0u;
}

void LedController_SetGlobalBrightness(LedController *c, uint8_t brightness)
{
  if (c->global_brightness != brightness) {
    c->global_brightness = brightness;
    c->dirty = 1u;
  }
}

static uint8_t scale(uint8_t value, uint8_t ring_brightness,
                     uint8_t global_brightness)
{
  return (uint8_t)(((uint16_t)value * ring_brightness * global_brightness) /
                   (255u * 255u));
}

uint8_t LedController_Update(LedController *c,
                             uint8_t out_grb[LED_CONTROLLER_MAX_LEDS][3])
{
  uint8_t i;
  uint16_t led;
  if (!c->dirty) return 0u;
  /* Clear every pixel so exiting full-strip modes also extinguishes LEDs 31..320. */
  memset(c->pixels, 0, sizeof(c->pixels));
  if (c->strip_mode) {
    for (led = 0u; led < LED_CONTROLLER_MAX_LEDS; ++led) {
      LedPixel *p = &c->pixels[led];
      p->red = c->strip_color[0]; p->green = c->strip_color[1];
      p->blue = c->strip_color[2];
      p->brightness = c->pixel_enabled[led] ? 255u : 0u;
    }
  } else {
    for (i = 0u; i < APP_LED_RING_COUNT; ++i) {
      LedRingState *s = &c->rings[i];
      for (led = s->first_led; led < s->first_led + s->led_count; ++led) {
        LedPixel *p = &c->pixels[led];
        p->red = s->red; p->green = s->green; p->blue = s->blue;
        p->brightness = c->pixel_enabled[led] ? s->brightness : 0u;
      }
    }
  }
  for (led = 0u; led < LED_CONTROLLER_MAX_LEDS; ++led) {
    LedPixel *p = &c->pixels[led];
    out_grb[led][0] = scale(p->green, p->brightness, c->global_brightness);
    out_grb[led][1] = scale(p->red, p->brightness, c->global_brightness);
    out_grb[led][2] = scale(p->blue, p->brightness, c->global_brightness);
  }
  c->dirty = 0u;
  return 1u;
}
