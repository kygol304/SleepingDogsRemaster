// Nouvelles armes : modèles créés en mémoire avec les fonctions du moteur (Illusion::Factory),
// comme l'exemple « model-binding » de SD-Playground. Aucun fichier du jeu n'est modifié.
//
// Le SD-SDK (external/SD-SDK) décrit bien les structures de notre exécutable, mais ses adresses de
// fonctions viennent d'une autre version : on n'appelle donc AUCUNE fonction du SDK, seulement les
// adresses ci-dessous, retrouvées dans notre exe (Ghidra, à partir des chaînes du moteur).
#include "../../external/SD-SDK/sdhd_sdk.hh"
#include "newweapons.h"

#include <cstdarg>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

using namespace UFG;

namespace nw {
namespace {

constexpr uptr kNewMaterial   = 0x930A0;    // (nom, uid, nb params, schéma, pool, options) — chaîne "MaterialUser"
constexpr uptr kNewBuffer     = 0xA1CAD0;   // (nom, uid, octets, schéma, nom d'alloc, pool, options) — "Illusion.BufferPlat"
constexpr uptr kNewModel      = 0xA1CE10;   // (nom, uid, nb maillages, schéma, pool, options) — "Illusion.Model"
constexpr uptr kWarehouse     = 0x1740E0;   // qResourceWarehouse::Instance()
constexpr uptr kWarehouseAdd  = 0x165B90;   // qResourceWarehouse::Add(entrepôt, ressource)
constexpr uptr kInventoryOf   = 0x170C00;   // inventaire d'un type de ressource
constexpr uptr kHandleCtor    = 0x161C00;   // qResourceHandle()
constexpr uptr kHandleInit    = 0x1735F0;   // qResourceHandle::Init(poignée, type, uid, inventaire)
constexpr uptr kAddRigid      = 0x1BF6C0;   // ModelType::AddRigidModelBinding(type, proxy, os, 0, index)
constexpr uptr kCreateTexture = 0x92EE0;    // Illusion::InternalCreateTexture(nom, l, h, prof., format, options, mips, type, 0, 0)

// Liaison rigide (0x78 octets, vérifiée dans AddRigidModelBinding) : proxy du modèle à +0x18
// (uid du modèle à +0x30), index à +0x6C, os à +0x70, visible à +0x72.
constexpr size_t kBind_ModelUid = 0x30;
constexpr size_t kBind_Index    = 0x6C;
constexpr size_t kBind_Bone     = 0x70;
constexpr size_t kBind_Visible  = 0x72;

LogFn g_logFn = nullptr;

void Log(const char* fmt, ...)
{
    if (!g_logFn) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_logFn(buf);
}

template <typename T> T Fn(uptr rva) { return reinterpret_cast<T>(SDK::gBaseAddress + rva); }

void* Warehouse() { return Fn<void* (__fastcall*)()>(kWarehouse)(); }
void* InventoryOf(u32 type) { return Fn<void* (__fastcall*)(void*, u32)>(kInventoryOf)(Warehouse(), type); }
void AddResource(void* data) { Fn<void(__fastcall*)(void*, void*)>(kWarehouseAdd)(Warehouse(), data); }

void InitHandle(void* handle, u32 type, u32 uid)
{
    Fn<void(__fastcall*)(void*, u32, u32, void*)>(kHandleInit)(handle, type, uid, InventoryOf(type));
}

bool Readable(const void* p, size_t n)
{
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const uint8_t*>(p) + n <= static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

CompositeDrawableComponent* DrawableOf(void* simObject)
{
    if (!Readable(simObject, sizeof(SimObjectProp))) return nullptr;
    auto* obj = static_cast<SimObject*>(simObject);
    if (!obj->IsProp()) return nullptr;
    auto* c = static_cast<SimObjectProp*>(obj)->GetCompositeDrawable();
    return Readable(c, sizeof(CompositeDrawableComponent)) ? c : nullptr;
}

// Parcours prudent des liaisons rigides : s'arrête sur tout pointeur invalide (au plus 32).
template <typename F>
void ForRigid(ModelType* mt, F&& fn)
{
    auto* head = &mt->mModelBindingRigidTable.mNode;
    auto* n = head->mNext;
    for (int i = 0; i < 32 && n != head; ++i) {
        if (!Readable(n, 0x78)) { Log("nw : liaison illisible %p", n); return; }
        auto* next = n->mNext;
        fn(reinterpret_cast<uint8_t*>(n));
        n = next;
    }
}

template <typename F>
void ForModelTypes(CompositeDrawableComponent* cdc, F&& fn)
{
    for (u32 l = 0; l < cdc->mNumModelTypeLODs && l < 5; ++l) {
        ModelType* mt = cdc->mModelType[l];
        if (Readable(mt, sizeof(ModelType))) fn(l, mt);
    }
}

std::unordered_map<std::string, u32> g_models;   // fichier .skm -> uid du modèle créé
u32 g_nextUid = 0x5D2A0100;                       // identifiants propres au mod

// Textures du mod : le moteur crée la texture (vide) sur son fil de rendu ; dès qu'elle existe côté
// D3D11, on lui substitue une texture D3D11 créée par nous avec nos pixels et toutes les mipmaps
// (créer une ressource sur le périphérique est sûr depuis n'importe quel fil, contrairement à
// une copie dans une texture existante).
struct PendingTexture { Illusion::Texture* tex; u32 w, h; std::vector<uint8_t> rgba; int tries; };
std::unordered_set<Illusion::Texture*> g_readyTex;   // textures dont nos pixels sont en place
struct Icon { Illusion::Texture* tex; bool failed; };
std::unordered_map<std::string, Icon> g_icons;   // icônes du mod par nom
std::vector<PendingTexture> g_pendingTex;

// Décodage PNG -> RGBA8 avec WIC (intégré à Windows).
bool DecodePng(const std::vector<uint8_t>& file, u32 w, u32 h, std::vector<uint8_t>& out)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    IWICImagingFactory* fac = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT fw = 0, fh = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))
        && SUCCEEDED(fac->CreateStream(&stream))
        && SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(file.data()), static_cast<DWORD>(file.size())))
        && SUCCEEDED(fac->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec))
        && SUCCEEDED(dec->GetFrame(0, &frame))
        && SUCCEEDED(frame->GetSize(&fw, &fh)) && fw == w && fh == h
        && SUCCEEDED(fac->CreateFormatConverter(&conv))
        && SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        out.resize(size_t(w) * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.size()), out.data()));
    }
    if (!ok) Log("nw : echec du decodage PNG (%ux%u attendu, %ux%u lu)", w, h, fw, fh);
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (stream) stream->Release();
    if (fac) fac->Release();
    if (co == S_OK || co == S_FALSE) CoUninitialize();
    return ok;
}
bool UploadTexture(PendingTexture& pt)
{
    auto* plat = pt.tex->GetPlat();
    auto* res = plat->mTextureD3DResourceHandle.GetData();
    if (!Readable(res, sizeof(Illusion::TextureD3DResource)) || !res->mGpuResource) return false;
    ID3D11Device* dev = nullptr;
    res->mGpuResource->GetDevice(&dev);
    if (!dev) return false;
    D3D11_TEXTURE2D_DESC desc = res->mDesc;
    bool bgra;
    switch (desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: bgra = true; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: bgra = false; break;
    default: Log("nw : format de texture inattendu %u", desc.Format); dev->Release(); pt.tries = 1000; return false;
    }
    // Chaîne de mipmaps (moyenne 2x2).
    u32 levels = 1;
    while ((pt.w >> levels) && (pt.h >> levels)) ++levels;
    std::vector<std::vector<uint8_t>> mips(levels);
    mips[0] = pt.rgba;
    if (bgra) for (size_t i = 0; i < mips[0].size(); i += 4) std::swap(mips[0][i], mips[0][i + 2]);
    for (u32 l = 1; l < levels; ++l) {
        const u32 pw = pt.w >> (l - 1), nw_ = pt.w >> l, nh = pt.h >> l;
        mips[l].resize(size_t(nw_) * nh * 4);
        const auto& src = mips[l - 1];
        for (u32 y = 0; y < nh; ++y)
            for (u32 x = 0; x < nw_; ++x)
                for (int c = 0; c < 4; ++c) {
                    const size_t a = (size_t(2 * y) * pw + 2 * x) * 4 + c, b = a + size_t(pw) * 4;
                    mips[l][(size_t(y) * nw_ + x) * 4 + c] = static_cast<uint8_t>((src[a] + src[a + 4] + src[b] + src[b + 4] + 2) / 4);
                }
    }
    std::vector<D3D11_SUBRESOURCE_DATA> init(levels);
    for (u32 l = 0; l < levels; ++l) init[l] = { mips[l].data(), (pt.w >> l) * 4, 0 };
    desc.Width = pt.w;
    desc.Height = pt.h;
    desc.MipLevels = levels;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc = { 1, 0 };
    // Nos pixels sont en sRGB : format _SRGB pour que le GPU les repasse en linéaire (sinon tout
    // paraît délavé, le métal presque blanc).
    desc.Format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ID3D11Texture2D* t2d = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = dev->CreateTexture2D(&desc, init.data(), &t2d);
    if (SUCCEEDED(hr)) hr = dev->CreateShaderResourceView(t2d, nullptr, &srv);
    dev->Release();
    if (FAILED(hr)) { Log("nw : echec creation texture D3D (%08X)", static_cast<u32>(hr)); if (t2d) t2d->Release(); pt.tries = 1000; return false; }
    // Les anciennes ressources ne sont pas libérées : le rendu peut encore s'en servir cette image.
    res->mGpuResource = t2d;
    res->mShaderResourceView = srv;
    g_readyTex.insert(pt.tex);
    Log("nw : texture %08X chargee (%ux%u, %u mips, format %u)", pt.tex->mNode.mUID, pt.w, pt.h, levels, desc.Format);
    return true;
}
std::unordered_map<u32, uint8_t*> g_proxies;   // un proxy permanent par modèle (copié par le jeu)

