#include "ws2812.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t grb[LED_CONTROLLER_MAX_LEDS][3];
static const uint16_t *dma_data;
static uint16_t dma_length;
static uint16_t starts;

HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *timer, uint32_t channel,
                                      const uint32_t *data, uint16_t length)
{
  (void)timer;
  assert(channel == TIM_CHANNEL_1);
  dma_data = (const uint16_t *)data;
  dma_length = length;
  ++starts;
  return HAL_OK;
}

int main(void)
{
  TIM_HandleTypeDef timer = {0};
  uint16_t led, index = 0u;
  uint8_t byte;
  for (led = 0u; led < LED_CONTROLLER_MAX_LEDS; ++led) {
    grb[led][0] = (uint8_t)(led >> 8u);
    grb[led][1] = (uint8_t)led;
    grb[led][2] = (uint8_t)(0x80u | (led * 7u));
  }
  WS2812_Init(&timer, TIM_CHANNEL_1);
  assert(WS2812_Repeat() == HAL_ERROR && starts == 0u);
  assert(WS2812_Start((const uint8_t (*)[3])grb) == HAL_OK);
  assert(starts == 1u && WS2812_GetState() == WS2812_SENDING);
  assert(dma_length == 7936u); /* 320 * 24 data slots + 256 reset slots. */
  for (led = 0u; led < LED_CONTROLLER_MAX_LEDS; ++led)
    for (byte = 0u; byte < 3u; ++byte) {
      uint8_t bit;
      for (bit = 0u; bit < 8u; ++bit) {
        uint16_t expected = (grb[led][byte] & (0x80u >> bit)) ?
                           APP_LED_T1H_TICKS : APP_LED_T0H_TICKS;
        assert(dma_data[index++] == expected);
      }
    }
  assert(index == 7680u);
  while (index < dma_length) assert(dma_data[index++] == 0u);

  /* A busy call must not zero or overwrite the active DMA buffer. */
  memset(grb, 0, sizeof(grb));
  assert(WS2812_Start((const uint8_t (*)[3])grb) == HAL_BUSY);
  assert(WS2812_Repeat() == HAL_BUSY);
  assert(starts == 1u && dma_data[16u] == APP_LED_T1H_TICKS);
  HAL_TIM_PWM_PulseFinishedCallback(&timer);
  assert(WS2812_GetState() == WS2812_SENDING);
  WS2812_Process();
  assert(WS2812_GetState() == WS2812_IDLE && timer.compare == 0u);
  /* Repeat the cached image even though the source GRB array has changed. */
  for (index = 0u; index < 100u; ++index) {
    assert(WS2812_Repeat() == HAL_OK);
    assert(dma_length == 7936u && dma_data[16u] == APP_LED_T1H_TICKS);
    assert(WS2812_Repeat() == HAL_BUSY);
    HAL_TIM_PWM_PulseFinishedCallback(&timer);
    WS2812_Process();
    assert(WS2812_GetState() == WS2812_IDLE);
  }
  assert(starts == 101u);
  assert(WS2812_Start((const uint8_t (*)[3])grb) == HAL_OK);
  assert(starts == 102u && dma_length == 7936u);
  for (index = 0u; index < 7680u; ++index)
    assert(dma_data[index] == APP_LED_T0H_TICKS);
  puts("320 LED PWM encoding and continuous cached repeat checks passed (7936 DMA slots)");
  return 0;
}
