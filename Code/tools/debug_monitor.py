"""Hardware bring-up console. Opening a port is an explicit user action; no flashing."""
from __future__ import annotations
import argparse
import binascii
import json
import random
import struct
import sys
import time

MAX_PAYLOAD = 192

def encode(kind: int, seq: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('payload too long')
    body = struct.pack('<BBHH', 1, kind, len(payload), seq) + payload
    return b'\xa5\x5a' + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))

class Decoder:
    def __init__(self):
        self.buffer = bytearray()
        self.errors = 0
        self.last = 0.0

    def feed(self, data: bytes, now=None):
        now = time.monotonic() if now is None else now
        if self.buffer and now - self.last > .1:
            self.buffer.clear()
            self.errors += 1
        if not data:
            return []
        self.last = now
        self.buffer.extend(data)
        packets = []
        while len(self.buffer) >= 2:
            if self.buffer[:2] != b'\xa5\x5a':
                del self.buffer[0]
                continue
            if len(self.buffer) < 8:
                break
            version, kind, length, seq = struct.unpack_from('<BBHH', self.buffer, 2)
            if version != 1 or length > MAX_PAYLOAD:
                self.errors += 1
                del self.buffer[0]
                continue
            if len(self.buffer) < length + 10:
                break
            crc, = struct.unpack_from('<H', self.buffer, length + 8)
            if crc != binascii.crc_hqx(self.buffer[2:length+8], 0xffff):
                self.errors += 1
                del self.buffer[0]
                continue
            packets.append((kind, seq, bytes(self.buffer[8:8+length])))
            del self.buffer[:length+10]
        return packets

def describe(kind, seq, data):
    result = {'type': hex(kind), 'seq': seq}
    if kind == 2 and len(data) == 3:
        result.update(event='ack', command=data[0], port=data[1], result=data[2],
                      meaning={0:'accepted',1:'invalid',2:'offline',3:'busy',4:'timeout',5:'unsupported'}.get(data[2],'unknown'))
    elif kind == 0x12 and len(data) == 106:
        port, snapshot, stamp = struct.unpack_from('<BBI', data)
        result.update(event='pressure', port=port, snapshot=snapshot, sample_ms=stamp, sensors=[])
        for i in range(10):
            ring, state, raw, baseline, delta, peak = struct.unpack_from('<BBHHhH', data, 6+10*i)
            result['sensors'].append(dict(ring=ring,state=state,raw=raw,baseline=baseline,delta=delta,peak=peak))
    elif kind == 0x11 and len(data) == 12:
        port, can_id, dlc = struct.unpack_from('<BHB', data)
        result.update(event='can_event', port=port, can_id=hex(can_id), dlc=dlc)
        if can_id == 0x513 and dlc == 8:
            hit_seq, ring, peak, stamp = struct.unpack_from('<BBHI', data, 4)
            result.update(event='hit', hit_seq=hit_seq, ring=ring, peak=peak, sample_ms=stamp)
        elif can_id == 0x510 and dlc == 8:
            version, state, errors, drops, uptime = struct.unpack_from('<BBHHH', data, 4)
            result.update(event='heartbeat',version=version,calibration=state&0x7f,adc_fault=bool(state&0x80),adc_errors=errors,dma_drops=drops,uptime_s=uptime)
    elif kind == 0x10 and len(data) == 28 and data[0] == 1:
        result.update(event='leaf_status',calibration=data[1]&0x7f,adc_fault=bool(data[1]&0x80),can_ready=bool(data[2]),bus_off=bool(data[3]))
        for name,value in zip(['uptime_ms','adc_errors','dma_drops','uart_errors','uart_tx_drops','can_event_drops'],struct.unpack_from('<6I',data,4)):
            result[name]=value
    elif kind == 0x10 and len(data) == 140 and data[0] == 2:
        result.update(event='center_status',selected_leaf=data[1],motor_write_enabled=bool(data[2]),can_ready_mask=data[3],leaves=[])
        for name,value in zip(['uptime_ms','uart_errors','uart_tx_drops','timer_ticks'],struct.unpack_from('<4I',data,4)):
            result[name]=value
        for i in range(5):
            port,online,state,uid_valid,age,errors,drops,rx_drops=struct.unpack_from('<BBBBHHHH',data,20+24*i)
            uid=data[32+24*i:44+24*i].hex() if uid_valid else None
            result['leaves'].append(dict(port=port,online=bool(online),calibration=state&0x7f,adc_fault=bool(state&0x80),age_ms=age,adc_errors=errors,dma_drops=drops,can_rx_drops=rx_drops,uid=uid))
    elif kind == 0x14 and len(data) == 14:
        motor,temp,angle,rpm,current,age,online,write=struct.unpack('<BBHhhIBB',data)
        result.update(event='motor_feedback',motor_id=motor,temp_c=temp,rotor_angle=angle,rotor_rpm=rpm,current_raw=current,age_ms=age,online=bool(online),motor_write_enabled=bool(write))
    else:
        result.update(event='unknown_packet',payload=data.hex())
    return result

