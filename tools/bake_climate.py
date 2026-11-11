#!/usr/bin/env python3
"""Bake the Koppen-Geiger climate map into resources/climate_kg.bin (build-time only).

Input: Beck_KG_V1_present_0p083.tif from Beck et al. 2018, "Present and future Koppen-Geiger climate classification
maps at 1-km resolution" (CC BY 4.0, https://doi.org/10.6084/m9.figshare.6396959; licenses/Beck-KG-NOTICE.txt).
Output: a 0.25 degree grid (1440 x 720, row 0 = 90N, column 0 = 180W) of the 30 Koppen classes (0 = water). Each cell
takes the most common land class of its 3 x 3 source cells, so coastal airports still find their climate.

File: "ZGKG 1 <w> <h>\n" then run-length pairs (count: u16 little endian, class: u8), row by row.
Usage: bake_climate.py <Beck_KG_V1_present_0p083.tif> <resources dir>    (needs Pillow)
"""
import collections
import os
import struct
import sys

from PIL import Image

STEP = 3            # 0.0833 deg -> 0.25 deg


def main(tif, res):
    Image.MAX_IMAGE_PIXELS = None
    im = Image.open(tif)
    sw, sh = im.size
    src = im.tobytes()
    w, h = sw // STEP, sh // STEP
    grid = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            c = collections.Counter()
            for dy in range(STEP):
                o = (y * STEP + dy) * sw + x * STEP
                for v in src[o:o + STEP]:
                    if 0 < v <= 30:
                        c[v] += 1
            grid[y * w + x] = c.most_common(1)[0][0] if c else 0
    out = bytearray(b"ZGKG 1 %d %d\n" % (w, h))
    k = 0
    while k < len(grid):
        v, n = grid[k], 1
        while k + n < len(grid) and grid[k + n] == v and n < 65535:
            n += 1
        out += struct.pack("<HB", n, v)
        k += n
    path = os.path.join(res, "climate_kg.bin")
    with open(path, "wb") as f:
        f.write(out)
    print("wrote %s: %dx%d, %d bytes" % (path, w, h, len(out)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
