#ifndef USB_GATEWAY_H
#define USB_GATEWAY_H
#include "gateway_core.h"
#define USB_OPEN 0x70u
#define USB_OPEN_ACK 0x71u
#define USB_PING 0x72u
#define USB_PONG 0x73u
#define USB_CLOSE 0x74u
#define USB_SESSION_MS 3000u
/* One USB business session, independent from the three BLE links. No allocations. */
typedef struct {
  FrameParser parser;
  uint64_t nonce, retired[8];
  uint32_t last_rx;
  uint8_t active, retired_next, reply_read, reply_count, channel;
  DebugPacket replies[4];
  uint8_t tx[DBG_FRAME_MAX];
  size_t tx_size, tx_sent;
} UsbGateway;
void UsbGateway_Init(UsbGateway *u);
void UsbGateway_Feed(UsbGateway *u, Gateway *g, uint8_t byte, uint32_t now);
void UsbGateway_Poll(UsbGateway *u, Gateway *g, uint32_t now);
/* Peek/consume permits nonblocking partial writes without dropping packet tails. */
size_t UsbGateway_Next(UsbGateway *u, Gateway *g, const uint8_t **bytes);
void UsbGateway_Sent(UsbGateway *u, size_t size);
#endif