uint8_t* ProxyFor(u32 uid)
{
    auto it = g_proxies.find(uid);
    if (it != g_proxies.end()) return it->second;
    auto* p = static_cast<uint8_t*>(_aligned_malloc(0x40, 16));
    memset(p, 0, 0x40);
    Fn<void* (__fastcall*)(void*)>(kHandleCtor)(p);
    InitHandle(p, RTypeUID_Model, uid);
    *reinterpret_cast<void**>(p + 0x20) = nullptr;   // mRModel
    g_proxies[uid] = p;
    return p;
}

}  // namespace

void SetLog(LogFn fn) { g_logFn = fn; }

void Update()
{
    for (size_t i = 0; i < g_pendingTex.size();) {
        PendingTexture& pt = g_pendingTex[i];
        if (UploadTexture(pt) || ++pt.tries > 600) {
            if (pt.tries > 600) Log("nw : texture %08X jamais prete, abandon", pt.tex->mNode.mUID);
            g_pendingTex.erase(g_pendingTex.begin() + i);
        } else {
            ++i;
        }
    }
}

uint32_t LoadModel(const char* path)
{
    auto known = g_models.find(path);
    if (known != g_models.end()) return known->second;
    g_models[path] = 0;   // un seul essai par fichier

    // Fichier .skm (tools_src/weapons3d/import_models.py) : "SKM3", nb sommets, nb indices, largeur,
    // hauteur, sommets { pos[3], normale[3], uv[2], rgba }, indices u16, taille + PNG de la texture.
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) { Log("nw : modele introuvable : %s", path); return 0; }
    char magic[4] = {};
    u32 nv = 0, ni = 0, tw = 0, th = 0;
    fread(magic, 1, 4, f);
    fread(&nv, 4, 1, f);
    fread(&ni, 4, 1, f);
    fread(&tw, 4, 1, f);
    fread(&th, 4, 1, f);
    struct SkmVertex { float p[3], n[3], uv[2]; uint8_t c[4]; };
    static_assert(sizeof(SkmVertex) == 36, "SkmVertex");
    // SKM2 : texture RGBA8 brute ; SKM3 : texture en PNG (taille u32 puis fichier PNG).
    const bool png = memcmp(magic, "SKM3", 4) == 0;
    const bool sizes = (png || memcmp(magic, "SKM2", 4) == 0) && nv > 0 && nv < 65535 && ni > 0 && ni % 3 == 0
        && tw >= 4 && th >= 4 && tw <= 4096 && th <= 4096 && !(tw & (tw - 1)) && !(th & (th - 1));
    std::vector<SkmVertex> sv(sizes ? nv : 0);
    std::vector<u16> si(sizes ? ni : 0);
    std::vector<uint8_t> pixels;
    bool ok = sizes && fread(sv.data(), sizeof(SkmVertex), nv, f) == nv && fread(si.data(), 2, ni, f) == ni;
    if (ok && png) {
        u32 len = 0;
        ok = fread(&len, 4, 1, f) == 1 && len > 8 && len < (64u << 20);
        std::vector<uint8_t> file(ok ? len : 0);
        ok = ok && fread(file.data(), 1, len, f) == len && DecodePng(file, tw, th, pixels);
    } else if (ok) {
        pixels.resize(size_t(tw) * th * 4);
        ok = fread(pixels.data(), 1, pixels.size(), f) == pixels.size();
    }
    fclose(f);
    if (!ok) { Log("nw : fichier de modele invalide : %s", path); return 0; }
    for (u16 i : si) if (i >= nv) { Log("nw : indice hors limites dans %s", path); return 0; }

    const u32 uid = g_nextUid;
    g_nextUid += 4;

    // Texture : nom unique d'après l'uid (le moteur en tire l'identifiant de la ressource).
    char texName[64];
    sprintf_s(texName, "SKR_WEAPON_%08X_D", uid);
    auto* tex = Fn<Illusion::Texture* (__fastcall*)(const char*, u16, u16, u16, u8, u32, u8, u8, u32, u32)>(kCreateTexture)(
        texName, static_cast<u16>(tw), static_cast<u16>(th), 1, Illusion::Texture::FORMAT_A8R8G8B8, 0, 1, Illusion::Texture::TYPE_2D, 0, 0);
    if (!tex) { Log("nw : echec texture"); return 0; }
    g_pendingTex.push_back({ tex, tw, th, std::move(pixels), 0 });

    auto* material = Fn<Illusion::Material* (__fastcall*)(const char*, u32, u32, void*, void*, u64)>(kNewMaterial)(
        "SKR_WEAPON", uid, 8, nullptr, nullptr, 0);
    if (!material) { Log("nw : echec materiau"); return 0; }
    static_assert(sizeof(Illusion::MaterialParam) == 0x38, "MaterialParam");
    u32 np = 0;
    material->SetParam(np++, 0x5C19C934, 0x8B5561A1, 0x192E0D6);                 // iShader HK_SIMPLE
    material->SetParam(np++, 0xC8377453, 0xDCE06689, 0x8B43FABF, tex->mNode.mUID);  // texDiffuse : notre texture
    material->SetParam(np++, 0xC0C265E6, 0x3BC715E0, 0x940FB8EC);                 // double face
    material->SetParam(np++, 0xEB98748F, 0x12C800F2, static_cast<u32>(-1));
    material->SetParam(np++, 0xF173D303, 0x4D04C7F2, 0xAF2B2668);
    material->SetParam(np++, 0xEA270604, 0x4D04C7F2, 0x241AB391);
    material->SetParam(np++, 0xB2F5D3F2, 0x4D04C7F2, 0xD0B4527C);
    material->mNumParams = np;

    using NewBufferFn = Illusion::Buffer* (__fastcall*)(const char*, u32, u32, void*, const char*, void*, u64);
    auto* vb = Fn<NewBufferFn>(kNewBuffer)("SKR_WEAPON.VB", uid + 1, sizeof(Render::vDynamic) * nv, nullptr, nullptr, nullptr, 0);
    auto* ib = Fn<NewBufferFn>(kNewBuffer)("SKR_WEAPON.IB", uid + 2, sizeof(u16) * ni, nullptr, nullptr, nullptr, 0);
    if (!vb || !ib) { Log("nw : echec tampons"); return 0; }
    vb->mBufferType = Illusion::Buffer::TYPE_VERTEX;
    vb->mElementByteSize = sizeof(Render::vDynamic);
    vb->mNumElements = nv;
    ib->mBufferType = Illusion::Buffer::TYPE_INDEX;
    ib->mElementByteSize = sizeof(u16);
    ib->mNumElements = ni;

    auto* v = static_cast<Render::vDynamic*>(vb->mData.Get());
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
    for (u32 i = 0; i < nv; ++i, ++v) {
        const SkmVertex& s = sv[i];
        for (int k = 0; k < 3; ++k) { if (s.p[k] < mn[k]) mn[k] = s.p[k]; if (s.p[k] > mx[k]) mx[k] = s.p[k]; }
        v->mPosition = { s.p[0], s.p[1], s.p[2] };
        v->mNormal = { s.n[0], s.n[1], s.n[2] };
        // Tangente quelconque perpendiculaire à la normale (pas de carte de relief).
        const float ax = fabsf(s.n[0]) < 0.9f ? 1.f : 0.f, ay = 1.f - ax;
        float t[3] = { ay * s.n[2], -ax * s.n[2], ax * s.n[1] - ay * s.n[0] };
        const float tl = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (tl > 1e-6f) { t[0] /= tl; t[1] /= tl; t[2] /= tl; } else { t[0] = 1.f; t[1] = t[2] = 0.f; }
        v->mTangent = { t[0], t[1], t[2] };
        v->SetUV(qVector2{ s.uv[0], s.uv[1] }, qVector2{ s.uv[0], s.uv[1] });
        v->SetColor(s.c[0], s.c[1], s.c[2], s.c[3]);
    }
    memcpy(ib->mData.Get(), si.data(), si.size() * 2);

    auto* model = Fn<Illusion::Model* (__fastcall*)(const char*, u32, u32, void*, void*, u64)>(kNewModel)(
        "SKR_WEAPON", uid, 1, nullptr, nullptr, 0);
    if (!model) { Log("nw : echec modele"); return 0; }
    for (int k = 0; k < 3; ++k) { model->mAABBMin[k] = mn[k]; model->mAABBMax[k] = mx[k]; }
    model->mNumPrims = ni / 3;
    auto* mesh = model->GetMesh(0);
    mesh->mMaterialHandle.mNameUID = material->mNode.mUID;
    InitHandle(&mesh->mVertexDeclHandle, RTypeUID_VertexDecl, 0x64A43DAE);   // sommets vDynamic
    mesh->mIndexBufferHandle.mNameUID = ib->mNode.mUID;
    mesh->mVertexBufferHandles[0].mNameUID = vb->mNode.mUID;
    mesh->mPrimType = Illusion::Primitive::TRIANGLELIST;
    mesh->mIndexStart = 0;
    mesh->mNumPrims = ni / 3;

    AddResource(material);
    AddResource(vb);
    AddResource(ib);
    AddResource(model);
    g_models[path] = uid;
    Log("nw : modele %s cree (%08X, %u triangles)", path, uid, ni / 3);
    return uid;
}

