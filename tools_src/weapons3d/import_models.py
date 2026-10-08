# Convertit des modèles glTF (Sketchfab, CC-BY) en fichiers .skm pour le mod.
#   blender --background --python import_models.py -- <dossier assets> <dossier de sortie>
#
# Axes du jeu : longueur sur Y (canon vers -Y), haut +Z, poignée près de l'origine, en mètres.
# Les deux modèles importés ont le canon vers +X et le haut vers +Z.
#
# Textures : le shader utilisé en jeu (HK_SIMPLE) n'a qu'une couleur diffuse. On cuit donc une
# seule texture par modèle : couleur de base, assombrie là où la surface est métallique (un métal
# paraît sombre sans reflets), multipliée par l'éclairage d'un ciel uniforme (occlusion + relief
# des cartes normales). Plusieurs matériaux -> cases côte à côte dans la texture.
#
# Format .skm "SKM3" (petit-boutiste) : u32 nb sommets, u32 nb indices, u32 largeur, u32 hauteur,
#   sommets { f32 pos[3], f32 normale[3], f32 uv[2], u8 rgba[4] }, indices u16,
#   u32 taille du PNG, PNG de la texture.
import bpy, bmesh, math, os, struct, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
ASSETS, OUT = args[0], args[1]

MODELS = {
    "ak47": {
        "file": "ak_47/scene.gltf",
        "exclude": [],
        "length": 0.88,                      # longueur réelle (m)
        "grip": (-35.0, 34.1),               # poignée (x, z) dans les unités du fichier
        "grip_target": (0.0, -0.06),         # où la poignée doit tomber en jeu (y, z)
        "cell": 2048,
        "gain": 1.2, "sat": 1.0,             # étalonnage final (luminosité, saturation)
    },
    "rpg7": {
        "file": "rpg_7/scene.gltf",
        "exclude": ["WoodenHeatShield_1", "WoodenHeatShield_2", "Open_"],   # roquette en éclaté
        "length": 1.30,                      # RPG-7 chargé (le fichier ne fait que 0,96 m)
        "grip": (0.0743, 0.094),
        "grip_target": (0.02, -0.085),       # tube dans l'axe du MGL (z ~ 0.03), poignée en main
        "cell": 1024,
        "gain": 0.8, "sat": 0.55,            # bois d'origine très orange : désaturé
    },
}


def load(cfg):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ASSETS, cfg["file"]))
    for o in list(bpy.context.scene.objects):
        if o.type == "MESH" and any(e in o.name for e in cfg["exclude"]):
            bpy.data.objects.remove(o, do_unlink=True)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    # Transformations appliquées, un seul objet.
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def to_game(obj, cfg):
    me = obj.data
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    s = cfg["length"] / (max(xs) - min(xs)) if cfg["length"] else 1.0
    cy = (min(ys) + max(ys)) / 2
    gx, gz = cfg["grip"]
    ty, tz = cfg["grip_target"]
    # (x, y, z) fichier -> (y, -x, z) jeu : canon vers -Y, repère direct conservé.
    for v in me.vertices:
        x, y, z = v.co
        v.co = Vector((s * (y - cy), -s * (x - gx) + ty, s * (z - gz) + tz))
    me.update()
    return s


def prepare_materials(obj):
    """Métal assombri dans la couleur de base, BSDF non métallique pour la cuisson diffuse."""
    for m in obj.data.materials:
        nt = m.node_tree
        bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if not bsdf:
            continue
        base_in, met_in = bsdf.inputs["Base Color"], bsdf.inputs["Metallic"]
        base_src = base_in.links[0].from_socket if base_in.links else None
        met_src = met_in.links[0].from_socket if met_in.links else None
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = 0.5
        if met_src:
            nt.links.new(met_src, mul.inputs[0])
        else:
            mul.inputs[0].default_value = met_in.default_value
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.inputs[7].default_value = (0.0, 0.0, 0.0, 1.0)
        nt.links.new(mul.outputs[0], mix.inputs[0])
        if base_src:
            nt.links.new(base_src, mix.inputs[6])
        else:
            mix.inputs[6].default_value = base_in.default_value
        nt.links.new(mix.outputs[2], base_in)
        for l in list(met_in.links):
            nt.links.remove(l)
        met_in.default_value = 0.0
        bsdf.inputs["Specular IOR Level"].default_value = 0.0


