"""Model the zoal-grass plant species in Blender and render them into alpha sprites (build-time only).

blender -b --factory-startup -P tools/blender_species.py -- <out dir> [only_name ...]

Every species is built from real geometry (tapered curved blades, spikelet seed heads, panicles, clover leaflets,
toothed rosette leaves, moss domes), lit by a soft sky plus a high sun so clumps get dark interiors and bright tips, and
rendered orthographically with a transparent background:
  side sprites  - camera looking north, ground at the bottom edge: upright cards in the sim
  top sprites   - camera looking down: flat cards lying on the ground (clover, rosettes)
Writes <out dir>/<name>.png and <out dir>/species.txt ("SPR name side|top w_px h_px w_m h_m"), which
tools/compose_atlas.py packs into the season atlases.
"""
import math
import os
import random
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/zg_species"
ONLY = set(argv[1:])
os.makedirs(OUT, exist_ok=True)


def lin(c):
    """sRGB 0..1 -> linear."""
    return tuple(v ** 2.2 for v in c)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def jitter(c, rng, amount):
    k = 1 + (rng.random() - 0.5) * amount
    h = (rng.random() - 0.5) * amount * 0.5
    return (min(1, c[0] * k * (1 + h)), min(1, c[1] * k), min(1, c[2] * k * (1 - h)))


class Geo:
    """Triangles with per-vertex colours (linear)."""

    def __init__(self):
        self.v, self.f, self.c = [], [], []

    def add(self, p, col):
        self.v.append(p)
        self.c.append(lin(col) + (1.0,))
        return len(self.v) - 1

    def tri(self, a, b, c):
        self.f.append((a, b, c))

    # ---- a grass blade: tapered, leaning, curving strip from root to tip
    def blade(self, root, h, phi, lean, droop, width, base_col, tip_col, segs=5, twist=0.0, tip_frac=1.0):
        dx, dy = math.cos(phi), math.sin(phi)
        sx, sy = -dy, dx
        tw = twist
        sx, sy = sx * math.cos(tw) + dx * math.sin(tw), sy * math.cos(tw) + dy * math.sin(tw)
        prev = None
        for k in range(segs + 1):
            t = k / segs
            out = lean * h * t + droop * h * t * t
            x, y = root[0] + dx * out, root[1] + dy * out
            z = root[2] + h * t * (1 - 0.45 * droop * t)
            col = mix(base_col, tip_col, min(1.0, t ** 0.8 * tip_frac))
            if k == segs:
                tip = self.add((x, y, z), col)
                self.tri(prev[0], prev[1], tip)
                break
            w = width * (1 - 0.85 * t ** 1.6) * 0.5
            a = self.add((x - sx * w, y - sy * w, z), col)
            b = self.add((x + sx * w, y + sy * w, z), col)
            if prev:
                self.tri(prev[0], prev[1], b)
                self.tri(prev[0], b, a)
            prev = (a, b)

    # ---- a small flat quad (spikelet, petal, moss leaf) centred at p along direction d
    def scale_quad(self, p, d, side, length, width, col, col2=None):
        ex = [d[i] * length * 0.5 for i in range(3)]
        sx = [side[i] * width * 0.5 for i in range(3)]
        a = self.add(tuple(p[i] - ex[i] for i in range(3)), col)
        b = self.add(tuple(p[i] - sx[i] for i in range(3)), col)
        c = self.add(tuple(p[i] + ex[i] for i in range(3)), col2 or col)
        e = self.add(tuple(p[i] + sx[i] for i in range(3)), col)
        self.tri(a, b, c)
        self.tri(a, c, e)

    # ---- thin stem polyline as a narrow ribbon facing the camera (side sprites)
    def stem(self, pts, width, col_a, col_b):
        prev = None
        n = len(pts)
        for k, p in enumerate(pts):
            t = k / (n - 1)
            col = mix(col_a, col_b, t)
            w = width * (1 - 0.4 * t) * 0.5
            a = self.add((p[0] - w, p[1], p[2]), col)
            b = self.add((p[0] + w, p[1], p[2]), col)
            if prev:
                self.tri(prev[0], prev[1], b)
                self.tri(prev[0], b, a)
            prev = (a, b)

    # ---- leaf from an outline: strip between a midrib and both edges, lying almost flat
    def leaf(self, base, phi, length, width_fn, lift, col_mid, col_edge, n=10, wave=None):
        dx, dy = math.cos(phi), math.sin(phi)
        sx, sy = -dy, dx
        mids, lefts, rights = [], [], []
        for k in range(n + 1):
            t = k / n
            x, y = base[0] + dx * length * t, base[1] + dy * length * t
            z = base[2] + lift * length * math.sin(t * math.pi * 0.8)
            w = width_fn(t) * length
            ww = w * (wave(t) if wave else 1.0)
            mids.append(self.add((x, y, z), col_mid))
            lefts.append(self.add((x - sx * ww, y - sy * ww, z - 0.002), col_edge))
            wr = w * (wave(t + 0.5 / n) if wave else 1.0)      # the teeth alternate between the two edges
            rights.append(self.add((x + sx * wr, y + sy * w, z - 0.002), col_edge))
        for k in range(n):
            self.tri(mids[k], lefts[k], lefts[k + 1])
            self.tri(mids[k], lefts[k + 1], mids[k + 1])
            self.tri(mids[k], mids[k + 1], rights[k + 1])
            self.tri(mids[k], rights[k + 1], rights[k])


