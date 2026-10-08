# Construit les modèles des nouvelles armes dans Blender et les exporte au format .skm du mod.
#   blender --background --python build_models.py -- <dossier de sortie>
#
# Axes du jeu (relevés sur ASSAULTRIFLE001_A) : longueur sur Y, canon vers -Y, haut vers +Z,
# poignée près de l'origine. Unité : mètre.
#
# Format .skm (petit-boutiste) : "SKM2", u32 nb sommets, u32 nb indices, u32 largeur, u32 hauteur,
#   sommets { f32 pos[3], f32 normale[3], f32 uv[2], u8 rgba[4] }, indices u16,
#   puis la texture RGBA8 (ligne du haut d'abord).
import bpy, bmesh, math, os, struct, sys
from mathutils import Vector, Matrix

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "."

METAL = (38, 38, 40)
METAL2 = (60, 60, 64)
WOOD = (118, 58, 26)
WOOD_DK = (84, 40, 18)
BAKELITE = (130, 52, 22)
OLIVE = (72, 82, 46)
OLIVE_DK = (52, 60, 34)


def reset():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def tag(obj, col):
    obj["rgb"] = col
    return obj


def box(name, x0, x1, y0, y1, z0, z1, col, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.object
    o.name = name
    o.scale = ((x1 - x0), (y1 - y0), (z1 - z0))
    o.location = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        m = o.modifiers.new("b", "BEVEL")
        m.width = bevel
        m.segments = 2
    return tag(o, col)


def cyl_y(name, r, y0, y1, x, z, col, verts=16, r2=None):
    """Cylindre (ou cône si r2) le long de Y, de y0 à y1."""
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=abs(y1 - y0))
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=abs(y1 - y0))
    o = bpy.context.object
    o.name = name
    o.rotation_euler = (math.radians(90), 0, 0)   # axe Z -> axe -Y : radius1 du côté +Y
    o.location = (x, (y0 + y1) / 2, z)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return tag(o, col)


