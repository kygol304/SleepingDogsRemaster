# Aperçu d'un modèle d'arme : vue de côté et 3/4.
#   blender --background <modele>.blend --python preview.py -- <image.png>
import bpy, math, sys
from mathutils import Vector

out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
import os
tex = bpy.data.filepath[:-6] + "_tex.png"
img = bpy.data.images.load(tex) if os.path.exists(tex) else None
for o in list(scene.objects):
    if o.type != "MESH":
        continue
    m = bpy.data.materials.new(o.name)
    if img:
        m.use_nodes = True
        t = m.node_tree.nodes.new("ShaderNodeTexImage")
        t.image = img
        m.node_tree.nodes.active = t
    else:
        c = o.get("rgb", (200, 200, 200))
        m.diffuse_color = (c[0] / 255, c[1] / 255, c[2] / 255, 1)
    o.data.materials.clear()
    o.data.materials.append(m)

scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.color_type = "TEXTURE" if img else "MATERIAL"
scene.display.shading.light = "STUDIO"
scene.display.shading.show_cavity = True
scene.render.resolution_x = 1600
scene.render.resolution_y = 600
scene.world = bpy.data.worlds.new("w")
scene.world.color = (0.75, 0.75, 0.78)

pts = [o.matrix_world @ Vector(v) for o in scene.objects if o.type == "MESH" for v in o.bound_box]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
ctr = (lo + hi) / 2
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
scene.collection.objects.link(cam)
scene.camera = cam
cam.data.type = "ORTHO"
cam.data.ortho_scale = (hi.y - lo.y) * 1.15
cam.location = ctr + Vector((1.5, 0.35, 0.25))
d = ctr - cam.location
cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
