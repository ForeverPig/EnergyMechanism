#include "can_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
FDCAN_GlobalTypeDef test_fdcan[3];
static unsigned sends;
static HAL_StatusTypeDef send_result;
static FDCAN_TxHeaderTypeDef sent_header;
static uint8_t sent_bytes[8];
/* These paths are not exercised by this targeted TX regression. */
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *h, const FDCAN_FilterTypeDef *f)
{ (void)h; (void)f; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)h; (void)a; (void)b; (void)c; (void)d; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h)
{ (void)h; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h)
{ (void)h; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b)
{ (void)h; (void)a; (void)b; assert(0); return HAL_ERROR; }
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *h, uint32_t a)
{ (void)h; (void)a; assert(0); return 0; }
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t a, FDCAN_RxHeaderTypeDef *r, uint8_t *b)
{ (void)h; (void)a; (void)r; (void)b; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *h, FDCAN_ProtocolStatusTypeDef *s)
{ (void)h; (void)s; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h, uint32_t a)
{ (void)h; (void)a; assert(0); return HAL_ERROR; }
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,
                                              const FDCAN_TxHeaderTypeDef *header,
                                              const uint8_t *data)
{
  (void)h;
  sends++;
  sent_header = *header;
  memcpy(sent_bytes, data, header->DataLength);
  return send_result;
}
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h)
{
  return h->Instance->TXFQS & 7u;
}
int main(void)
{
  FDCAN_HandleTypeDef h = {.Instance = FDCAN1};
  CanBus b = {.can = &h, .ready = 1, .center = 1};
  CanFrame f = {.id = CAN_COMMAND, .len = 8, .data = {1,2,3,4,5,6,7,8}};
  /* Measured on G474 Tx Queue mode: TXFQS=0, yet three TX slots are free. */
  assert(HAL_FDCAN_GetTxFifoFreeLevel(&h) == 0);
  assert(CanBus_Send(&b, &f) && sends == 1);
  assert(sent_header.Identifier == CAN_COMMAND && sent_header.DataLength == 8);
  assert(memcmp(sent_bytes, f.data, 8) == 0);
  FDCAN1->TXFQS = FDCAN_TXFQS_TFQF;
  assert(!CanBus_Send(&b, &f) && sends == 1);
  FDCAN1->TXFQS = 3; /* FIFO mode also works. */
  assert(CanBus_Send(&b, &f) && sends == 2);
  f.id = 0x200; assert(!CanBus_Send(&b, &f));
  f.id = 0x1ff; assert(!CanBus_Send(&b, &f));
  f.id = CAN_COMMAND; f.len = 9; assert(!CanBus_Send(&b, &f));
  f.len = 8; b.ready = 0; assert(!CanBus_Send(&b, &f));
  b.ready = 1; b.bus_off = 1; assert(!CanBus_Send(&b, &f));
  b.bus_off = 0; send_result = HAL_ERROR; assert(!CanBus_Send(&b, &f));
  b.center = 0; send_result = HAL_OK; f.id = CAN_HEARTBEAT;
  FDCAN1->TXFQS = 0;
  assert(CanBus_Send(&b, &f));
  puts("can_bus: queue/FIFO availability and motor-ID protection PASS");
  return 0;
}
