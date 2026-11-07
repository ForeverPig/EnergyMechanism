#ifndef UART_LINK_H
#define UART_LINK_H
#include "main.h"
#include "debug_protocol.h"
#define UART_RX_CAP 512u
#define UART_TX_DEPTH 8u
typedef struct
{
  UART_HandleTypeDef *uart;
  uint8_t rx[UART_RX_CAP];
  uint8_t tx[UART_TX_DEPTH][DBG_FRAME_MAX];
  uint16_t tx_len[UART_TX_DEPTH];
  volatile uint32_t wraps;
  uint32_t consumed;
  volatile uint8_t tx_read, tx_write, busy, restart;
  uint32_t rx_overruns, tx_drops, errors;
  FrameParser parser;
} UartLink;
bool UartLink_Init(UartLink *l, UART_HandleTypeDef *uart);
void UartLink_Poll(UartLink *l);
bool UartLink_Receive(UartLink *l, DebugPacket *packet);
bool UartLink_Send(UartLink *l, uint8_t type, uint16_t seq, const uint8_t *p, uint16_t n);
#endif
