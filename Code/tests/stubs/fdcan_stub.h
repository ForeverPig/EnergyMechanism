#ifndef FDCAN_STUB_H
#define FDCAN_STUB_H
typedef struct { uint32_t TXFQS, TXBRP; } FDCAN_GlobalTypeDef;
typedef struct
{
  FDCAN_GlobalTypeDef *Instance;
  struct { uint32_t StdFiltersNbr; } Init;
} FDCAN_HandleTypeDef;
typedef struct
{
  uint32_t IdType, FilterIndex, FilterType, FilterConfig, FilterID1, FilterID2;
} FDCAN_FilterTypeDef;
typedef struct
{
  uint32_t Identifier, IdType, TxFrameType, DataLength, ErrorStateIndicator,
      BitRateSwitch, FDFormat, TxEventFifoControl;
} FDCAN_TxHeaderTypeDef;
typedef struct
{
  uint32_t Identifier, IdType, RxFrameType, DataLength, FDFormat;
} FDCAN_RxHeaderTypeDef;
typedef struct { uint32_t BusOff; } FDCAN_ProtocolStatusTypeDef;
extern FDCAN_GlobalTypeDef test_fdcan[3];
#define FDCAN1 (&test_fdcan[0])
#define FDCAN2 (&test_fdcan[1])
#define FDCAN_TXFQS_TFQF (1u << 21)
#define FDCAN_STANDARD_ID 0u
#define FDCAN_FILTER_RANGE 0u
#define FDCAN_FILTER_DISABLE 0u
#define FDCAN_FILTER_TO_RXFIFO0 1u
#define FDCAN_REJECT 2u
#define FDCAN_REJECT_REMOTE 1u
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE 1u
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST 8u
#define FDCAN_RX_FIFO0 0u
#define FDCAN_DATA_FRAME 0u
#define FDCAN_CLASSIC_CAN 0u
#define FDCAN_ESI_ACTIVE 0u
#define FDCAN_BRS_OFF 0u
#define FDCAN_NO_TX_EVENTS 0u
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *, const FDCAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *, uint32_t, uint32_t, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *, uint32_t, FDCAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *, const FDCAN_TxHeaderTypeDef *, const uint8_t *);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *, FDCAN_ProtocolStatusTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *, uint32_t);
#endif
