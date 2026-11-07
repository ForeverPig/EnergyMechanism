#include "status_led.h"
#include "main.h"
static uint32_t last_toggle;
static GPIO_PinState level;
void StatusLed_Init(uint32_t now)
{
  last_toggle = now;
  level = GPIO_PIN_RESET;
  HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, level);
}
void StatusLed_Process(uint32_t now)
{
  /* Main-loop liveness only. A stalled loop must not keep blinking in an ISR. */
  if ((uint32_t)(now - last_toggle) < 500u)
    return;
  last_toggle = now;
  level = level == GPIO_PIN_RESET ? GPIO_PIN_SET : GPIO_PIN_RESET;
  HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, level);
}