def palette(name):
    # (base, mid, tip) sRGB
    return {
        "lush": ((0.14, 0.20, 0.07), (0.32, 0.48, 0.16), (0.66, 0.78, 0.36)),
        "yellowgreen": ((0.17, 0.21, 0.07), (0.42, 0.52, 0.18), (0.80, 0.82, 0.42)),
        "bluegreen": ((0.12, 0.19, 0.11), (0.28, 0.44, 0.25), (0.58, 0.72, 0.50)),
        "dry": ((0.24, 0.21, 0.12), (0.56, 0.49, 0.29), (0.90, 0.82, 0.60)),
        "straw": ((0.30, 0.25, 0.14), (0.68, 0.59, 0.35), (0.94, 0.87, 0.66)),
        "olive": ((0.17, 0.17, 0.09), (0.42, 0.42, 0.23), (0.78, 0.74, 0.52)),
        "sedge": ((0.08, 0.12, 0.05), (0.22, 0.34, 0.14), (0.44, 0.52, 0.24)),
    }[name]


# ------------------------------------------------------------------------------------------------ species builders
def lawn(g, rng, pal, n=520, rx=0.29, ry=0.14, hmin=0.08, hmax=0.30, dead=0.06):
    base, mid, tip = palette(pal)
    for _ in range(n):
        a, r = rng.random() * 2 * math.pi, math.sqrt(rng.random())
        root = (math.cos(a) * r * rx, math.sin(a) * r * ry, 0.0)
        h = hmin + (hmax - hmin) * rng.random() ** 0.8 * (1 - 0.35 * r)       # taller in the middle: a rounded clump
        if rng.random() < dead:
            b, t = palette("dry")[0], palette("dry")[2]
        else:
            b, t = jitter(base, rng, 0.3), jitter(mix(mid, tip, rng.random()), rng, 0.3)
        g.blade(root, h, rng.random() * 2 * math.pi, 0.1 + 0.35 * rng.random(), 0.1 + 0.6 * rng.random() ** 2,
                0.004 + 0.004 * rng.random(), b, t, segs=4, twist=(rng.random() - 0.5) * 0.8)


