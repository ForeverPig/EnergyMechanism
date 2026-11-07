#include "usb_gateway.h"
#include <string.h>

void UsbGateway_Init(UsbGateway *u)
{
  memset(u, 0, sizeof(*u));
}
static void retire(UsbGateway *u, Gateway *g)
{
  if (u->active)
  {
    u->retired[u->retired_next++ % 8] = u->nonce;
    Gateway_Disconnect(g, GW_USB_CLIENT);
  }
  u->active = 0;
  u->nonce = 0;
  u->reply_count = u->reply_read = 0;
  u->tx_size = u->tx_sent = 0;
}
static void reply(UsbGateway *u, Gateway *g, uint8_t type)
{
  if (u->reply_count == 4)
  {
    retire(u, g);
    return;
  }
  DebugPacket *p = &u->replies[(u->reply_read + u->reply_count++) % 4];
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = type;
  p->len = type == USB_OPEN_ACK ? 24 : 8;
  V2Write64(p->data, u->nonce);
  if (type == USB_OPEN_ACK)
    Gateway_Info(g, p->data + 8);
}
void UsbGateway_Feed(UsbGateway *u, Gateway *g, uint8_t byte, uint32_t now)
{
  DebugPacket p;
  if (!WireFeed(&u->parser, byte, now, &p, true) || p.version != 2 || p.len < 8)
    return;
  const uint64_t nonce = V2Read64(p.data);
  if (p.type == USB_OPEN && !p.seq && p.len == 8 && nonce)
  {
    if (!u->active || nonce != u->nonce)
    {
      for (unsigned i = 0; i < 8; i++)
        if (u->retired[i] == nonce)
          return;
      retire(u, g);
      Gateway_ConnectUsb(g);
      u->active = 1;
      u->nonce = nonce;
    }
    u->last_rx = now;
    reply(u, g, USB_OPEN_ACK);
    return;
  }
  if (!u->active || nonce != u->nonce || g->client[GW_USB_CLIENT].close)
    return;
  if (!p.seq && p.len == 8 && p.type == USB_CLOSE)
  {
    retire(u, g);
    return;
  }
  if (!p.seq && p.len == 8 && p.type == USB_PING)
  {
    u->last_rx = now;
    reply(u, g, USB_PONG);
  }
  else if (p.type == V2_REQUEST && p.seq && p.len >= 8 + V2_REQUEST_HEADER)
  {
    u->last_rx = now;
    p.len -= 8;
    memmove(p.data, p.data + 8, p.len);
    Gateway_Command(g, GW_USB_CLIENT, &p, now);
  }
}
void UsbGateway_Poll(UsbGateway *u, Gateway *g, uint32_t now)
{
  if (u->active && ((uint32_t)(now - u->last_rx) >= USB_SESSION_MS ||
                    !g->client[GW_USB_CLIENT].active || g->client[GW_USB_CLIENT].close))
    retire(u, g);
}
size_t UsbGateway_Next(UsbGateway *u, Gateway *g, const uint8_t **bytes)
{
  if (!u->active)
    return 0;
  if (u->tx_sent == u->tx_size)
  {
    DebugPacket p;
    bool found = false;
    if (u->reply_count)
    {
      p = u->replies[u->reply_read];
      u->reply_read = (uint8_t)((u->reply_read + 1) % 4);
      u->reply_count--;
      found = true;
    }
    else
    {
      /* Round robin between control/event/pressure; bounded work per main-loop turn. */
      for (unsigned n = 0; n < 3; n++)
      {
        uint8_t ch = u->channel;
        u->channel = (uint8_t)((u->channel + 1) % 3);
        if (Gateway_NextPacket(g, GW_USB_CLIENT, ch, &p))
        {
          memmove(p.data + 8, p.data, p.len);
          V2Write64(p.data, u->nonce);
          p.len += 8;
          found = true;
          break;
        }
      }
    }
    if (!found)
      return 0;
    u->tx_size = WireEncode(u->tx, 2, p.type, p.seq, p.data, p.len);
    u->tx_sent = 0;
  }
  *bytes = u->tx + u->tx_sent;
  return u->tx_size - u->tx_sent;
}
void UsbGateway_Sent(UsbGateway *u, size_t size)
{
  if (size <= u->tx_size - u->tx_sent)
    u->tx_sent += size;
}