bool ApplyModel(void* simObject, uint32_t uid)
{
    auto* cdc = DrawableOf(simObject);
    if (!cdc || !uid) return false;
    bool added = false;
    ForModelTypes(cdc, [&](u32 l, ModelType* mt) {
        u16 bone = 0xFFFF;
        u32 index = 0;
        bool has = false;
        // Pièces d'origine (corps, culasse, chargeur...) masquées à chaque passage : le jeu peut
        // en réafficher (rechargement). Notre modèle prend l'os le plus bas (le corps de l'arme).
        ForRigid(mt, [&](uint8_t* b) {
            if (*reinterpret_cast<u32*>(b + kBind_ModelUid) == uid) { has = true; return; }
            const u16 bn = *reinterpret_cast<u16*>(b + kBind_Bone);
            if (bone == 0xFFFF || bn < bone) { bone = bn; index = *reinterpret_cast<u32*>(b + kBind_Index); }
            *reinterpret_cast<u16*>(b + kBind_Visible) = 0;
        });
        if (has || bone == 0xFFFF) return;
        Fn<void* (__fastcall*)(void*, void*, u32, void*, u32)>(kAddRigid)(mt, ProxyFor(uid), bone, nullptr, index);
        Log("nw : modele %08X pose sur %p (lod %u, os %u)", uid, simObject, l, bone);
        added = true;
    });
    return added;
}

