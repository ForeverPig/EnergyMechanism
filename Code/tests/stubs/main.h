#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
typedef enum
{
  HAL_OK = 0,
  HAL_ERROR = 1
} HAL_StatusTypeDef;
typedef struct
{
  uint32_t Instance, ErrorCode;
} ADC_HandleTypeDef;
typedef struct
{
  uint32_t Instance;
} TIM_HandleTypeDef;
typedef struct
{
  uint32_t Instance;
} SPI_HandleTypeDef;
typedef struct
{
  uint32_t Instance;
} GPIO_TypeDef;
typedef enum
{
  GPIO_PIN_RESET = 0,
  GPIO_PIN_SET = 1
} GPIO_PinState;
#define ADC_SINGLE_ENDED 0u
#define TIM_FLAG_UPDATE 1u
static inline uint32_t __get_PRIMASK(void)
{
  return 0;
}
static inline void __disable_irq(void)
{
}
static inline void __set_PRIMASK(uint32_t k)
{
  (void)k;
}
static inline void __DMB(void)
{
}
#define __HAL_TIM_SET_COUNTER(h, v) ((void)(h), (void)(v))
#define __HAL_TIM_CLEAR_FLAG(h, v) ((void)(h), (void)(v))
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *, uint32_t *, uint32_t);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *, uint8_t *, uint8_t *, uint16_t,
                                          uint32_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick(void);
#endif
