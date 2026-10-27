"""Convert the raw 320x170 RGB888 frames from screenshot.cpp to PNGs, upscaled with nearest-neighbour."""
import struct
import sys
import zlib

W, H = 320, 170


def write_png(path, rgb, scale):
    rows = []
    for y in range(H):
        row = rgb[y * W * 3:(y + 1) * W * 3]
        wide = b''.join(row[x * 3:x * 3 + 3] * scale for x in range(W))
        rows.extend([b'\0' + wide] * scale)

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', W * scale, H * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(b''.join(rows), 9)))
        f.write(chunk(b'IEND', b''))


scale = int(sys.argv[1])
for src in sys.argv[2:]:
    write_png(src[:-4] + '.png', open(src, 'rb').read(), scale)
    print(src[:-4] + '.png')
