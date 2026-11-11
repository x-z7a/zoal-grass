#!/usr/bin/env python3
"""Pack the Blender species sprites into the season atlases (build-time only).

    blender -b --factory-startup -P tools/blender_species.py -- <sprite dir>
    python3 tools/compose_atlas.py <sprite dir> resources

The atlas grows from 1024 x 512 (the DINKIssTyle art of tools/bake_art.py) to 2048 x 1024: the original art stays in
the bottom-left quarter (its UVs are halved) and the species sprites are shelf-packed into the rest, including the left
half of that quarter (the Lawn07 tuft sprites and fringe strip, no longer used: their SPRITE / STRIP lines are dropped).
The blade colour strip and the flower sprites (right half of the original art) are kept. Each sprite is graded
per season with bake_art's grading, and its colour is bled into the transparent pixels so mipmaps do not darken the
cut-out edges. grass_art.txt becomes ZGART 3 with one CARD line per sprite:

    CARD name side|top u_left v_base u_right v_top width_m height_m

Re-running is safe: an already composed atlas is cut back to its original quarter first.
Needs Pillow and numpy.
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_art import STYLES  # noqa: E402  (the same season grades as the original art)

AW, AH = 2048, 1024
OW, OH = 1024, 512           # original art, bottom-left
PAD = 6                      # pixels between sprites (mip levels down to 1/8 stay apart)
FREE = [(0, 0, AW, AH - OH), (OW, AH - OH, AW - OW, OH), (0, AH - OH, OW // 2 - PAD, OH)]   # (x, y, w, h), y from top


def grade(rgb, sat, tint, amount, bright):
    """numpy version of bake_art.grade."""
    t = np.array(tint, dtype=np.float32)
    tn = t / (0.299 * t[0] + 0.587 * t[1] + 0.114 * t[2])
    lum = (0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2])[..., None]
    c = lum + (rgb - lum) * sat
    c = c * (1 - amount) + lum * tn * amount
    return np.clip(c * bright + 0.5, 0, 255)


def bleed(img):
    """Spread the colour of opaque pixels into the transparent ones (alpha unchanged)."""
    rgb = img[..., :3].astype(np.float32)
    known = img[..., 3] > 24
    if not known.any():
        return img
    for _ in range(24):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]
            cnt += k
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    if not known.all():
        rgb[~known] = rgb[known].mean(axis=0)
    out = img.copy()
    out[..., :3] = rgb.astype(np.uint8)
    return out


def lum_mean(rgba):
    m = rgba[..., 3] > 128
    c = rgba[..., :3][m].astype(np.float32)
    return float((c @ np.array([0.299, 0.587, 0.114], np.float32)).mean()) if len(c) else 0.0


def pack(sprites):
    """MaxRects packing (best short side fit), largest first; every sprite keeps PAD pixels to its right and below.
    Returns {name: (x, y)} or exits when the atlas is full."""
    free = [(x, y, w + PAD, h + PAD) for x, y, w, h in FREE]   # the region edges need no padding
    places = {}
    for name, _kind, w, h, *_ in sorted(sprites, key=lambda s: -s[2] * s[3]):
        pw, ph = w + PAD, h + PAD
        best = None
        for fx, fy, fw, fh in free:
            if pw <= fw and ph <= fh:
                score = (min(fw - pw, fh - ph), max(fw - pw, fh - ph))
                if best is None or score < best[0]:
                    best = (score, fx, fy)
        if best is None:
            sys.exit("atlas full: no room for %s (%dx%d)" % (name, w, h))
        _, x, y = best
        places[name] = (x, y)
        nxt = []
        for fx, fy, fw, fh in free:                       # split every free rect the sprite overlaps
            if x >= fx + fw or x + pw <= fx or y >= fy + fh or y + ph <= fy:
                nxt.append((fx, fy, fw, fh))
                continue
            if x > fx:
                nxt.append((fx, fy, x - fx, fh))
            if x + pw < fx + fw:
                nxt.append((x + pw, fy, fx + fw - x - pw, fh))
            if y > fy:
                nxt.append((fx, fy, fw, y - fy))
            if y + ph < fy + fh:
                nxt.append((fx, y + ph, fw, fy + fh - y - ph))
        free = [a for a in nxt if not any(a != b and b[0] <= a[0] and b[1] <= a[1] and a[0] + a[2] <= b[0] + b[2]
                                          and a[1] + a[3] <= b[1] + b[3] for b in nxt)]
    return places


def original(path, mode):
    im = Image.open(path).convert(mode)
    if im.size == (AW, AH):
        im = im.crop((0, AH - OH, OW, AH))
    if im.size != (OW, OH):
        sys.exit("unexpected atlas size %s: %s" % (im.size, path))
    return im


def main(sprite_dir, res):
    sprites = []
    for line in open(os.path.join(sprite_dir, "species.txt")):
        t = line.split()
        if t and t[0] == "SPR":
            sprites.append((t[1], t[2], int(t[3]), int(t[4]), float(t[5]), float(t[6])))
    places = pack(sprites)
    imgs = {s[0]: np.array(Image.open(os.path.join(sprite_dir, s[0] + ".png")).convert("RGBA")) for s in sprites}

    # The lawn clumps take over from the 3D blades at a distance: match their brightness to the blade colour strip
    # (one gain for all sprites, so the plants keep their relative tones).
    tex = os.path.join(res, "textures")
    ref = np.array(original(os.path.join(tex, "zg_atlas_summer.png"), "RGBA"))
    blade = [line.split() for line in open(os.path.join(res, "grass_art.txt")) if line.startswith("BLADE 0")]
    if blade and "lawn_lush" in imgs:
        b = [float(v) for v in blade[0][2:6]]
        if open(os.path.join(res, "grass_art.txt")).readline().startswith("ZGART 3"):
            b = [v * 2 for v in b]                       # back to the original quarter's UVs
        x0, x1 = sorted((int(b[0] * OW), int(b[1] * OW)))
        y0, y1 = sorted((int((1 - b[2]) * OH), int((1 - b[3]) * OH)))
        target = lum_mean(ref[y0:y1, x0:x1]) / STYLES["summer"][3] * 0.9   # before the summer grade
        have = lum_mean(imgs["lawn_lush"])
        gain = min(1.6, max(0.8, target / max(1.0, have)))
        print("sprite gain %.2f (lawn %.0f, blades %.0f)" % (gain, have, target))
        for im in imgs.values():
            im[..., :3] = np.clip(im[..., :3].astype(np.float32) * gain, 0, 255).astype(np.uint8)
    imgs = {k: bleed(v) for k, v in imgs.items()}

    nrm = Image.new("RGB", (AW, AH), (128, 128, 255))
    nrm.paste(original(os.path.join(tex, "zg_atlas_NRM.png"), "RGB"), (0, AH - OH))
    nrm.paste((128, 128, 255), (0, AH - OH, OW // 2, AH))
    for s in sprites:                                     # the sprites are lit by their vertex normals only
        x, y = places[s[0]]
        nrm.paste((128, 128, 255), (x, y, x + s[2], y + s[3]))
    nrm.save(os.path.join(tex, "zg_atlas_NRM.png"), optimize=True)
    for style, g in STYLES.items():
        atlas = np.zeros((AH, AW, 4), np.uint8)
        atlas[AH - OH:, :OW] = np.array(original(os.path.join(tex, "zg_atlas_%s.png" % style), "RGBA"))
        atlas[AH - OH:, :OW // 2] = 0                    # the unused Lawn07 sprites and fringe strip
        for s in sprites:
            x, y = places[s[0]]
            im = imgs[s[0]].copy()
            im[..., :3] = grade(im[..., :3].astype(np.float32), *g).astype(np.uint8)
            atlas[y:y + s[3], x:x + s[2]] = im
        Image.fromarray(atlas, "RGBA").save(os.path.join(tex, "zg_atlas_%s.png" % style), optimize=True)
        print("wrote zg_atlas_%s.png" % style)

    # grass_art.txt: halve the original UVs (ZGART 2 only), replace the CARD lines
    path = os.path.join(res, "grass_art.txt")
    lines = open(path).read().splitlines()
    old = lines[0].startswith("ZGART 2")
    out = ["ZGART 3", "ATLAS %d %d" % (AW, AH)]
    for line in lines[1:]:
        t = line.split()
        if not t or t[0] in ("ATLAS", "CARD", "SPRITE", "STRIP") or line.startswith(("# CARD", "# SPRITE", "# STRIP")):
            continue
        if old and t[0] == "BLADE":
            line = "%s %s %s" % (t[0], t[1], " ".join("%.5f" % (float(v) * 0.5) for v in t[2:6]))
        elif old and t[0] == "V":
            line = "V %s %.5f %.5f" % (" ".join(t[1:7]), float(t[7]) * 0.5, float(t[8]) * 0.5)
        out.append(line.replace(", same layout as STRIP", ""))
    out.append("# CARD name side|top u_left v_base u_right v_top width_m height_m   (Blender species sprites)")
    for name, kind, w, h, wm, hm in sprites:
        x, y = places[name]
        u0, u1 = (x + 0.5) / AW, (x + w - 0.5) / AW
        vb, vt = 1 - (y + h - 0.5) / AH, 1 - (y + 0.5) / AH
        out.append("CARD %s %s %.5f %.5f %.5f %.5f %.4f %.4f" % (name, kind, u0, vb, u1, vt, wm, hm))
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("packed %d sprites into %dx%d; %s" % (len(sprites), AW, AH, path))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
