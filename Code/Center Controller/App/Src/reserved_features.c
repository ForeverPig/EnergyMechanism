#include "reserved_features.h"
#include "main.h"
void LedOutput_Disable(void)
{
  HAL_GPIO_WritePin(GPIOA, B_G_Pin | R_G_Pin, GPIO_PIN_RESET);
}
bool ParameterStorage_Available(void)
{
  return false;
}