void* MakeRocketWeaponInfo(void* srcInfo, const RocketParams& rp, const char* (*symbolName)(uint32_t))
{
    auto* src = static_cast<WeaponTypeInfo*>(srcInfo);
    if (!Readable(src, sizeof(WeaponTypeInfo)) || src->mNumFireModes < 1 || src->mNumFireModes > 2) {
        Log("nw : fiche d'arme illisible %p", srcInfo);
        return nullptr;
    }
    auto* info = static_cast<WeaponTypeInfo*>(_aligned_malloc(sizeof(WeaponTypeInfo), 16));
    *info = *src;
    for (u32 i = 0; i < src->mNumFireModes; ++i) {
        FireModeInfo* fm = src->mFireModes[i];
        if (!Readable(fm, sizeof(FireModeInfo))) { Log("nw : mode de tir illisible"); return nullptr; }
        const char* expl = symbolName ? symbolName(fm->mExplosionType.mUID) : nullptr;
        Log("nw : arme de base mode %u : vitesse %.1f gravite %.2f armement %.1f chargeur %d/%d cadence %.2f degats %.0f x%.2f explosion %s (%08X) effet %08X",
            i, fm->mProjectileSpeed, fm->mProjectileGravity, fm->mArmingDistance, fm->mAmmoClipCapacity, fm->mAmmoTotalCapacity,
            fm->mRateOfFire, fm->mDamage, fm->mDamageMultiplier, expl ? expl : "?", fm->mExplosionType.mUID, fm->mBulletEffectID);
        // Garde-fou : si les champs ne ressemblent pas à une fiche d'arme, on ne touche à rien.
        // (Le type d'explosion du MGL n'a pas de nom lisible : il n'entre pas dans le contrôle.)
        const bool sane = fm->mProjectileSpeed > 1.f && fm->mProjectileSpeed < 2000.f && fm->mAmmoClipCapacity >= 1
            && fm->mAmmoClipCapacity <= 200 && fm->mProjectileGravity >= 0.f && fm->mProjectileGravity < 100.f;
        if (!sane) { Log("nw : valeurs inattendues, lance-roquettes laisse d'origine"); _aligned_free(info); return nullptr; }
        auto* copy = static_cast<FireModeInfo*>(_aligned_malloc(sizeof(FireModeInfo), 16));
        memcpy(copy, fm, sizeof(FireModeInfo));   // copie superficielle : jamais détruite
        copy->mProjectileSpeed *= rp.speedMul;
        copy->mProjectileSpeedAI *= rp.speedMul;
        copy->mProjectileGravity *= rp.gravityMul;
        copy->mDamage *= rp.damageMul;
        if (rp.clip > 0) copy->mAmmoClipCapacity = rp.clip;
        if (rp.explosion) copy->mExplosionType.mUID = rp.explosion;
        Log("nw : lance-roquettes mode %u : vitesse %.1f gravite %.2f degats %.0f explosion %08X",
            i, copy->mProjectileSpeed, copy->mProjectileGravity, copy->mDamage, copy->mExplosionType.mUID);
        info->mFireModes[i] = copy;
    }
    // Un seul mode de tir : la roquette.
    info->mNumFireModes = 1;
    return info;
}

