#!/usr/bin/env python3
"""Generate the prototype icon from the renderer's bitmap alphabet (no image dependencies)."""
from pathlib import Path
import re
import struct
import zlib
root = Path(__file__).resolve().parents[1]
font = {ch: list(map(int, values.split(','))) for ch, values in re.findall(r"\{'(.)', \{([0-9, ]+)\}\}", (root / 'src/demo_renderer.cpp').read_text())}
size = 512
pixels = bytearray(bytes([16,22,28]) * size * size)
def rect(x,y,w,h,color):
    for row in range(y,y+h):
        for col in range(x,x+w):
            offset=(row*size+col)*3
            pixels[offset:offset+3]=bytes(color)
def text(value,y,scale,color):
    x=(size-(len(value)*6-1)*scale)//2
    for ch in value:
        for row,bits in enumerate(font[ch]):
            for col in range(5):
                if bits & (1 << (4-col)): rect(x+col*scale,y+row*scale,scale,scale,color)
        x+=6*scale
text('OPEN',80,14,(244,247,255))
text('NOW',210,14,(86,230,158))
rect(90,337,332,4,(86,230,158))
text('PS5',383,8,(244,247,255))
def chunk(tag,data):
    return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
raw=b''.join(b'\0'+pixels[y*size*3:(y+1)*size*3] for y in range(size))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
(root/'sce_sys/icon0.png').write_bytes(png)