def main(argv=None):
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',required=True,help='Windows COM port, e.g. COM5')
    ap.add_argument('--board',choices=['leaf','center'],required=True)
    ap.add_argument('--baud',type=int,help='default: leaf 115200, center 1000000')
    ap.add_argument('--watch',type=int,choices=range(0,6),help='0 = directly connected leaf; center uses 1..5')
    ap.add_argument('--calibrate',type=int,choices=range(0,6),help='explicitly recalibrate port 0 (leaf) or 1..5 (center)')
    ap.add_argument('--duration',type=float,default=0,help='seconds, 0 = until Ctrl+C')
    ap.add_argument('--quiet-snapshots',action='store_true')
    args=ap.parse_args(argv)
    for field in ['watch','calibrate']:
        value=getattr(args,field)
        if value is not None and ((args.board=='leaf' and value!=0) or (args.board=='center' and value==0)):
            ap.error(f'--{field}: leaf requires 0; center requires 1..5')
    try:
        import serial
    except ImportError:
        ap.error('Install the console dependency: python -m pip install -r Code/tools/requirements.txt')
    baud=args.baud or (115200 if args.board=='leaf' else 1000000)
    decoder=Decoder();seq=random.randrange(1,60000);pending={};seen=False;calibration_sent=False
    def send(port,op,value,device):
        nonlocal seq
        seq=(seq+1)&0xffff
        device.write(encode(1,seq,bytes([op,port,value])))
        pending[seq]=(time.monotonic(),op)
    started=time.monotonic();last_request=started
    try:
        with serial.Serial(args.port,baud,timeout=.02,write_timeout=.5) as device:
            send(0,1,0,device)
            if args.watch is not None:send(args.watch,2,1,device)
            while not args.duration or time.monotonic()-started<args.duration:
                now=time.monotonic()
                for kind,number,payload in decoder.feed(device.read(max(1,min(device.in_waiting,4096)))):
                    result=describe(kind,number,payload);seen=True;pending.pop(number,None)
                    if not (args.quiet_snapshots and result.get('event')=='pressure'):
                        print(json.dumps(result,ensure_ascii=False),flush=True)
                    # Wait for a real status response so CAN discovery can complete before calibration.
                    if args.calibrate is not None and not calibration_sent:
                        ready=result.get('event')=='leaf_status' or any(x['port']==args.calibrate and x['online'] for x in result.get('leaves',[]))
                        if ready:send(args.calibrate,3,0,device);calibration_sent=True
                for number,(sent,op) in list(pending.items()):
                    if now-sent>2:
                        print(f'Command timeout: seq={number}, op={op}',file=sys.stderr);del pending[number]
                if now-last_request>2:
                    send(0,1,0,device);last_request=now
                if not seen and now-started>5:
                    raise RuntimeError('No valid packets in 5 s; check board selection, baud, serial wiring and firmware.')
    except KeyboardInterrupt:
        pass
    except (serial.SerialException,RuntimeError) as exc:
        print(str(exc),file=sys.stderr);return 1
    finally:
        # Closing a console does not send motor commands or change training state.
        print(f'Parser errors: {decoder.errors}',file=sys.stderr)
    if args.calibrate is not None and not calibration_sent:
        print('Calibration was not sent: target leaf was not online.',file=sys.stderr);return 1
    return 0

if __name__=='__main__':
    raise SystemExit(main())