void** WeaponInfoSlot(void* simObject)
{
    if (!Readable(simObject, sizeof(SimObjectProp))) return nullptr;
    auto* obj = static_cast<SimObject*>(simObject);
    if (!obj->IsProp()) return nullptr;
    auto* prop = static_cast<SimObjectProp*>(obj);
    auto* comp = prop->GetSimObjectWeaponProperties();
    static int traces = 0;
    if (traces < 3) {
        ++traces;
        SimComponent* raw = prop->m_Components.p[Prop_SimObjectPropertiesComponent].m_pComponent;
        Log("nw : composant d'arme %p (emplacement %p type %08X), fiche a +0x%zx", comp, raw,
            raw ? raw->m_TypeUID : 0, offsetof(SimObjectWeaponPropertiesComponent, mWeaponTypeInfo));
    }
    if (!Readable(comp, sizeof(SimObjectWeaponPropertiesComponent))) return nullptr;
    return reinterpret_cast<void**>(&comp->mWeaponTypeInfo);
}

int IconState(const char* name, const char* pngPath)
{
    auto it = g_icons.find(name);
    if (it == g_icons.end()) {
        Icon ic{ nullptr, true };
        // Taille lue dans l'en-tête PNG (largeur/hauteur gros-boutistes aux octets 16..23).
        std::vector<uint8_t> file;
        if (FILE* f = nullptr; fopen_s(&f, pngPath, "rb") == 0 && f) {
            fseek(f, 0, SEEK_END);
            file.resize(static_cast<size_t>(ftell(f)));
            fseek(f, 0, SEEK_SET);
            if (fread(file.data(), 1, file.size(), f) != file.size()) file.clear();
            fclose(f);
        }
        u32 w = 0, h = 0;
        if (file.size() > 24) {
            w = (u32(file[16]) << 24) | (u32(file[17]) << 16) | (u32(file[18]) << 8) | file[19];
            h = (u32(file[20]) << 24) | (u32(file[21]) << 16) | (u32(file[22]) << 8) | file[23];
        }
        std::vector<uint8_t> px;
        if (w && h && w <= 2048 && h <= 2048 && !(w & (w - 1)) && !(h & (h - 1)) && DecodePng(file, w, h, px)) {
            // Le nom de la texture est celui que le HUD demande (img://<nom>) : son uid en découle.
            auto* tex = Fn<Illusion::Texture* (__fastcall*)(const char*, u16, u16, u16, u8, u32, u8, u8, u32, u32)>(kCreateTexture)(
                _strdup(name), static_cast<u16>(w), static_cast<u16>(h), 1, Illusion::Texture::FORMAT_A8R8G8B8, 0, 1, Illusion::Texture::TYPE_2D, 0, 0);
            if (tex) {
                g_pendingTex.push_back({ tex, w, h, std::move(px), 0 });
                ic = { tex, false };
                Log("nw : icone %s (%ux%u) en preparation, uid %08X", name, w, h, tex->mNode.mUID);
            }
        } else {
            Log("nw : icone %s illisible (%s)", name, pngPath);
        }
        it = g_icons.emplace(name, ic).first;
    }
    if (it->second.failed) return -1;
    return g_readyTex.count(it->second.tex) ? 2 : 1;
}