def material_uv(m, me):
    """Carte UV d'un matériau : celle d'un nœud « UV Map » s'il y en a un (glTF TEXCOORD_1), sinon
    la première. Toutes ses textures sont ensuite reliées explicitement à cette carte."""
    nt = m.node_tree
    name = next((n.uv_map for n in nt.nodes if n.type == "UVMAP" and n.uv_map), me.uv_layers[0].name)
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = name
    for n in nt.nodes:
        if n.type == "TEX_IMAGE":
            for l in list(n.inputs["Vector"].links):
                nt.links.remove(l)
            nt.links.new(uvn.outputs["UV"], n.inputs["Vector"])
    return name


def atlas_uv(obj):
    """Nouvelle carte UV : la carte d'origine de chaque matériau placée dans sa case."""
    me = obj.data
    srcs = [me.uv_layers[material_uv(m, me)] for m in me.materials]
    print("UV des materiaux :", [s.name for s in srcs])
    n = max(1, len(me.materials))
    dst = me.uv_layers.new(name="atlas")
    for poly in me.polygons:
        cell = poly.material_index
        src = srcs[cell]
        for li in poly.loop_indices:
            u, v = src.data[li].uv
            u -= math.floor(u) if (u < 0 or u > 1) else 0
            v -= math.floor(v) if (v < 0 or v > 1) else 0
            dst.data[li].uv = ((cell + min(max(u, 0.0), 1.0)) / n, min(max(v, 0.0), 1.0))
    me.uv_layers.active = dst     # la cuisson écrit dans l'atlas
    return n


def grade(img, gain, sat):
    import numpy as np
    px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)
    rgb = px[:, :3]
    lum = (rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32))[:, None]
    rgb = (lum + (rgb - lum) * sat) * gain
    px[:, :3] = np.clip(rgb, 0.0, 1.0)
    img.pixels[:] = px.ravel()


def bake(obj, name, n, cell, gain=1.0, sat=1.0):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    world = bpy.data.worlds.new("ciel")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (1, 1, 1, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    scene.world = world
    w, h = cell * n, cell
    img = bpy.data.images.new(name + "_bake", w, h, alpha=False)
    for m in obj.data.materials:
        node = m.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = img
        m.node_tree.nodes.active = node
    rb = scene.render.bake
    rb.margin = 6
    rb.use_pass_direct = True
    rb.use_pass_indirect = True
    rb.use_pass_color = True
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="DIFFUSE")
    grade(img, gain, sat)
    path = os.path.join(OUT, name + "_tex.png")
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return path, w, h


def export(obj, name, png, w, h):
    dg = bpy.context.evaluated_depsgraph_get()
    eo = obj.evaluated_get(dg)
    me = eo.to_mesh()
    me.calc_loop_triangles()
    uvl = me.uv_layers["atlas"].data
    cn = me.corner_normals
    verts, idx, seen = [], [], {}
    for tri in me.loop_triangles:
        for li in tri.loops:
            co = me.vertices[me.loops[li].vertex_index].co
            nrm = cn[li].vector
            uv = uvl[li].uv
            key = (round(co.x, 5), round(co.y, 5), round(co.z, 5), round(nrm.x, 3), round(nrm.y, 3), round(nrm.z, 3), round(uv.x, 5), round(uv.y, 5))
            k = seen.get(key)
            if k is None:
                k = len(verts)
                seen[key] = k
                verts.append((co.x, co.y, co.z, nrm.x, nrm.y, nrm.z, uv.x, 1.0 - uv.y))
            idx.append(k)
    eo.to_mesh_clear()
    assert len(verts) < 65535, "trop de sommets : %d" % len(verts)
    data = open(png, "rb").read()
    out = os.path.join(OUT, name + ".skm")
    with open(out, "wb") as fp:
        fp.write(b"SKM3" + struct.pack("<IIII", len(verts), len(idx), w, h))
        for v in verts:
            fp.write(struct.pack("<8f4B", *v, 255, 255, 255, 255))
        fp.write(struct.pack("<%dH" % len(idx), *idx))
        fp.write(struct.pack("<I", len(data)))
        fp.write(data)
    xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
    print("SKM %s : %d sommets, %d triangles, texture %dx%d (%d Ko), x[%.3f %.3f] y[%.3f %.3f] z[%.3f %.3f]" % (
        name, len(verts), len(idx) // 3, w, h, len(data) // 1024, min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, name + ".blend"))


os.makedirs(OUT, exist_ok=True)
only = args[2:] or list(MODELS)
for name in only:
    cfg = MODELS[name]
    obj = load(cfg)
    to_game(obj, cfg)
    prepare_materials(obj)
    n = atlas_uv(obj)
    png, w, h = bake(obj, name, n, cfg["cell"], cfg.get("gain", 1.0), cfg.get("sat", 1.0))
    export(obj, name, png, w, h)
