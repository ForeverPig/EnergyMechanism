#ifndef WS2812_HAL_STUB_H
#define WS2812_HAL_STUB_H

#include <stdint.h>

/* Force-included with -D__MAIN_H for the host-only WS2812 driver test.
 * Production ws2812.c and ws2812.h are compiled unchanged. */
typedef struct { uint32_t compare; } TIM_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2 } HAL_StatusTypeDef;
#define TIM_CHANNEL_1 0u
#define __HAL_TIM_SET_COMPARE(timer, channel, value) ((timer)->compare = (value))
HAL_StatusTypeDef HAL_TIM_PWM_Start_DMA(TIM_HandleTypeDef *timer, uint32_t channel,
                                      const uint32_t *data, uint16_t length);
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *timer);

#endif