uint32_t LastModeExplosion(const void* weaponInfo)
{
    auto* info = static_cast<const WeaponTypeInfo*>(weaponInfo);
    if (!Readable(info, sizeof(WeaponTypeInfo)) || info->mNumFireModes < 1 || info->mNumFireModes > 2) return 0;
    const FireModeInfo* fm = info->mFireModes[info->mNumFireModes - 1];
    return Readable(fm, sizeof(FireModeInfo)) ? fm->mExplosionType.mUID : 0;
}

// Types d'explosions du jeu : tableau de 34 pointeurs (0x2403430) rempli au chargement
// (FUN_140469420) ; la recherche par nom (FUN_1404669a0) s'arrête à la première case vide et renvoie
// nul si le nom manque (le jeu plante alors au tir). Les fiches sont allouées dans le pool
// 0x216B300 (FUN_140166b90) et libérées par le jeu à son rechargement : la nôtre aussi.
uint32_t EnsureRocketExplosion(uint32_t ourSym, uint32_t baseSym, float radiusMul, const char* (*symbolName)(uint32_t),
                               const uint32_t* prefer, int nPrefer)
{
    ExplosionTypeInfo* preferred = nullptr;
    int preferRank = nPrefer;
    auto** table = reinterpret_cast<ExplosionTypeInfo**>(SDK::gBaseAddress + 0x2403430);
    constexpr int kSlots = 0x22;
    int freeSlot = -1;
    ExplosionTypeInfo* base = nullptr;
    ExplosionTypeInfo* donor = nullptr;
    static bool listed = false;
    for (int i = 0; i < kSlots; ++i) {
        ExplosionTypeInfo* e = table[i];
        if (!e) { freeSlot = i; break; }
        if (!Readable(e, sizeof(ExplosionTypeInfo))) return 0;
        if (e->mPropertySetName.mUID == ourSym) return ourSym;
        if (!listed) {
            const char* n = symbolName ? symbolName(e->mPropertySetName.mUID) : nullptr;
            Log("nw : explosion %2d %08X %-28s rayon %.1f duree %.2f effet %08X secousse %.2f",
                i, e->mPropertySetName.mUID, n ? n : "?", e->mExplosionRadius, e->mExplosionDuration,
                e->mExplosionEffectID, e->mCameraShakeAmplitude);
        }
        if (e->mPropertySetName.mUID == baseSym) base = e;
        for (int k = 0; k < preferRank; ++k)
            if (prefer[k] == e->mPropertySetName.mUID) { preferred = e; preferRank = k; break; }
        if (!e->mExplosionNoDamage && (!donor || e->mExplosionRadius > donor->mExplosionRadius)) donor = e;
    }
    listed = true;
    if (!base || freeSlot < 0) { Log("nw : explosion de roquette impossible (base %p, case %d)", base, freeSlot); return 0; }
    if (preferred) donor = preferred;   // explosion de voiture, de camion... plutôt que la plus grosse
    if (!donor) donor = base;
    auto* e = static_cast<ExplosionTypeInfo*>(Fn<void* (__fastcall*)(void*, u64, const char*, u64, u32)>(0x166B90)(
        reinterpret_cast<void*>(SDK::gBaseAddress + 0x216B300), sizeof(ExplosionTypeInfo), "ExplosionTypeInfo", 0, 1));
    if (!e) return 0;
    memcpy(e, base, sizeof(ExplosionTypeInfo));
    e->mPropertySetName.mUID = ourSym;
    // Effet visuel et physique de la grenade (sûrs) ; seulement le rayon et la secousse agrandis.
    // (L'effet de la plus grosse explosion du jeu ne s'affichait pas, et le jeu a planté ensuite.)
    e->mExplosionRadius = base->mExplosionRadius * radiusMul;
    if (e->mCameraShakeAmplitude < 7.f) e->mCameraShakeAmplitude = 7.f;
    e->mCameraShakeMaxDistanceSquared = base->mCameraShakeMaxDistanceSquared * radiusMul * radiusMul;
    (void)donor;    table[freeSlot] = e;
    Log("nw : explosion de roquette %08X ajoutee (case %d) : rayon %.1f, effet %08X", ourSym, freeSlot,
        e->mExplosionRadius, e->mExplosionEffectID);
    return ourSym;
}

