import importlib.util
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('monitor',Path(__file__).parents[1]/'tools/debug_monitor.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ProtocolTests(unittest.TestCase):
    def test_chunked_crc_and_resync(self):
        frame=m.encode(1,123,b'\x01\x00\x00');d=m.Decoder();packets=[]
        for byte in b'noise'+frame:
            packets+=d.feed(bytes([byte]),1)
        self.assertEqual(packets,[(1,123,b'\x01\x00\x00')])
        bad=bytearray(frame);bad[-1]^=1
        self.assertEqual(d.feed(bytes(bad)+frame,2),[(1,123,b'\x01\x00\x00')])
        self.assertEqual(d.errors,1)
    def test_timeout_and_bad_length(self):
        d=m.Decoder();frame=m.encode(1,1,b'\x01\x00\x00')
        d.feed(frame[:5],1);d.feed(b'',1.2)
        self.assertFalse(d.buffer)
        self.assertEqual(d.feed(b'\xa5\x5a\x01\x01\xff\xff\x00\x00'+frame,2),[(1,1,b'\x01\x00\x00')])
    def test_pressure_and_status(self):
        p=struct.pack('<BBI',5,255,123)+b''.join(struct.pack('<BBHHhH',i+1,1,1024,1000,-24,80) for i in range(10))
        result=m.describe(0x12,0,p)
        self.assertEqual(result['port'],5);self.assertEqual(result['sensors'][9]['delta'],-24)
        status=bytearray(140);status[0]=2;status[20]=1;status[21]=1;status[23]=1;status[32:44]=bytes(range(12))
        result=m.describe(0x10,0,status)
        self.assertEqual(result['leaves'][0]['uid'],'000102030405060708090a0b')
        self.assertFalse(result['motor_write_enabled'])
    def test_motor_signed(self):
        p=struct.pack('<BBHhhIBB',1,50,8191,-123,-321,100,1,0)
        result=m.describe(0x14,0,p)
        self.assertEqual(result['rotor_rpm'],-123);self.assertEqual(result['current_raw'],-321)

if __name__=='__main__':unittest.main()
