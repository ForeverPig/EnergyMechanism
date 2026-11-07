#include "pressure_sampler.h"
#include "adc.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
ADC_HandleTypeDef hadc1 = {1, 0}, hadc2 = {2, 0}, hadc3 = {3, 0};
TIM_HandleTypeDef htim6 = {6};
static uint16_t *buffers[3];
static uint32_t lengths[3];
static unsigned fail, stops, started;
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *h)
{
  (void)h;
  started = 0;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h)
{
  (void)h;
  started = 1;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *h)
{
  (void)h;
  stops++;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t v)
{
  (void)h;
  (void)v;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h, uint32_t *p, uint32_t n)
{
  if (h->Instance == fail)
    return HAL_ERROR;
  buffers[h->Instance - 1] = (uint16_t *)p;
  lengths[h->Instance - 1] = n;
  return HAL_OK;
}
static void fill(void)
{
  for (unsigned s = 0; s < 3; s++)
  {
    unsigned ranks = s == 2 ? 2 : 4;
    for (unsigned f = 0; f < 32; f++)
      for (unsigned r = 0; r < ranks; r++)
        buffers[s][f * ranks + r] = (uint16_t)(1000 + s * 100 + f * 2 + r);
  }
}
int main(void)
{
  PressureBlock block;
  assert(PressureSampler_Start());
  assert(started && lengths[0] == 128 && lengths[1] == 128 && lengths[2] == 64);
  fill();
  HAL_ADC_ConvHalfCpltCallback(&hadc3);
  assert(!PressureSampler_Take(&block));
  HAL_ADC_ConvHalfCpltCallback(&hadc1);
  HAL_ADC_ConvHalfCpltCallback(&hadc2);
  assert(PressureSampler_Take(&block));
  for (unsigned f = 0; f < 16; f++)
  {
    assert(block.samples[f][8] == 1200 + f * 2);
    assert(block.samples[f][9] == 1201 + f * 2);
    assert(block.samples[f][4] == 1100 + f * 2);
  }
  HAL_ADC_ConvCpltCallback(&hadc1);
  HAL_ADC_ConvCpltCallback(&hadc2);
  HAL_ADC_ConvCpltCallback(&hadc3);
  assert(PressureSampler_Take(&block));
  assert(block.first_sample == 16 && block.samples[0][8] == 1232 && block.samples[15][9] == 1263);
  assert(PressureSampler_Start());
  HAL_ADC_ConvCpltCallback(&hadc1);
  assert(PressureSampler_Faulted());
  assert(!PressureSampler_Take(&block));
  assert(PressureSampler_Start());
  for (unsigned i = 0; i < 9; i++)
  {
    if (i & 1)
      HAL_ADC_ConvCpltCallback(&hadc1);
    else
      HAL_ADC_ConvHalfCpltCallback(&hadc1);
  }
  assert(PressureSampler_Faulted() && PressureSampler_Drops() >= 2);
  fail = 2;
  stops = 0;
  assert(!PressureSampler_Start());
  assert(!started && stops >= 6 && PressureSampler_Errors());
  puts("PASS: actual ADC sampler packed 4/4/2 halves, asynchronous arrivals, skipped IRQ, "
       "overflow, partial-start cleanup");
  return 0;
}