void SetRocketExplosion(void* weaponInfo, uint32_t sym)
{
    auto* info = static_cast<WeaponTypeInfo*>(weaponInfo);
    if (!info || !sym) return;
    for (u32 i = 0; i < info->mNumFireModes && i < 2; ++i) if (info->mFireModes[i]) info->mFireModes[i]->mExplosionType.mUID = sym;
    Log("nw : explosion de roquette remise a %08X", sym);
}

void** TextureSrvSlot(const char* name)
{
    static std::unordered_map<std::string, uint8_t*> handles;
    auto it = handles.find(name);
    if (it == handles.end()) {
        // Poignée permanente vers la texture du jeu (uid = hachage du nom, comme InternalCreateTexture).
        const u32 uid = Fn<u32(__fastcall*)(const char*, u32)>(0x18BB00)(name, 0xFFFFFFFF);
        auto* h = static_cast<uint8_t*>(_aligned_malloc(0x20, 16));
        memset(h, 0, 0x20);
        Fn<void* (__fastcall*)(void*)>(kHandleCtor)(h);
        InitHandle(h, RTypeUID_Texture, uid);
        it = handles.emplace(name, h).first;
    }
    auto* tex = *reinterpret_cast<Illusion::Texture**>(it->second + 0x10);   // qResourceHandle::mData
    if (!Readable(tex, sizeof(Illusion::Texture) + 0xB0)) return nullptr;
    auto* res = tex->GetPlat()->mTextureD3DResourceHandle.GetData();
    if (!Readable(res, sizeof(Illusion::TextureD3DResource)) || !res->mShaderResourceView) return nullptr;
    return reinterpret_cast<void**>(&res->mShaderResourceView);
}

void* IconSrv(const char* name)
{
    auto it = g_icons.find(name);
    if (it == g_icons.end() || it->second.failed || !g_readyTex.count(it->second.tex)) return nullptr;
    auto* res = it->second.tex->GetPlat()->mTextureD3DResourceHandle.GetData();
    return Readable(res, sizeof(Illusion::TextureD3DResource)) ? res->mShaderResourceView : nullptr;
}

void AddRefSrv(void* srv) { if (srv) static_cast<ID3D11ShaderResourceView*>(srv)->AddRef(); }


void BindingVisibility(void* simObject, int* visible, int* total)
{
    *visible = *total = 0;
    auto* cdc = DrawableOf(simObject);
    if (!cdc) return;
    ForModelTypes(cdc, [&](u32, ModelType* mt) {
        ForRigid(mt, [&](uint8_t* b) {
            ++*total;
            if (*reinterpret_cast<u16*>(b + kBind_Visible)) ++*visible;
        });
    });
}