def panicle(g, rng, top, length, col, col2):
    """Feathery meadow-grass panicle (Poa / bent / fescue): whorls of drooping branches with spikelets."""
    whorls = 7
    for w in range(whorls):
        t = w / whorls
        z = top[2] - length * t
        nb = 4 + rng.randrange(4)
        for _b in range(nb):
            phi = rng.random() * 2 * math.pi
            bl = length * (0.25 + 0.45 * t) * (0.7 + 0.6 * rng.random())
            up = 0.6 - 0.5 * t
            d = (math.cos(phi) * (1 - up * 0.5), math.sin(phi) * (1 - up * 0.5), up)
            for s in range(8):
                f = (s + 1) / 8
                p = (top[0] + d[0] * bl * f, top[1] + d[1] * bl * f, z + d[2] * bl * f - 0.03 * f * f)
                side = (-math.sin(phi), math.cos(phi), 0)
                g.scale_quad(p, d, side, 0.013, 0.006, mix(col, col2, rng.random()), col2)
    g.stem([(top[0], top[1], top[2] - length), top], 0.0015, col, col2)


def spike(g, rng, top, length, radius, col, col2, awns=False):
    """Dense cylindrical seed spike (timothy / foxtail / wheat-like)."""
    n = int(length / 0.004 * 4)
    for k in range(n):
        t = k / n
        a = k * 2.39996 + rng.random() * 0.3                      # golden-angle phyllotaxis
        r = radius * (1 - 0.6 * abs(t - 0.4) ** 1.5)
        z = top[2] - length * t
        d = (math.cos(a) * 0.5, math.sin(a) * 0.5, 0.85)
        p = (top[0] + math.cos(a) * r, top[1] + math.sin(a) * r, z)
        side = (-math.sin(a), math.cos(a), 0)
        g.scale_quad(p, d, side, 0.008, 0.004, mix(col, col2, rng.random() * 0.7), col2)
        if awns and k % 3 == 0:
            q = (p[0] + d[0] * 0.012, p[1] + d[1] * 0.012, p[2] + 0.02)
            g.scale_quad(q, (d[0] * 0.3, d[1] * 0.3, 1), side, 0.035, 0.0012, col2)


def meadow(g, rng, pal):
    lawn(g, rng, pal, n=380, hmin=0.12, hmax=0.36)
    base, mid, tip = palette(pal)
    for _ in range(12):
        x, y = (rng.random() - 0.5) * 0.45, (rng.random() - 0.5) * 0.15
        h = 0.40 + 0.20 * rng.random()
        lean = (rng.random() - 0.5) * 0.12
        top = (x + lean, y, h)
        g.stem([(x, y, 0), (x + lean * 0.4, y, h * 0.5), top], 0.0022, mid, tip)
        spikelet, spikelet_tip = mix(mid, (0.50, 0.40, 0.40), 0.3), mix(tip, (0.62, 0.56, 0.40), 0.5)
        panicle(g, rng, top, 0.15 + 0.07 * rng.random(), spikelet, spikelet_tip)


def seed_tall(g, rng, pal, awns):
    base, mid, tip = palette(pal)
    lawn(g, rng, pal, n=300, rx=0.27, hmin=0.18, hmax=0.45, dead=0.3)
    for _ in range(22):
        x, y = (rng.random() - 0.5) * 0.5, (rng.random() - 0.5) * 0.16
        h = 0.55 + 0.3 * rng.random()
        lean = (rng.random() - 0.5) * 0.2
        top = (x + lean, y, h)
        g.stem([(x, y, 0), (x + lean * 0.3, y, h * 0.5), (x + lean * 0.75, y, h * 0.85), top], 0.0025, mid, tip)
        spike(g, rng, top, 0.06 + 0.05 * rng.random(), 0.006, mix(mid, tip, 0.5), tip, awns)


def tussock(g, rng, pal):
    base, mid, tip = palette(pal)
    straw = palette("straw")
    for _ in range(380):
        a, r = rng.random() * 2 * math.pi, math.sqrt(rng.random()) * 0.075
        root = (math.cos(a) * r, math.sin(a) * r, 0.0)
        outer = r / 0.075
        h = 0.22 + 0.28 * rng.random() * (1.1 - 0.5 * outer)
        dead = rng.random() < 0.3
        b, t = (straw[0], straw[2]) if dead else (jitter(base, rng, 0.3), jitter(tip, rng, 0.25))
        g.blade(root, h, a + (rng.random() - 0.5) * 0.6, 0.15 + 0.55 * outer, 0.2 + 0.7 * outer * rng.random(),
                0.0028, b, t, segs=5)


