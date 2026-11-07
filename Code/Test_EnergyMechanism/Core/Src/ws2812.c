#include "ws2812.h"
#include "app_config.h"
#include <string.h>

#define WS2812_BITS_PER_LED 24u
#define WS2812_DATA_SLOTS (LED_CONTROLLER_MAX_LEDS * WS2812_BITS_PER_LED)
#define WS2812_TOTAL_SLOTS (WS2812_DATA_SLOTS + APP_LED_RESET_SLOTS)
#if WS2812_TOTAL_SLOTS > 65535u
#error "WS2812 frame exceeds HAL DMA transfer length"
#endif

static TIM_HandleTypeDef *s_timer;
static uint32_t s_channel;
static uint16_t s_pwm[WS2812_TOTAL_SLOTS];
static volatile Ws2812State s_state = WS2812_IDLE;
static volatile uint8_t s_done;
static uint8_t s_frame_valid;

void WS2812_Init(TIM_HandleTypeDef *timer, uint32_t channel)
{
  s_timer = timer;
  s_channel = channel;
  s_state = WS2812_IDLE;
  s_done = 0u;
  s_frame_valid = 0u;
  memset(s_pwm, 0, sizeof(s_pwm));
}

HAL_StatusTypeDef WS2812_Start(const uint8_t grb[LED_CONTROLLER_MAX_LEDS][3])
{
  uint16_t out = 0u;
  uint16_t led;
  uint8_t byte_index;
  if (s_timer == 0 || s_state != WS2812_IDLE) return HAL_BUSY;

  memset(s_pwm, 0, sizeof(s_pwm));
  for (led = 0u; led < LED_CONTROLLER_MAX_LEDS; ++led) {
    for (byte_index = 0u; byte_index < 3u; ++byte_index) {
      uint8_t bit;
      uint8_t value = grb[led][byte_index];
      for (bit = 0u; bit < 8u; ++bit) {
        s_pwm[out++] = (value & (uint8_t)(0x80u >> bit)) ?
                       APP_LED_T1H_TICKS : APP_LED_T0H_TICKS;
      }
    }
  }
  s_frame_valid = 1u;
  return WS2812_Repeat();
}

HAL_StatusTypeDef WS2812_Repeat(void)
{
  HAL_StatusTypeDef status;
  if (s_timer == 0 || s_state != WS2812_IDLE) return HAL_BUSY;
  if (!s_frame_valid) return HAL_ERROR;
  s_done = 0u;
  s_state = WS2812_SENDING;
  status = HAL_TIM_PWM_Start_DMA(s_timer, s_channel,
                                (const uint32_t *)s_pwm, WS2812_TOTAL_SLOTS);
  if (status != HAL_OK) {
    s_state = WS2812_IDLE;
    return status;
  }
  return HAL_OK;
}

void WS2812_Process(void)
{
  if (s_done) {
    s_done = 0u;
    __HAL_TIM_SET_COMPARE(s_timer, s_channel, 0u);
    s_state = WS2812_IDLE;
  }
}

Ws2812State WS2812_GetState(void)
{
  return s_state;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *timer)
{
  if (timer == s_timer) s_done = 1u;
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *timer)
{
  if (timer == s_timer) s_state = WS2812_ERROR;
}
