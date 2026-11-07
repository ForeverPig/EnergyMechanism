#ifndef PRESSURE_SAMPLER_H
#define PRESSURE_SAMPLER_H
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#define SAMPLE_HZ 10000u
#define SAMPLE_HALF_FRAMES 16u
typedef struct
{
  uint16_t samples[SAMPLE_HALF_FRAMES][10];
  uint32_t first_sample;
} PressureBlock;
bool PressureSampler_Start(void);
void PressureSampler_Stop(void);
bool PressureSampler_Take(PressureBlock *out);
bool PressureSampler_Faulted(void);
uint32_t PressureSampler_Drops(void);
uint32_t PressureSampler_Errors(void);
#endif
