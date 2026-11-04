#!/usr/bin/env python3
# placeholder menu art when the repo has no private/art (it never ships capcom's pictures).
# icon.png 48x48 rgb (smdh has no alpha), banner.png 256x128 rgb. plain black, no deps.
import struct, zlib, sys, os

def png(w, h, rgb=(0, 0, 0)):
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)
    raw = b''.join(b'\x00' + bytes(rgb) * w for _ in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
open(os.path.join(out, 'icon.png'), 'wb').write(png(48, 48))
open(os.path.join(out, 'banner.png'), 'wb').write(png(256, 128))
print('placeholder icon 48x48 + banner 256x128 written to', out)
