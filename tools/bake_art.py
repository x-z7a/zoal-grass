#!/usr/bin/env python3
"""Bake the DINKIssTyle grass art into the plugin's own atlas and description (build-time only).

Input: the DINKIssTyle_LIB_Grass folder of the original "DINKIssTyle 3D Grass" download (not kept in this repo):
Texture/3D_Grass_7.dds + _NRM.png (Lawn07 tuft sprites and the dense blade strip), Texture/3D_Grass.dds + _NRM.png
(flower sprites), 3D/Lawn07.for (tuft sprite rectangles), Flower/Lawn06_S_Flower01.for (flower plants).

Outputs (resources/):
  textures/zg_atlas_<style>.png  1024 x 512: left = 3D_Grass_7 with ambient occlusion baked toward the blade bases,
                                 right = the flower sprites of 3D_Grass; graded per season style
  textures/zg_atlas_NRM.png      the matching normal maps
  grass_art.txt                  sprite / strip rectangles in atlas UV, and the flower plants

No .for forest is ever placed in X-Plane; the plugin builds instanced OBJ clumps from these.
Usage: bake_art.py <DINKIssTyle_LIB_Grass dir> <resources dir>
"""
import collections
import os
import struct
import sys
import zlib

AW, AH = 1024, 512

# grade: (saturation, tint rgb, tint amount, brightness). "summer" is the base grade: DINKIssTyle's colours a little
# darker and calmer so they sit on X-Plane's terrain instead of glowing on it.
STYLES = {
    "summer": (0.85, (120, 140, 70), 0.10, 0.80),
    "spring": (1.00, (110, 190, 60), 0.20, 0.86),
    "late_summer": (0.75, (200, 180, 95), 0.30, 0.80),
    "autumn": (0.65, (175, 130, 70), 0.45, 0.74),
    "winter": (0.32, (176, 152, 114), 0.62, 0.72),
}
AO_BASE = 0.45          # brightness at the blade bases (1.0 at the tips)


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def decode_dxt5(path):
    d = open(path, "rb").read()
    if d[:4] != b"DDS " or d[84:88] != b"DXT5":
        sys.exit("expected a DXT5 .dds: " + path)
    h, w = struct.unpack_from("<II", d, 12)
    px = bytearray(w * h * 4)
    pos = 128
    for by in range(h // 4):
        for bx in range(w // 4):
            b = d[pos:pos + 16]
            pos += 16
            a0, a1 = b[0], b[1]
            ab = int.from_bytes(b[2:8], "little")
            if a0 > a1:
                ap = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
            else:
                ap = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
            c0, c1 = rgb565(b[8] | b[9] << 8), rgb565(b[10] | b[11] << 8)
            c2 = tuple((2 * p + q) // 3 for p, q in zip(c0, c1))
            c3 = tuple((p + 2 * q) // 3 for p, q in zip(c0, c1))
            cp = [c0, c1, c2, c3]
            cb = int.from_bytes(b[12:16], "little")
            for k in range(16):
                r, g, bb = cp[(cb >> 2 * k) & 3]
                o = ((by * 4 + (k >> 2)) * w + bx * 4 + (k & 3)) * 4
                px[o:o + 4] = bytes((r, g, bb, ap[(ab >> 3 * k) & 7]))
    return w, h, px


def read_png(path):
    """Minimal PNG reader for 8-bit RGB / RGBA, non-interlaced (the DINKIssTyle normal maps)."""
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack_from(">IIBB", body)
            if depth != 8 or ctype not in (2, 6):
                sys.exit("unsupported PNG " + path)
            ch = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(w):
            o = (y * w + x) * 4
            out[o:o + 3] = line[x * ch:x * ch + 3]
            out[o + 3] = line[x * ch + 3] if ch == 4 else 255
        prev = line
    return w, h, out


def write_png(path, w, h, px, alpha=True):
    rows = []
    for y in range(h):
        row = px[y * w * 4:(y + 1) * w * 4]
        if not alpha:
            row = bytes(b for i, b in enumerate(row) if i % 4 != 3)
        rows.append(b"\0" + bytes(row))

    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


def read_for(path):
    verts, idx, in_mesh = [], [], False
    for line in open(path, errors="replace"):
        t = line.split()
        if not t:
            continue
        if t[0] == "MESH":
            if in_mesh:
                break
            in_mesh = True
        elif t[0] == "VERTEX" and in_mesh:
            verts.append([float(v) for v in t[1:9]])
        elif t[0] == "IDX" and in_mesh:
            idx += [int(v) for v in t[1:]]
    return verts, idx


def components(verts, idx):
    parent = list(range(len(verts)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for k in range(0, len(idx), 3):
        a = find(idx[k])
        parent[find(idx[k + 1])] = a
        parent[find(idx[k + 2])] = a
    comps, tris = collections.defaultdict(list), collections.defaultdict(list)
    for i in range(len(verts)):
        comps[find(i)].append(i)
    for k in range(0, len(idx), 3):
        tris[find(idx[k])].append(idx[k:k + 3])
    return [(m, tris[r]) for r, m in comps.items()]


def blit(dst, dw, src, sw, sx0, sy0, sx1, sy1, dx0, dy0):
    for y in range(sy0, sy1):
        so = (y * sw + sx0) * 4
        do = ((dy0 + y - sy0) * dw + dx0) * 4
        dst[do:do + (sx1 - sx0) * 4] = src[so:so + (sx1 - sx0) * 4]


def grade(px, sat, tint, amount, bright):
    tl = 0.299 * tint[0] + 0.587 * tint[1] + 0.114 * tint[2]
    tn = [c / tl for c in tint]
    out = bytearray(px)
    lut = {}
    for o in range(0, len(px), 4):
        if px[o + 3] == 0:
            continue
        key = (px[o], px[o + 1], px[o + 2])
        res = lut.get(key)
        if res is None:
            lum = 0.299 * key[0] + 0.587 * key[1] + 0.114 * key[2]
            c = [lum + (v - lum) * sat for v in key]
            c = [v * (1 - amount) + lum * t * amount for v, t in zip(c, tn)]
            res = lut[key] = bytes(max(0, min(255, int(v * bright + 0.5))) for v in c)
        out[o:o + 3] = res
    return out


def main(art, res):
    tex = os.path.join(art, "Texture")
    w7, h7, g7 = decode_dxt5(os.path.join(tex, "3D_Grass_7.dds"))
    w3, h3, g3 = decode_dxt5(os.path.join(tex, "3D_Grass.dds"))
    assert (w7, h7) == (512, 512) and (w3, h3) == (1024, 1024)

    # --- Lawn07 tuft sprites: rectangles from the mesh (t low = tips, t high = base: the sprite is stored upside down)
    verts, idx = read_for(os.path.join(art, "3D", "Lawn07.for"))
    rects = collections.Counter()
    for members, _tris in components(verts, idx):
        if len(members) != 4:
            continue
        s = [verts[i][6] for i in members]
        t = [verts[i][7] for i in members]
        rects[(round(min(s), 3), round(max(s), 3), round(min(t), 3), round(max(t), 3))] += 1
    sprites = sorted(r for r, n in rects.items() if n >= 3)
    # merge near-duplicates (same sprite cell)
    cells = []
    for r in sprites:
        if not any(abs(r[0] - c[0]) < 0.02 for c in cells):
            cells.append(r)
    cells.sort()

    # --- the blade strip: alpha bounding box in the right part of 3D_Grass_7 (blades grow leftwards from x = 511)
    xs, ys = [], []
    for y in range(0, 380):
        for x in range(360, 512):
            if g7[(y * 512 + x) * 4 + 3] > 128:
                xs.append(x)
                ys.append(y)
    sx0, sx1, sy0, sy1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    split = (sy0 + sy1) // 2

    # --- ambient occlusion toward the blade bases
    def ao(px, x0, x1, y0, y1, base_side):
        for y in range(y0, y1):
            for x in range(x0, x1):
                if base_side == "top":
                    f = (y - y0) / max(1, y1 - y0 - 1)
                else:                                    # base at the right edge
                    f = (x1 - 1 - x) / max(1, x1 - x0 - 1)
                k = AO_BASE + (1 - AO_BASE) * min(1.0, f * 1.4)
                o = (y * 512 + x) * 4
                px[o] = int(px[o] * k)
                px[o + 1] = int(px[o + 1] * k)
                px[o + 2] = int(px[o + 2] * k)

    g7 = bytearray(g7)
    g7_raw = bytes(g7)                    # before ambient occlusion: the colour source of the 3D blades
    for s0, s1, t0, t1 in cells:
        ao(g7, int(s0 * 512), int(s1 * 512), int((1 - t1) * 512), int((1 - t0) * 512), "top")
    ao(g7, sx0, sx1, sy0, sy1, "right")

    # --- atlas: left half grass 7, right half the flower sprites of 3D_Grass (x 0..512, y 0..200 of 1024)
    atlas = bytearray(AW * AH * 4)
    blit(atlas, AW, g7, 512, 0, 0, 512, 512, 0, 0)
    blit(atlas, AW, g3, 1024, 0, 0, 512, 200, 512, 0)
    # blade colour strip: the blade strip without occlusion, a little brighter, every other row, at (520, 220)
    BX, BY = 520, 220
    bh = (sy1 - sy0) // 2
    for y in range(bh):
        for x in range(sx0, sx1):
            so = ((sy0 + 2 * y) * 512 + x) * 4
            do = ((BY + y) * AW + BX + (x - sx0)) * 4
            r, g, b, a = g7_raw[so:so + 4]
            atlas[do:do + 4] = bytes((min(255, int(r * 1.25)), min(255, int(g * 1.25)), min(255, int(b * 1.2)), a))
    n7 = read_png(os.path.join(tex, "3D_Grass_7_NRM.png"))
    n3 = read_png(os.path.join(tex, "3D_Grass_NRM.png"))
    natlas = bytearray([128, 128, 255, 255] * (AW * AH))
    blit(natlas, AW, n7[2], n7[0], 0, 0, 512, 512, 0, 0)
    blit(natlas, AW, n3[2], n3[0], 0, 0, 512, 200, 512, 0)

    out_tex = os.path.join(res, "textures")
    os.makedirs(out_tex, exist_ok=True)
    for name in os.listdir(out_tex):
        os.remove(os.path.join(out_tex, name))
    write_png(os.path.join(out_tex, "zg_atlas_NRM.png"), AW, AH, natlas, alpha=False)
    for name, g in STYLES.items():
        write_png(os.path.join(out_tex, "zg_atlas_%s.png" % name), AW, AH, grade(atlas, *g))
        print("wrote zg_atlas_%s.png" % name)

    def u7(s):
        return s * 0.5

    with open(os.path.join(res, "grass_art.txt"), "w", newline="\n") as f:
        f.write("ZGART 2\n# baked from DINKIssTyle 3D Grass (Lawn07 sprites, 3D_Grass_7 blade strip, Lawn06 flowers)\n")
        f.write("# SPRITE dry u_left u_right v_base v_tip   (an upright card)\n")
        for n, (s0, s1, t0, t1) in enumerate(cells):
            dry = 1 if n >= len(cells) // 2 else 0       # the right-hand sprites are the yellow ones
            f.write("SPRITE %d %.5f %.5f %.5f %.5f\n" % (dry, u7(s0), u7(s1), t1, t0))
        f.write("# STRIP dry u_base u_tip v_start v_end   (blades along u, the strip runs along v)\n")
        for dry, (ya, yb) in ((0, (sy0, split)), (1, (split, sy1))):
            f.write("STRIP %d %.5f %.5f %.5f %.5f\n" % (dry, u7(sx1 / 512), u7(sx0 / 512), 1 - ya / 512, 1 - yb / 512))
        f.write("# BLADE dry u_base u_tip v_start v_end   (colour source of the 3D blades, same layout as STRIP)\n")
        bsplit = BY + (split - sy0) // 2
        for dry, (ya, yb) in ((0, (BY, bsplit)), (1, (bsplit, BY + bh))):
            f.write("BLADE %d %.5f %.5f %.5f %.5f\n" % (dry, (BX + sx1 - sx0) / AW, BX / AW, 1 - ya / AH, 1 - yb / AH))
        # flowers: Lawn06_S_Flower01 plants, UVs moved into the right half of the atlas
        fv, fi = read_for(os.path.join(art, "Flower", "Lawn06_S_Flower01.for"))
        heads = (((0.0, 0.11), (0.905, 1.0)), ((0.11, 0.21), (0.905, 1.0)), ((0.0, 0.11), (0.81, 0.905)),
                 ((0.11, 0.21), (0.81, 0.905)))

        def head_type(m):
            s = sum(fv[i][6] for i in m) / len(m)
            t = sum(fv[i][7] for i in m) / len(m)
            for k, ((a0, a1), (b0, b1)) in enumerate(heads):
                if a0 <= s < a1 and b0 <= t < b1:
                    return k
            return -1

        comps = components(fv, fi)
        hs = [(m, t, head_type(m)) for m, t in comps if head_type(m) >= 0]
        parts = [(m, t) for m, t in comps if head_type(m) < 0]
        def centre(m):
            return sum(fv[i][0] for i in m) / len(m), sum(fv[i][2] for i in m) / len(m)

        cs = [centre(h[0]) for h in hs]
        plants = [[h] for h in hs]
        for m, t in parts:
            px, pz = centre(m)
            k = min(range(len(cs)), key=lambda n: (cs[n][0] - px) ** 2 + (cs[n][1] - pz) ** 2)
            plants[k].append((m, t, -1))
        for k, plant in enumerate(plants):
            members = [i for p in plant for i in p[0]]
            tris = [t for p in plant for t in p[1]]
            ymin = min(fv[i][1] for i in members)
            bx, bz = cs[k]
            local = {i: n for n, i in enumerate(members)}
            f.write("FLOWER %d %d %d\n" % (len(members), len(tris), plant[0][2]))
            for i in members:
                v = fv[i]
                u = (512 + v[6] * 1024) / AW
                vv = 1 - ((1 - v[7]) * 1024) / AH
                f.write("V %.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f\n"
                        % (v[0] - bx, v[1] - ymin, v[2] - bz, v[3], v[4], v[5], u, vv))
            for a, b, c in tris:
                f.write("I %d %d %d\n" % (local[a], local[b], local[c]))
    print("sprites", len(cells), "strip px", (sx0, sx1, sy0, sy1), "flowers", len(plants))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
