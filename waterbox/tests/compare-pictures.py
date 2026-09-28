#!/usr/bin/env python3
"""Two 32-bit TGA pictures, the same pixels? Either may be stored top-down or
bottom-up (run-native writes the one, chimera-run the other); alpha is not
compared.

usage: compare-pictures.py <a.tga> <b.tga>   (exit 0 when the same)
"""
import struct
import sys


def rows(path):
    d = open(path, "rb").read()
    w, h = struct.unpack("<HH", d[12:16])
    if d[16] != 32:
        sys.exit(f"{path}: {d[16]} bits a pixel")
    px = d[18 + d[0]:18 + d[0] + w * h * 4]
    lines = [bytes(b for i, b in enumerate(px[y * w * 4:(y + 1) * w * 4]) if i % 4 != 3) for y in range(h)]
    if not d[17] & 0x20:
        lines.reverse()
    return w, h, lines


a, b = rows(sys.argv[1]), rows(sys.argv[2])
if a[:2] != b[:2]:
    sys.exit(f"sizes differ: {a[:2]} and {b[:2]}")
diff = sum(1 for x, y in zip(a[2], b[2]) if x != y)
if diff:
    sys.exit(f"{diff} of {a[1]} lines differ")
print(f"{a[0]}x{a[1]}, every pixel the same")