def moss(g, rng):
    cols = [(0.26, 0.40, 0.11), (0.38, 0.52, 0.16), (0.52, 0.60, 0.24)]
    ring = []                                                # dark core so the gaps read as depth, not holes
    for i in range(7):
        row = []
        for j in range(24):
            v, u = (i / 6) * math.pi / 2, j / 24 * 2 * math.pi
            row.append(g.add((math.sin(v) * math.cos(u) * 0.225, math.sin(v) * math.sin(u) * 0.11,
                              math.cos(v) * 0.12), mix((0.10, 0.16, 0.05), (0.22, 0.34, 0.09), 1 - i / 6)))
        ring.append(row)
    for i in range(6):
        for j in range(24):
            a, b, c, d = ring[i][j], ring[i][(j + 1) % 24], ring[i + 1][(j + 1) % 24], ring[i + 1][j]
            g.tri(a, b, c)
            g.tri(a, c, d)
    for _ in range(9000):
        # point on an ellipsoidal dome (rx 0.24, ry 0.12, h 0.13), denser on top
        u, v = rng.random() * 2 * math.pi, math.acos(rng.random() ** 0.7)
        nx, ny, nz = math.sin(v) * math.cos(u), math.sin(v) * math.sin(u), math.cos(v)
        bump = 1 + 0.12 * math.sin(5 * u) * math.sin(3 * v)
        p = (nx * 0.24 * bump, ny * 0.12 * bump, nz * 0.13 * bump)
        d = (nx + (rng.random() - 0.5) * 0.8, ny + (rng.random() - 0.5) * 0.8, nz + 0.3)
        m = math.sqrt(sum(c * c for c in d))
        d = tuple(c / m for c in d)
        side = (-d[1], d[0], 0) if abs(d[2]) < 0.95 else (1, 0, 0)
        col = mix(cols[rng.randrange(3)], (0.08, 0.12, 0.04), max(0, 0.6 - nz) * 0.6)
        g.scale_quad(p, d, side, 0.009, 0.004, col, mix(col, (0.6, 0.62, 0.3), 0.3))


def sedge(g, rng):
    base, mid, tip = palette("sedge")
    for _ in range(46):
        a = rng.random() * 2 * math.pi
        root = (math.cos(a) * 0.03 * rng.random(), math.sin(a) * 0.03 * rng.random(), 0.0)
        h = 0.32 + 0.3 * rng.random()
        g.blade(root, h, a, 0.1 + 0.45 * rng.random(), 0.15 + 0.6 * rng.random() ** 2, 0.006, base,
                jitter(mix(mid, tip, rng.random()), rng, 0.2), segs=7)


def clover(g, rng):
    green, light = (0.16, 0.34, 0.08), (0.48, 0.62, 0.36)
    for _ in range(70):
        a, r = rng.random() * 2 * math.pi, 0.18 * math.sqrt(rng.random())
        c = (math.cos(a) * r, math.sin(a) * r, 0.02 + 0.03 * rng.random())
        rot = rng.random() * 2 * math.pi
        size = 0.011 + 0.008 * rng.random()
        col = jitter(green, rng, 0.3)
        for k in range(3):                                   # three obcordate leaflets with the pale chevron
            phi = rot + k * 2 * math.pi / 3
            dx, dy, sx, sy = math.cos(phi), math.sin(phi), -math.sin(phi), math.cos(phi)
            hub = g.add(c, mix(col, (0.05, 0.1, 0.02), 0.4))
            ring, chev = [], []
            for s in range(11):
                th = -1.2 + 2.4 * s / 10
                notch = 1 - 0.18 * math.exp(-(th * 5) ** 2)
                rr = size * (0.55 + 0.5 * math.cos(th * 0.7)) * notch
                px, py = math.cos(th) * rr * 1.1, math.sin(th) * rr
                ring.append(g.add((c[0] + dx * px + sx * py, c[1] + dy * px + sy * py, c[2]), col))
                chev.append(g.add((c[0] + dx * px * 0.55 + sx * py * 0.55, c[1] + dy * px * 0.55 + sy * py * 0.55,
                                   c[2] + 0.0005), mix(col, light, 0.7 if abs(th) > 0.2 else 0.2)))
            for s in range(10):
                g.tri(hub, chev[s], chev[s + 1])
                g.tri(chev[s], ring[s], ring[s + 1])
                g.tri(chev[s], ring[s + 1], chev[s + 1])


