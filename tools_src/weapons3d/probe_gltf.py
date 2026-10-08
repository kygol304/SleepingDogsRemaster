# Importe un glTF, affiche ses dimensions et rend 3 vues orthographiques (X, Y, Z) pour repérer
# l'orientation du modèle.  blender --background --python probe_gltf.py -- <scene.gltf> <prefixe image>
import bpy, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args[0])
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == "MESH"]
pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
print("PROBE objets", len(meshes), "min", tuple(round(v, 4) for v in lo), "max", tuple(round(v, 4) for v in hi))
for o in meshes:
    print("PROBE", o.name, len(o.data.vertices), "sommets", [m.name for m in o.data.materials])

scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.color_type = "TEXTURE"
scene.display.shading.light = "STUDIO"
scene.render.resolution_x = 1200
scene.render.resolution_y = 600
w = bpy.data.worlds.new("w"); w.color = (0.7, 0.7, 0.72); scene.world = w
ctr = (lo + hi) / 2
size = max(hi - lo)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam)
scene.camera = cam
cam.data.type = "ORTHO"
cam.data.ortho_scale = size * 1.1
for axis, d in (("X", Vector((1, 0, 0))), ("Y", Vector((0, 1, 0))), ("Z", Vector((0, 0, 1)))):
    cam.location = ctr + d * size * 2
    # vue de côté : haut de l'image = +Z (vue de dessus : haut = +Y)
    up = Vector((0, 0, 1)) if axis != "Z" else Vector((0, 1, 0))
    fwd = -d
    right = fwd.cross(up).normalized()
    from mathutils import Matrix
    cam.matrix_world = Matrix.Translation(cam.location) @ Matrix((right, up, -fwd)).transposed().to_4x4()
    scene.render.filepath = "%s_%s.png" % (args[1], axis)
    bpy.ops.render.render(write_still=True)
