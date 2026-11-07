#include "app.h"
#include "app_config.h"
#include "app_adc_dma.h"
#include "adc.h"
#include "tim.h"
#include "ws2812.h"
#include "led_controller.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

#define ADC_SOURCE_COUNT 3u
#define DEBUG_EVENT_QUEUE_DEPTH 4u
#define DEBUG_TX_CAPACITY 1024u
#define DEBUG_PERIOD_MS 50u

extern USBD_HandleTypeDef hUsbDeviceFS;

typedef struct {
  uint16_t samples[APP_ADC_DMA_HALF_FRAMES][APP_ADC_MAX_RANKS];
  uint16_t sequence;
} AdcQueuedBlock;

typedef struct {
  AdcQueuedBlock blocks[APP_ADC_QUEUE_DEPTH];
  volatile uint16_t write_count;
  volatile uint16_t read_count;
} AdcSourceQueue;

/* Flat storage: each ADC's DMA scan stride is its actual rank count. */
static uint16_t s_adc_dma[ADC_SOURCE_COUNT][APP_ADC_DMA_FRAMES * APP_ADC_MAX_RANKS];
static AdcSourceQueue s_adc_queues[ADC_SOURCE_COUNT];
static HitDetector s_detector;
static HitDetectorConfig s_detector_config;
static LedController s_leds;
static uint8_t s_led_grb[LED_CONTROLLER_MAX_LEDS][3];
static HitEvent s_event_queue[DEBUG_EVENT_QUEUE_DEPTH];
static volatile uint8_t s_event_write;
static volatile uint8_t s_event_read;
static uint8_t s_tx_buffer[DEBUG_TX_CAPACITY];
static volatile uint8_t s_tx_complete;
static uint8_t s_tx_active;
static volatile uint8_t s_recovery_requested;
static uint8_t s_adc_running;
static uint8_t s_timestamp_divider;
static uint32_t s_sample_timestamp_ms;
static uint32_t s_last_debug_ms;
static volatile uint32_t s_dma_drop_count;
static volatile uint32_t s_adc_error_count;
static uint32_t s_debug_drop_count;
static HitSensorSnapshot s_snapshots[HIT_SENSOR_COUNT];

static void count_dma_drop(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  ++s_dma_drop_count;
  __set_PRIMASK(primask);
}

static uint8_t adc_source(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1) return 0u;
  if (hadc == &hadc2) return 1u;
  if (hadc == &hadc3) return 2u;
  return 0xFFu;
}

static uint8_t adc_ranks(uint8_t source)
{
  return source == 2u ? 2u : 4u;
}

static void adc_queue_reset(void)
{
  uint8_t source;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (source = 0u; source < ADC_SOURCE_COUNT; ++source) {
    s_adc_queues[source].write_count = 0u;
    s_adc_queues[source].read_count = 0u;
  }
  __set_PRIMASK(primask);
}

static HAL_StatusTypeDef start_adc_sampling(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK ||
      HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK ||
      HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED) != HAL_OK)
    return HAL_ERROR;

  adc_queue_reset();
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma[0],
                        APP_ADC_DMA_FRAMES * adc_ranks(0u)) != HAL_OK ||
      HAL_ADC_Start_DMA(&hadc2, (uint32_t *)s_adc_dma[1],
                        APP_ADC_DMA_FRAMES * adc_ranks(1u)) != HAL_OK ||
      HAL_ADC_Start_DMA(&hadc3, (uint32_t *)s_adc_dma[2],
                        APP_ADC_DMA_FRAMES * adc_ranks(2u)) != HAL_OK) {
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_ADC_Stop_DMA(&hadc2);
    (void)HAL_ADC_Stop_DMA(&hadc3);
    return HAL_ERROR;
  }
  s_adc_running = 1u;
  if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_ADC_Stop_DMA(&hadc2);
    (void)HAL_ADC_Stop_DMA(&hadc3);
    s_adc_running = 0u;
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void recover_sampling(void)
{
  __disable_irq();
  s_recovery_requested = 0u;
  __enable_irq();
  (void)HAL_TIM_Base_Stop(&htim6);
  if (s_adc_running) {
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_ADC_Stop_DMA(&hadc2);
    (void)HAL_ADC_Stop_DMA(&hadc3);
  }
  s_adc_running = 0u;
  adc_queue_reset();
  HitDetector_Reset(&s_detector);
  if (start_adc_sampling() != HAL_OK) Error_Handler();
}

