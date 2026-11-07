#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include "main.h"
#include "led_controller.h"

typedef enum {
  WS2812_IDLE = 0,
  WS2812_SENDING,
  WS2812_ERROR
} Ws2812State;

void WS2812_Init(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef WS2812_Start(const uint8_t grb[LED_CONTROLLER_MAX_LEDS][3]);
/* Resend the last encoded frame without touching the PWM buffer. */
HAL_StatusTypeDef WS2812_Repeat(void);
void WS2812_Process(void);
Ws2812State WS2812_GetState(void);

#endif
