#ifndef APP_ADC_DMA_H
#define APP_ADC_DMA_H

#include "app_config.h"

/* DMA packs successive scans without padding: ADC1/2 have four ranks,
 * ADC3 has two. Only the queued destination uses the maximum-rank stride.
 * Called after the corresponding DMA half has completed. */
static inline void AppAdc_CopyDmaHalf(
    uint16_t destination[APP_ADC_DMA_HALF_FRAMES][APP_ADC_MAX_RANKS],
    const uint16_t *packed_dma, uint8_t ranks, uint8_t second_half)
{
  uint16_t frame;
  uint16_t offset = second_half ? APP_ADC_DMA_HALF_FRAMES * ranks : 0u;
  for (frame = 0u; frame < APP_ADC_DMA_HALF_FRAMES; ++frame) {
    uint8_t rank;
    for (rank = 0u; rank < ranks; ++rank)
      destination[frame][rank] = packed_dma[offset + frame * ranks + rank];
  }
}

#endif
