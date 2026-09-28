#!/usr/bin/env python3
"""Makes a level set of one's own from LEVELS.DAT: one tile changed.

A DAT file is a table of resources; level N is resource 2000+N, a checksum
byte and then the level as SDLPoP's level_type lays it out (the foreground
tiles first, 30 per room). This sets the low five bits (the tile type) of one
tile and keeps the rest of its byte - what a level editor does.

usage: make-levels.py <LEVELS.DAT> <out> <level> <room 1-24> <tile 0-29> <tile type>
"""
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
level, room, tile, kind = (int(a) for a in sys.argv[3:7])
data = bytearray(open(src, "rb").read())
table_offset, table_size = struct.unpack_from("<IH", data, 0)
count = struct.unpack_from("<H", data, table_offset)[0]
for i in range(count):
    rid, off, size = struct.unpack_from("<HIH", data, table_offset + 2 + i * 8)
    if rid == 2000 + level:
        at = off + 1 + (room - 1) * 30 + tile
        data[at] = (data[at] & 0xE0) | (kind & 0x1F)
        open(out, "wb").write(data)
        sys.exit(0)
sys.exit(f"no level {level} in {src}")