def rosette(g, rng, kind):
    if kind == "dandelion":
        n, length, col_mid, col_edge = 13, 0.16, (0.45, 0.55, 0.30), (0.14, 0.30, 0.07)

        def width(t):
            return 0.11 * math.sin(math.pi * min(1, t * 1.05)) ** 0.8 + 0.02

        def wave(t):                                          # runcinate teeth pointing back to the centre
            return 0.45 + 0.75 * ((t * 6) % 1.0)
    else:                                                     # broadleaf plantain
        n, length, col_mid, col_edge = 10, 0.13, (0.42, 0.52, 0.30), (0.16, 0.32, 0.10)

        def width(t):
            return 0.32 * math.sin(math.pi * t ** 0.8) ** 0.9

        wave = None
    for k in range(n):
        phi = k * 2.39996 + rng.random() * 0.3
        L = length * (0.65 + 0.5 * rng.random())
        g.leaf((0, 0, 0.005 + 0.002 * k), phi, L, width, 0.10 + 0.1 * rng.random(), jitter(col_mid, rng, 0.2),
               jitter(col_edge, rng, 0.25), n=14, wave=wave)


# ---- the sprite list: name, kind, metres (w, h), pixels (w, h), builder
SPECIES = [
    ("lawn_lush", "side", (0.64, 0.32), (496, 248), lambda g, r: lawn(g, r, "lush")),
    ("lawn_yellowgreen", "side", (0.64, 0.32), (496, 248), lambda g, r: lawn(g, r, "yellowgreen")),
    ("lawn_bluegreen", "side", (0.64, 0.32), (496, 248), lambda g, r: lawn(g, r, "bluegreen")),
    ("lawn_dry", "side", (0.64, 0.32), (496, 248), lambda g, r: lawn(g, r, "dry", dead=0.5)),
    # the same clumps seen from above: flat canopy cards that fill the field for high and overhead views
    ("lawn_top_lush", "top", (0.64, 0.64), (224, 224), lambda g, r: lawn(g, r, "lush", n=700, rx=0.25, ry=0.25)),
    ("lawn_top_yellowgreen", "top", (0.64, 0.64), (224, 224),
     lambda g, r: lawn(g, r, "yellowgreen", n=700, rx=0.25, ry=0.25)),
    ("lawn_top_bluegreen", "top", (0.64, 0.64), (224, 224),
     lambda g, r: lawn(g, r, "bluegreen", n=700, rx=0.25, ry=0.25)),
    ("lawn_top_dry", "top", (0.64, 0.64), (224, 224), lambda g, r: lawn(g, r, "dry", n=700, rx=0.25, ry=0.25, dead=0.5)),
    ("meadow_a", "side", (0.56, 0.70), (256, 320), lambda g, r: meadow(g, r, "lush")),
    ("meadow_b", "side", (0.56, 0.70), (256, 320), lambda g, r: meadow(g, r, "yellowgreen")),
    ("seed_timothy", "side", (0.64, 0.96), (256, 384), lambda g, r: seed_tall(g, r, "olive", False)),
    ("seed_wild", "side", (0.64, 0.96), (256, 384), lambda g, r: seed_tall(g, r, "straw", True)),
    ("tussock_green", "side", (0.80, 0.48), (384, 224), lambda g, r: tussock(g, r, "olive")),
    ("tussock_dry", "side", (0.80, 0.48), (384, 224), lambda g, r: tussock(g, r, "dry")),
    ("moss", "side", (0.56, 0.18), (384, 128), lambda g, r: moss(g, r)),
    ("sedge", "side", (0.80, 0.56), (320, 224), lambda g, r: sedge(g, r)),
    ("clover", "top", (0.40, 0.40), (256, 256), lambda g, r: clover(g, r)),
    ("rosette_dandelion", "top", (0.36, 0.36), (224, 224), lambda g, r: rosette(g, r, "dandelion")),
    ("rosette_plantain", "top", (0.30, 0.30), (192, 192), lambda g, r: rosette(g, r, "plantain")),
]


