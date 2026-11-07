#include "pressure_sampler.h"
#include "adc.h"
#include "tim.h"
#include <string.h>
#define DEPTH 8u
typedef struct
{
  uint16_t samples[16][4];
  uint32_t sequence;
} SampleHalf;
typedef struct
{
  SampleHalf slots[DEPTH];
  volatile uint32_t written, read;
} SourceQueue;
static uint16_t dma[3][32 * 4];
static SourceQueue queues[3];
static volatile uint8_t fault, running;
static volatile uint32_t drops, errors;
static ADC_HandleTypeDef *const adc[3] = {&hadc1, &hadc2, &hadc3};
static const uint8_t ranks[3] = {4, 4, 2};
void PressureSampler_Stop(void)
{
  running = 0;
  (void)HAL_TIM_Base_Stop(&htim6);
  for (unsigned i = 0; i < 3; i++)
    (void)HAL_ADC_Stop_DMA(adc[i]);
}
bool PressureSampler_Start(void)
{
  PressureSampler_Stop();
  uint32_t key = __get_PRIMASK();
  __disable_irq();
  memset(queues, 0, sizeof(queues));
  fault = 0;
  __set_PRIMASK(key);
  __HAL_TIM_SET_COUNTER(&htim6, 0);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
  for (unsigned i = 0; i < 3; i++)
  {
    if (HAL_ADCEx_Calibration_Start(adc[i], ADC_SINGLE_ENDED) != HAL_OK ||
        HAL_ADC_Start_DMA(adc[i], (uint32_t *)dma[i], 32u * ranks[i]) != HAL_OK)
    {
      PressureSampler_Stop();
      fault = 1;
      errors++;
      return false;
    }
  }
  running = 1;
  if (HAL_TIM_Base_Start(&htim6) != HAL_OK)
  {
    PressureSampler_Stop();
    fault = 1;
    errors++;
    return false;
  }
  return true;
}
static void capture(ADC_HandleTypeDef *h, uint8_t half)
{
  if (!running || fault)
    return;
  unsigned s = 0;
  while (s < 3 && h != adc[s])
    s++;
  if (s == 3)
    return;
  SourceQueue *q = &queues[s];
  uint32_t w = q->written;
  /* Alternating half/full IRQs also detect a lost DMA callback. */
  if (w - q->read >= DEPTH || (w & 1u) != half)
  {
    drops++;
    fault = 1;
    return;
  }
  SampleHalf *b = &q->slots[w % DEPTH];
  unsigned rank = ranks[s], offset = half * 16u * rank;
  for (unsigned f = 0; f < 16; f++)
    for (unsigned r = 0; r < rank; r++)
      b->samples[f][r] = dma[s][offset + f * rank + r];
  b->sequence = w;
  __DMB();
  q->written = w + 1u;
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h)
{
  capture(h, 0);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
  capture(h, 1);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *h)
{
  (void)h;
  errors++;
  fault = 1;
}
bool PressureSampler_Take(PressureBlock *out)
{
  if (fault)
    return false;
  uint32_t seq = queues[0].read;
  for (unsigned s = 0; s < 3; s++)
  {
    SourceQueue *q = &queues[s];
    if (q->read == q->written)
      return false;
    if (q->read != seq || q->slots[q->read % DEPTH].sequence != seq)
    {
      drops++;
      fault = 1;
      return false;
    }
  }
  for (unsigned f = 0; f < 16; f++)
    for (unsigned s = 0; s < 3; s++)
    {
      SampleHalf *b = &queues[s].slots[seq % DEPTH];
      unsigned base = s * 4;
      for (unsigned r = 0; r < ranks[s]; r++)
        out->samples[f][base + r] = b->samples[f][r];
    }
  out->first_sample = seq * 16u;
  __DMB();
  for (unsigned s = 0; s < 3; s++)
    queues[s].read = seq + 1u;
  return true;
}
bool PressureSampler_Faulted(void)
{
  return fault != 0;
}
uint32_t PressureSampler_Drops(void)
{
  return drops;
}
uint32_t PressureSampler_Errors(void)
{
  return errors;
}
