#ifndef MCP2515_H
#define MCP2515_H
#include "main.h"
#include "debug_protocol.h"
typedef struct
{
  SPI_HandleTypeDef *spi;
  GPIO_TypeDef *cs_port, *reset_port, *int_port;
  uint16_t cs_pin, reset_pin, int_pin;
  uint8_t ready, eflg;
  uint32_t io_errors, rx_overruns, tx_aborts, last_health, tx_since;
  volatile uint8_t irq;
} Mcp2515;
bool Mcp2515_Init(Mcp2515 *m);
bool Mcp2515_Send(Mcp2515 *m, const CanFrame *f);
bool Mcp2515_Receive(Mcp2515 *m, CanFrame *f);
void Mcp2515_Poll(Mcp2515 *m, uint32_t now);
#endif
