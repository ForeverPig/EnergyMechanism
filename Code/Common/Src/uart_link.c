#include "uart_link.h"
#include <string.h>
static UartLink *link;
bool UartLink_Init(UartLink *l, UART_HandleTypeDef *uart)
{
  memset(l, 0, sizeof(*l));
  l->uart = uart;
  link = l;
  if (HAL_UART_Receive_DMA(uart, l->rx, UART_RX_CAP) != HAL_OK)
    return false;
  __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
  return true;
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (link && uart == link->uart)
    link->wraps++;
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if (link && uart == link->uart)
  {
    link->tx_read = (uint8_t)((link->tx_read + 1u) % UART_TX_DEPTH);
    link->busy = 0;
  }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (link && uart == link->uart)
  {
    link->errors++;
    link->restart = 1;
  }
}
static uint32_t producer(UartLink *l)
{
  uint32_t key = __get_PRIMASK();
  __disable_irq();
  DMA_HandleTypeDef *d = l->uart->hdmarx;
  uint32_t flag = __HAL_DMA_GET_TC_FLAG_INDEX(d), before, after, left;
  /* Account for a transfer-complete IRQ that is pending while interrupts are masked. */
  do
  {
    before = __HAL_DMA_GET_FLAG(d, flag);
    left = __HAL_DMA_GET_COUNTER(d);
    after = __HAL_DMA_GET_FLAG(d, flag);
  } while (before != after);
  uint32_t n = l->wraps * UART_RX_CAP;
  if (after && left != 0u)
    n += UART_RX_CAP;
  n += UART_RX_CAP - left;
  __set_PRIMASK(key);
  return n;
}
bool UartLink_Receive(UartLink *l, DebugPacket *packet)
{
  if (l->restart)
    return false;
  uint32_t end = producer(l);
  if ((uint32_t)(end - l->consumed) > UART_RX_CAP)
  {
    l->rx_overruns++;
    l->consumed = end;
    l->parser.used = 0;
    return false;
  }
  unsigned budget = UART_RX_CAP;
  while (l->consumed != end && budget--)
  {
    uint8_t b = l->rx[l->consumed++ % UART_RX_CAP];
    if (DebugFeed(&l->parser, b, HAL_GetTick(), packet))
      return true;
  }
  return false;
}
bool UartLink_Send(UartLink *l, uint8_t type, uint16_t seq, const uint8_t *p, uint16_t n)
{
  uint8_t next = (uint8_t)((l->tx_write + 1u) % UART_TX_DEPTH);
  if (next == l->tx_read || n > DBG_PAYLOAD_MAX)
  {
    l->tx_drops++;
    return false;
  }
  l->tx_len[l->tx_write] = (uint16_t)DebugEncode(l->tx[l->tx_write], type, seq, p, n);
  __DMB();
  l->tx_write = next;
  return true;
}
void UartLink_Poll(UartLink *l)
{
  if (l->restart)
  {
    (void)HAL_UART_Abort(l->uart);
    l->restart = 0;
    l->busy = 0;
    l->tx_read = l->tx_write;
    l->wraps = 0;
    l->consumed = 0;
    l->parser.used = 0;
    if (HAL_UART_Receive_DMA(l->uart, l->rx, UART_RX_CAP) != HAL_OK)
    {
      l->restart = 1;
      return;
    }
    __HAL_DMA_DISABLE_IT(l->uart->hdmarx, DMA_IT_HT);
  }
  if (!l->busy && l->tx_read != l->tx_write)
  {
    l->busy = 1;
    if (HAL_UART_Transmit_DMA(l->uart, l->tx[l->tx_read], l->tx_len[l->tx_read]) != HAL_OK)
    {
      l->busy = 0;
      l->errors++;
      l->restart = 1;
    }
  }
}
