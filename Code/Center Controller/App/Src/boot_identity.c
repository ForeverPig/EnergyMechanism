#include "boot_identity.h"
#include "main.h"
/* PLLQ is already 40 MHz. RNG needs no pin, interrupt or DMA allocation. */
uint64_t BootIdentity_Create(void)
{
  RCC_PeriphCLKInitTypeDef clk = {0};
  clk.PeriphClockSelection = RCC_PERIPHCLK_RNG;
  clk.RngClockSelection = RCC_RNGCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK)
    return 0;
  __HAL_RCC_RNG_CLK_ENABLE();
  RNG->CR = RNG_CR_RNGEN;
  uint32_t words[3] = {0}, start = HAL_GetTick();
  for (unsigned i = 0; i < 3; i++)
  {
    while (!(RNG->SR & RNG_SR_DRDY))
    {
      if ((RNG->SR & (RNG_SR_CECS | RNG_SR_SECS)) || (uint32_t)(HAL_GetTick() - start) > 10u)
      {
        RNG->CR = 0;
        return 0;
      }
    }
    if (RNG->SR & (RNG_SR_CECS | RNG_SR_SECS))
    {
      RNG->CR = 0;
      return 0;
    }
    words[i] = RNG->DR;
  }
  RNG->CR = 0;
  if (words[0] == words[1] || words[1] == words[2])
    return 0;
  return (uint64_t)words[1] | ((uint64_t)words[2] << 32);
}
