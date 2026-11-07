#include "can_bus.h"
#include <string.h>
static CanBus *buses[3];
static unsigned index_of(FDCAN_HandleTypeDef *h)
{
  return h->Instance == FDCAN1 ? 0u : h->Instance == FDCAN2 ? 1u : 2u;
}
bool CanBus_Init(CanBus *b, FDCAN_HandleTypeDef *h, bool center, bool motor)
{
  memset(b, 0, sizeof(*b));
  b->can = h;
  b->center = (uint8_t)center;
  buses[index_of(h)] = b;
  for (uint32_t i = 0; i < h->Init.StdFiltersNbr; i++)
  {
    FDCAN_FilterTypeDef f = {0};
    f.IdType = FDCAN_STANDARD_ID;
    f.FilterIndex = i;
    f.FilterType = FDCAN_FILTER_RANGE;
    f.FilterConfig = FDCAN_FILTER_DISABLE;
    if (i == 0)
    {
      f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
      f.FilterID1 = center ? CAN_ACK : CAN_COMMAND;
      f.FilterID2 = center ? 0x539u : CAN_COMMAND;
    }
    if (i == 1 && motor)
    {
      f.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
      f.FilterID1 = 0x201;
      f.FilterID2 = 0x208;
    }
    if (HAL_FDCAN_ConfigFilter(h, &f) != HAL_OK)
      return false;
  }
  if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                   FDCAN_REJECT_REMOTE) != HAL_OK)
    return false;
  if (HAL_FDCAN_Start(h) != HAL_OK)
    return false;
  if (HAL_FDCAN_ActivateNotification(
          h, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0) != HAL_OK)
    return false;
  b->ready = 1;
  return true;
}
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t flags)
{
  CanBus *b = buses[index_of(h)];
  if (!b)
    return;
  if (flags & FDCAN_IT_RX_FIFO0_MESSAGE_LOST)
    b->rx_drops++;
  unsigned budget = 3;
  while (HAL_FDCAN_GetRxFifoFillLevel(h, FDCAN_RX_FIFO0) && budget--)
  {
    FDCAN_RxHeaderTypeDef head;
    uint8_t bytes[64];
    if (HAL_FDCAN_GetRxMessage(h, FDCAN_RX_FIFO0, &head, bytes) != HAL_OK)
      break;
    uint8_t len = (uint8_t)head.DataLength; /* CubeG4 1.6.3 uses an unshifted DLC. */
    if (head.IdType != FDCAN_STANDARD_ID || head.RxFrameType != FDCAN_DATA_FRAME ||
        head.FDFormat != FDCAN_CLASSIC_CAN || len > 8)
      continue;
    uint8_t next = (uint8_t)((b->write + 1u) % CAN_RX_DEPTH);
    if (next == b->read)
    {
      b->rx_drops++;
      continue;
    }
    CanFrame *f = &b->rx[b->write];
    f->id = (uint16_t)head.Identifier;
    f->len = len;
    memcpy(f->data, bytes, len);
    __DMB();
    b->write = next;
  }
}
bool CanBus_Receive(CanBus *b, CanFrame *f)
{
  if (b->read == b->write)
    return false;
  *f = b->rx[b->read];
  __DMB();
  b->read = (uint8_t)((b->read + 1u) % CAN_RX_DEPTH);
  return true;
}
bool CanBus_Send(CanBus *b, const CanFrame *f)
{
  if (!b->ready || b->bus_off || f->len > 8 || !CanIdAllowed(b->center != 0, f->id) ||
      (b->can->Instance->TXFQS & FDCAN_TXFQS_TFQF))
    return false;
  /* RM0440: TFFL reads zero in Tx Queue mode. TFQF is valid in both modes. */
  FDCAN_TxHeaderTypeDef h = {0};
  h.Identifier = f->id;
  h.IdType = FDCAN_STANDARD_ID;
  h.TxFrameType = FDCAN_DATA_FRAME;
  h.DataLength = f->len;
  h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  h.BitRateSwitch = FDCAN_BRS_OFF;
  h.FDFormat = FDCAN_CLASSIC_CAN;
  h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  return HAL_FDCAN_AddMessageToTxFifoQ(b->can, &h, f->data) == HAL_OK;
}
void CanBus_Poll(CanBus *b, uint32_t now)
{
  if (!b->ready || (uint32_t)(now - b->last_check) < 10u)
    return;
  b->last_check = now;
  FDCAN_ProtocolStatusTypeDef s;
  if (HAL_FDCAN_GetProtocolStatus(b->can, &s) != HAL_OK)
    return;
  b->bus_off = (uint8_t)s.BusOff;
  if (b->bus_off)
  {
    (void)HAL_FDCAN_AbortTxRequest(b->can, 7u);
    if (HAL_FDCAN_Stop(b->can) == HAL_OK && HAL_FDCAN_Start(b->can) == HAL_OK)
    {
      b->restarts++;
    }
    else
      b->ready = 0;
    return;
  }
  if (b->can->Instance->TXBRP)
  {
    if (!b->pending_since)
      b->pending_since = now;
    if ((uint32_t)(now - b->pending_since) >= 100u)
    {
      (void)HAL_FDCAN_AbortTxRequest(b->can, 7u);
      b->tx_aborts++;
      b->pending_since = now;
    }
  }
  else
    b->pending_since = 0;
}