static void adc_block_callback(ADC_HandleTypeDef *hadc, uint8_t second_half)
{
  uint8_t source = adc_source(hadc);
  uint8_t ranks;
  uint16_t write, read;
  AdcSourceQueue *q;
  if (source >= ADC_SOURCE_COUNT) return;
  q = &s_adc_queues[source];
  write = q->write_count;
  read = q->read_count;
  if ((uint16_t)(write - read) >= APP_ADC_QUEUE_DEPTH) {
    count_dma_drop();
    s_recovery_requested = 1u;
    return;
  }
  ranks = adc_ranks(source);
  AppAdc_CopyDmaHalf(q->blocks[write % APP_ADC_QUEUE_DEPTH].samples,
                     s_adc_dma[source], ranks, second_half);
  q->blocks[write % APP_ADC_QUEUE_DEPTH].sequence = write;
  __DMB();
  q->write_count = (uint16_t)(write + 1u);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  adc_block_callback(hadc, 0u);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  adc_block_callback(hadc, 1u);
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if ((hadc->ErrorCode & (HAL_ADC_ERROR_OVR | HAL_ADC_ERROR_DMA)) != 0u)
  {
    ++s_adc_error_count;
    s_recovery_requested = 1u;
  }
}

static uint8_t queues_ready(void)
{
  return (uint8_t)(s_adc_queues[0].read_count != s_adc_queues[0].write_count &&
                   s_adc_queues[1].read_count != s_adc_queues[1].write_count &&
                   s_adc_queues[2].read_count != s_adc_queues[2].write_count);
}

static void process_adc_queues(void)
{
  uint16_t raw[HIT_SENSOR_COUNT];
  while (queues_ready()) {
    uint16_t read[ADC_SOURCE_COUNT];
    uint8_t frame;
    uint16_t slot[ADC_SOURCE_COUNT];
    uint8_t source;
    for (source = 0u; source < ADC_SOURCE_COUNT; ++source) {
      read[source] = s_adc_queues[source].read_count;
      slot[source] = (uint16_t)(read[source] % APP_ADC_QUEUE_DEPTH);
    }
    if (s_adc_queues[0].blocks[slot[0]].sequence !=
          s_adc_queues[1].blocks[slot[1]].sequence ||
        s_adc_queues[0].blocks[slot[0]].sequence !=
          s_adc_queues[2].blocks[slot[2]].sequence) {
      count_dma_drop();
      s_recovery_requested = 1u;
      return;
    }
    for (frame = 0u; frame < APP_ADC_DMA_HALF_FRAMES; ++frame) {
      HitEvent event;
      uint8_t event_ready;
      raw[0] = s_adc_queues[0].blocks[slot[0]].samples[frame][0];
      raw[1] = s_adc_queues[0].blocks[slot[0]].samples[frame][1];
      raw[2] = s_adc_queues[0].blocks[slot[0]].samples[frame][2];
      raw[3] = s_adc_queues[0].blocks[slot[0]].samples[frame][3];
      raw[4] = s_adc_queues[1].blocks[slot[1]].samples[frame][0];
      raw[5] = s_adc_queues[1].blocks[slot[1]].samples[frame][1];
      raw[6] = s_adc_queues[1].blocks[slot[1]].samples[frame][2];
      raw[7] = s_adc_queues[1].blocks[slot[1]].samples[frame][3];
      raw[8] = s_adc_queues[2].blocks[slot[2]].samples[frame][0];
      raw[9] = s_adc_queues[2].blocks[slot[2]].samples[frame][1];
      HitDetector_Process(&s_detector, &s_detector_config, raw,
                          s_sample_timestamp_ms, &event, &event_ready);
      if (++s_timestamp_divider >= 10u) {
        s_timestamp_divider = 0u;
        ++s_sample_timestamp_ms;
      }
      if (event_ready) {
        uint8_t next = (uint8_t)((s_event_write + 1u) %
                                  DEBUG_EVENT_QUEUE_DEPTH);
        App_OnHitEvent(&event);
        if (next == s_event_read) {
          ++s_debug_drop_count;
        } else {
          s_event_queue[s_event_write] = event;
          s_event_write = next;
        }
      }
    }
    __DMB();
    for (source = 0u; source < ADC_SOURCE_COUNT; ++source)
      s_adc_queues[source].read_count = (uint16_t)(read[source] + 1u);
  }
  HitDetector_GetSnapshot(&s_detector, s_snapshots);
}

static uint16_t format_snapshot(void)
{
  uint16_t used = 0u;
  uint8_t i;
  used += (uint16_t)snprintf((char *)s_tx_buffer + used,
                             DEBUG_TX_CAPACITY - used,
                             "S,%lu,%u,%lu,%lu,%lu\r\n",
                             (unsigned long)s_sample_timestamp_ms,
                             HitDetector_GetCalibrationStatus(&s_detector),
                             (unsigned long)App_GetDmaDropCount(),
                             (unsigned long)App_GetAdcErrorCount(),
                             (unsigned long)App_GetDebugDropCount());
  for (i = 0u; i < HIT_SENSOR_COUNT && used < DEBUG_TX_CAPACITY - 48u; ++i) {
    used += (uint16_t)snprintf((char *)s_tx_buffer + used,
                               DEBUG_TX_CAPACITY - used,
                               "%u,%u,%ld,%d,%u,%u\r\n",
                               APP_SENSOR_RING[i], s_snapshots[i].raw,
                               (long)s_snapshots[i].baseline,
                               s_snapshots[i].delta, s_snapshots[i].peak,
                               s_snapshots[i].state);
  }
  return used;
}