void* IconSrvFor(const char* name, const char* pngPath, uint32_t targetW, uint32_t targetH)
{
    auto it = g_icons.find(name);
    if (it == g_icons.end() || it->second.failed || !g_readyTex.count(it->second.tex)) return nullptr;
    const u32 w = it->second.tex->mWidth, h = it->second.tex->mHeight;
    // Mêmes proportions que la texture visée : l'icône telle quelle.
    if (!targetW || !targetH || u64(w) * targetH == u64(h) * targetW) return IconSrv(name);
    // Sinon une variante à ses proportions : l'icône recadrée sur ce qu'elle dessine, centrée avec
    // une marge, sans déformation (ex. pistolets : icônes du jeu carrées, les nôtres 2:1).
    static std::unordered_map<std::string, Illusion::Texture*> variants;
    char key[160];
    sprintf_s(key, "%s_%ux%u", name, targetW, targetH);
    auto v = variants.find(key);
    if (v == variants.end()) {
        Illusion::Texture* tex = nullptr;
        std::vector<uint8_t> file, src;
        if (FILE* f = nullptr; fopen_s(&f, pngPath, "rb") == 0 && f) {
            fseek(f, 0, SEEK_END);
            file.resize(static_cast<size_t>(ftell(f)));
            fseek(f, 0, SEEK_SET);
            if (fread(file.data(), 1, file.size(), f) != file.size()) file.clear();
            fclose(f);
        }
        if (!file.empty() && DecodePng(file, w, h, src)) {
            u32 x0 = w, y0 = h, x1 = 0, y1 = 0;   // zone dessinée (alpha)
            for (u32 y = 0; y < h; ++y)
                for (u32 x = 0; x < w; ++x)
                    if (src[(size_t(y) * w + x) * 4 + 3] > 8) { x0 = x < x0 ? x : x0; y0 = y < y0 ? y : y0; x1 = x > x1 ? x : x1; y1 = y > y1 ? y : y1; }
            // Toile : 512 de large (ou de haut), aux proportions visées (puissances de 2).
            u32 cw = 512, ch = 512;
            if (targetW > targetH) ch = 512 * targetH / targetW; else cw = 512 * targetW / targetH;
            std::vector<uint8_t> dst(size_t(cw) * ch * 4, 0);
            if (x1 >= x0 && y1 >= y0) {
                const float bw = float(x1 - x0 + 1), bh = float(y1 - y0 + 1);
                const float sc = (std::min)(cw * 0.90f / bw, ch * 0.90f / bh);
                const float ox = (cw - bw * sc) * 0.5f, oy = (ch - bh * sc) * 0.5f;
                for (u32 y = 0; y < ch; ++y)
                    for (u32 x = 0; x < cw; ++x) {
                        // Échantillonnage bilinéaire de la zone dessinée.
                        const float sx = (x + 0.5f - ox) / sc - 0.5f + x0, sy = (y + 0.5f - oy) / sc - 0.5f + y0;
                        if (sx < x0 - 1 || sy < y0 - 1 || sx > x1 + 1 || sy > y1 + 1) continue;
                        const int ix = int(floorf(sx)), iy = int(floorf(sy));
                        const float fx = sx - ix, fy = sy - iy;
                        for (int c = 0; c < 4; ++c) {
                            float acc = 0;
                            for (int k = 0; k < 4; ++k) {
                                const int px = ix + (k & 1), py = iy + (k >> 1);
                                if (px < 0 || py < 0 || px >= int(w) || py >= int(h)) continue;
                                const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                                acc += wgt * src[(size_t(py) * w + px) * 4 + c];
                            }
                            dst[(size_t(y) * cw + x) * 4 + c] = static_cast<uint8_t>(acc + 0.5f);
                        }
                    }
            }
            tex = Fn<Illusion::Texture* (__fastcall*)(const char*, u16, u16, u16, u8, u32, u8, u8, u32, u32)>(kCreateTexture)(
                _strdup(key), static_cast<u16>(cw), static_cast<u16>(ch), 1, Illusion::Texture::FORMAT_A8R8G8B8, 0, 1, Illusion::Texture::TYPE_2D, 0, 0);
            if (tex) g_pendingTex.push_back({ tex, cw, ch, std::move(dst), 0 });
            Log("nw : variante d'icone %s (%ux%u) %s", key, cw, ch, tex ? "en preparation" : "impossible");
        }
        v = variants.emplace(key, tex).first;
    }
    Illusion::Texture* tex = v->second;
    if (!tex || !g_readyTex.count(tex)) return nullptr;
    auto* res = tex->GetPlat()->mTextureD3DResourceHandle.GetData();
    return Readable(res, sizeof(Illusion::TextureD3DResource)) ? res->mShaderResourceView : nullptr;
}

bool TextureSize(const char* name, uint32_t* w, uint32_t* h)
{
    void** slot = TextureSrvSlot(name);
    if (!slot) return false;
    auto* res = reinterpret_cast<Illusion::TextureD3DResource*>(reinterpret_cast<uint8_t*>(slot) - offsetof(Illusion::TextureD3DResource, mShaderResourceView));
    *w = res->mDesc.Width;
    *h = res->mDesc.Height;
    return *w && *h;
}

}  // namespace nw
