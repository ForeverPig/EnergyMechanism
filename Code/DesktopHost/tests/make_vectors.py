"""Independent Python encoding vectors, shared by the C++/C wire tests."""
import binascii
import json
import pathlib
import struct

def main():
    cases = [(1, 1, struct.pack('<BQI3B', 1, 0, 0, 1, 2, 2)),
             (0x70, 0, struct.pack('<Q', 0x0123456789abcdef)),
             (0x12, 0, bytes(range(125))),
             (1, 65535, bytes(range(192)))]
    vectors = []
    for kind, seq, payload in cases:
        body = struct.pack('<BBHH', 2, kind, len(payload), seq) + payload
        wire = b'\xa5\x5a' + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))
        vectors.append(dict(type=kind, seq=seq, payload=payload.hex(), wire=wire.hex()))
    path = pathlib.Path(__file__).with_name('vectors.json')
    path.write_text(json.dumps(vectors, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