static uint8_t start_next_usb_message(void)
{
  uint16_t length;
  uint8_t is_event = 0u;
  uint8_t event_slot = s_event_read;
  if (s_event_read != s_event_write) {
    HitEvent *e = &s_event_queue[event_slot];
    length = (uint16_t)snprintf((char *)s_tx_buffer, sizeof(s_tx_buffer),
                                "E,%u,%u,%lu\r\n", e->ring, e->peak,
                                (unsigned long)e->timestamp_ms);
    is_event = 1u;
  } else {
    uint32_t now = HAL_GetTick();
    if ((now - s_last_debug_ms) < DEBUG_PERIOD_MS) return 0u;
    s_last_debug_ms = now;
    length = format_snapshot();
  }
  if (CDC_Transmit_FS(s_tx_buffer, length) == USBD_OK) {
    s_tx_active = 1u;
    if (is_event)
      s_event_read = (uint8_t)((event_slot + 1u) % DEBUG_EVENT_QUEUE_DEPTH);
    return 1u;
  }
  if (!is_event) ++s_debug_drop_count;
  return 0u;
}

static void update_led_output(void)
{
  HAL_StatusTypeDef status;
  if (WS2812_GetState() != WS2812_IDLE) return;
  if (LedController_Update(&s_leds, s_led_grb))
    status = WS2812_Start((const uint8_t (*)[3])s_led_grb);
  else if (APP_LED_CONTINUOUS_REFRESH)
    status = WS2812_Repeat();
  else
    return;
  /* Preserve the latest scene if a transfer could not be started. */
  if (status != HAL_OK) s_leds.dirty = 1u;
}

void App_Init(void)
{
  HitDetector_Init(&s_detector);
  HitDetector_DefaultConfig(&s_detector_config);
  LedController_Init(&s_leds);
  if (APP_LED_SOLID_MODE)
    LedController_SetSolidColor(&s_leds, APP_LED_SOLID_COLOR[0],
                                 APP_LED_SOLID_COLOR[1], APP_LED_SOLID_COLOR[2]);
  else
    LedController_StartAnimation(&s_leds, HAL_GetTick());
  WS2812_Init(&htim1, TIM_CHANNEL_1);
  adc_queue_reset();
  s_event_write = 0u;
  s_event_read = 0u;
  s_tx_active = 0u;
  s_tx_complete = 0u;
  s_recovery_requested = 0u;
  s_dma_drop_count = 0u;
  s_adc_error_count = 0u;
  s_debug_drop_count = 0u;
  s_sample_timestamp_ms = 0u;
  s_timestamp_divider = 0u;
  s_last_debug_ms = HAL_GetTick();

  update_led_output();
  if (start_adc_sampling() != HAL_OK) Error_Handler();
}

void App_Process(void)
{
  uint32_t primask;
  WS2812_Process();
  LedController_ProcessAnimation(&s_leds, HAL_GetTick());
  if (s_recovery_requested) {
    recover_sampling();
    return;
  }
  process_adc_queues();
  update_led_output();

  primask = __get_PRIMASK();
  __disable_irq();
  if (s_tx_complete) {
    s_tx_complete = 0u;
    s_tx_active = 0u;
  }
  __set_PRIMASK(primask);
  if (s_tx_active && hUsbDeviceFS.pClassData == 0)
    s_tx_active = 0u;
  if (!s_tx_active) (void)start_next_usb_message();
}

void App_UsbTransmitComplete(void)
{
  s_tx_complete = 1u;
}

void App_GetSnapshots(HitSensorSnapshot out[HIT_SENSOR_COUNT])
{
  if (out != 0) memcpy(out, s_snapshots, sizeof(s_snapshots));
}

uint32_t App_GetDmaDropCount(void) { return s_dma_drop_count; }
uint32_t App_GetAdcErrorCount(void) { return s_adc_error_count; }
uint32_t App_GetDebugDropCount(void) { return s_debug_drop_count; }
uint8_t App_GetCalibrationStatus(void)
{
  return HitDetector_GetCalibrationStatus(&s_detector);
}

uint8_t App_SetRingColor(uint8_t ring, uint8_t red, uint8_t green, uint8_t blue)
{
  return LedController_SetRingColor(&s_leds, ring, red, green, blue);
}

uint8_t App_SetRingBrightness(uint8_t ring, uint8_t brightness)
{
  return LedController_SetRingBrightness(&s_leds, ring, brightness);
}

void App_SetGlobalBrightness(uint8_t brightness)
{
  LedController_SetGlobalBrightness(&s_leds, brightness);
}

__weak void App_OnHitEvent(const HitEvent *event)
{
  (void)event;
}
