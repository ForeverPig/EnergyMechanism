#include "reserved_features.h"
#include "main.h"
void LedOutput_Disable(void)
{
  HAL_GPIO_WritePin(PWM_OE_GPIO_Port, PWM_OE_Pin, GPIO_PIN_RESET);
}
bool ParameterStorage_Available(void)
{
  return false;
}
