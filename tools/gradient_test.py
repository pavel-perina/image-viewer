"""Writes a 16-bit PNG of dark gradients for checking whether 10 bits reach the screen.

    python tools/gradient_test.py gradients.png

See "Display depth" in doc/manual-checks.md.

Each gradient is drawn three times, one above the other:
  a) full 16 bits, b) rounded to 10 bits, c) rounded to 8 bits.
On an 8-bit path a) and b) look like c), banded. On a 10-bit path a) and b) are smooth
and only c) shows bands. Values are sRGB-encoded; the file has no colour profile.
"""
import struct
import sys
import zlib

WIDTH = 1920
STRIP = 110   # height of one strip
GAP = 6       # black line between strips
SPACE = 30    # black space between gradients

# (start, end) in 8-bit sRGB units, deliberately dark and close together.
GRADIENTS = [
    ((16, 34, 16), (36, 16, 40)),   # green to purple
    ((0, 0, 0), (24, 24, 24)),      # near black grey
    ((20, 20, 40), (40, 28, 20)),   # navy to brown
]


def quantize(value, bits):
    """value in 0..1 to 16 bits, after rounding to the given number of bits."""
    levels = (1 << bits) - 1
    return round(round(value * levels) / levels * 65535)


def row(start, end, bits):
    out = bytearray([0])  # filter type None
    for x in range(WIDTH):
        t = x / (WIDTH - 1)
        for c in range(3):
            v = (start[c] + (end[c] - start[c]) * t) / 255
            out += struct.pack(">H", quantize(v, bits))
    return bytes(out)


def black_rows(n):
    return [bytes(1 + WIDTH * 6)] * n


rows = []
for start, end in GRADIENTS:
    for bits in (16, 10, 8):
        rows += [row(start, end, bits)] * STRIP
        rows += black_rows(GAP)
    rows += black_rows(SPACE)
height = len(rows)


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, height, 16, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
png += chunk(b"IEND", b"")

with open(sys.argv[1], "wb") as f:
    f.write(png)
print(f"{sys.argv[1]}: {WIDTH}x{height}, 16-bit RGB")
