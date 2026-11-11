"""Render generated Zoal Grass clump objects in Blender, to judge the look without X-Plane.

blender -b --factory-startup -P tools/preview_blender.py -- <dir with zg_*.obj> <out prefix> [spacing_m] [grid]

Reads the X-Plane OBJ8 files the plugin writes (VT / IDX / TRIS of the nearest LOD, TEXTURE), lays clumps out on the
same grid the plugin uses (quarter-turn rotations, small jitter), and renders an eye-level and a low aerial view with
Cycles. Writes <out prefix>_eye.png and <out prefix>_air.png.
"""
import glob
import math
import os
import random
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = argv[0], argv[1]
SPACING = float(argv[2]) if len(argv) > 2 else 4.5
GRID = int(argv[3]) if len(argv) > 3 else 9


def read_obj8(path):
    verts, idx, lods, tex = [], [], [], None
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == "TEXTURE":
            tex = t[1]
        elif t[0] == "VT":
            verts.append([float(v) for v in t[1:9]])
        elif t[0] in ("IDX", "IDX10"):
            idx += [int(v) for v in t[1:]]
        elif t[0] == "TRIS":
            lods.append((int(t[1]), int(t[2])))
    return verts, idx, lods, tex


def make_mesh(name, verts, idx, first, count):
    used = sorted(set(idx[first:first + count]))
    remap = {v: n for n, v in enumerate(used)}
    # X-Plane: x east, y up, z south  ->  Blender: x east, y north, z up
    co = [(verts[v][0], -verts[v][2], verts[v][1]) for v in used]
    faces = [tuple(remap[idx[k + j]] for j in range(3)) for k in range(first, first + count, 3)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(co, [], faces)
    uv = me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = used[me.loops[li].vertex_index]
            uv.data[li].uv = (verts[v][6], verts[v][7])
    normals = [(verts[used[me.loops[li].vertex_index]][3], -verts[used[me.loops[li].vertex_index]][5],
                verts[used[me.loops[li].vertex_index]][4]) for li in range(len(me.loops))]
    me.normals_split_custom_set(normals)
    me.update()
    return me


def material(tex_path):
    mat = bpy.data.materials.new("grass")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    img = nt.nodes.new("ShaderNodeTexImage")
    img.image = bpy.data.images.load(tex_path)
    img.interpolation = "Linear"
    # alpha test at 0.5, like GLOBAL_no_blend 0.5
    gt = nt.nodes.new("ShaderNodeMath")
    gt.operation = "GREATER_THAN"
    gt.inputs[1].default_value = 0.5
    nt.links.new(img.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(img.outputs["Alpha"], gt.inputs[0])
    nt.links.new(gt.outputs[0], bsdf.inputs["Alpha"])
    bsdf.inputs["Roughness"].default_value = 0.9
    try:
        bsdf.inputs["Subsurface Weight"].default_value = 0.0
    except KeyError:
        pass
    return mat


scene = bpy.context.scene
for ob in list(scene.objects):          # run with --factory-startup; drop the default cube, light and camera
    bpy.data.objects.remove(ob, do_unlink=True)
scene.render.engine = "CYCLES"
scene.cycles.samples = 48
scene.cycles.use_denoising = True
scene.render.resolution_x, scene.render.resolution_y = 1280, 720
scene.view_settings.view_transform = "AgX"

world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.55, 0.65, 0.85, 1)    # plain sky: Blender's physical sky differs by version
bg.inputs["Strength"].default_value = 0.8

sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 4.0
sun.rotation_euler = (math.radians(55), 0, math.radians(140))
scene.collection.objects.link(sun)

# ground: a muted green-brown like X-Plane's airport grass terrain
half_size = GRID * SPACING * 0.8
gme = bpy.data.meshes.new("ground")
hs_ = half_size
corners = [(-hs_, -hs_, 0), (hs_, -hs_, 0), (hs_, hs_, 0), (-hs_, hs_, 0)]
gme.from_pydata(corners, [], [(0, 1, 2, 3)])
ground = bpy.data.objects.new("ground", gme)
scene.collection.objects.link(ground)
gm = bpy.data.materials.new("ground")
gm.use_nodes = True
gb = gm.node_tree.nodes["Principled BSDF"]
gb.inputs["Base Color"].default_value = (0.075, 0.08, 0.04, 1)     # X-Plane-like dry grass terrain
gb.inputs["Roughness"].default_value = 1.0
gme.materials.append(gm)

paths = sorted(p for p in glob.glob(os.path.join(SRC, "zg_*_n_*.obj")))
if not paths:
    sys.exit("no zg_*_n_*.obj in " + SRC)
LOD_NEAR = float(argv[4]) if len(argv) > 4 else 40.0
LOD_MID = float(argv[5]) if len(argv) > 5 else 140.0
meshes = []          # [variant][lod]
mat = None
for path in paths:
    verts, idx, lods, tex = read_obj8(path)
    if mat is None:
        mat = material(os.path.join(SRC, tex))
    per = []
    for n, (first, count) in enumerate(lods):
        me = make_mesh("%s_%d" % (os.path.basename(path), n), verts, idx, first, count)
        me.materials.append(mat)
        per.append(me)
    meshes.append(per)
print("clump meshes:", len(meshes), "triangles per level:", [len(m.polygons) for m in meshes[0]])

half = GRID // 2
clumps = []


def place(eye):
    """(Re)build the clump objects, each with the level of detail X-Plane would pick for this camera."""
    for ob in clumps:
        bpy.data.objects.remove(ob, do_unlink=True)
    clumps.clear()
    rng = random.Random(3)
    for i in range(-half, half + 1):
        for j in range(-half, half + 1):
            var = meshes[rng.randrange(len(meshes))]
            rot = rng.randrange(4)
            loc = Vector((i * SPACING, j * SPACING, -0.03))
            d = (loc - Vector(eye)).length
            lod = 0 if d < LOD_NEAR else 1 if d < LOD_MID else 2
            ob = bpy.data.objects.new("c%d_%d" % (i, j), var[min(lod, len(var) - 1)])
            ob.location = loc
            ob.rotation_euler = (0, 0, math.radians(90 * rot))
            scene.collection.objects.link(ob)
            clumps.append(ob)


def shoot(name, loc, target, lens):
    place(loc)
    cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
    cam.data.lens = lens
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.filepath = "%s_%s.png" % (OUT, name)
    bpy.ops.render.render(write_still=True)


extent = half * SPACING
shoot("eye", (0, -extent + 6.0, 1.7), (0, 0, 0.0), 35)
shoot("close", (0, -extent + 10.0, 0.6), (0, -extent + 14.0, 0.1), 35)
shoot("air", (-extent * 0.3, -extent - 8.0, 15.0), (0, extent * 0.2, 0), 35)