def prism(name, pts_yz, x0, x1, col, bevel=0.0):
    """Profil polygonal dans le plan YZ, extrudé en épaisseur sur X."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    front = [bm.verts.new((x1, y, z)) for y, z in pts_yz]
    back = [bm.verts.new((x0, y, z)) for y, z in pts_yz]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    n = len(pts_yz)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((front[i], back[i], back[j], front[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    if bevel > 0:
        bpy.context.view_layer.objects.active = o
        m = o.modifiers.new("b", "BEVEL")
        m.width = bevel
        m.segments = 2
    return tag(o, col)


def curved_mag(name, y_top, z_top, length, width, depth, curve_deg, col, segs=8):
    """Chargeur courbe : tranches successives qui tournent vers l'avant (-Y)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    step = length / segs
    ang = 0.0
    cy, cz = y_top, z_top
    for i in range(segs + 1):
        # Repère local : direction de descente d, et avant f
        d = Vector((0, -math.sin(ang), -math.cos(ang)))
        f = Vector((0, -math.cos(ang), math.sin(ang)))
        c = Vector((0, cy, cz))
        hw = width / 2
        hd = depth / 2 * (1.0 + 0.15 * i / segs)
        ring = [bm.verts.new(c + Vector((sx * hw, 0, 0)) + f * (sf * hd)) for sx, sf in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        rings.append(ring)
        cy += d.y * step
        cz += d.z * step
        ang += math.radians(curve_deg) / segs
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    for a, b in zip(rings, rings[1:]):
        for k in range(4):
            l = (k + 1) % 4
            bm.faces.new((a[k], a[l], b[l], b[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return tag(o, col)


def shift(dx, dy, dz):
    for o in bpy.context.scene.objects:
        o.location = o.location + Vector((dx, dy, dz))


def build_ak():
    reset()
    zc = 0.035                     # axe du canon
    # Boîte de culasse et couvercle
    box("receiver", -0.022, 0.022, -0.14, 0.10, -0.005, 0.045, METAL, 0.004)
    prism("cover", [(-0.13, 0.045), (0.11, 0.045), (0.11, 0.060), (0.02, 0.066), (-0.13, 0.058)], -0.019, 0.019, METAL2, 0.003)
    box("trigger_guard", -0.005, 0.005, -0.03, 0.022, -0.042, -0.036, METAL)
    box("trigger_guard_front", -0.005, 0.005, -0.036, -0.03, -0.042, -0.004, METAL)
    box("trigger", -0.003, 0.003, -0.002, 0.006, -0.030, -0.004, METAL2)
    box("selector", 0.022, 0.026, -0.10, 0.02, 0.020, 0.030, METAL2)
    # Canon, tube de gaz, guidon, cache-flamme
    cyl_y("barrel", 0.0085, -0.56, -0.14, 0, zc, METAL, 14)
    cyl_y("muzzle", 0.012, -0.60, -0.56, 0, zc, METAL2, 14)
    cyl_y("gas_tube", 0.010, -0.42, -0.15, 0, zc + 0.022, METAL, 14)
    box("gas_block", -0.012, 0.012, -0.45, -0.41, zc - 0.012, zc + 0.034, METAL)
    box("front_sight", -0.006, 0.006, -0.535, -0.51, zc, zc + 0.036, METAL, 0.002)
    box("rear_sight", -0.013, 0.013, -0.17, -0.12, 0.045, 0.062, METAL, 0.002)
    # Garde-main bois (bas + dessus)
    prism("handguard", [(-0.40, zc - 0.035), (-0.15, zc - 0.035), (-0.15, zc + 0.012), (-0.40, zc + 0.008)], -0.024, 0.024, WOOD, 0.006)
    prism("upper_guard", [(-0.38, zc + 0.012), (-0.16, zc + 0.012), (-0.16, zc + 0.036), (-0.38, zc + 0.034)], -0.016, 0.016, WOOD, 0.005)
    # Poignée pistolet (vers l'arrière et le bas)
    prism("grip", [(0.020, -0.004), (0.060, -0.004), (0.095, -0.110), (0.060, -0.118)], -0.016, 0.016, BAKELITE, 0.006)
    # Crosse bois
    prism("stock", [(0.098, 0.040), (0.098, -0.002), (0.36, -0.075), (0.38, -0.075), (0.38, 0.030), (0.36, 0.034)], -0.020, 0.020, WOOD, 0.008)
    box("buttplate", -0.021, 0.021, 0.378, 0.388, -0.078, 0.033, METAL)
    # Chargeur courbe
    curved_mag("magazine", -0.085, -0.002, 0.20, 0.026, 0.050, 32, BAKELITE)
    shift(0, -0.055, 0)            # poignée sous la main (y ~ 0), comme le fusil du jeu
    return "ak47"


def build_rpg():
    reset()
    zc = 0.075                     # axe du tube, au-dessus de la poignée
    cyl_y("tube", 0.021, -0.45, 0.50, 0, zc, OLIVE_DK, 18)
    cyl_y("heat_guard_front", 0.030, -0.22, -0.05, 0, zc, WOOD, 18)
    cyl_y("heat_guard_rear", 0.030, 0.02, 0.18, 0, zc, WOOD, 18)
    cyl_y("exhaust", 0.045, 0.62, 0.50, 0, zc, OLIVE_DK, 18, r2=0.021)
    cyl_y("exhaust_ring", 0.047, 0.66, 0.62, 0, zc, METAL, 18)
    # Roquette : propulseur, ogive, coiffe
    cyl_y("rocket_motor", 0.020, -0.62, -0.45, 0, zc, OLIVE, 14)
    cyl_y("rocket_neck", 0.020, -0.66, -0.62, 0, zc, OLIVE, 14, r2=0.044)
    cyl_y("warhead", 0.044, -0.80, -0.66, 0, zc, OLIVE, 18)
    cyl_y("warhead_cone", 0.044, -0.92, -0.80, 0, zc, OLIVE, 18, r2=0.010)
    cyl_y("fuze", 0.010, -0.96, -0.92, 0, zc, METAL2, 10)
    # Poignées et détente
    prism("grip", [(-0.010, zc - 0.022), (0.030, zc - 0.022), (0.045, -0.060), (0.010, -0.066)], -0.015, 0.015, WOOD_DK, 0.005)
    prism("front_grip", [(-0.170, zc - 0.022), (-0.135, zc - 0.022), (-0.125, -0.040), (-0.160, -0.045)], -0.014, 0.014, WOOD_DK, 0.005)
    box("trigger_guard", -0.004, 0.004, -0.045, 0.0, zc - 0.062, zc - 0.056, METAL)
    box("trigger_guard_front", -0.004, 0.004, -0.051, -0.045, zc - 0.062, zc - 0.020, METAL)
    box("trigger", -0.003, 0.003, -0.025, -0.018, zc - 0.050, zc - 0.020, METAL2)
    # Viseur optique à gauche
    box("sight_mount", -0.040, -0.020, -0.04, 0.03, zc - 0.005, zc + 0.012, METAL)
    cyl_y("scope", 0.016, -0.06, 0.10, -0.052, zc + 0.020, METAL, 14)
    box("iron_sight", -0.004, 0.004, -0.30, -0.28, zc + 0.021, zc + 0.045, METAL)
    return "rpg7"


# ---- Matériaux procéduraux (cuits dans une texture) ------------------------------------------
KIND = {METAL: "steel", METAL2: "steel_lt", WOOD: "wood", WOOD_DK: "wood_dk", BAKELITE: "bakelite",
        OLIVE: "olive", OLIVE_DK: "olive_dk"}


def make_material(kind):
    m = bpy.data.materials.new(kind)
    m.use_nodes = True
    nt = m.node_tree
    N, L = nt.nodes, nt.links
    bsdf = N["Principled BSDF"]
    coord = N.new("ShaderNodeTexCoord")

    def noise(scale, detail=6.0, rough=0.6):
        n = N.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        L.new(coord.outputs["Object"], n.inputs["Vector"])
        return n

    def ramp(src, stops):
        r = N.new("ShaderNodeValToRGB")
        r.color_ramp.elements[0].position, r.color_ramp.elements[0].color = stops[0]
        r.color_ramp.elements[1].position, r.color_ramp.elements[1].color = stops[1]
        for pos, col in stops[2:]:
            e = r.color_ramp.elements.new(pos)
            e.color = col
        L.new(src, r.inputs["Fac"])
        return r

    def mix(a, b, fac):
        mx = N.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        L.new(fac, mx.inputs[0])
        L.new(a, mx.inputs[6])
        L.new(b, mx.inputs[7])
        return mx.outputs[2]

    def edge_wear(base, worn, amount=0.55):
        geo = N.new("ShaderNodeNewGeometry")
        r = ramp(geo.outputs["Pointiness"], [(amount, (0, 0, 0, 1)), (amount + 0.08, (1, 1, 1, 1))])
        nz = noise(90.0)
        mul = N.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        L.new(r.outputs["Color"], mul.inputs[0])
        L.new(nz.outputs["Fac"], mul.inputs[1])
        return mix(base, worn, mul.outputs[0])

    if kind in ("steel", "steel_lt"):
        dark = (0.035, 0.037, 0.042, 1) if kind == "steel" else (0.07, 0.07, 0.075, 1)
        lite = (0.075, 0.078, 0.085, 1) if kind == "steel" else (0.12, 0.12, 0.125, 1)
        base = ramp(noise(35.0).outputs["Fac"], [(0.35, dark), (0.75, lite)]).outputs["Color"]
        col = edge_wear(base, ramp(noise(200.0).outputs["Fac"], [(0.3, (0.32, 0.32, 0.33, 1)), (0.7, (0.45, 0.45, 0.46, 1))]).outputs["Color"])
    elif kind in ("wood", "wood_dk"):
        wave = N.new("ShaderNodeTexWave")
        wave.wave_type = "BANDS"
        wave.bands_direction = "Z"
        wave.inputs["Scale"].default_value = 9.0
        wave.inputs["Distortion"].default_value = 9.0
        wave.inputs["Detail"].default_value = 4.0
        L.new(coord.outputs["Object"], wave.inputs["Vector"])
        if kind == "wood":
            stops = [(0.15, (0.20, 0.07, 0.02, 1)), (0.85, (0.42, 0.17, 0.05, 1)), (0.5, (0.31, 0.11, 0.03, 1))]
        else:
            stops = [(0.15, (0.10, 0.04, 0.015, 1)), (0.85, (0.22, 0.09, 0.03, 1))]
        base = ramp(wave.outputs["Fac"], stops).outputs["Color"]
        col = edge_wear(base, (ramp(noise(60.0).outputs["Fac"], [(0.3, (0.45, 0.25, 0.10, 1)), (0.7, (0.55, 0.32, 0.14, 1))]).outputs["Color"]), 0.6)
    elif kind == "bakelite":
        base = ramp(noise(25.0).outputs["Fac"], [(0.3, (0.22, 0.06, 0.015, 1)), (0.8, (0.34, 0.10, 0.03, 1))]).outputs["Color"]
        col = edge_wear(base, (ramp(noise(80.0).outputs["Fac"], [(0.3, (0.45, 0.18, 0.06, 1)), (0.7, (0.5, 0.22, 0.08, 1))]).outputs["Color"]))
    else:  # olive, olive_dk : peinture militaire éraflée
        a = (0.10, 0.12, 0.05, 1) if kind == "olive" else (0.06, 0.075, 0.035, 1)
        b = (0.15, 0.17, 0.08, 1) if kind == "olive" else (0.09, 0.105, 0.05, 1)
        paint = ramp(noise(18.0).outputs["Fac"], [(0.3, a), (0.75, b)]).outputs["Color"]
        scratches = ramp(noise(140.0, 2.0, 0.9).outputs["Fac"], [(0.71, (0, 0, 0, 1)), (0.74, (1, 1, 1, 1))])
        metal = (0.22, 0.22, 0.21, 1)
        col = mix(paint, ramp(noise(300.0).outputs["Fac"], [(0.3, metal), (0.7, (0.3, 0.3, 0.29, 1))]).outputs["Color"], scratches.outputs["Color"])
        col = edge_wear(col, (ramp(noise(200.0).outputs["Fac"], [(0.3, (0.25, 0.25, 0.24, 1)), (0.7, (0.38, 0.38, 0.36, 1))]).outputs["Color"]))
    L.new(col, bsdf.inputs["Base Color"])
    return m


def export(name, size=1024):
    scene = bpy.context.scene
    mats = {}
    objs = [o for o in scene.objects if o.type == "MESH"]
    for o in objs:
        kind = KIND.get(tuple(o.get("rgb", METAL)), "steel")
        if kind not in mats:
            mats[kind] = make_material(kind)
        o.data.materials.clear()
        o.data.materials.append(mats[kind])
        bpy.context.view_layer.objects.active = o
        for mod in list(o.modifiers):
            bpy.ops.object.modifier_apply(modifier=mod.name)
    # Un seul objet, normales lissées sous 35° (les cylindres paraissent ronds), UV dépliées.
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35))
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004)
    bpy.ops.uv.pack_islands(margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")

    # Cuisson : couleur (DIFFUSE couleur seule) puis occlusion ambiante, combinées.
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    scene.render.bake.margin = 8
    imgs = {}
    for key in ("albedo", "ao"):
        img = bpy.data.images.new(name + "_" + key, size, size, alpha=False)
        imgs[key] = img
    def target(img):
        for m in obj.data.materials:
            n = m.node_tree.nodes.get("bake") or m.node_tree.nodes.new("ShaderNodeTexImage")
            n.name = "bake"
            n.image = img
            m.node_tree.nodes.active = n
    target(imgs["albedo"])
    scene.render.bake.use_pass_direct = False
    scene.render.bake.use_pass_indirect = False
    scene.render.bake.use_pass_color = True
    bpy.ops.object.bake(type="DIFFUSE")
    target(imgs["ao"])
    bpy.ops.object.bake(type="AO")
    alb = list(imgs["albedo"].pixels)
    ao = list(imgs["ao"].pixels)

    # Image finale RGBA8, ligne du haut d'abord (convention D3D : v = 0 en haut).
    px = bytearray(size * size * 4)
    for y in range(size):
        src_row = (size - 1 - y) * size * 4
        dst_row = y * size * 4
        for x in range(size):
            i = src_row + x * 4
            occ = 0.30 + 0.70 * ao[i]
            o = dst_row + x * 4
            for c in range(3):
                # pixels de l'image cuite déjà en sRGB ; assombris par l'occlusion
                px[o + c] = max(0, min(255, int(alb[i + c] * occ * 255 + 0.5)))
            px[o + 3] = 255
    # Aperçu PNG
    prev = bpy.data.images.new(name + "_tex", size, size, alpha=False)
    flt = [0.0] * (size * size * 4)
    for y in range(size):
        for x in range(size):
            s_ = (y * size + x) * 4
            d_ = ((size - 1 - y) * size + x) * 4
            for c in range(4):
                flt[d_ + c] = px[s_ + c] / 255.0
    prev.pixels = flt
    prev.filepath_raw = os.path.join(OUT, name + "_tex.png")
    prev.file_format = "PNG"
    prev.save()

    # Sommets par coin de triangle (normale lissée + UV), dédoublonnés.
    dg = bpy.context.evaluated_depsgraph_get()
    eo = obj.evaluated_get(dg)
    me = eo.to_mesh()
    me.calc_loop_triangles()
    uvl = me.uv_layers.active.data
    cn = me.corner_normals
    verts, idx, seen = [], [], {}
    mw = obj.matrix_world
    for tri in me.loop_triangles:
        for li in tri.loops:
            co = mw @ me.vertices[me.loops[li].vertex_index].co
            n = cn[li].vector
            uv = uvl[li].uv
            key = (round(co.x, 5), round(co.y, 5), round(co.z, 5), round(n.x, 3), round(n.y, 3), round(n.z, 3), round(uv.x, 5), round(uv.y, 5))
            k = seen.get(key)
            if k is None:
                k = len(verts)
                seen[key] = k
                verts.append((co.x, co.y, co.z, n.x, n.y, n.z, uv.x, 1.0 - uv.y))
            idx.append(k)
    eo.to_mesh_clear()
    assert len(verts) < 65535, "trop de sommets"
    path = os.path.join(OUT, name + ".skm")
    with open(path, "wb") as fp:
        fp.write(b"SKM2" + struct.pack("<IIII", len(verts), len(idx), size, size))
        for v in verts:
            fp.write(struct.pack("<8f4B", *v, 255, 255, 255, 255))
        fp.write(struct.pack("<%dH" % len(idx), *idx))
        fp.write(bytes(px))
    print("SKM %s : %d sommets, %d triangles, texture %dx%d" % (name, len(verts), len(idx) // 3, size, size))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, name + ".blend"))

os.makedirs(OUT, exist_ok=True)
export(build_ak())
export(build_rpg())
