#include "usb_gateway.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Gateway g;
static UsbGateway u;
static DebugPacket sent[32];
static unsigned count;
static bool uart_out(void *ctx,const DebugPacket *p) {(void)ctx;assert(count<32);sent[count++]=*p;return true;}
static void feed(uint8_t type,uint16_t seq,uint64_t nonce,const uint8_t *payload,uint16_t size,uint32_t now) {
  uint8_t data[192],wire[202];V2Write64(data,nonce);if(size)memcpy(data+8,payload,size);
  size_t n=WireEncode(wire,2,type,seq,data,(uint16_t)(size+8));
  for(size_t i=0;i<n;i++) UsbGateway_Feed(&u,&g,wire[i],now);
}
static DebugPacket read_usb(void) {
  const uint8_t *bytes;FrameParser p={0};DebugPacket packet={0};bool done=false;
  while(!done) {
    size_t n=UsbGateway_Next(&u,&g,&bytes);assert(n);if(n>7)n=7;
    for(size_t i=0;i<n;i++)if(WireFeed(&p,bytes[i],1,&packet,true))done=true;
    UsbGateway_Sent(&u,n);
  }return packet;
}
static DebugPacket request_packet(uint8_t op,uint16_t seq) {
  DebugPacket p={.version=2,.type=V2_REQUEST,.seq=seq,.len=13};p.data[0]=op;V2Write64(p.data+1,123);return p;
}
static void inject_state(uint16_t seq,uint32_t now) {
  DebugPacket p={.version=2,.type=V2_STATE,.seq=seq,.len=56};
  V2Write64(p.data,g.epoch);V2Write64(p.data+8,123);WriteU32(p.data+16,1);WriteU32(p.data+20,1);p.data[8+24]=1;
  Gateway_Uart(&g,&p,now);
}
int main(void) {
  const uint8_t device[12]={1,2,3};Gateway_Init(&g,77,device,(GatewayBackend){uart_out,NULL});UsbGateway_Init(&u);
  uint8_t info[16];Gateway_Info(&g,info);assert(info[8]==3 && (info[9]&4) && ReadU32(info+12)==3);
  for(unsigned i=0;i<3;i++) {assert(Gateway_Connect(&g,(uint16_t)i)==(int)i);for(uint8_t ch=0;ch<3;ch++)Gateway_Subscribe(&g,i,ch,true);}
  assert(Gateway_Connect(&g,99)==-1);
  UsbGateway_Feed(&u,&g,'x',0);feed(USB_OPEN,0,111,NULL,0,1);assert(g.client[3].active);
  DebugPacket p=read_usb();assert(p.type==USB_OPEN_ACK && p.len==24 && V2Read64(p.data)==111);
  uint64_t session=g.client[3].session;feed(USB_OPEN,0,111,NULL,0,2);(void)read_usb();assert(g.client[3].session==session);
  for(unsigned i=0;i<4;i++) {
    p=request_packet(V2_HELLO,1);p.len=16;p.data[13]=1;p.data[14]=2;p.data[15]=2;
    if(i==3)feed(V2_REQUEST,p.seq,111,p.data,p.len,3);else Gateway_Command(&g,i,&p,3);
    DebugPacket answer;if(i==3){answer=read_usb();assert(V2Read64(answer.data)==111);assert(V2Read64(answer.data+8+20)==g.client[3].session);}
    else {assert(Gateway_NextPacket(&g,i,0,&answer));assert(V2Read64(answer.data+20)==g.client[i].session);}
    assert(answer.type==V2_SESSION && answer.seq==1);
  }
  g.linked=1;g.boot=123;
  for(unsigned i=0;i<4;i++) {p=request_packet(V2_GET_STATE,2);if(i==3)feed(V2_REQUEST,2,111,p.data,p.len,10);else Gateway_Command(&g,i,&p,10);}
  Gateway_Poll(&g,10);assert(count==4);
  for(unsigned i=0;i<count;i++)inject_state(sent[i].seq,20);
  for(unsigned i=0;i<3;i++){assert(Gateway_NextPacket(&g,i,0,&p));assert(p.seq==2 && p.type==V2_STATE);}
  p=read_usb();assert(p.seq==2 && p.type==V2_STATE && V2Read64(p.data)==111 && V2Read64(p.data+8)==123);
  // Duplicate request is replayed locally; it must not be forwarded or executed again.
  p=request_packet(V2_GET_STATE,2);feed(V2_REQUEST,2,111,p.data,p.len,20);p=read_usb();assert(p.type==V2_STATE && count==4);
  // An in-flight old request cannot leak to a new USB session with reused client seq.
  p=request_packet(V2_GET_STATE,3);feed(V2_REQUEST,3,111,p.data,p.len,30);Gateway_Poll(&g,30);assert(count==5);uint16_t oldseq=sent[4].seq;
  feed(USB_OPEN,0,222,NULL,0,40);(void)read_usb();assert(g.client[3].session!=session);
  inject_state(oldseq,45);const uint8_t *bytes;assert(UsbGateway_Next(&u,&g,&bytes)==0);
  feed(USB_OPEN,0,111,NULL,0,50);assert(u.nonce==222); // retired OPEN cannot reclaim a session
  feed(USB_PING,0,111,NULL,0,51);assert(UsbGateway_Next(&u,&g,&bytes)==0);
  feed(USB_PING,0,222,NULL,0,52);p=read_usb();assert(p.type==USB_PONG);
  // Pressure lease retirement is local; BLE observers retain their tokens.
  g.client[0].token=7;g.client[3].token=8;UsbGateway_Poll(&u,&g,3052);
  assert(!u.active && !g.client[3].active && !g.client[3].token && g.client[0].token==7);
  feed(USB_OPEN,0,333,NULL,0,0xfffffff0u);(void)read_usb();UsbGateway_Poll(&u,&g,1000);assert(u.active);UsbGateway_Poll(&u,&g,4000);assert(!u.active);
  feed(USB_OPEN,0,444,NULL,0,5000);(void)read_usb();feed(USB_CLOSE,0,444,NULL,0,5001);assert(!u.active);
  for(unsigned i=0;i<3;i++)assert(g.client[i].active);
  assert(!CanIdAllowed(true,0x200) && !CanIdAllowed(true,0x1ff) && CanIdAllowed(true,0x500));
  puts("USB V2: framing, partial writes, 3 BLE + USB routing, replay, nonce isolation, expiry: PASS");return 0;
}