# ------------------------------------------------------------------------------------------------ scene and render
def setup_scene():
    scene = bpy.context.scene
    for ob in list(scene.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 96
    scene.cycles.use_denoising = False
    scene.cycles.filter_width = 1.0
    scene.cycles.max_bounces = 4
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    world = bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.92, 0.96, 1.0, 1)
    bg.inputs["Strength"].default_value = 1.0
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.6
    sun.data.angle = math.radians(8)
    sun.rotation_euler = (math.radians(35), 0, math.radians(20))   # high, from in front of and left of the camera
    scene.collection.objects.link(sun)
    mat = bpy.data.materials.new("plant")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "col"
    diff = nt.nodes.new("ShaderNodeBsdfDiffuse")
    trans = nt.nodes.new("ShaderNodeBsdfTranslucent")
    mixs = nt.nodes.new("ShaderNodeMixShader")
    mixs.inputs[0].default_value = 0.3                      # sunlight through the blades: bright backlit tips
    nt.links.new(attr.outputs["Color"], diff.inputs["Color"])
    nt.links.new(attr.outputs["Color"], trans.inputs["Color"])
    nt.links.new(diff.outputs[0], mixs.inputs[1])
    nt.links.new(trans.outputs[0], mixs.inputs[2])
    nt.links.new(mixs.outputs[0], out.inputs["Surface"])
    return scene, mat


def render(scene, mat, name, kind, size_m, size_px, build, seed):
    size_m = (size_m[0], size_m[0] * size_px[1] / size_px[0])     # metres follow the pixel aspect
    for ob in list(scene.objects):
        if ob.type in ("MESH", "CAMERA"):
            bpy.data.objects.remove(ob, do_unlink=True)
    g = Geo()
    build(g, random.Random(seed))
    me = bpy.data.meshes.new(name)
    me.from_pydata(g.v, [], g.f)
    ca = me.color_attributes.new("col", "FLOAT_COLOR", "POINT")
    for i, c in enumerate(g.c):
        ca.data[i].color = c
    me.materials.append(mat)
    me.update()
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    w, h = size_m
    cam.data.ortho_scale = max(w, h)
    if kind == "side":
        cam.location = (0, -5, h / 2)
        cam.rotation_euler = (math.radians(90), 0, 0)
    else:
        cam.location = (0, 0, 5)
        cam.rotation_euler = (0, 0, 0)
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = size_px
    scene.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("rendered %s: %d triangles" % (name, len(g.f)))


scene, mat = setup_scene()
lines = []
for k, (name, kind, size_m, size_px, build) in enumerate(SPECIES):
    if not ONLY or name in ONLY:
        render(scene, mat, name, kind, size_m, size_px, build, 1000 + k * 37)
    lines.append("SPR %s %s %d %d %.4f %.4f\n" % (name, kind, size_px[0], size_px[1], size_m[0],
                                                   size_m[0] * size_px[1] / size_px[0]))
with open(os.path.join(OUT, "species.txt"), "w") as f:
    f.writelines(lines)
