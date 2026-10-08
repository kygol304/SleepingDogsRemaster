# Icônes de la roue pour les nouvelles armes : silhouette blanche sur fond transparent, canon vers
# la droite, comme les icônes du jeu (Icons_Weapon_*).
#   blender --background --python render_icons.py -- <dossier assets> <dossier de sortie>
import bpy, os, sys
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index("--") + 1:]
ASSETS, OUT = args[0], args[1]
ICONS = {
    "SKR_Icon_AK47": ("ak_47/scene.gltf", []),
    "SKR_Icon_RPG7": ("rpg_7/scene.gltf", ["WoodenHeatShield_1", "WoodenHeatShield_2", "Open_"]),
}
W, H = 512, 256

for name, (path, exclude) in ICONS.items():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ASSETS, path))
    for o in list(bpy.context.scene.objects):
        if o.type == "MESH" and any(e in o.name for e in exclude):
            bpy.data.objects.remove(o, do_unlink=True)
    scene = bpy.context.scene
    meshes = [o for o in scene.objects if o.type == "MESH"]
    pts = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    ctr = (lo + hi) / 2
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (1, 1, 1)
    scene.render.film_transparent = True
    scene.render.resolution_x, scene.render.resolution_y = W, H
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.type = "ORTHO"
    # Vue de côté depuis -Y : +X (canon) à droite, +Z en haut ; marge de 6 %.
    cam.data.ortho_scale = max(hi.x - lo.x, (hi.z - lo.z) * W / H) * 1.06
    loc = ctr + Vector((0, -(hi.y - lo.y) * 4 - 1, 0))
    right, up, back = Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, -1, 0))
    cam.matrix_world = Matrix.Translation(loc) @ Matrix((right, up, back)).transposed().to_4x4()
    cam.data.clip_end = 1e5
    scene.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("ICONE", name)
