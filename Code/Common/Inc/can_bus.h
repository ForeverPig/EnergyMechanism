#ifndef CAN_BUS_H
#define CAN_BUS_H
#include "main.h"
#include "debug_protocol.h"
#define CAN_RX_DEPTH 32u
typedef struct
{
  FDCAN_HandleTypeDef *can;
  CanFrame rx[CAN_RX_DEPTH];
  volatile uint8_t read, write;
  volatile uint32_t rx_drops;
  uint32_t tx_aborts, restarts, last_check, pending_since;
  uint8_t center, ready, bus_off;
} CanBus;
bool CanBus_Init(CanBus *b, FDCAN_HandleTypeDef *can, bool center, bool motor_feedback);
bool CanBus_Send(CanBus *b, const CanFrame *f);
bool CanBus_Receive(CanBus *b, CanFrame *f);
void CanBus_Poll(CanBus *b, uint32_t now);
#endif
