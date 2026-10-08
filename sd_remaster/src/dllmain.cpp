// Sleeping Dogs Remaster — caméra moderne (à la Watch Dogs 2 / GTA) pour Sleeping Dogs: Definitive Edition.
// Chargé comme proxy dinput8.dll. Aucune adresse figée : la caméra est retrouvée via les informations
// de type (RTTI) de l'exécutable, puis ses réglages par situation (FollowParams) sont ajustés selon
// SleepingDogsRemaster.ini, relu automatiquement quand il change.
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <atomic>
#include <share.h>
#include <vector>
#include <deque>
#include <unordered_map>
#include <string>

#include "newweapons.h"

namespace {

// ---- Disposition des structures (SDK SDmodding, valide pour l'exe actuel) ----------------
constexpr size_t kComp_TypeUID      = 0x18;   // SimComponent::m_TypeUID
constexpr uint32_t kFollowTypeUID   = 0x1E000025;
constexpr size_t kFollow_Params     = 0x628;  // FollowCameraComponent::mContextParameters[15]
constexpr size_t kFollowParamsSize  = 0x13C;
constexpr int    kNumContexts       = 15;
constexpr size_t kFP_Fov            = 0x00;
constexpr size_t kFP_FovMax         = 0x04;
constexpr size_t kFP_EyeRise        = 0x0C;
constexpr size_t kFP_RadiusMin      = 0x1C;
constexpr size_t kFP_RadiusMax      = 0x20;
constexpr size_t kFP_LookOffset     = 0x78;   // qVector3
constexpr size_t kFP_SmoothPos      = 0xA8;
constexpr size_t kFP_SmoothRot      = 0xAC;

const char* kContextNames[kNumContexts] = {
    "marche", "sprint", "marche lente", "social", "visee", "visee telephone", "sniper",
    "visee en l'air", "marche ciblee", "marche arme", "sprint arme", "remorquage",
    "visee (forcee)", "focus (combat)", "prise",
};

FILE* g_log = nullptr;
#define LOG(...) do { if (g_log) { fprintf(g_log, __VA_ARGS__); fputc('\n', g_log); fflush(g_log); } } while (0)

std::atomic<bool> g_running{true};
uintptr_t g_base = 0;

// ---- Réglages (SleepingDogsRemaster.ini) ---------------------------------------------------
struct Settings {
    bool  enabled = true;
    float fovScale = 1.25f;       // champ de vision x1.25
    float distanceScale = 1.05f;  // un peu plus de recul
    float height = -0.10f;        // caméra un peu plus basse (m)
    float shoulder = 0.45f;       // décalage latéral par-dessus l'épaule (m), négatif = épaule gauche
    float smoothing = 1.0f;       // >1 = caméra plus douce
    bool  combatToo = true;       // appliquer aussi en combat (focus / prise)
    float combatFovScale = 1.10f;       // combat au corps à corps : un peu plus large
    float combatDistanceScale = 0.75f;  // ... mais caméra plus proche
    float combatHeight = -0.40f;        // et moins haute (le jeu la place à 1,7 m en combat)
    bool  vehEnabled = true;
    float vehFovScale = 1.15f;    // champ de vision en voiture
    float vehDistanceScale = 0.95f;
    float vehHeight = -0.15f;     // caméra de conduite plus basse (m)
};
Settings g_cfg;
std::atomic<bool> g_roundMinimap{true};
char g_iniPath[MAX_PATH];
FILETIME g_iniTime{};

void ReadMinimapSetting()
{
    g_roundMinimap.store(GetPrivateProfileIntA("Minimap", "Rounded", 1, g_iniPath) != 0,
                         std::memory_order_relaxed);
}

float IniFloat(const char* key, float def, const char* section = "Camera")
{
    char buf[64], defs[32];
    sprintf_s(defs, "%g", def);
    GetPrivateProfileStringA(section, key, defs, buf, sizeof(buf), g_iniPath);
    return static_cast<float>(atof(buf));
}

void WriteDefaultIni()
{
    FILE* f = nullptr;
    if (fopen_s(&f, g_iniPath, "w") != 0 || !f) return;
    fputs("; Sleeping Dogs Remaster - reglages (camera, mini-carte, armes).\n"
          "; La camera est relue en direct ; le reste au prochain lancement du jeu.\n"
          "[Camera]\n"
          "; 1 = actif, 0 = camera d'origine\n"
          "Enabled=1\n"
          "; Champ de vision a pied (1.0 = origine, 1.2 = 20 % plus large)\n"
          "FovScale=1.2\n"
          "; Distance de la camera (1.0 = origine)\n"
          "DistanceScale=1.0\n"
          "; Hauteur en metres (negatif = plus bas)\n"
          "Height=0\n"
          "; Decalage par-dessus l'epaule en metres (0 = centre, negatif = epaule gauche)\n"
          "Shoulder=0\n"
          "; Douceur des mouvements (1.0 = origine, 1.5 = plus doux)\n"
          "Smoothing=1.0\n"
          "; Appliquer aussi pendant les combats (1/0)\n"
          "CombatToo=1\n"
          "\n[Combat]\n"
          "; Camera du combat au corps a corps (marche ciblee, focus, prise)\n"
          "FovScale=1.10\n"
          "DistanceScale=0.75\n"
          "Height=-0.40\n"
          "\n[Vehicle]\n"
          "Enabled=1\n"
          "; Champ de vision en vehicule (1.0 = origine)\n"
          "FovScale=1.15\n"
          "; Distance de la camera derriere le vehicule (1.0 = origine)\n"
          "DistanceScale=1.08\n"
          "; Hauteur en metres (negatif = plus bas)\n"
          "Height=-0.15\n"
          "\n[Minimap]\n"
          "; 1 = rectangle aux coins arrondis facon GTA, 0 = rond d'origine\n"
          "Rounded=1\n"
          "; decalage en pixels (negatif = gauche / haut)\n"
          "OffsetX=-40\n"
          "OffsetY=40\n"
          "; zoom minimum (plus grand = plus rapproche, evite les trous en voiture)\n"
          "MinZoom=1.05\n"
          "\n[Wheel]\n"
          "; roue d'arme : RB maintenu (ou Tab), choix au stick droit\n"
          "; ralenti pendant la roue (0.25 = 4x plus lent, 1 = pas de ralenti)\n"
          "SlowMo=0.25\n"
          "; secondes de maintien de RB avant d'ouvrir la roue\n"
          "HoldTime=0.5\n"
          "; icone des mains nues (Y dans la roue pour la changer)\n"
          "FistIcon=4\n"
          "\n[Loadout]\n"
          "; 1 = garder les armes de la roue d'une session a l'autre, 0 = inventaire vide au lancement\n"
          "KeepBetweenSessions=0\n"
          "\n[Rocket]\n"
          "; lance-roquettes : vitesse, chute, degats et rayon d'explosion (x origine), roquettes par chargeur\n"
          "Speed=3.0\n"
          "Gravity=0.05\n"
          "Damage=2.0\n"
          "Radius=1.6\n"
          "Clip=1\n"
          "\n[Safehouses]\n"
          "; planques (F10 dans une planque pour l'ajouter) : F9 y donne un kit d'armes\n"
          "Count=1\n"
          "P1=875.4,-96.7\n", f);
    fclose(f);
}
bool ReloadIniIfChanged()
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &a)) { WriteDefaultIni(); return false; }
    if (CompareFileTime(&a.ftLastWriteTime, &g_iniTime) == 0) return false;
    g_iniTime = a.ftLastWriteTime;
    Settings s;
    s.enabled = IniFloat("Enabled", 1) != 0;
    s.fovScale = IniFloat("FovScale", s.fovScale);
    s.distanceScale = IniFloat("DistanceScale", s.distanceScale);
    s.height = IniFloat("Height", s.height);
    s.shoulder = IniFloat("Shoulder", s.shoulder);
    s.smoothing = IniFloat("Smoothing", s.smoothing);
    s.combatToo = IniFloat("CombatToo", 1) != 0;
    s.combatFovScale = IniFloat("FovScale", s.combatFovScale, "Combat");
    s.combatDistanceScale = IniFloat("DistanceScale", s.combatDistanceScale, "Combat");
    s.combatHeight = IniFloat("Height", s.combatHeight, "Combat");
    s.vehEnabled = IniFloat("Enabled", 1, "Vehicle") != 0;
    s.vehFovScale = IniFloat("FovScale", s.vehFovScale, "Vehicle");
    s.vehDistanceScale = IniFloat("DistanceScale", s.vehDistanceScale, "Vehicle");
    s.vehHeight = IniFloat("Height", s.vehHeight, "Vehicle");
    g_cfg = s;
    ReadMinimapSetting();
    LOG("reglages : actif=%d fov x%.2f distance x%.2f hauteur %+.2f epaule %+.2f douceur x%.2f combat=%d | vehicule actif=%d fov x%.2f distance x%.2f hauteur %+.2f | minimap arrondie=%d",
        s.enabled, s.fovScale, s.distanceScale, s.height, s.shoulder, s.smoothing, s.combatToo,
        s.vehEnabled, s.vehFovScale, s.vehDistanceScale, s.vehHeight,
        g_roundMinimap.load(std::memory_order_relaxed) ? 1 : 0);
    return true;
}

// ---- Mémoire -----------------------------------------------------------------------------
bool IsReadable(uintptr_t p, size_t n = 8)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    return p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

struct Section { uintptr_t start, end; };
Section GetSection(const char* name)
{
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
    auto* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
        if (strncmp(reinterpret_cast<const char*>(s->Name), name, 8) == 0)
            return { g_base + s->VirtualAddress, g_base + s->VirtualAddress + s->Misc.VirtualSize };
    return { 0, 0 };
}

// Retrouve la vtable d'une classe à partir de son nom RTTI (TypeDescriptor -> CompleteObjectLocator -> vtable).
uintptr_t FindVTable(const char* rttiName)
{
    const size_t len = strlen(rttiName);
    uintptr_t td = 0;
    for (const char* sec : { ".data", ".rdata" }) {
        auto s = GetSection(sec);
        for (uintptr_t p = s.start; s.start && p + len < s.end; ++p)
            if (memcmp(reinterpret_cast<void*>(p), rttiName, len + 1) == 0) { td = p - 0x10; break; }
        if (td) break;
    }
    if (!td) return 0;
    const uint32_t tdRva = static_cast<uint32_t>(td - g_base);
    auto rdata = GetSection(".rdata");
    uintptr_t col = 0;
    for (uintptr_t p = rdata.start; p + 24 <= rdata.end; p += 4) {
        auto* c = reinterpret_cast<const uint32_t*>(p);
        if (c[0] == 1 && c[1] == 0 && c[3] == tdRva && c[5] == static_cast<uint32_t>(p - g_base)) { col = p; break; }
    }
    if (!col) return 0;
    for (uintptr_t p = rdata.start; p + 8 <= rdata.end; p += 8)
        if (*reinterpret_cast<const uintptr_t*>(p) == col) return p + 8;
    return 0;
}

// Une région mémoire peut être libérée pendant la lecture : lecture protégée (pas d'objet C++ ici).
size_t ScanRegion(const uintptr_t* p, size_t n, uintptr_t vtable, uintptr_t* out, size_t cap)
{
    size_t count = 0;
    __try {
        for (size_t i = 0; i < n && count < cap; ++i)
            if (p[i] == vtable) out[count++] = reinterpret_cast<uintptr_t>(&p[i]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

// Parcourt la mémoire privée du jeu à la recherche des objets dont la vtable correspond.
std::vector<uintptr_t> FindInstances(uintptr_t vtable)
{
    std::vector<uintptr_t> found;
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull; a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi))) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        uintptr_t hits[16];
        const size_t n = ScanRegion(reinterpret_cast<const uintptr_t*>(mbi.BaseAddress), mbi.RegionSize / 8, vtable, hits, 16);
        found.insert(found.end(), hits, hits + n);
    }
    return found;
}

// ---- Caméra ------------------------------------------------------------------------------
struct CameraState {
    uintptr_t obj = 0;
    uint8_t original[kNumContexts * kFollowParamsSize];
};
std::vector<CameraState> g_cams;

float& F(uintptr_t params, size_t off) { return *reinterpret_cast<float*>(params + off); }

void ApplyCamera(CameraState& cam)
{
    for (int c = 0; c < kNumContexts; ++c) {
        const uintptr_t dst = cam.obj + kFollow_Params + c * kFollowParamsSize;
        const uintptr_t src = reinterpret_cast<uintptr_t>(cam.original) + c * kFollowParamsSize;
        memcpy(reinterpret_cast<void*>(dst), reinterpret_cast<void*>(src), kFollowParamsSize);
        const bool aiming = (c >= 4 && c <= 7) || c == 12;  // la visée garde son cadrage d'origine
        const bool combat = (c == 8 || c == 13 || c == 14);  // marche ciblée, focus, prise
        if (!g_cfg.enabled || aiming || (combat && !g_cfg.combatToo)) continue;
        const float fovScale  = combat ? g_cfg.combatFovScale : g_cfg.fovScale;
        const float distScale = combat ? g_cfg.combatDistanceScale : g_cfg.distanceScale;
        const float height    = combat ? g_cfg.combatHeight : g_cfg.height;
        F(dst, kFP_Fov)       = F(src, kFP_Fov) * fovScale;
        F(dst, kFP_FovMax)    = F(src, kFP_FovMax) * fovScale;
        F(dst, kFP_RadiusMin) = F(src, kFP_RadiusMin) * distScale;
        F(dst, kFP_RadiusMax) = F(src, kFP_RadiusMax) * distScale;
        F(dst, kFP_EyeRise)   = F(src, kFP_EyeRise) + height;
        F(dst, kFP_LookOffset + 0) = F(src, kFP_LookOffset + 0) + g_cfg.shoulder;
        if (g_cfg.smoothing > 0.01f) {
            F(dst, kFP_SmoothPos) = F(src, kFP_SmoothPos) / g_cfg.smoothing;
            F(dst, kFP_SmoothRot) = F(src, kFP_SmoothRot) / g_cfg.smoothing;
        }
    }
}

// Écarte les faux positifs (adresse de la vtable qui traîne sur une pile, etc.) : on n'écrit
// que dans un vrai FollowCameraComponent aux réglages plausibles.
bool LooksLikeFollowCamera(uintptr_t obj)
{
    if (!IsReadable(obj, kFollow_Params + kNumContexts * kFollowParamsSize)) return false;
    if (*reinterpret_cast<uint32_t*>(obj + kComp_TypeUID) != kFollowTypeUID) return false;
    const uintptr_t walk = obj + kFollow_Params;
    const float fov = F(walk, kFP_Fov), rMax = F(walk, kFP_RadiusMax);
    return std::isfinite(fov) && fov > 0.1f && fov < 180.f && std::isfinite(rMax) && rMax > 0.2f && rMax < 50.f;
}

void LogDefaults(const CameraState& cam)
{
    LOG("camera trouvee a %p, reglages d'origine :", reinterpret_cast<void*>(cam.obj));
    for (int c = 0; c < kNumContexts; ++c) {
        const uintptr_t p = reinterpret_cast<uintptr_t>(cam.original) + c * kFollowParamsSize;
        LOG("  %-16s fov %.3f/%.3f  hauteur %.2f  distance %.2f-%.2f  regard (%.2f %.2f %.2f)  lissage %.2f/%.2f",
            kContextNames[c], F(p, kFP_Fov), F(p, kFP_FovMax), F(p, kFP_EyeRise), F(p, kFP_RadiusMin), F(p, kFP_RadiusMax),
            F(p, kFP_LookOffset), F(p, kFP_LookOffset + 4), F(p, kFP_LookOffset + 8), F(p, kFP_SmoothPos), F(p, kFP_SmoothRot));
    }
}

// ---- Caméra de véhicule : réglages SOURCE (ChaseCameraParameters) ------------------------
// Le composant ChaseCameraComponent recopie en permanence les réglages de la situation courante
// (conduite, course, fuite...) : on modifie donc directement ces réglages source.
// Une structure est reconnue si son identifiant mNameSymbol est bien le hash de son nom mName.
constexpr size_t kCP_Name      = 0x00;  // const char*
constexpr size_t kCP_Symbol    = 0x08;  // qSymbol (u32)
constexpr size_t kCP_Context   = 0x0C;  // ChaseCameraContext (0..12)
constexpr size_t kCP_EyeLo     = 0xB0;  // qVector4 x3 (Lo, Med, Hi)
constexpr size_t kCP_FovRamp   = 0xE0;  // qVector4 : (vitesse min, vitesse max, fov min, fov max)
constexpr size_t kCP_Size      = 0x140;

struct ChaseParams {
    uintptr_t addr = 0;
    char name[32] = {};
    float origEye[12];
    float origFov[4];
};
std::vector<ChaseParams> g_chaseParams;

uint32_t SdHash(const char* s, bool upper)  // qStringHash32 / qStringHashUpper32 du moteur
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; ++k) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t h = 0xFFFFFFFFu;
    for (; *s; ++s) {
        uint8_t ch = static_cast<uint8_t>(*s);
        if (upper && ch >= 'a' && ch <= 'z') ch -= 32;
        h = (h << 8) ^ table[((h >> 24) ^ ch) & 0xFF];
    }
    return h;
}

float* FP(uintptr_t a) { return reinterpret_cast<float*>(a); }

// Parcourt une région mémoire : filtre rapide (situation 0..12, fov plausibles), puis vérifie que
// mNameSymbol est bien le hash du nom. Lecture protégée, sans objet C++ (contrainte de __try).
size_t ScanChaseRegion(uintptr_t start, uintptr_t end, uintptr_t* out, size_t cap)
{
    size_t count = 0;
    __try {
        for (uintptr_t p = start; p + kCP_Size <= end && count < cap; p += 8) {
            if (*reinterpret_cast<const uint32_t*>(p + kCP_Context) > 12) continue;
            const float f2 = FP(p + kCP_FovRamp)[2], f3 = FP(p + kCP_FovRamp)[3];
            if (!(f2 > 20.f && f2 < 150.f && f3 > 20.f && f3 < 150.f)) continue;
            const uintptr_t name = *reinterpret_cast<const uintptr_t*>(p + kCP_Name);
            if (name < 0x10000 || name > 0x7FFFFFFFFFFFull) continue;
            char buf[32];
            int n = 0;
            __try {
                for (; n < 31; ++n) {
                    const char c = reinterpret_cast<const char*>(name)[n];
                    if (!c) break;
                    if (c < 0x20 || c > 0x7E) { n = -1; break; }  // texte imprimable (les noms peuvent contenir des espaces)
                    buf[n] = c;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { n = -1; }
            if (n < 3 || n >= 31) continue;
            buf[n] = 0;
            const uint32_t sym = *reinterpret_cast<const uint32_t*>(p + kCP_Symbol);
            if (sym == SdHash(buf, false) || sym == SdHash(buf, true)) out[count++] = p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

void FindChaseParams()
{
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull; a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi))) break;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_READONLY)) || (mbi.Protect & PAGE_GUARD)) continue;
        uintptr_t found[64];
        const uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t n = ScanChaseRegion(start, start + mbi.RegionSize, found, 64);
        for (size_t i = 0; i < n; ++i) {
            const uintptr_t p = found[i];
            bool known = false;
            for (auto& c : g_chaseParams) known |= c.addr == p;
            if (known) continue;
            ChaseParams cp;
            cp.addr = p;
            strncpy_s(cp.name, *reinterpret_cast<const char* const*>(p + kCP_Name), _TRUNCATE);
            memcpy(cp.origEye, FP(p + kCP_EyeLo), sizeof(cp.origEye));
            memcpy(cp.origFov, FP(p + kCP_FovRamp), sizeof(cp.origFov));
            g_chaseParams.push_back(cp);
            LOG("reglages vehicule \"%s\" (situation %u) : oeil (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)  fov %.0f -> %.0f (vitesse %.0f -> %.0f)",
                cp.name, *reinterpret_cast<uint32_t*>(p + kCP_Context),
                cp.origEye[0], cp.origEye[1], cp.origEye[2], cp.origEye[4], cp.origEye[5], cp.origEye[6], cp.origEye[8], cp.origEye[9], cp.origEye[10],
                cp.origFov[2], cp.origFov[3], cp.origFov[0], cp.origFov[1]);
        }
    }
}

void ApplyChaseParams()
{
    for (auto& cp : g_chaseParams) {
        float eye[12], fov[4];
        memcpy(eye, cp.origEye, sizeof(eye));
        memcpy(fov, cp.origFov, sizeof(fov));
        if (g_cfg.vehEnabled) {
            for (int i = 0; i < 3; ++i) {
                eye[i * 4 + 0] *= g_cfg.vehDistanceScale;  // recul (axe avant/arrière)
                eye[i * 4 + 1] *= g_cfg.vehDistanceScale;
                eye[i * 4 + 2] += g_cfg.vehHeight;          // hauteur
            }
            fov[2] *= g_cfg.vehFovScale;  // fov à basse vitesse
            fov[3] *= g_cfg.vehFovScale;  // fov à haute vitesse
        }
        DWORD old = 0;
        const bool unlocked = VirtualProtect(reinterpret_cast<void*>(cp.addr), kCP_Size, PAGE_READWRITE, &old) != 0;
        memcpy(FP(cp.addr + kCP_EyeLo), eye, sizeof(eye));
        memcpy(FP(cp.addr + kCP_FovRamp), fov, sizeof(fov));
        if (unlocked && old != PAGE_READWRITE) VirtualProtect(reinterpret_cast<void*>(cp.addr), kCP_Size, old, &old);
    }
}

void ApplyWatchIfRequested();
void ArmWatch();

DWORD WINAPI MainThread(void*)
{
    Sleep(5000);  // laisse le jeu démarrer
    const uintptr_t vt = FindVTable(".?AVFollowCameraComponent@UFG@@");
    LOG("vtable FollowCameraComponent : %s", vt ? "trouvee" : "INTROUVABLE");
    if (!vt) return 0;
    ULONGLONG lastScan = 0, lastChaseScan = 0;
    while (g_running) {
        const bool cfgChanged = ReloadIniIfChanged();

        // Véhicule : les réglages source sont cherchés une fois en jeu (nouvel essai toutes les 10 s
        // tant qu'on n'en a pas trouvé), puis réappliqués à chaque modification du .ini.
        static int chaseScans = 0;  // recherche lourde : 6 essais maximum
        if (g_chaseParams.size() < 4 && chaseScans < 6 && !g_cams.empty() && GetTickCount64() - lastChaseScan > 10000) {
            ++chaseScans;
            lastChaseScan = GetTickCount64();
            FindChaseParams();
            LOG("reglages vehicule trouves : %zu", g_chaseParams.size());
            ApplyChaseParams();
        } else if (cfgChanged) {
            ApplyChaseParams();
        }

        // Caméras disparues (chargement de partie...) : on les oublie.
        for (size_t i = 0; i < g_cams.size();)
            if (!IsReadable(g_cams[i].obj) || *reinterpret_cast<uintptr_t*>(g_cams[i].obj) != vt) g_cams.erase(g_cams.begin() + i);
            else ++i;

        const ULONGLONG now = GetTickCount64();
        if (g_cams.empty() && now - lastScan > 3000) {
            lastScan = now;
            const auto candidates = FindInstances(vt);
            size_t kept = 0;
            for (uintptr_t obj : candidates) {
                if (!LooksLikeFollowCamera(obj)) continue;
                ++kept;
                CameraState cam;
                cam.obj = obj;
                memcpy(cam.original, reinterpret_cast<void*>(obj + kFollow_Params), sizeof(cam.original));
                LogDefaults(cam);
                g_cams.push_back(cam);
                ApplyCamera(g_cams.back());
            }
            static size_t lastLogged = SIZE_MAX;
            if (candidates.size() != lastLogged) {
                lastLogged = candidates.size();
                LOG("recherche camera : %zu candidats, %zu retenus", candidates.size(), kept);
            }
        } else if (cfgChanged) {
            for (auto& cam : g_cams) ApplyCamera(cam);
        }
        ApplyWatchIfRequested();
        Sleep(250);
    }
    return 0;
}


// ---- Mini-carte : rectangle façon GTA ------------------------------------------------------
// Le moteur n'a pas de mode rectangle. FUN_1400c0760 ne lit que circle_mask._width (un rayon),
// et le pochoir de FUN_1400c8060 utilise un seul entier (RVA 0x2033D90, 215) pour les deux axes,
// centré sur (0x20341B8, 0x20341BC) = (255, 890). useWideTiles (octet +0x1C) zoome les deux axes
// pareil. Les échelles H/V (0x20F2FC0 / 0x20F2FC4) valent 1 et sont partagées avec la grande carte.
//
// Les rues sont des puces MMAP_CHIP de 128 px, décalées de -128, tournées, puis coupées
// par le pochoir. On élargit seulement ce pochoir. Le plafond de 7 puces (0xBF810) reste.
// Un CALL relatif écrit directement vers ce plugin dépasse souvent les 2 Go et a fait
// un écran blanc dès que la carte de la rue se charge. La transparence passe par un
// stub alloué à portée du CALL 0xC85F8, qui saute en absolu vers HookTiles.
// Le second CALL du pochoir est à 0xC82F4 et revient en 0xC82F9. 0xC82FF est le CALL
// d'après, qui ne dessine pas le quad.
// La grande carte (octet +0x38 non nul, autres adresses de retour) n'est pas touchée.
// FUN_1400bf720 n'est appelée que depuis ce CALL : la grande carte ne passe pas ici.
constexpr uintptr_t kQuadRva = 0xBFEC0;
constexpr uintptr_t kClampRva = 0xC6090;
constexpr uintptr_t kTileRva = 0xBF720;
constexpr uintptr_t kTileCall = 0xC85F8;         // seul appel des tuiles de la mini-carte
constexpr uintptr_t kMaskSizeRva = 0x2033D90;
constexpr uintptr_t kMinimapRetA = 0xC826A;      // retour du disque couleur
constexpr uintptr_t kMinimapRetB = 0xC82F9;      // retour du pochoir
constexpr uintptr_t kPipFlagRva = 0x2033DB6;
constexpr uint32_t kFxCircleFill = 0x60A840F0;
constexpr uint32_t kAlphaModulated = 0xA3833FDE;
constexpr float kMapAspect = 1.40f;              // largeur / hauteur
constexpr float kBackAlpha = 0.55f;              // fond sombre semi-transparent
constexpr float kTileAlpha = 0.85f;              // rues légèrement transparentes
constexpr float kCornerRatio = 0.08f;            // rayon des coins / hauteur (215 px -> ~17 px)
constexpr uintptr_t kTileTree = 0x249BD10;       // puces de mini-carte chargées (arbre)
constexpr uintptr_t kTreeFirst = 0x170B20;
constexpr uintptr_t kTreeNext = 0x1714C0;
constexpr uintptr_t kDrawTileRects = 0xBFA10;
constexpr size_t kStolen = 15;

const uint8_t kQuadPrologue[kStolen] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0xC7, 0x44, 0x24, 0x38, 0xFE, 0xFF, 0xFF, 0xFF
};
const uint8_t kClampPrologue[kStolen] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30
};

using QuadDrawFn = void(__fastcall*)(
    void* view, float x, float y, float size, float size2,
    float* color, uint32_t texture, uint64_t extra,
    uint32_t alphaState, uint32_t rasterState, float* matrix);
using ClampFn = void(__fastcall*)(
    void* math, void* ctx, float* inPoint, float* outPoint, char enable, unsigned char* outside);
using TileFn = void(__fastcall*)(
    void* widget, void* view, float* color,
    uint32_t alphaState, uint32_t rasterState, float* matrix);
// Position monde du joueur (armurerie), relevée dans la mise à jour du HUD.
std::atomic<float> g_playerX{0.f}, g_playerY{0.f};
std::atomic<bool> g_playerKnown{false};
float g_mapOrigX = 255.f, g_mapOrigY = 890.f;   // centre d'origine de la mini-carte
float g_mapDX = 0.f, g_mapDY = 0.f;              // décalage appliqué ([Minimap] OffsetX/Y)
QuadDrawFn g_origQuad = nullptr;
ClampFn g_origClamp = nullptr;
TileFn g_origTiles = nullptr;

void* MakeTrampoline(const uint8_t* stolen, uintptr_t resume)
{
    auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stolen, kStolen);
    mem[kStolen + 0] = 0xFF;
    mem[kStolen + 1] = 0x25;
    *reinterpret_cast<uint32_t*>(mem + kStolen + 2) = 0;
    *reinterpret_cast<uint64_t*>(mem + kStolen + 6) = resume;
    return mem;
}

bool PatchAbsJump(uintptr_t at, const uint8_t* expected, void* dest)
{
    if (memcmp(reinterpret_cast<void*>(at), expected, kStolen) != 0) return false;
    uint8_t buf[kStolen];
    memset(buf, 0x90, kStolen);
    buf[0] = 0xFF;
    buf[1] = 0x25;
    *reinterpret_cast<uint32_t*>(buf + 2) = 0;
    *reinterpret_cast<uint64_t*>(buf + 6) = reinterpret_cast<uintptr_t>(dest);
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(at), kStolen, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(reinterpret_cast<void*>(at), buf, kStolen);
    DWORD now = 0;
    VirtualProtect(reinterpret_cast<void*>(at), kStolen, old, &now);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), kStolen);
    return true;
}

__declspec(noinline) void __fastcall HookQuad(
    void* view, float x, float y, float size, float size2,
    float* color, uint32_t texture, uint64_t extra,
    uint32_t alphaState, uint32_t rasterState, float* matrix)
{
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool minimap = ret == g_base + kMinimapRetA || ret == g_base + kMinimapRetB;
    if (!g_origQuad) return;
    if (!minimap || !g_roundMinimap.load(std::memory_order_relaxed) || texture != kFxCircleFill || !(size > 8.f)) {
        g_origQuad(view, x, y, size, size2, color, texture, extra, alphaState, rasterState, matrix);
        return;
    }

    // Les deux passages (fond sombre, puis pochoir qui découpe les rues) prennent la même forme :
    // un rectangle aux coins arrondis, même hauteur que le rond d'origine, plus large, recentré.
    // Il est fait de 7 morceaux qui ne se recouvrent pas (sinon le fond serait plus foncé
    // aux recouvrements) : la colonne centrale, les deux bandes de côté et quatre quarts de
    // disque pris dans la texture FX_Circle_Fill (paramètre UV = u, v, largeur, hauteur).
    // Les morceaux droits échantillonnent le centre du disque, qui est plein.
    const float h = size2;
    const float w = h * kMapAspect;
    const float l = x + (size - w) * 0.5f;
    const float t = y;
    float r = h * kCornerRatio;
    if (r < 1.f) r = 1.f;
    static const float uvFull[4] = { 0.49f, 0.49f, 0.02f, 0.02f };
    static const float uvTL[4] = { 0.0f, 0.0f, 0.5f, 0.5f };
    static const float uvTR[4] = { 0.5f, 0.0f, 0.5f, 0.5f };
    static const float uvBL[4] = { 0.0f, 0.5f, 0.5f, 0.5f };
    static const float uvBR[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    // Transparence : seul le fond (premier passage) est atténué ; le pochoir garde sa couleur.
    float back[4] = { 0.f, 0.f, 0.f, 0.f };
    float* col = color;
    if (ret == g_base + kMinimapRetA && color) {
        back[0] = color[0]; back[1] = color[1]; back[2] = color[2]; back[3] = color[3] * kBackAlpha;
        col = back;
    }
    auto piece = [&](float px, float py, float pw, float ph, const float* uv) {
        g_origQuad(view, px, py, pw, ph, col, texture, reinterpret_cast<uint64_t>(uv), alphaState, rasterState, matrix);
    };
    piece(l + r, t, w - 2 * r, h, uvFull);
    piece(l, t + r, r, h - 2 * r, uvFull);
    piece(l + w - r, t + r, r, h - 2 * r, uvFull);
    piece(l, t, r, r, uvTL);
    piece(l + w - r, t, r, r, uvTR);
    piece(l, t + h - r, r, r, uvBL);
    piece(l + w - r, t + h - r, r, r, uvBR);
    static std::atomic<int> logged{0};
    if (logged.exchange(1) == 0) LOG("minimap : rectangle arrondi %.0f x %.0f (coins %.0f px)", w, h, r);
}

// FUN_1400c5930 : projection monde -> écran autour de l'origine ctx[0..1] (= l'ancien centre
// de la mini-carte, via circle_mask). Icônes et zones de police y passent : on décale le
// résultat comme la carte. La grande carte a une autre origine et n'est pas touchée.
constexpr uintptr_t kProjectRva = 0xC5930;
const uint8_t kProjectPrologue[kStolen] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50, 0xF3, 0x0F, 0x10, 0x42, 0x04
};
using ProjectFn = void(__fastcall*)(float* out, float* in, float* ctx);
ProjectFn g_origProject = nullptr;

// Zoom minimum : en voiture le jeu dézoome jusqu'à ~0.68, et les 7 puces chargées autour
// du joueur ne couvrent plus les coins du rectangle (trous). widget+0x18 = zoom, lu par la
// projection des icônes, les zones et le rendu des rues.
float g_minZoom = 1.05f;
void ClampZoom(uint8_t* widget)
{
    auto* z = reinterpret_cast<float*>(widget + 0x18);
    if (*z > 0.1f && *z < g_minZoom) *z = g_minZoom;
}

__declspec(noinline) void __fastcall HookProject(float* out, float* in, float* ctx)
{
    if (!g_origProject) return;
    const bool hudOld = ctx && fabsf(ctx[0] - g_mapOrigX) < 8.f && fabsf(ctx[1] - g_mapOrigY) < 8.f;
    const bool hudNew = ctx && fabsf(ctx[0] - g_mapOrigX - g_mapDX) < 8.f && fabsf(ctx[1] - g_mapOrigY - g_mapDY) < 8.f;
    if (hudOld || hudNew) ClampZoom(reinterpret_cast<uint8_t*>(ctx));
    g_origProject(out, in, ctx);
    if (!out || !hudOld || (g_mapDX == 0.f && g_mapDY == 0.f)) return;
    // Seulement pour les icônes (appel depuis FUN_1400c6090) : les autres utilisateurs
    // de la projection (éléments Flash) se décalaient deux fois.
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (ret > g_base + kClampRva && ret < g_base + kClampRva + 0xF2) {
        out[0] += g_mapDX;
        out[1] += g_mapDY;
    }
}

__declspec(noinline) void __fastcall HookClamp(
    void* math, void* ctx, float* inPoint, float* outPoint, char enable, unsigned char* outside)
{
    if (!g_origClamp) return;
    auto* raw = static_cast<unsigned char*>(math);
    const float radius = *reinterpret_cast<float*>(raw + 0x28);
    const bool hud = g_roundMinimap.load(std::memory_order_relaxed)
        && raw[0x38] == 0
        && std::isfinite(radius) && radius > 16.f;
    auto* flag = reinterpret_cast<unsigned char*>(g_base + kPipFlagRva);
    const unsigned char saved = flag ? *flag : 0;
    if (hud && flag) *flag = 0;
    g_origClamp(math, ctx, inPoint, outPoint, enable, outside);
    if (hud && flag) *flag = saved;
    if (!hud || !outPoint) return;

    // Le point a déjà été décalé par HookProject ; le centre de rognage doit suivre.
    float cx = *reinterpret_cast<float*>(raw + 0x20);
    float cy = *reinterpret_cast<float*>(raw + 0x24);
    if (fabsf(cx - g_mapOrigX) < 8.f && fabsf(cy - g_mapOrigY) < 8.f) {
        cx += g_mapDX;
        cy += g_mapDY;
    }
    if (!enable || !saved || !outside) return;
    float dx = outPoint[0] - cx;
    float dy = cy - outPoint[1];
    const int sidePx = *reinterpret_cast<int*>(g_base + kMaskSizeRva);
    const float half = sidePx > 16 ? static_cast<float>(sidePx) * 0.5f : radius;
    const float hx = half * kMapAspect - 10.f;   // un peu à l'intérieur du bord
    const float hy = half - 10.f;
    const bool outX = fabsf(dx) > hx;
    const bool outY = fabsf(dy) > hy;
    if (!outX && !outY) return;
    *outside = 1;
    if (outX) dx = copysignf(hx, dx);
    if (outY) dy = copysignf(hy, dy);
    outPoint[0] = cx + dx;
    outPoint[1] = cy - dy;
}

// Le CALL 0xC85F8 est un E8 relatif. Il ne peut pas viser le plugin si l'exe et la
// DLL sont à plus de 2 Go l'un de l'autre.
// On alloue le stub dans cette fenêtre, et le stub fait un saut absolu.
void* AllocWithinRel32(uintptr_t callSite, size_t bytes)
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t origin = callSite + 5;
    uintptr_t lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    constexpr uintptr_t kSpan = 0x7FFF0000ull;
    if (origin > kSpan && origin - kSpan > lo) lo = origin - kSpan;
    if (hi > origin + kSpan) hi = origin + kSpan;

    uintptr_t addr = lo;
    for (int guard = 0; addr < hi && guard < 100000; ++guard) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end = base + mbi.RegionSize;
        if (end <= addr) break;
        if (mbi.State == MEM_FREE && end > lo) {
            uintptr_t aligned = (base + gran - 1) & ~(gran - 1);
            if (aligned < lo) aligned = (lo + gran - 1) & ~(gran - 1);
            if (aligned >= base && aligned + bytes <= end && aligned + bytes <= hi) {
                const int64_t rel = static_cast<int64_t>(aligned) - static_cast<int64_t>(origin);
                if (rel >= static_cast<int64_t>(INT32_MIN) && rel <= static_cast<int64_t>(INT32_MAX)) {
                    void* p = VirtualAlloc(reinterpret_cast<void*>(aligned), bytes,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                    if (p) return p;
                }
            }
        }
        addr = end;
    }
    return nullptr;
}

// Réécriture de FUN_1400bf720 sans son plafond de 7 puces. L'original range les puces dans
// un tableau de 7 sur la pile : le relever débordait sur la copie de travail (écran blanc).
// Ici chaque puce chargée est dessinée une par une avec FUN_1400bfa10 (même calcul de
// position que le jeu), donc le rectangle large est rempli au lieu d'avoir des trous.
//   x0 = k * (puce.x - widget.x), y0 = -k * (puce.y - widget.y), k = échelle / (a4 - a0)
using TreeFirstFn = void* (__fastcall*)(void* tree);
using TreeNextFn = void* (__fastcall*)(void* tree, void* item);
using DrawTileRectsFn = void(__fastcall*)(
    void* view, float* entries, int count, float* color,
    uint32_t texture, uint32_t alphaState, uint32_t rasterState, float* matrix);

__declspec(noinline) void __fastcall HookTiles(
    void* widget, void* view, float* color,
    uint32_t alphaState, uint32_t rasterState, float* matrix)
{
    if (!g_origTiles) return;
    if (!g_roundMinimap.load(std::memory_order_relaxed)) {
        g_origTiles(widget, view, color, alphaState, rasterState, matrix);
        return;
    }
    auto G = [](uintptr_t rva) { return *reinterpret_cast<float*>(g_base + rva); };
    auto* w = static_cast<uint8_t*>(widget);
    ClampZoom(w);
    float faded[4] = { 1.f, 1.f, 1.f, 1.f };
    if (color) {
        faded[0] = color[0]; faded[1] = color[1]; faded[2] = color[2]; faded[3] = color[3] * kTileAlpha;
        color = faded;
    }
    const float a0 = G(0x21740A0), a4 = G(0x21740A4), b0 = G(0x21740B0);
    const float k = G(0x166C900) / (a4 - a0) * b0;
    const float wx = *reinterpret_cast<float*>(w + 0x8);
    const float wy = *reinterpret_cast<float*>(w + 0xC);

    auto first = reinterpret_cast<TreeFirstFn>(g_base + kTreeFirst);
    auto next = reinterpret_cast<TreeNextFn>(g_base + kTreeNext);
    auto draw = reinterpret_cast<DrawTileRectsFn>(g_base + kDrawTileRects);
    void* tree = reinterpret_cast<void*>(g_base + kTileTree);
    int drawn = 0;
    int guard = 0;
    for (void* it = first(tree); it && guard < 256; it = next(tree, it), ++guard) {
        auto* item = static_cast<uint8_t*>(it);
        if (*reinterpret_cast<int32_t*>(item + 0x60) == 0) continue;
        auto* tex = *reinterpret_cast<uint8_t**>(item + 0x78);
        if (!tex) continue;
        const float x0 = k * (*reinterpret_cast<float*>(item + 0x58) - wx);
        const float y0 = -k * (*reinterpret_cast<float*>(item + 0x5C) - wy);
        float e[10] = {
            x0, 0.f, x0 + *reinterpret_cast<uint16_t*>(tex + 0x64), 1.f,
            y0, 0.f, y0 + *reinterpret_cast<uint16_t*>(tex + 0x66), 1.f,
            0.f, 0.f };
        draw(view, e, 1, color, *reinterpret_cast<uint32_t*>(item + 0x80), alphaState, rasterState, matrix);
        ++drawn;
    }
    // Diagnostic de couverture (toutes les ~5 s) : étendue des puces autour du joueur,
    // avant zoom et rotation, et zoom courant (0x2173F10 + widget+0x18, +0.5 en voiture).
    static DWORD lastDiag = 0;
    const DWORD tick = GetTickCount();
    if (tick - lastDiag > 5000) {
        lastDiag = tick;
        float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
        int n = 0;
        guard = 0;
        for (void* it = first(tree); it && guard < 256; it = next(tree, it), ++guard) {
            auto* item = static_cast<uint8_t*>(it);
            if (*reinterpret_cast<int32_t*>(item + 0x60) == 0) continue;
            auto* tex = *reinterpret_cast<uint8_t**>(item + 0x78);
            if (!tex) continue;
            const float x0 = k * (*reinterpret_cast<float*>(item + 0x58) - wx);
            const float y0 = -k * (*reinterpret_cast<float*>(item + 0x5C) - wy);
            mnx = (std::min)(mnx, x0); mny = (std::min)(mny, y0);
            mxx = (std::max)(mxx, x0 + *reinterpret_cast<uint16_t*>(tex + 0x64));
            mxy = (std::max)(mxy, y0 + *reinterpret_cast<uint16_t*>(tex + 0x66));
            ++n;
        }
        LOG("minimap diag : %d puces, x %.0f..%.0f y %.0f..%.0f, zoom base %.3f widget %.3f voiture=%d k=%.4f",
            n, mnx, mxx, mny, mxy, G(0x2173F10), *reinterpret_cast<float*>(w + 0x18), w[0x1C], k);
    }
    static std::atomic<int> best{0};
    if (drawn > best.load(std::memory_order_relaxed)) {
        best.store(drawn, std::memory_order_relaxed);
        LOG("minimap : %d puces dessinees (le jeu s'arretait a 7)", drawn);
    }
}

bool HookMinimapTiles()
{
    auto* call = reinterpret_cast<uint8_t*>(g_base + kTileCall);
    if (call[0] != 0xE8) {
        LOG("minimap : appel des tuiles inattendu (%02x)", call[0]);
        return false;
    }
    int32_t rel = 0;
    memcpy(&rel, call + 1, 4);
    const uintptr_t target = reinterpret_cast<uintptr_t>(call + 5) + static_cast<intptr_t>(rel);
    if (target != g_base + kTileRva) {
        LOG("minimap : tuiles pas a l'adresse attendue");
        return false;
    }
    void* stub = AllocWithinRel32(reinterpret_cast<uintptr_t>(call), 32);
    if (!stub) {
        LOG("minimap : pas de place a portee de l'exe, fond laisse opaque");
        return false;
    }
    auto* code = static_cast<uint8_t*>(stub);
    code[0] = 0xFF;
    code[1] = 0x25;
    *reinterpret_cast<uint32_t*>(code + 2) = 0;
    *reinterpret_cast<uint64_t*>(code + 6) = reinterpret_cast<uintptr_t>(&HookTiles);

    const uintptr_t origin = reinterpret_cast<uintptr_t>(call + 5);
    const int64_t rel64 = static_cast<int64_t>(reinterpret_cast<uintptr_t>(stub)) - static_cast<int64_t>(origin);
    if (rel64 < static_cast<int64_t>(INT32_MIN) || rel64 > static_cast<int64_t>(INT32_MAX)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        LOG("minimap : stub hors de portee, fond laisse opaque");
        return false;
    }
    const int32_t neu = static_cast<int32_t>(rel64);
    DWORD old = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(call + 1, &neu, 4);
    DWORD now = 0;
    VirtualProtect(call, 5, old, &now);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    g_origTiles = reinterpret_cast<TileFn>(target);
    LOG("minimap : detour des puces installe (sans plafond)");
    return true;
}

void EnsureMinimapIni()
{
    if (GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        WriteDefaultIni();
        return;
    }
    if (GetPrivateProfileIntA("Minimap", "Rounded", -1, g_iniPath) != -1) return;
    FILE* f = nullptr;
    if (fopen_s(&f, g_iniPath, "a") != 0 || !f) return;
    fputs("\r\n[Minimap]\r\n"
          "; 1 = rectangle aux coins arrondis facon GTA, 0 = rond d'origine (relancer le jeu)\r\n"
          "Rounded=1\r\n"
          "; decalage en pixels (negatif = gauche / haut)\r\n"
          "OffsetX=-40\r\n"
          "OffsetY=40\r\n", f);
    fclose(f);
}

// Pendant le chargement et l'intro du HUD (états 1 et 2, souvent ~2 s au retour
// d'un menu ou d'une cinématique), le jeu réécrit chaque image l'alpha de l'ancien
// anneau, de l'arc de vie et du pulse. Ça se fait après le script Flash, donc le
// masquage ActionScript ne tient pas tant que cet état dure. On saute ce bloc.
bool SkipVanillaHudIntro()
{
    auto* ja = reinterpret_cast<uint8_t*>(g_base + 0x5E59F0);
    const uint8_t expect[6] = { 0x0F, 0x87, 0xEE, 0x00, 0x00, 0x00 };
    if (memcmp(ja, expect, 6) != 0) {
        LOG("hud : saut d'intro inattendu (%02x %02x)", ja[0], ja[1]);
        return false;
    }
    const uint8_t jmp[6] = { 0xE9, 0xEF, 0x00, 0x00, 0x00, 0x90 };
    DWORD old = 0;
    if (!VirtualProtect(ja, 6, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(ja, jmp, 6);
    DWORD now = 0;
    VirtualProtect(ja, 6, old, &now);
    FlushInstructionCache(GetCurrentProcess(), ja, 6);
    return true;
}

void InstallMinimap()
{
    ReadMinimapSetting();
    const uintptr_t quad = g_base + kQuadRva;
    void* tramp = MakeTrampoline(kQuadPrologue, quad + kStolen);
    if (!tramp) {
        LOG("minimap : trampoline quad impossible");
        return;
    }
    g_origQuad = reinterpret_cast<QuadDrawFn>(tramp);
    if (!PatchAbsJump(quad, kQuadPrologue, reinterpret_cast<void*>(&HookQuad))) {
        LOG("minimap : prologue quad inattendu, carte laissee ronde");
        g_origQuad = nullptr;
        return;
    }
    LOG("minimap : detour quad installe");

    const uintptr_t clamp = g_base + kClampRva;
    void* clampTramp = MakeTrampoline(kClampPrologue, clamp + kStolen);
    if (!clampTramp) {
        LOG("minimap : trampoline icones impossible");
        return;
    }
    g_origClamp = reinterpret_cast<ClampFn>(clampTramp);
    if (!PatchAbsJump(clamp, kClampPrologue, reinterpret_cast<void*>(&HookClamp))) {
        LOG("minimap : prologue icones inattendu, icones laissees sur le cercle");
        g_origClamp = nullptr;
        return;
    }
    LOG("minimap : detour icones installe, rectangle=%d", g_roundMinimap.load() ? 1 : 0);
    if (g_roundMinimap.load() && !HookMinimapTiles()) LOG("minimap : puces limitees a 7 (detour impossible)");

    const uintptr_t proj = g_base + kProjectRva;
    if (void* projTramp = MakeTrampoline(kProjectPrologue, proj + kStolen)) {
        g_origProject = reinterpret_cast<ProjectFn>(projTramp);
        if (PatchAbsJump(proj, kProjectPrologue, reinterpret_cast<void*>(&HookProject)))
            LOG("minimap : detour projection installe");
        else {
            g_origProject = nullptr;
            LOG("minimap : prologue projection inattendu");
        }
    }

    // Position : le centre (255, 890) est une donnée lue à chaque image par le fond, le pochoir,
    // les puces, les icônes et le placement Flash (0x5E5970). On le décale une fois au lancement.
    char zbuf[32];
    GetPrivateProfileStringA("Minimap", "MinZoom", "1.05", zbuf, sizeof(zbuf), g_iniPath);
    g_minZoom = static_cast<float>(atof(zbuf));
    const int dx = GetPrivateProfileIntA("Minimap", "OffsetX", -40, g_iniPath);
    const int dy = GetPrivateProfileIntA("Minimap", "OffsetY", 40, g_iniPath);
    auto* center = reinterpret_cast<float*>(g_base + 0x20341B8);
    DWORD old = 0;
    if ((dx || dy) && VirtualProtect(center, 8, PAGE_READWRITE, &old)) {
        const float cx = center[0], cy = center[1];
        if (cx > 100.f && cx < 600.f && cy > 600.f && cy < 1080.f) {
            g_mapOrigX = cx;
            g_mapOrigY = cy;
            g_mapDX = static_cast<float>(dx);
            g_mapDY = static_cast<float>(dy);
            center[0] = cx + dx;
            center[1] = cy + dy;
            LOG("minimap : centre (%.0f, %.0f) -> (%.0f, %.0f)", cx, cy, center[0], center[1]);
        } else {
            LOG("minimap : centre inattendu (%.0f, %.0f), laisse en place", cx, cy);
        }
        DWORD now = 0;
        VirtualProtect(center, 8, old, &now);
    }
}

// ---- Diagnostic : fichiers ouverts par le jeu ---------------------------------------------
// Sert à savoir si le moteur cherche des fichiers « libres » (ex. data\UI\Screens\Hud.bin)
// avant ses archives .big. Actif seulement si [Debug] LogFiles=1 dans le .ini.
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileA_t g_realCreateFileA = nullptr;
CreateFileW_t g_realCreateFileW = nullptr;
std::atomic<int> g_fileLogCount{0};

HANDLE WINAPI LogCreateFileA(LPCSTR name, DWORD a, DWORD sh, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    HANDLE r = g_realCreateFileA(name, a, sh, sa, c, f, t);
    if (name && g_fileLogCount++ < 3000) LOG("fichier A %s -> %s", name, r == INVALID_HANDLE_VALUE ? "absent" : "ouvert");
    return r;
}
HANDLE WINAPI LogCreateFileW(LPCWSTR name, DWORD a, DWORD sh, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    HANDLE r = g_realCreateFileW(name, a, sh, sa, c, f, t);
    if (name && g_fileLogCount++ < 3000) LOG("fichier W %ls -> %s", name, r == INVALID_HANDLE_VALUE ? "absent" : "ouvert");
    return r;
}

void HookImport(const char* dll, const char* func, void* hook, void** original)
{
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(g_base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(g_base + imp->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(g_base + imp->OriginalFirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA64*>(g_base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            if (strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(g_base + names->u1.AddressOfData)->Name, func) != 0) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, 8, PAGE_READWRITE, &old);
            *original = reinterpret_cast<void*>(iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONGLONG>(hook);
            VirtualProtect(&iat->u1.Function, 8, old, &old);
            return;
        }
    }
}

// ---- Proxy dinput8 -----------------------------------------------------------------------
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
DirectInput8Create_t g_realCreate = nullptr;

// ---- Armes : 3 emplacements (main, ceinture, dos) ------------------------------------------
// Le moteur a déjà des emplacements d'équipement « rangés » (eINVENTORY_EQUIP_SLOT_STOWED_1H/2H)
// et InventoryComponent::Stow (0x552CA0) qui déplace un objet d'un emplacement à l'autre en
// transférant son attache au squelette. Emplacements : 0 main droite, 4 ceinture (1H), 5 dos (2H).
// Personnage joueur : pointeur global 0x235C488. Composants : *(obj+0x68) +0x140 ciblage, +0x270
// inventaire. L'objet d'un emplacement est la cible 0x11 / 0x2A / 0x2B du ciblage.
// Étape 1 (test) : journal des équipements du joueur + touches F5..F8 pour ranger / ressortir.
constexpr uintptr_t kPlayerPtr = 0x235C488;
constexpr uintptr_t kEquipRva = 0x527200;
constexpr uintptr_t kUnEquipRva = 0x557CA0;
constexpr uintptr_t kStowRva = 0x552CA0;
constexpr uintptr_t kHudUpdateRva = 0x5E5970;
constexpr uintptr_t kSymbolNameRva = 0x16F0C0;
enum { kSlotHand = 0, kSlot1H = 4, kSlot2H = 5 };

const uint8_t kEquipPro[22] = { 0x40,0x56,0x41,0x55,0x48,0x83,0xEC,0x78,0x48,0x8B,0xF2,0x4C,0x8B,0xE9,
                                0xC6,0x84,0x24,0x98,0x00,0x00,0x00,0x00 };
const uint8_t kUnEquipPro[16] = { 0x44,0x88,0x44,0x24,0x18,0x41,0x54,0x41,0x55,0x48,0x83,0xEC,0x68,0x4C,0x8B,0xE9 };
const uint8_t kStowPro[17] = { 0x44,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x53,0x57,0x41,0x56,0x48,0x83,0xEC,0x60 };
const uint8_t kHudUpdatePro[22] = { 0x48,0x8B,0xC4,0x55,0x48,0x8B,0xEC,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,
                                    0x48,0xC7,0x45,0xB0,0xFE,0xFF,0xFF,0xFF };

using EquipFn = uint8_t(__fastcall*)(void* inv, void* item, uint64_t a3, uint64_t a4);
using UnEquipFn = void(__fastcall*)(void* inv, int slot, char destroy);
using StowFn = void(__fastcall*)(void* inv, int from, int to);
using HudUpdateFn = void(__fastcall*)(void* hud);
using SymbolNameFn = const char* (__fastcall*)(uint32_t symbol);
EquipFn g_origEquip = nullptr;
UnEquipFn g_origUnEquip = nullptr;
StowFn g_origStow = nullptr;
HudUpdateFn g_origHudUpdate = nullptr;
std::atomic<ULONGLONG> g_fakeAimFrom{0}, g_fakeAimUntil{0};   // visée simulée après une sortie d'arme (caméra)
std::unordered_map<uint8_t*, int> g_home;   // arme -> emplacement d'origine (ceinture / dos)

void* MakeTrampolineN(const uint8_t* stolen, size_t n, uintptr_t resume)
{
    auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!mem) return nullptr;
    memcpy(mem, stolen, n);
    mem[n] = 0xFF;
    mem[n + 1] = 0x25;
    *reinterpret_cast<uint32_t*>(mem + n + 2) = 0;
    *reinterpret_cast<uint64_t*>(mem + n + 6) = resume;
    return mem;
}

bool PatchAbsJumpN(uintptr_t at, const uint8_t* expected, size_t n, void* dest)
{
    if (n < 14 || memcmp(reinterpret_cast<void*>(at), expected, n) != 0) return false;
    uint8_t buf[32];
    memset(buf, 0x90, n);
    buf[0] = 0xFF;
    buf[1] = 0x25;
    *reinterpret_cast<uint32_t*>(buf + 2) = 0;
    *reinterpret_cast<uint64_t*>(buf + 6) = reinterpret_cast<uintptr_t>(dest);
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(at), n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(reinterpret_cast<void*>(at), buf, n);
    DWORD now = 0;
    VirtualProtect(reinterpret_cast<void*>(at), n, old, &now);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), n);
    return true;
}

template <typename Fn>
bool Detour(const char* what, uintptr_t rva, const uint8_t* pro, size_t n, void* hook, Fn& orig)
{
    void* tramp = MakeTrampolineN(pro, n, g_base + rva + n);
    if (!tramp) return false;
    orig = reinterpret_cast<Fn>(tramp);
    if (!PatchAbsJumpN(g_base + rva, pro, n, hook)) {
        orig = nullptr;
        LOG("armes : prologue inattendu pour %s, detour annule", what);
        return false;
    }
    return true;
}

uint8_t* Player()
{
    return *reinterpret_cast<uint8_t**>(g_base + kPlayerPtr);
}

// Table de composants d'un personnage (bit 14 de +0x4C), sinon nullptr.
uint8_t* CharComponents(uint8_t* obj)
{
    if (!obj || !((*reinterpret_cast<uint16_t*>(obj + 0x4C) >> 14) & 1)) return nullptr;
    return *reinterpret_cast<uint8_t**>(obj + 0x68);
}

void* PlayerInventory()
{
    uint8_t* comps = CharComponents(Player());
    return comps ? *reinterpret_cast<void**>(comps + 0x270) : nullptr;
}

int SlotTarget(int slot)
{
    switch (slot) {
    case 1: return 0x12;
    case 2: return 0x13;
    case 3: return 0x14;
    case 4: return 0x2A;
    case 5: return 0x2B;
    default: return 0x11;
    }
}

uint8_t* SlotItem(uint8_t* character, int slot)
{
    uint8_t* comps = CharComponents(character);
    if (!comps) return nullptr;
    auto* ts = *reinterpret_cast<uint8_t**>(comps + 0x140);
    if (!ts) return nullptr;
    auto* map = *reinterpret_cast<uint8_t**>(ts + 0x60);
    auto* entries = *reinterpret_cast<uint8_t**>(ts + 0x58);
    if (!map || !entries) return nullptr;
    const uint8_t idx = map[8 + SlotTarget(slot)];
    if (idx == 0) return nullptr;
    return *reinterpret_cast<uint8_t**>(entries + idx * 0x38 + 0x28);
}

const char* ObjName(uint8_t* obj)
{
    if (!obj) return "-";
    const char* s = reinterpret_cast<SymbolNameFn>(g_base + kSymbolNameRva)(*reinterpret_cast<uint32_t*>(obj + 0x48));
    return s ? s : "?";
}

bool IsPlayerInventory(void* inv)
{
    return inv && Player() && *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(inv) + 0x28) == Player();
}

const char* SlotName(int slot)
{
    switch (slot) {
    case 0: return "main";
    case 1: return "main gauche";
    case 2: return "chapeau";
    case 3: return "lunettes";
    case 4: return "ceinture";
    case 5: return "dos";
    default: return "?";
    }
}

void LogSlots(const char* why)
{
    uint8_t* p = Player();
    LOG("armes [%s] main=%s ceinture=%s dos=%s", why,
        ObjName(SlotItem(p, kSlotHand)), ObjName(SlotItem(p, kSlot1H)), ObjName(SlotItem(p, kSlot2H)));
}

struct WeaponDef;
WeaponDef* DefOf(uint8_t* item);
bool DrawInProgress();
extern int g_handSlot;
WeaponDef*& SlotRef(int slot);
bool InSlots(WeaponDef* d);
int FreeSlotFor(WeaponDef* d);
int g_pickupSlot = -1;   // emplacement dont hérite l'arme ramassée
bool HolsterHand();
const char* LabelOf(uint8_t* item);

__declspec(noinline) uint8_t __fastcall HookEquip(void* inv, void* item, uint64_t a3, uint64_t a4)
{
    const bool mine = IsPlayerInventory(inv);
    if (mine) LOG("armes : equiper %s (args %llx %llx)", ObjName(static_cast<uint8_t*>(item)), a3, a4);
    // Ramassage avec une arme en main (échange du jeu) : la nouvelle prendra son emplacement.
    if (mine && item && !DrawInProgress()) {
        uint8_t* h = SlotItem(Player(), kSlotHand);
        if (h && h != item && g_handSlot >= 1) g_pickupSlot = g_handSlot;
    }
    const uint8_t r = g_origEquip(inv, item, a3, a4);
    if (mine) LogSlots("apres equiper");
    if (mine && !DrawInProgress()) {
        // Arme ramassée : elle remplace l'arme tenue avant (même emplacement) ; sinon un emplacement libre.
        WeaponDef* nd = DefOf(static_cast<uint8_t*>(item));
        int k = g_pickupSlot;
        g_pickupSlot = -1;
        if (nd && SlotItem(Player(), kSlotHand) == item) {
            if (k < 1) k = InSlots(nd) ? -1 : FreeSlotFor(nd);
            if (k >= 1) { SlotRef(k) = nd; g_handSlot = k; LOG("armes : %s ramassee -> emplacement %d", LabelOf(static_cast<uint8_t*>(item)), k); }
            else g_handSlot = -1;
        } else {
            g_handSlot = -1;
        }
    }
    return r;
}

__declspec(noinline) void __fastcall HookUnEquip(void* inv, int slot, char destroy)
{
    const bool mine = IsPlayerInventory(inv);
    if (mine) LOG("armes : retirer %s de %s (detruire=%d)", ObjName(SlotItem(Player(), slot)), SlotName(slot), destroy);
    // Le mod ne retire jamais l'arme en main par UnEquip (il passe par Stow) : si ça arrive, c'est le
    // jeu (arme jetée, lâchée, désarmement). Elle quitte alors aussi son emplacement de la roue.
    if (mine && slot == kSlotHand && g_handSlot >= 1) {
        uint8_t* h = SlotItem(Player(), kSlotHand);
        if (h && SlotRef(g_handSlot) == DefOf(h)) {
            LOG("armes : %s jetee, retiree de l'emplacement %d", LabelOf(h), g_handSlot);
            SlotRef(g_handSlot) = nullptr;
        }
        g_handSlot = -1;
    }
    g_origUnEquip(inv, slot, destroy);
    if (mine) LogSlots("apres retirer");
}

__declspec(noinline) void __fastcall HookStow(void* inv, int from, int to)
{
    const bool mine = IsPlayerInventory(inv);
    if (mine) LOG("armes : ranger %s -> %s", SlotName(from), SlotName(to));
    uint8_t* moved = mine ? SlotItem(Player(), from) : nullptr;
    g_origStow(inv, from, to);
    if (moved && (to == kSlot1H || to == kSlot2H) && SlotItem(Player(), to) == moved) g_home[moved] = to;
    if (mine) LogSlots("apres ranger");
}

// ---- Armurerie des planques ----------------------------------------------------------------
// Créer une arme : fiche de propriétés (nom -> symbole CRC 0x180910 -> fiche 0x1F76B0), puis
// FUN_140524150(fiche, personnage, 0) qui crée l'objet « attachableProp » et l'équipe en main.
// F10 enregistre la position courante comme planque ([Safehouses] du .ini), F9 dans une planque
// remplit les emplacements vides : pistolet à la ceinture, fusil dans le dos, pompe en main.
// Position du joueur : nœud de transformation *(objet+0x58), mis à jour par 0x191CF0, position +0xB0.
constexpr uintptr_t kMakeSymbolRva = 0x180910;
constexpr uintptr_t kFindPropSetRva = 0x1F76B0;
constexpr uintptr_t kCreateEquipRva = 0x524150;
constexpr uintptr_t kUniqueNameRva = 0x1909E0;      // nom unique d'objet (contexte 0x235C3A0)
constexpr uintptr_t kUniqueNameCtxRva = 0x235C3A0;
constexpr uintptr_t kSpawnObjectRva = 0x5B7460;     // création d'un objet depuis sa fiche
constexpr uintptr_t kSetTransformRva = 0x556AD0;    // place un objet (matrice 4x4)
using MakeSymbolFn = uint32_t* (__fastcall*)(uint32_t* out, const char* name);
using FindPropSetFn = void* (__fastcall*)(uint32_t* symbol);
using CreateEquipFn = void(__fastcall*)(void* propSet, void* character, char flag);


struct Safehouse { float x, y; };
std::vector<Safehouse> g_safehouses;
float g_safehouseRadius = 25.f;
bool g_safehouseOnly = true;
char g_kitBelt[96] = "object-physical-weapon-firearm-pistol-50cal";
char g_kitBack[96] = "object-physical-weapon-firearm-rifle-assault";
char g_kitHand[96] = "object-physical-weapon-firearm-shotgun-pump";
char g_floorList[1024] = "";   // armes posées au sol (désactivé par défaut : elles bloquaient le passage)

void ReadFistChoice();

void LoadArmory()
{
    g_safehouses.clear();
    const int n = GetPrivateProfileIntA("Safehouses", "Count", 0, g_iniPath);
    for (int i = 0; i < n && i < 32; ++i) {
        char key[16], buf[64];
        sprintf_s(key, "P%d", i + 1);
        GetPrivateProfileStringA("Safehouses", key, "", buf, sizeof(buf), g_iniPath);
        Safehouse s{};
        if (sscanf_s(buf, "%f,%f", &s.x, &s.y) == 2) g_safehouses.push_back(s);
    }
    g_safehouseOnly = GetPrivateProfileIntA("Armory", "SafehouseOnly", 1, g_iniPath) != 0;
    char buf[32];
    GetPrivateProfileStringA("Armory", "Radius", "25", buf, sizeof(buf), g_iniPath);
    g_safehouseRadius = static_cast<float>(atof(buf));
    GetPrivateProfileStringA("Armory", "Belt", g_kitBelt, g_kitBelt, sizeof(g_kitBelt), g_iniPath);
    GetPrivateProfileStringA("Armory", "Back", g_kitBack, g_kitBack, sizeof(g_kitBack), g_iniPath);
    GetPrivateProfileStringA("Armory", "Hand", g_kitHand, g_kitHand, sizeof(g_kitHand), g_iniPath);
    GetPrivateProfileStringA("Armory", "Floor", g_floorList, g_floorList, sizeof(g_floorList), g_iniPath);
    ReadFistChoice();
    LOG("armurerie : %d planque(s), rayon %.0f m, planque obligatoire=%d", static_cast<int>(g_safehouses.size()),
        g_safehouseRadius, g_safehouseOnly ? 1 : 0);
}

void RecordSafehouse()
{
    if (!g_playerKnown.load()) {
        LOG("armurerie : position inconnue (mini-carte pas encore affichee)");
        return;
    }
    Safehouse s{ g_playerX.load(), g_playerY.load() };
    g_safehouses.push_back(s);
    char key[16], val[64], cnt[16];
    sprintf_s(key, "P%d", static_cast<int>(g_safehouses.size()));
    sprintf_s(val, "%.1f,%.1f", s.x, s.y);
    sprintf_s(cnt, "%d", static_cast<int>(g_safehouses.size()));
    WritePrivateProfileStringA("Safehouses", key, val, g_iniPath);
    WritePrivateProfileStringA("Safehouses", "Count", cnt, g_iniPath);
    LOG("armurerie : planque %s enregistree en (%.1f, %.1f)", key, s.x, s.y);
}

bool InSafehouse()
{
    if (!g_safehouseOnly) return true;
    if (!g_playerKnown.load()) return false;
    const float x = g_playerX.load(), y = g_playerY.load();
    for (const auto& s : g_safehouses) {
        const float dx = x - s.x, dy = y - s.y;
        if (dx * dx + dy * dy < g_safehouseRadius * g_safehouseRadius) return true;
    }
    return false;
}

// ---- Changer d'arme ------------------------------------------------------------------------
// Stow refuse une destination occupée : pour sortir une arme alors qu'on en tient une, l'arme
// en main retourne d'abord à son emplacement (ceinture ou dos, mémorisé quand elle y a été
// rangée), ou, si c'est le même, on échange via la main gauche (emplacement 1) comme relais.
int HomeOf(uint8_t* item)
{
    auto it = g_home.find(item);
    return it != g_home.end() ? it->second : -1;
}

bool StowTo(void* inv, int from, int to)
{
    uint8_t* p = Player();
    uint8_t* item = SlotItem(p, from);
    if (!item || SlotItem(p, to)) return false;
    HookStow(inv, from, to);
    return SlotItem(p, to) == item;
}

void* FindPropSet(const char* name)
{
    uint32_t sym = 0xFFFFFFFF;
    reinterpret_cast<MakeSymbolFn>(g_base + kMakeSymbolRva)(&sym, name);
    return reinterpret_cast<FindPropSetFn>(g_base + kFindPropSetRva)(&sym);
}

// ---- Identification des armes --------------------------------------------------------------
// Chaque arme porte un InventoryItemComponent (type de composant 0x2087200) ; son champ +0x6C
// est le symbole du bloc « objet d'inventaire » de sa fiche. Pour chaque fiche connue on lit ce
// même bloc (FUN_1401E9F60(fiche, &id 0x23AD8AC, 1) puis +0x28 relatif, FUN_1401F6FC0) : son
// 3e mot donne la clé. Ainsi n'importe quelle arme, ramassée ou créée, retrouve sa fiche.
// Classe 1 = une main (ceinture), 2 = arme longue (emplacements gérés par le mod).
// model : nouvelle arme du mod (fichier SleepingDogsRemaster\<model>.skm affiché à la place du modèle du jeu).
struct WeaponDef { const char* name; const char* label; int cls; void* ps; uint32_t key; const char* icon; bool hidden; const char* model = nullptr; };
WeaponDef g_defs[] = {
    // Arme du mod : un 9mm gardé invisible, on « tire avec les doigts » (le nom après # la distingue).
    { "object-physical-weapon-firearm-pistol-9mm#doigt", "Pistolet-doigt", 1, nullptr, 0, "SKR_Icon_FingerGun", true },
    { "object-physical-weapon-firearm-pistol-9mm", "Pistolet 9mm", 1 },
    { "object-physical-weapon-firearm-pistol-service", "Pistolet de service", 1 },
    { "object-physical-weapon-firearm-pistol-45cal", "Pistolet .45", 1 },
    { "object-physical-weapon-firearm-pistol-45cal_taclight", "Pistolet .45 tactique", 1 },
    { "object-physical-weapon-firearm-pistol-50cal", "Pistolet .50 or", 1 },   // la fiche « 50cal » est dorée en jeu
    { "object-physical-weapon-firearm-pistol-50cal_silver", "Pistolet .50", 1 },
    // DLC (noms avec majuscules, absents de la première recherche)
    { "object-physical-weapon-firearm-pistol-HandGun_Hitman001", "Pistolet silencieux (Hitman)", 1, nullptr, 0, "Icons_Weapon_SILVERBALLER_PISTOL", false },
    { "object-physical-weapon-firearm-pistol-HandGun_JC2001", "Pistolet de Rico (Just Cause)", 1, nullptr, 0, "Icons_Weapon_RICO_PISTOL", false },
    { "object-physical-weapon-firearm-rifle-Rifle_DeusEx001", "Fusil Deus Ex (DLC)", 2, nullptr, 0, "Icons_Weapon_COMBAT_RIFLE", false },
    { "object-physical-weapon-firearm-rifle-DLC_MGL", "Lance-grenades multiple (DLC)", 2, nullptr, 0, "Icons_Weapon_ARGL", false },
    { "object-physical-weapon-firearm-pistol-50cal_gold", "Pistolet .50 or (DLC)", 1 },
    { "object-physical-weapon-firearm-smg-machine_pistol", "Pistolet-mitrailleur", 1 },
    { "object-physical-weapon-firearm-smg-45cal", "Mitraillette", 2 },
    { "object-physical-weapon-firearm-smg-45cal_gold", "Mitraillette or", 2 },
    { "object-physical-weapon-firearm-smg-45cal_taclight", "Mitraillette tactique", 2 },
    { "object-physical-weapon-firearm-rifle-assault", "Fusil d'assaut", 2 },
    { "object-physical-weapon-firearm-rifle-assault_dlc", "Fusil d'assaut (DLC)", 2 },
    { "object-physical-weapon-firearm-rifle-assault_taclight", "Fusil d'assaut tactique", 2 },
    { "object-physical-weapon-firearm-rifle-grenade", "Lance-grenades", 2 },
    // Nouvelles armes du mod : fonctionnement d'une arme du jeu, modèle 3D du mod (newweapons.cpp).
    { "object-physical-weapon-firearm-rifle-assault#ak47", "AK-47", 2, nullptr, 0, "SKR_Icon_AK47", false, "ak47" },
    { "object-physical-weapon-firearm-rifle-DLC_MGL#rpg", "Lance-roquettes", 2, nullptr, 0, "SKR_Icon_RPG7", false, "rpg7" },
    { "object-physical-weapon-firearm-shotgun-pump", "Fusil a pompe", 2 },
    { "object-physical-weapon-firearm-shotgun-antiriot_taclight", "Fusil anti-emeute", 2 },
    { "object-physical-weapon-firearm-shotgun-antiriotnolight", "Fusil anti-emeute", 2 },
    // Mêlée (classe 3) : le jeu ne les range pas à la ceinture, elles sortent / se rangent directement.
    { "object-physical-weapon-melee-baton", "Matraque", 3, nullptr, 0, "Icons_Weapon_BATON", false },
    { "object-physical-weapon-melee-nightstick", "Matraque telescopique", 3, nullptr, 0, "Icons_Weapon_TelescopingBaton", false },
    { "object-physical-weapon-melee-cleaver", "Hachoir", 3, nullptr, 0, "Icons_Weapon_CLEAVER", false },
    { "object-physical-weapon-melee-kitchenknife", "Couteau", 3, nullptr, 0, "Icons_Weapon_KNIFE", false },
    { "object-physical-weapon-melee-crowbar", "Pied-de-biche", 3, nullptr, 0, "Icons_Weapon_CROWBAR", false },
    { "object-physical-weapon-melee-hammer", "Marteau", 3, nullptr, 0, "Icons_Weapon_HAMMER", false },
    { "object-physical-weapon-melee-tireiron", "Demonte-pneu", 3, nullptr, 0, "Icons_Weapon_TIREIRON", false },
    { "object-physical-weapon-melee-woodenclub", "Gourdin", 3, nullptr, 0, "Icons_Weapon_STICK", false },
    { "object-physical-weapon-melee-woodenstick", "Baton en bois", 3, nullptr, 0, "Icons_Weapon_STICK", false },
    { "object-physical-weapon-melee-stickbamboo", "Baton de bambou", 3, nullptr, 0, "Icons_Weapon_STICK", false },
    { "object-physical-weapon-melee-sticktree", "Branche", 3, nullptr, 0, "Icons_Weapon_STICK", false },
    { "object-physical-weapon-melee-umbrella", "Parapluie", 3, nullptr, 0, "Icons_Weapon_UMBRELLA", false },
    { "object-physical-weapon-melee-canewalking", "Canne", 3, nullptr, 0, "Icons_Weapon_CANEWALKING", false },
    { "object-physical-weapon-melee-hackbroom", "Balai", 3, nullptr, 0, "Icons_Weapon_BROOM", false },
    { "object-physical-weapon-melee-squeegee", "Raclette", 3, nullptr, 0, "Icons_Weapon_SQUEEGEE", false },
    { "object-physical-weapon-melee-scrubbrush", "Brosse", 3, nullptr, 0, "Icons_Weapon_SCRUBBRUSH", false },
};
bool g_defsReady = false;

// ---- Langue ---------------------------------------------------------------------------------
// Les textes du mod suivent la langue du jeu : le Flash (HUD, garde-robe) demande au traducteur du
// jeu le nom d'une catégorie de la garde-robe ($ITEMCATEGORY_BODY = « HAUTS » en français) et
// prévient le plugin (ExternalInterface « GTA_LANG »). Français si c'est le cas, anglais sinon.
// Avant ce message, on suit la langue de Windows.
std::atomic<bool> g_langFr{ PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH };
const char* Tr(const char* fr, const char* en) { return g_langFr.load() ? fr : en; }

// Nom affiché d'une arme dans la langue du jeu.
// Nom français d'une arme à feu d'après son icône (celle que le jeu lui donne vraiment) : les noms
// internes des fiches ne correspondent pas toujours à l'arme vue en jeu (« 50cal » est dorée).
const char* FrenchLabel(const WeaponDef* d)
{
    if (!d || d->hidden || d->cls == 3 || !d->icon || strncmp(d->name, "dyn:", 4) != 0) return d ? d->label : "";
    static const struct { const char* icon; const char* label; } kByIcon[] = {
        { "Icons_Weapon_GOLD50", "Pistolet .50 or" }, { "Icons_Weapon_RICO_PISTOL", "Pistolet .50" },
        { "Icons_Weapon_SILVERBALLER_PISTOL", "Pistolet silencieux" }, { "Icons_Weapon_9MM", "Pistolet 9mm" },
        { "Icons_Weapon_QSZ92", "Pistolet de service" }, { "Icons_Weapon_45CAL", "Pistolet .45" },
        { "Icons_Weapon_45CAL02", "Pistolet .45 tactique" }, { "Icons_Weapon_TT", "Pistolet TT" },
        { "Icons_Weapon_MP", "Pistolet-mitrailleur" }, { "Icons_Weapon_SMG", "Mitraillette tactique" },
        { "Icons_Weapon_SMG02", "Mitraillette" }, { "Icons_Weapon_ACT", "Fusil d'assaut" },
        { "Icons_Weapon_ACT02", "Fusil d'assaut tactique" }, { "Icons_Weapon_COMBAT_RIFLE", "Fusil de combat" },
        { "Icons_Weapon_ARGL", "Lance-grenades" }, { "Icons_Weapon_TearGas", "Lance-lacrymogenes" },
        { "Icons_Weapon_PUMPS", "Fusil a pompe" }, { "Icons_Weapon_ANTIRIOT", "Fusil anti-emeute" },
        { "Icons_Weapon_ANTIRIOTNOLIGHT", "Fusil anti-emeute" },
    };
    for (const auto& k : kByIcon) if (strcmp(d->icon, k.icon) == 0) return k.label;
    return d->label;
}

const char* WeaponLabel(const WeaponDef* d)
{
    if (!d) return Tr("(vide)", "(empty)");
    const char* fr = FrenchLabel(d);
    if (g_langFr.load()) return fr;
    static const struct { const char* fr; const char* en; } kEn[] = {
        { "Pistolet-doigt", "Finger gun" }, { "Pistolet 9mm", "9mm pistol" },
        { "Pistolet de service", "Service pistol" }, { "Pistolet .45 tactique", "Tactical .45 pistol" },
        { "Pistolet .45", ".45 pistol" }, { "Pistolet .50 or", "Gold .50 pistol" },
        { "Pistolet .50 argent", "Silver .50 pistol" }, { "Pistolet .50", ".50 pistol" },
        { "Pistolet-mitrailleur", "Machine pistol" }, { "Mitraillette or", "Gold SMG" },
        { "Mitraillette tactique", "Tactical SMG" }, { "Mitraillette", "SMG" },
        { "Fusil d'assaut (DLC)", "Assault rifle (DLC)" }, { "Fusil d'assaut tactique", "Tactical assault rifle" },
        { "Fusil d'assaut", "Assault rifle" }, { "Lance-grenades", "Grenade launcher" }, { "Lance-roquettes", "Rocket launcher" },
        { "Fusil a pompe", "Pump shotgun" }, { "Fusil anti-emeute", "Riot shotgun" },
        { "Pistolet silencieux (Hitman)", "Silenced pistol (Hitman)" }, { "Pistolet de Rico (Just Cause)", "Rico's pistol (Just Cause)" },
        { "Fusil Deus Ex (DLC)", "Deus Ex rifle (DLC)" }, { "Lance-grenades multiple (DLC)", "Multiple grenade launcher (DLC)" },
        { "Pistolet .50 or (DLC)", "Gold .50 pistol (DLC)" },
        { "Pistolet silencieux", "Silenced pistol" }, { "Pistolet de Rico", "Rico's pistol" }, { "Pistolet TT", "TT pistol" },
        { "Fusil de combat", "Combat rifle" }, { "Lance-lacrymogenes", "Tear gas launcher" },
        { "Matraque telescopique", "Nightstick" }, { "Matraque", "Baton" }, { "Hachoir", "Cleaver" },
        { "Couteau", "Kitchen knife" }, { "Pied-de-biche", "Crowbar" }, { "Marteau", "Hammer" },
        { "Demonte-pneu", "Tire iron" }, { "Gourdin", "Wooden club" }, { "Baton en bois", "Wooden stick" },
        { "Baton de bambou", "Bamboo stick" }, { "Branche", "Tree branch" }, { "Parapluie", "Umbrella" },
        { "Canne", "Walking cane" }, { "Balai", "Broom" }, { "Raclette", "Squeegee" }, { "Brosse", "Scrub brush" },
    };
    for (const auto& e : kEn) if (strcmp(fr, e.fr) == 0) return e.en;
    return fr;
}
// Icône « mains nues » : au choix parmi les icônes de combat du jeu ([Wheel] FistIcon, Y dans la
// roue pour faire défiler). Textures et fichier qui les contient.
struct FistChoice { const char* icon; const char* pack; };
const FistChoice kFistChoices[] = {
    { "ICON_REWARD_MELEEOFFENSE", "Data\\UI\\ICON_REWARD_MELEEOFFENSE_tp.perm.bin" },
    { "ICON_REWARD_MELEEDEFENSE", "Data\\UI\\Icon_Reward_MeleeDefense_tp.perm.bin" },
    { "ICON_UPGRADES_MELEE_1", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_2", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_3", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_4", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_5", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_6", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_7", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_8", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_9", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_10", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_11", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
    { "ICON_UPGRADES_MELEE_12", "Data\\UI\\upgrades_win32_texturepack.perm.bin" },
};
int g_fistChoice = 0;
const char* FistIcon() { return kFistChoices[g_fistChoice].icon; }

// Icône des mains nues choisie ([Wheel] FistIcon), lue dès le démarrage du mod.
void ReadFistChoice()
{
    g_fistChoice = GetPrivateProfileIntA("Wheel", "FistIcon", 0, g_iniPath);
    if (g_fistChoice < 0 || g_fistChoice >= static_cast<int>(sizeof(kFistChoices) / sizeof(kFistChoices[0]))) g_fistChoice = 0;
    LOG("armes : icone mains nues = %s (reglage %d)", FistIcon(), g_fistChoice);
}
void LoadIconPack(const char* name, const char* pack);
std::unordered_map<uint8_t*, WeaponDef*> g_itemDef;   // arme créée par le mod -> définition

constexpr uintptr_t kItemCompTypeRva = 0x2087200;
constexpr uintptr_t kFindComponentRva = 0x190A60;
constexpr uintptr_t kPropBlockRva = 0x1E9F60;
constexpr uintptr_t kPropBlockDataRva = 0x1F6FC0;
constexpr uintptr_t kItemBlockIdRva = 0x23AD8AC;

uint32_t* ItemBlock(void* ps)
{
    if (!ps) return nullptr;
    using BlockFn = uint8_t* (__fastcall*)(void*, void*, uint32_t);
    using DataFn = uint8_t* (__fastcall*)(void*);
    uint8_t* e = reinterpret_cast<BlockFn>(g_base + kPropBlockRva)(ps, reinterpret_cast<void*>(g_base + kItemBlockIdRva), 1);
    if (!e) return nullptr;
    return reinterpret_cast<uint32_t*>(reinterpret_cast<DataFn>(g_base + kPropBlockDataRva)(e));
}

uint8_t* ItemComp(uint8_t* item)
{
    if (!item) return nullptr;
    using FindCompFn = uint8_t* (__fastcall*)(void*, uint32_t);
    const uint32_t type = *reinterpret_cast<uint32_t*>(g_base + kItemCompTypeRva);
    return reinterpret_cast<FindCompFn>(g_base + kFindComponentRva)(item, type);
}

void EnsureDefs()
{
    if (g_defsReady) return;
    int n = 0;
    // Table des armes à feu du jeu (0x240EEC0, 120 entrées de 24 octets : symbole de la fiche, modes
    // de tir...), remplie au chargement à partir des fiches qui ont des statistiques d'arme.
    const int tableCount = *reinterpret_cast<int*>(g_base + 0x240E3D8);
    const uint32_t* table = reinterpret_cast<const uint32_t*>(g_base + 0x240EEC0);
    LOG("armes : %d armes a feu dans la table du jeu", tableCount);
    for (auto& d : g_defs) {
        char base[128];
        strcpy_s(base, d.name);
        if (char* hash = strchr(base, '#')) *hash = 0;
        d.ps = FindPropSet(base);
        uint32_t* blk = ItemBlock(d.ps);
        d.key = blk ? blk[2] : 0xFFFFFFFF;
        // Les fiches qui ne sont pas de vraies armes du jeu sont écartées (fusil de chasse : plantage).
        if (d.ps && d.cls != 3 && tableCount > 0) {
            const uint32_t sym = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(d.ps) + 0x40);
            bool found = false;
            for (int i = 0; i < tableCount; ++i) if (table[i * 6] == sym) { found = true; break; }
            if (!found) { LOG("armes : %s n'est pas une arme du jeu, ecartee", d.name); d.ps = nullptr; }
        }
        if (d.ps && d.cls == 3 && !blk) { LOG("armes : %s sans fiche d'inventaire, ecartee", d.name); d.ps = nullptr; }
        if (d.ps) ++n;
    }
    // Icônes connues d'avance (table du jeu FUN_1405EA0F0, par type d'arme) : la roue les montre
    // sans qu'il faille d'abord sortir chaque arme. Une icône observée en jeu remplace celle-ci.
    static const struct { const char* name; const char* icon; } kIcons[] = {
        { "pistol-9mm", "Icons_Weapon_9MM" }, { "pistol-service", "Icons_Weapon_QSZ92" },
        { "pistol-45cal_taclight", "Icons_Weapon_45CAL02" }, { "pistol-45cal", "Icons_Weapon_45CAL" },
        { "pistol-50cal_gold", "Icons_Weapon_GOLD50" },
        { "pistol-50cal", "Icons_Weapon_GOLD50" }, { "smg-machine_pistol", "Icons_Weapon_MP" },
        { "smg-45cal_taclight", "Icons_Weapon_SMG" }, { "smg-45cal_gold", "Icons_Weapon_SMG02" },
        { "smg-45cal", "Icons_Weapon_SMG02" }, { "rifle-assault_taclight", "Icons_Weapon_ACT02" },
        { "rifle-assault_dlc", "Icons_Weapon_ACT02" }, { "rifle-assault", "Icons_Weapon_ACT" },
        { "rifle-grenade", "Icons_Weapon_ARGL" }, { "shotgun-pump", "Icons_Weapon_PUMPS" },
        { "shotgun-antiriot_taclight", "Icons_Weapon_ANTIRIOT" }, { "shotgun-antiriotnolight", "Icons_Weapon_ANTIRIOTNOLIGHT" },
    };
    for (auto& d : g_defs) {
        if (d.icon) continue;
        for (const auto& ic : kIcons) {
            const size_t ln = strlen(d.name), lk = strlen(ic.name);
            if (ln >= lk && strcmp(d.name + ln - lk, ic.name) == 0) { d.icon = ic.icon; break; }
        }
    }
    g_defsReady = n > 0;
    // Toutes les icônes sont chargées d'avance : une arme ramassée s'affiche aussitôt dans la roue.
    for (auto& d : g_defs) if (d.ps && d.icon) LoadIconPack(d.icon, nullptr);
    for (const auto& fc : kFistChoices) LoadIconPack(fc.icon, fc.pack);
    LOG("armes : %d fiches d'armes trouvees", n);
}

// Symbole exact de la fiche d'une arme à feu : son composant d'arme (type 0x208773C) pointe à +0xB0
// vers son entrée de la table des armes du jeu, dont le 1er mot est le symbole de la fiche
// (*(fiche+0x40)). Plusieurs armes partagent le même objet d'inventaire (.50 et .50 or...) : la clé
// d'inventaire seule les confondait.
uint8_t* WeaponPropsOf(uint8_t* item)
{
    if (!item) return nullptr;
    const uint32_t ctype = *reinterpret_cast<uint32_t*>(g_base + 0x208773C);
    const uint16_t f = *reinterpret_cast<uint16_t*>(item + 0x4C);
    if (((f >> 14) & 1) || static_cast<int16_t>(f) < 0) return nullptr;    // personnage
    return ((f >> 13) & 1) || ((f >> 12) & 1)
        ? reinterpret_cast<uint8_t* (__fastcall*)(void*, uint32_t)>(g_base + 0x52BE30)(item, ctype)
        : reinterpret_cast<uint8_t* (__fastcall*)(void*, uint32_t)>(g_base + kFindComponentRva)(item, ctype);
}

uint32_t WeaponSymbolOf(uint8_t* item)
{
    uint8_t* comp = WeaponPropsOf(item);
    if (!comp) return 0;
    auto* entry = *reinterpret_cast<uint32_t**>(comp + 0xB0);
    return entry ? entry[0] : 0;
}

uint32_t PropSetSymbol(const WeaponDef& d)
{
    return d.ps ? *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(d.ps) + 0x40) : 0;
}

const char* IconOfItem(uint8_t* item);

// Armes à feu inconnues de la liste du mod (DLC...) : ajoutées à la volée, nom tiré de leur icône.
std::deque<WeaponDef> g_dynDefs;

WeaponDef* AddDynamicDef(uint8_t* item, uint32_t sym)
{
    uint32_t s = sym;
    void* ps = reinterpret_cast<FindPropSetFn>(g_base + kFindPropSetRva)(&s);
    if (!ps) return nullptr;
    const char* icon = IconOfItem(item);
    static const struct { const char* icon; const char* label; int cls; } kKnown[] = {
        { "Icons_Weapon_SILVERBALLER_PISTOL", "Pistolet silencieux", 1 }, { "Icons_Weapon_RICO_PISTOL", "Pistolet de Rico", 1 },
        { "Icons_Weapon_TT", "Pistolet TT", 1 }, { "Icons_Weapon_GOLD50", "Pistolet .50 or", 1 },
        { "Icons_Weapon_9MM", "Pistolet 9mm", 1 }, { "Icons_Weapon_QSZ92", "Pistolet de service", 1 },
        { "Icons_Weapon_45CAL", "Pistolet .45", 1 }, { "Icons_Weapon_45CAL02", "Pistolet .45 tactique", 1 },
        { "Icons_Weapon_MP", "Pistolet-mitrailleur", 1 }, { "Icons_Weapon_COMBAT_RIFLE", "Fusil de combat", 2 },
        { "Icons_Weapon_TearGas", "Lance-lacrymogenes", 2 }, { "Icons_Weapon_ARGL", "Lance-grenades", 2 },
        { "Icons_Weapon_ACT", "Fusil d'assaut", 2 }, { "Icons_Weapon_ACT02", "Fusil d'assaut tactique", 2 },
        { "Icons_Weapon_PUMPS", "Fusil a pompe", 2 }, { "Icons_Weapon_ANTIRIOT", "Fusil anti-emeute", 2 },
        { "Icons_Weapon_ANTIRIOTNOLIGHT", "Fusil anti-emeute", 2 }, { "Icons_Weapon_SMG", "Mitraillette tactique", 2 },
        { "Icons_Weapon_SMG02", "Mitraillette", 2 },
    };
    const char* label = "Arme";
    int cls = 2;
    for (const auto& k : kKnown) if (icon && strcmp(icon, k.icon) == 0) { label = k.label; cls = k.cls; break; }
    char* name = static_cast<char*>(malloc(40));
    sprintf_s(name, 40, "dyn:%08x", sym);
    uint32_t* blk = ItemBlock(ps);
    g_dynDefs.push_back({ name, label, cls, ps, blk ? blk[2] : 0xFFFFFFFF, icon, false });
    LOG("armes : arme inconnue ajoutee (%s, icone %s)", label, icon ? icon : "-");
    return &g_dynDefs.back();
}

WeaponDef* DefOf(uint8_t* item)
{
    if (!item) return nullptr;
    // Le cache objet -> arme n'est sûr que tant que l'objet existe : la mémoire d'une arme supprimée
    // est réutilisée pour la suivante. On revérifie donc le symbole de la fiche à chaque fois.
    const uint32_t sym0 = WeaponSymbolOf(item);
    auto it = g_itemDef.find(item);
    if (it != g_itemDef.end()) {
        if (!sym0 || PropSetSymbol(*it->second) == sym0) return it->second;
        g_itemDef.erase(it);
    }
    EnsureDefs();
    // Armes à feu : identification exacte par la fiche d'origine.
    if (const uint32_t sym = sym0) {
        // Les variantes du mod (« nom#xxx ») ne se reconnaissent qu'à leur création, jamais par la fiche.
        for (auto& d : g_defs) if (d.ps && !strchr(d.name, '#') && PropSetSymbol(d) == sym) { g_itemDef[item] = &d; return &d; }
        for (auto& d : g_dynDefs) if (PropSetSymbol(d) == sym) { g_itemDef[item] = &d; return &d; }
        if (WeaponDef* d = AddDynamicDef(item, sym)) { g_itemDef[item] = d; return d; }
    }
    // Mêlée (pas dans la table des armes à feu) : par l'objet d'inventaire.
    uint8_t* comp = ItemComp(item);
    if (!comp) return nullptr;
    const uint32_t key = *reinterpret_cast<uint32_t*>(comp + 0x6C);
    for (auto& d : g_defs) {
        if (d.ps && d.cls == 3 && d.key == key && key != 0xFFFFFFFF) { g_itemDef[item] = &d; return &d; }
    }
    static uint8_t* warnedFor = nullptr;
    if (warnedFor != item) {
        warnedFor = item;
        LOG("armes : arme non reconnue (cle %08x)", key);
    }
    return nullptr;
}

const char* LabelOf(uint8_t* item)
{
    WeaponDef* d = DefOf(item);
    return d ? WeaponLabel(d) : (item ? Tr("Arme", "Weapon") : "");
}

// ---- Icônes ---------------------------------------------------------------------------------
// Le HUD tire l'icône d'une arme de son type (composant 0x20871CC, champ +0x68, eINVENTORY_ITEM)
// via FUN_1405EA0F0(type, &pack, &nom) : nom = « Icons_Weapon_9MM », pack = fichier de textures
// à charger (Data\UI\Icons_Weapon_9MM_TP.perm.bin). Le HUD ne garde chargée que l'icône de l'arme
// en main : le mod charge les autres lui-même (FUN_1405C1D20, compteur de références).
constexpr uintptr_t kWeaponPropsTypeRva = 0x20871CC;
constexpr uintptr_t kIconOfTypeRva = 0x5EA0F0;
constexpr uintptr_t kStreamMgrRva = 0x5C1990;
constexpr uintptr_t kStreamLoadRva = 0x5C1D20;

void __fastcall IconPackLoaded(void*, void*, void*, void*) {}

// Charge (une fois) le fichier de textures d'une icône. Sans chemin, on le déduit du nom
// (Data\UI\<nom>_TP.perm.bin, la règle suivie par les armes).
std::unordered_map<std::string, ULONGLONG> g_iconLoadTick;   // nom d'icône -> moment de la demande

// Fichier du dossier du mod (SleepingDogsRemaster\<nom><ext>, à côté du .ini).
std::string ModFile(const char* name, const char* ext)
{
    char path[MAX_PATH];
    strcpy_s(path, g_iniPath);
    if (char* slash = strrchr(path, '\\')) slash[1] = 0;
    return std::string(path) + "SleepingDogsRemaster\\" + name + ext;
}

// Icônes des armes du mod (« SKR_... ») : PNG du dossier du mod, chargé comme texture nommée.
bool IsModIcon(const char* name) { return name && strncmp(name, "SKR_", 4) == 0; }

void LoadIconPack(const char* name, const char* pack)
{
    if (IsModIcon(name)) { nw::IconState(name, ModFile(name, ".png").c_str()); return; }
    if (!g_iconLoadTick.count(name)) g_iconLoadTick[name] = GetTickCount64();
    static std::unordered_map<std::string, bool> loaded;
    std::string path = pack ? pack : std::string("Data\\UI\\") + name + "_TP.perm.bin";
    if (loaded[path]) return;
    loaded[path] = true;
    const char* keep = _strdup(path.c_str());     // le jeu garde le pointeur de chaîne

    void* mgr = reinterpret_cast<void* (__fastcall*)()>(g_base + kStreamMgrRva)();
    reinterpret_cast<void(__fastcall*)(void*, const char*, uint32_t, void*, void*)>(g_base + kStreamLoadRva)(
        mgr, keep, 0x2000, reinterpret_cast<void*>(&IconPackLoaded), nullptr);
    LOG("armes : icone %s chargee", name);
}

// Type d'arme (eINVENTORY_ITEM) d'un objet, -1 si ce n'est pas une arme.
int WeaponTypeOf(uint8_t* item)
{
    if (!item) return -1;
    const uint32_t type = *reinterpret_cast<uint32_t*>(g_base + kWeaponPropsTypeRva);
    const uint16_t f = *reinterpret_cast<uint16_t*>(item + 0x4C);
    uint8_t* comp = nullptr;
    if ((f >> 14) & 1 || static_cast<int16_t>(f) < 0) return -1;          // personnage : pas une arme
    if ((f >> 13) & 1) comp = *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(item + 0x68) + 0xB0);
    else if ((f >> 12) & 1) comp = reinterpret_cast<uint8_t* (__fastcall*)(void*, uint32_t)>(g_base + 0x52BE30)(item, type);
    else comp = reinterpret_cast<uint8_t* (__fastcall*)(void*, uint32_t)>(g_base + kFindComponentRva)(item, type);
    return comp ? *reinterpret_cast<int*>(comp + 0x68) : -1;
}

const char* IconOfItem(uint8_t* item)
{
    const int wtype = WeaponTypeOf(item);
    if (wtype < 0) return nullptr;
    const char* pack = nullptr;
    const char* name = nullptr;
    reinterpret_cast<const char* (__fastcall*)(int, const char**, const char**)>(g_base + kIconOfTypeRva)(wtype, &pack, &name);
    if (!name || !pack || strcmp(name, "Invalid") == 0) return nullptr;
    LoadIconPack(name, pack);
    return name;
}

// Retient l'icône d'une arme sur sa définition (pour l'afficher une fois l'arme rangée).
void RememberIcon(uint8_t* item)
{
    WeaponDef* d = DefOf(item);
    if (!d || IsModIcon(d->icon)) return;   // armes du mod : leur propre icône
    if (const char* ic = IconOfItem(item)) {   // l'icône vue en jeu remplace celle de la table
        if (!d->icon || strcmp(d->icon, ic) != 0) LOG("armes : %s -> icone reelle %s", d->name, ic);
        d->icon = ic;
    }
}
// ---- Emplacements --------------------------------------------------------------------------
// 3 emplacements du mod : 1 ceinture (une main), 2 et 3 armes longues. Une arme sortie garde son
// emplacement (g_handSlot) : la roue montre toujours les 3 armes et met en évidence celle en main.
// Le mod ne crée ni ne détruit jamais d'arme en main (la caméra se déréglait, comme en vue
// subjective, jusqu'à la visée) ; il passe par l'emplacement « rangé une main » du jeu (slot 4),
// utilisé comme sas :
//   - ranger : Stow main -> sas (fonction du jeu, comme en montant en voiture), puis l'objet du sas
//     est supprimé et l'arme notée dans son emplacement ;
//   - sortir : l'arme est créée et aussitôt rangée dans le sas, puis on simule un appui sur RB :
//     le jeu la sort lui-même, avec son animation.
WeaponDef* g_long[2] = { nullptr, nullptr };
WeaponDef* g_belt = nullptr;
int g_handSlot = -1;     // emplacement de l'arme en main (1..3), -1 inconnu (ramassée...)

WeaponDef*& SlotRef(int slot)
{
    static WeaponDef* none = nullptr;
    none = nullptr;
    return slot == 1 ? g_belt : slot == 2 ? g_long[0] : slot == 3 ? g_long[1] : none;
}

uint8_t* CreateInHand(WeaponDef* d)
{
    uint8_t* p = Player();
    if (!p || !d || !d->ps || SlotItem(p, kSlotHand)) return nullptr;
    reinterpret_cast<CreateEquipFn>(g_base + kCreateEquipRva)(d->ps, p, 0);
    uint8_t* item = SlotItem(p, kSlotHand);
    if (item) {
        g_itemDef[item] = d;
        RememberIcon(item);
    }
    return item;
}

WeaponDef* DefByName(const char* name)
{
    EnsureDefs();
    for (auto& d : g_defs) if (_stricmp(d.name, name) == 0 && d.ps) return &d;
    LOG("armes : fiche inconnue ou introuvable : %s", name);
    return nullptr;
}

void RunDroppedCleanup() {}

// Supprime l'objet du sas (emplacement rangé du jeu) en gardant sa définition.
WeaponDef* ClearStaging()
{
    uint8_t* p = Player();
    void* inv = PlayerInventory();
    uint8_t* s = SlotItem(p, kSlot1H);
    if (!inv || !s) return nullptr;
    WeaponDef* d = DefOf(s);
    RememberIcon(s);
    HookUnEquip(inv, kSlot1H, 1);
    g_itemDef.erase(s);
    return d;
}

// Range au sas puis supprime : utilisé aussi pour l'arme de la ceinture du jeu au chargement.
bool ToBelt()
{
    void* inv = PlayerInventory();
    return inv && StowTo(inv, kSlotHand, kSlot1H);
}

// L'arme est-elle déjà dans un des 3 emplacements ? (évite les doublons quand le jeu l'a rangée)
bool InSlots(WeaponDef* d)
{
    return d && (g_belt == d || g_long[0] == d || g_long[1] == d);
}

int FreeSlotFor(WeaponDef* d)
{
    const int order1[] = { 1, 2, 3 }, order2[] = { 2, 3, 1 };
    const int* order = d && d->cls == 1 ? order1 : order2;
    for (int i = 0; i < 3; ++i) if (!SlotRef(order[i])) return order[i];
    return -1;
}

// Range l'arme en main dans son emplacement (mains nues ensuite). Faux si impossible.
bool HolsterHand()
{
    uint8_t* p = Player();
    void* inv = PlayerInventory();
    uint8_t* h = SlotItem(p, kSlotHand);
    if (!h) { g_handSlot = -1; return true; }
    if (!inv) return false;
    WeaponDef* d = DefOf(h);
    if (!d) { LOG("armes : arme non reconnue, impossible de la ranger"); return false; }
    int target = (g_handSlot >= 1 && SlotRef(g_handSlot) == d) ? g_handSlot : FreeSlotFor(d);
    if (target < 1) { LOG("armes : plus de place pour ranger %s", d->label); return false; }
    if (SlotItem(p, kSlot1H)) {
        // Le sas doit être vide (arme rangée par le jeu, en voiture ou au chargement) : on la garde.
        WeaponDef* s = ClearStaging();
        if (s && s != d && !InSlots(s)) { const int free = FreeSlotFor(s); if (free >= 1 && free != target) SlotRef(free) = s; }
    }
    if (!StowTo(inv, kSlotHand, kSlot1H)) {
        // Arme de mêlée (non « rangeable ») : retirée de la main et supprimée.
        HookUnEquip(inv, kSlotHand, 1);
        if (SlotItem(p, kSlotHand)) { LOG("armes : impossible de ranger %s", d->label); return false; }
        g_itemDef.erase(h);
        SlotRef(target) = d;
        g_handSlot = -1;
        return true;
    }
    ClearStaging();
    SlotRef(target) = d;
    g_handSlot = -1;
    return true;
}

// Sortie en plusieurs images : création, rangement au sas, appui simulé sur RB, attente.
enum class DrawStep { Idle, Create, Stage, Pulse, Wait };
struct DrawState { DrawStep step; int slot; WeaponDef* d; uint8_t* item; ULONGLONG t; };
DrawState g_draw{ DrawStep::Idle, 0, nullptr, nullptr, 0 };
std::atomic<ULONGLONG> g_drawPulseFrom{0}, g_drawPulseUntil{0};
bool UsingKbm();
void TapKey(WPARAM vk, ULONGLONG holdMs);
extern std::atomic<int> g_mouseX, g_mouseY;

void ForceAllowEquip()
{
    uint8_t* comps = CharComponents(Player());
    if (!comps) return;
    if (auto* ch = *reinterpret_cast<uint8_t**>(comps + 0x30)) *reinterpret_cast<uint64_t*>(ch + 0xF0) |= 0x200;
}

bool DrawInProgress() { return g_draw.step != DrawStep::Idle; }

void RunPendingCreate()
{
    if (g_draw.step == DrawStep::Idle) return;
    uint8_t* p = Player();
    void* inv = PlayerInventory();
    if (!p || !inv) { g_draw.step = DrawStep::Idle; return; }
    const ULONGLONG now = GetTickCount64();
    switch (g_draw.step) {
    case DrawStep::Idle:
        return;
    case DrawStep::Create:
        if (SlotItem(p, kSlotHand)) { g_draw.step = DrawStep::Idle; return; }
        if (SlotItem(p, kSlot1H)) {
            WeaponDef* s = ClearStaging();
            if (s && s != g_draw.d && !InSlots(s)) { const int free = FreeSlotFor(s); if (free >= 1) SlotRef(free) = s; }
        }
        g_draw.item = CreateInHand(g_draw.d);
        g_draw.step = g_draw.item ? DrawStep::Stage : DrawStep::Idle;
        if (!g_draw.item) LOG("armes : creation de %s impossible", g_draw.d->label);
        return;
    case DrawStep::Stage:
        if (!StowTo(inv, kSlotHand, kSlot1H)) {
            // Le jeu refuse le sas : l'arme reste en main telle quelle.
            g_handSlot = g_draw.slot;
            g_draw.step = DrawStep::Idle;
            LOG("armes : %s en main (sans animation)", g_draw.d->label);
            return;
        }
        g_draw.step = DrawStep::Pulse;
        return;
    case DrawStep::Pulse:
        ForceAllowEquip();
        // Le jeu sort l'arme rangée : RB à la manette, flèche du bas (« sortir le pistolet ») au clavier.
        if (UsingKbm()) TapKey(VK_DOWN, 120);
        else { g_drawPulseFrom = now; g_drawPulseUntil = now + 150; }
        g_draw.t = now;
        g_draw.step = DrawStep::Wait;
        return;
    case DrawStep::Wait:
        if (SlotItem(p, kSlotHand) == g_draw.item) {
            g_handSlot = g_draw.slot;
            g_draw.step = DrawStep::Idle;
            LOG("armes : %s sortie par le jeu", g_draw.d->label);
        } else if (now - g_draw.t > 1500) {
            // Le jeu ne l'a pas sortie : on la met en main directement.
            if (SlotItem(p, kSlot1H) == g_draw.item) StowTo(inv, kSlot1H, kSlotHand);
            g_handSlot = SlotItem(p, kSlotHand) == g_draw.item ? g_draw.slot : -1;
            g_draw.step = DrawStep::Idle;
            LOG("armes : %s sortie sans le jeu", g_draw.d->label);
        } else {
            ForceAllowEquip();
        }
        return;
    }
}

// Choix : 0 mains nues, 1 ceinture, 2 / 3 armes longues.
void SelectWeapon(int choice)
{
    if (g_draw.step != DrawStep::Idle) return;   // un changement est déjà en cours
    uint8_t* p = Player();
    if (!p || !PlayerInventory()) return;
    uint8_t* h = SlotItem(p, kSlotHand);
    if (choice == 0) {
        HolsterHand();
    } else {
        WeaponDef* d = SlotRef(choice);
        if (!d) { LOG("armes : emplacement %d vide", choice); return; }
        if (h && g_handSlot == choice && DefOf(h) == d) return;   // déjà en main
        if (h && !HolsterHand()) return;
        g_draw = { DrawStep::Create, choice, d, nullptr, 0 };
    }
    LOG("armes : choix %d -> ceinture=%s long1=%s long2=%s (en main : %d)", choice,
        g_belt ? g_belt->label : "-", g_long[0] ? g_long[0]->label : "-", g_long[1] ? g_long[1]->label : "-", g_handSlot);
}

bool GiveWeapon(const char* propName)
{
    WeaponDef* d = DefByName(propName);
    return d && CreateInHand(d);
}

// Kit de planque : remplit les emplacements vides, mains nues à la fin.
enum class KitStep { Idle, ClearHand, GiveBelt, StowBelt, GiveLong1, StowLong1, GiveLong2, StowLong2, Done };
KitStep g_kit = KitStep::Idle;

void RunKitStep()
{
    if (g_kit == KitStep::Idle) return;
    g_kit = KitStep::Idle;
    for (const char* want : { g_kitBelt, g_kitBack, g_kitHand }) {
        WeaponDef* d = DefByName(want);
        if (!d || g_belt == d || g_long[0] == d || g_long[1] == d) continue;
        const int k = FreeSlotFor(d);
        if (k >= 1) SlotRef(k) = d;
    }
    LOG("armurerie : kit pret (ceinture=%s, longues=%s / %s)", g_belt ? g_belt->label : "-",
        g_long[0] ? g_long[0]->label : "-", g_long[1] ? g_long[1]->label : "-");
}

// ---- Manette (XInput) ----------------------------------------------------------------------
// Le jeu lit la manette par XINPUT9_1_0.dll : on intercepte cet import pour lui cacher la croix bas et le
// stick droit pendant que la roue est ouverte. Le mod lit la manette par xinput1_4 (non filtré).
struct PadState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, PadState*);
XInputGetStateFn g_xinput = nullptr;          // pour le mod
XInputGetStateFn g_gameXInput = nullptr;      // import du jeu
std::atomic<bool> g_wheelOpen{false};
extern std::atomic<bool> g_armOpen;
bool ArmHoverActive();
void UpdateWardrobeArmory();
void BlockAutoEquip();
bool PickupTargeted();
std::atomic<ULONGLONG> g_pulseEnd{0};
extern std::atomic<ULONGLONG> g_drawPulseFrom, g_drawPulseUntil;
std::atomic<bool> g_holsterForPickup{false};
extern std::atomic<bool> g_lastInputKbm;
constexpr WORD kPadWheel = 0x0200;   // RB maintenu = roue (appui court = recharger / ramasser)
float g_wheelHold = 0.5f;   // secondes de maintien de RB avant d'ouvrir la roue ([Wheel] HoldTime)
std::atomic<bool> g_wheelUsed{false};   // l'appui RB en cours a ouvert la roue

// RB est gardé par le mod pendant l'appui : le jeu ne le voit pas (il sortirait l'arme rangée,
// ou ramasserait pendant qu'on vise la roue). Au relâchement d'un appui court (roue non ouverte),
// on rejoue un appui de 120 ms au jeu : recharger / ramasser fonctionnent toujours.
DWORD WINAPI GameXInputGetState(DWORD user, PadState* st)
{
    const DWORD r = g_gameXInput ? g_gameXInput(user, st) : ERROR_DEVICE_NOT_CONNECTED;
    if (r == 0 && st && user < 4) {
        if (st->buttons || abs(st->lx) > 9000 || abs(st->ly) > 9000 || abs(st->rx) > 9000 || abs(st->ry) > 9000
            || st->lt > 60 || st->rt > 60) g_lastInputKbm = false;
        if (ArmHoverActive()) st->buttons &= ~0x1000;   // A sur « ARMES » : pour le mod
        static ULONGLONG downSince[4] = {}, pulseFrom[4] = {}, pulseUntil[4] = {};
        const ULONGLONG now = GetTickCount64();
        if (st->buttons & kPadWheel) {
            if (!downSince[user]) { downSince[user] = now; g_wheelUsed = false; }
            st->buttons &= ~kPadWheel;
        } else if (downSince[user]) {
            if (!g_wheelUsed.load() && now - downSince[user] < static_cast<ULONGLONG>(g_wheelHold * 1000.f)) {
                // Appui court près d'une arme au sol avec une arme en main : le mod range d'abord
                // l'arme en main (mise à jour du HUD), puis l'appui est rejoué : le jeu ramasse
                // comme avec les mains vides (plus besoin de l'appui long « échanger »).
                if (SlotItem(Player(), kSlotHand) && PickupTargeted()) {
                    g_holsterForPickup = true;
                    pulseFrom[user] = now + 200;
                    pulseUntil[user] = now + 330;
                } else {
                    pulseFrom[user] = now;
                    pulseUntil[user] = now + 120;
                }
            }
            downSince[user] = 0;
        }
        if (now >= pulseFrom[user] && now < pulseUntil[user]) {
            g_pulseEnd = pulseUntil[user];
            st->buttons |= kPadWheel;
            // Mains vides : le jeu sortirait l'arme rangée sur cet appui ; seul la roue choisit.
            if (!SlotItem(Player(), kSlotHand)) BlockAutoEquip();
        }
        if (now >= g_drawPulseFrom.load() && now < g_drawPulseUntil.load()) st->buttons |= kPadWheel;   // sortie d'arme par le jeu
        if (g_wheelOpen.load()) { st->rx = 0; st->ry = 0; st->buttons &= ~(0x1000 | 0x2000 | 0x8000); }
        // Menu ouvert : rien pour le jeu ; après fermeture, A / B restent bloqués jusqu'au relâchement
        // (sinon le B qui ferme le menu ferait aussi quitter la garde-robe).
        static bool holdAfterMenu = false;
        if (g_armOpen.load()) { st->buttons = 0; st->lx = st->ly = st->rx = st->ry = 0; st->lt = st->rt = 0; holdAfterMenu = true; }
        else if (holdAfterMenu) { if (st->buttons & 0x3000) st->buttons &= ~0x3000; else holdAfterMenu = false; }
    }
    return r;
}

// Le jeu sort tout seul l'arme rangée (« allow equip stowed weapon », bit 0x200 de +0xF0 du
// composant personnage *(objet+0x68)+0x30) : le choix doit passer par la roue, on le coupe.
// Cible 0x29 (eTARGET_TYPE_PICKUP_ITEM) du ciblage du joueur : une arme au sol est-elle ciblée ?
bool PickupTargeted()
{
    if (!g_defsReady) return false;   // fiches chargées par le fil du jeu seulement
    uint8_t* comps = CharComponents(Player());
    if (!comps) return false;
    auto* ts = *reinterpret_cast<uint8_t**>(comps + 0x140);
    if (!ts) return false;
    auto* map = *reinterpret_cast<uint8_t**>(ts + 0x60);
    auto* entries = *reinterpret_cast<uint8_t**>(ts + 0x58);
    if (!map || !entries) return false;
    const uint8_t idx = map[8 + 0x29];
    if (!idx) return false;
    uint8_t* item = *reinterpret_cast<uint8_t**>(entries + idx * 0x38 + 0x28);
    // Seulement les armes à feu reconnues : en planque, nourriture et objets sont souvent ciblés.
    return item && DefOf(item) != nullptr;
}

// Le réglage « arme rangée autorisée » n'est coupé que le temps d'un appui court ; le jeu ne le
// remet pas de lui-même (sans lui, il range aussitôt tout pistolet sorti) : on le rétablit après.
// Armes « invisibles » du mod (pistolet-doigt) : composant d'affichage (type 0x20212FC) marqué
// caché par FUN_140004100(composant, 1), la fonction qu'utilise le jeu pour les armes rangées.
// Refait à chaque image : le jeu peut la réafficher en la sortant.
uint8_t* DrawComp(uint8_t* obj)
{
    if (!obj) return nullptr;
    const uint32_t ctype = *reinterpret_cast<uint32_t*>(g_base + 0x20212FC);
    const uint16_t f = *reinterpret_cast<uint16_t*>(obj + 0x4C);
    if (((f >> 14) & 1) || static_cast<int16_t>(f) < 0) return *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(obj + 0x68) + 0xE0);
    if ((f >> 13) & 1) return *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(obj + 0x68) + 0x90);
    return reinterpret_cast<uint8_t* (__fastcall*)(void*, uint32_t)>(g_base + kFindComponentRva)(obj, ctype);
}

bool IsObjectHidden(uint8_t* obj)
{
    uint8_t* comp = DrawComp(obj);
    return comp && *(comp + 0x4D8) != 0;
}

void SetObjectHidden(uint8_t* obj, bool hidden)
{
    uint8_t* comp = DrawComp(obj);
    if (!comp || *(comp + 0x4D8) == (hidden ? 1 : 0)) return;
    reinterpret_cast<void(__fastcall*)(void*, uint8_t)>(g_base + 0x4100)(comp, hidden ? 1 : 0);
}

// Arme jetée : le jeu la retire de la main sans passer par UnEquip. D'une image à l'autre, si
// l'arme tenue (avec un emplacement) a disparu de la main sans que le mod l'ait rangée et sans
// être dans l'emplacement rangé du jeu (voiture...), elle quitte son emplacement de la roue.
void WatchThrownWeapon()
{
    static uint8_t* lastHand = nullptr;
    static WeaponDef* lastDef = nullptr;
    static int lastSlot = -1;
    uint8_t* p = Player();
    uint8_t* h = SlotItem(p, kSlotHand);
    if (!h && lastHand && lastSlot >= 1 && g_handSlot == lastSlot && !DrawInProgress()
        && SlotItem(p, kSlot1H) != lastHand && SlotRef(lastSlot) == lastDef) {
        LOG("armes : %s n'est plus en main (jetee), retiree de l'emplacement %d", lastDef ? lastDef->label : "?", lastSlot);
        SlotRef(lastSlot) = nullptr;
        g_handSlot = -1;
    }
    if (!h && lastHand && SlotItem(p, kSlot1H) != lastHand) g_itemDef.erase(lastHand);   // objet parti : oublié
    // Arme ramassée : le jeu ne passe pas toujours par Equip. Toute nouvelle arme en main sans
    // emplacement en reçoit un tout de suite (celui de l'arme échangée, le sien, ou un libre).
    if (h && h != lastHand && g_handSlot < 1 && !DrawInProgress()) {
        if (WeaponDef* nd = DefOf(h)) {
            int k = g_pickupSlot;
            g_pickupSlot = -1;
            if (k < 1) for (int s = 1; s <= 3; ++s) if (SlotRef(s) == nd) { k = s; break; }
            if (k < 1) k = FreeSlotFor(nd);
            if (k >= 1) {
                SlotRef(k) = nd;
                g_handSlot = k;
                if (nd->icon) LoadIconPack(nd->icon, nullptr);
                LOG("armes : %s ramassee -> emplacement %d", nd->label, k);
            }
        }
    }
    lastHand = h;
    lastSlot = h ? g_handSlot : -1;
    lastDef = h ? DefOf(h) : nullptr;
    if (h) { auto it = g_itemDef.find(h); if (it != g_itemDef.end()) lastDef = it->second; }
}

bool IconReady(const char* icon);

// Lance-roquettes : basé sur le lance-grenades multiple (MGL, une seule fonction de tir ; le
// « rifle-grenade » est un fusil d'assaut à lance-grenades). Son objet reçoit sa propre fiche d'arme,
// copie de celle du MGL : un seul mode de tir, une roquette par chargeur, plus rapide et presque sans
// chute, plus grosse explosion ([Rocket] du .ini). La fiche du MGL d'origine n'est pas modifiée.
void KeepRocketInfo(uint8_t* item)
{
    void** pinfo = nw::WeaponInfoSlot(item);
    if (!pinfo) return;
    static void* src = nullptr;
    static void* rocket = nullptr;
    static bool failed = false;
    static uint32_t glExpl = 0, ourExpl = 0;
    static float radiusMul = 1.6f;
    static uint32_t prefer[4] = {};
    auto symName = [](uint32_t s) -> const char* { return reinterpret_cast<SymbolNameFn>(g_base + kSymbolNameRva)(s); };
    // Notre type d'explosion doit être dans le tableau du jeu avant chaque tir (le jeu le vide à son
    // rechargement) ; sinon retour à l'explosion de grenade plutôt qu'un type introuvable (plantage).
    if (rocket && ourExpl && !nw::EnsureRocketExplosion(ourExpl, glExpl, radiusMul, symName, prefer, 4)) {
        nw::SetRocketExplosion(rocket, glExpl);
        ourExpl = 0;
    }
    if (!*pinfo || *pinfo == rocket || failed) return;
    if (!rocket) {
        char buf[64];
        nw::RocketParams rp{};
        GetPrivateProfileStringA("Rocket", "Speed", "3.0", buf, sizeof(buf), g_iniPath);
        rp.speedMul = static_cast<float>(atof(buf));
        GetPrivateProfileStringA("Rocket", "Gravity", "0.05", buf, sizeof(buf), g_iniPath);
        rp.gravityMul = static_cast<float>(atof(buf));
        GetPrivateProfileStringA("Rocket", "Damage", "2.0", buf, sizeof(buf), g_iniPath);
        rp.damageMul = static_cast<float>(atof(buf));
        GetPrivateProfileStringA("Rocket", "Radius", "1.6", buf, sizeof(buf), g_iniPath);
        radiusMul = static_cast<float>(atof(buf));
        // Explosion : celle de la grenade explosive du lance-grenades du jeu (celle du MGL est du gaz),
        // au rayon agrandi ([Rocket] Radius), ajoutée comme nouveau type
        // « explosion-skr_rocket ». (Un type absent du tableau, comme « explosion-vehicle », plante.)
        if (WeaponDef* gl = DefByName("object-physical-weapon-firearm-rifle-grenade")) {
            const uint32_t glSym = PropSetSymbol(*gl);
            const int count = *reinterpret_cast<int*>(g_base + 0x240E3D8);
            auto* table = reinterpret_cast<uint8_t*>(g_base + 0x240EEC0);
            for (int i = 0; i < count; ++i) {
                if (*reinterpret_cast<uint32_t*>(table + i * 24) != glSym) continue;
                glExpl = nw::LastModeExplosion(table + i * 24);
                break;
            }
        }
        const char* names[4] = { "explosion-vehicle", "explosion-truck", "explosion-gasoline", "explosion-propane" };
        for (int k = 0; k < 4; ++k) { prefer[k] = 0xFFFFFFFF; reinterpret_cast<MakeSymbolFn>(g_base + kMakeSymbolRva)(&prefer[k], names[k]); }
        uint32_t sym = 0xFFFFFFFF;
        reinterpret_cast<MakeSymbolFn>(g_base + kMakeSymbolRva)(&sym, "explosion-skr_rocket");
        ourExpl = glExpl ? nw::EnsureRocketExplosion(sym, glExpl, radiusMul, symName, prefer, 4) : 0;
        rp.explosion = ourExpl ? ourExpl : glExpl;
        LOG("armes : explosion de roquette %08X (grenade %08X)", rp.explosion, glExpl);
        rp.clip = GetPrivateProfileIntA("Rocket", "Clip", 1, g_iniPath);
        src = *pinfo;
        rocket = nw::MakeRocketWeaponInfo(src, rp, symName);
        if (!rocket) { failed = true; return; }
    }
    if (*pinfo != src) return;   // pas la fiche attendue (MGL) : on ne touche à rien
    *pinfo = rocket;
    LOG("armes : lance-roquettes, fiche de roquette posee");
}
// Nouvelles armes : leur modèle 3D (créé en mémoire au premier besoin) est posé sur l'objet de l'arme,
// en main ou dans le sas, et les pièces d'origine sont gardées masquées.
void KeepCustomModels()
{
    static bool logSet = false;
    if (!logSet) { logSet = true; nw::SetLog([](const char* s) { LOG("%s", s); }); }
    nw::Update();
    uint8_t* p = Player();
    // Icône du HUD (en haut à droite) des armes du mod. Le HUD garde le nom d'icône d'origine (sinon
    // il affiche « maintenir pour lâcher » au lieu des munitions) ; seule l'image change : tant que
    // l'arme du mod est en main, la texture d'origine affiche notre silhouette, puis
    // retrouve son image. Une référence en plus sur la nôtre : le jeu peut libérer ce qu'il trouve.
    {
        uint8_t* h = SlotItem(p, kSlotHand);
        auto it = h ? g_itemDef.find(h) : g_itemDef.end();
        void** slot = nullptr;
        void* ours = nullptr;
        if (it != g_itemDef.end() && IsModIcon(it->second->icon) && IconReady(it->second->icon)) {
            if (const char* orig = IconOfItem(h)) {
                // Roue ouverte : on ne garde notre image que si aucune autre arme de la roue n'utilise
                // cette icône d'origine (un vrai fusil d'assaut y montrerait sinon notre silhouette).
                bool shared = false;
                if (g_wheelOpen.load())
                    for (int k = 1; k <= 3; ++k)
                        if (WeaponDef* o = SlotRef(k); o && o != it->second && o->icon && strcmp(o->icon, orig) == 0) shared = true;
                if (!shared) {
                    slot = nw::TextureSrvSlot(orig);
                    uint32_t tw = 0, th = 0;
                    nw::TextureSize(orig, &tw, &th);   // pistolets : icônes carrées, fusils : 2:1
                    ours = nw::IconSrvFor(it->second->icon, ModFile(it->second->icon, ".png").c_str(), tw, th);
                }
            }
        }
        static void** swapped = nullptr;
        static void* saved = nullptr;
        static void* put = nullptr;
        if (swapped && (swapped != slot || !ours)) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(swapped, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && *swapped == put) *swapped = saved;
            swapped = nullptr;
        }
        if (slot && ours && !swapped) {
            saved = *slot;
            nw::AddRefSrv(ours);
            *slot = ours;
            put = ours;
            swapped = slot;
        }
    }    for (uint8_t* item : { SlotItem(p, kSlotHand), SlotItem(p, kSlot1H) }) {
        if (!item) continue;
        auto it = g_itemDef.find(item);
        if (it == g_itemDef.end() || !it->second->model) continue;
        if (const uint32_t uid = nw::LoadModel(ModFile(it->second->model, ".skm").c_str())) nw::ApplyModel(item, uid);
        if (strstr(it->second->name, "#rpg")) KeepRocketInfo(item);
    }
}

void KeepHiddenWeapons()
{
    uint8_t* h = SlotItem(Player(), kSlotHand);
    static uint8_t* lastHand = nullptr;
    static ULONGLONG since = 0;
    const ULONGLONG now = GetTickCount64();
    if (h != lastHand) { lastHand = h; since = now; }
    if (!h) return;
    auto it = g_itemDef.find(h);
    const bool modHidden = it != g_itemDef.end() && it->second->hidden;
    if (modHidden) { SetObjectHidden(h, true); return; }
    // Changement d'arme pendant un rechargement : le jeu cache l'arme le temps de l'animation et
    // la nouvelle arme restait cachée. Dans les 4 s qui suivent sa sortie, une arme cachée est
    // réaffichée (pas au-delà : le jeu cache parfois l'arme lui-même, cinématiques...).
    if (now - since < 4000 && IsObjectHidden(h)) {
        int vis = 0, total = 0;
        nw::BindingVisibility(h, &vis, &total);
        LOG("armes : %s en main mais cachee (pieces visibles %d/%d) : reaffichee", LabelOf(h), vis, total);
        SetObjectHidden(h, false);
    }
    static uint8_t* checked = nullptr;
    if (checked != h && now - since > 1500) {
        checked = h;
        int vis = 0, total = 0;
        nw::BindingVisibility(h, &vis, &total);
        if (total && !vis) LOG("armes : %s en main, aucune piece visible (%d)", LabelOf(h), total);
    }
}

void RestoreAutoEquip()
{
    if (GetTickCount64() < g_pulseEnd.load() + 300) return;
    uint8_t* comps = CharComponents(Player());
    if (!comps) return;
    auto* ch = *reinterpret_cast<uint8_t**>(comps + 0x30);
    if (!ch) return;
    *reinterpret_cast<uint64_t*>(ch + 0xF0) |= 0x200;
}

void BlockAutoEquip()
{
    uint8_t* comps = CharComponents(Player());
    if (!comps) return;
    auto* ch = *reinterpret_cast<uint8_t**>(comps + 0x30);
    if (!ch) return;
    auto* flags = reinterpret_cast<uint64_t*>(ch + 0xF0);
    if (*flags & 0x200) {
        *flags &= ~0x200ull;
        static int logged = 0;
        if (logged++ < 3) LOG("armes : sortie automatique de l'arme rangee coupee");
    }
}

bool ReadPad(PadState& out)
{
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (const char* dll : { "xinput1_4.dll", "xinput1_3.dll" }) {
            if (HMODULE m = LoadLibraryA(dll)) {
                g_xinput = reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
                if (g_xinput) { LOG("armes : manette lue via %s", dll); break; }
            }
        }
    }
    if (!g_xinput) return false;
    for (DWORD i = 0; i < 4; ++i) if (g_xinput(i, &out) == 0) return true;
    return false;
}

// ---- Roue d'arme ---------------------------------------------------------------------------
// Maintenir RB 0,5 s (manette) ou Tab (clavier) : la roue s'affiche (dessinée par le HUD Flash,
// ui\hud_gta.as) ; stick droit ou flèches pour choisir ; relâcher pour équiper.
// Haut = mains nues, gauche = ceinture, droite = arme longue 1, bas = arme longue 2.
// Le mod écrit l'état dans des variables Flash (_root.gtaWheel*) via Movie::SetVariable
// (FUN_14090EB90, valeur GFx : +0x10 objet, +0x18 type 5 nombre / 6 chaîne, +0x20 valeur).
struct GfxValue { void* prev; void* next; void* obj; uint32_t type; uint32_t pad; union { double num; const char* str; }; };
constexpr uintptr_t kSetVariableRva = 0x90EB90;
int g_wheelSel = 0;

void SetFlashNumber(void* movie, const char* path, double v)
{
    GfxValue g{};
    g.type = 5;
    g.num = v;
    const uint8_t ok = reinterpret_cast<uint8_t(__fastcall*)(void*, const char*, GfxValue*, int)>(g_base + kSetVariableRva)(movie, path, &g, 1);
    static int logged = 0;
    if (strstr(path, "gtaArmOpen") && v != 0 && logged++ < 6)
        LOG("flash : ecriture %s=%.0f -> %s (cible %p, movie %p)", path, v, ok ? "ok" : "refusee", movie,
            movie ? *reinterpret_cast<void**>(static_cast<uint8_t*>(movie) + 0x18) : nullptr);
}

void SetFlashString(void* movie, const char* path, const char* s)
{
    GfxValue g{};
    g.type = 6;
    g.str = s;
    reinterpret_cast<void(__fastcall*)(void*, const char*, GfxValue*, int)>(g_base + kSetVariableRva)(movie, path, &g, 1);
}

void* HudMovie(void* hud)
{
    if (!hud) return nullptr;
    auto* a = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(hud) + 0x60);
    return a ? *reinterpret_cast<void**>(a + 0x140) : nullptr;
}

// Vrai quand le fichier de l'icône est chargé (demandé il y a plus de 1,2 s) ; demande le chargement sinon.
bool IconReady(const char* icon)
{
    if (!icon || !icon[0]) return false;
    if (IsModIcon(icon)) return nw::IconState(icon, ModFile(icon, ".png").c_str()) == 2;
    auto it = g_iconLoadTick.find(icon);
    if (it == g_iconLoadTick.end()) { LoadIconPack(icon, nullptr); return false; }
    return GetTickCount64() - it->second > 1200;
}

void PushWheel(void* movie, bool open)
{
    if (!movie) return;
    uint8_t* p = Player();
    SetFlashNumber(movie, "_root.gtaWheelOpen", open ? 1 : 0);
    // Les icônes sont envoyées même roue fermée : le HUD les charge à l'avance (sinon elles
    // apparaissaient avec un temps de retard, et la roue clignotait à l'ouverture).
    LoadIconPack(FistIcon(), kFistChoices[g_fistChoice].pack);   // le bon fichier (pas le nom déduit)
    SetFlashString(movie, "_root.gtaWheelI0", IconReady(FistIcon()) ? FistIcon() : "");
    static const char* ipc[] = { "", "_root.gtaWheelI1", "_root.gtaWheelI2", "_root.gtaWheelI3" };
    for (int k = 1; k <= 3; ++k) { WeaponDef* d = SlotRef(k); SetFlashString(movie, ipc[k], d && IconReady(d->icon) ? d->icon : ""); }
    if (!open) return;
    // Pointeur de la souris dans la roue (clavier/souris seulement) : déplacement cumulé, borné à
    // 300, ramené au rayon de la roue (250 pixels).
    const bool kbm = UsingKbm();
    SetFlashNumber(movie, "_root.gtaWheelMouse", kbm ? 1 : 0);
    if (kbm) {
        SetFlashNumber(movie, "_root.gtaWheelMX", g_mouseX.load() * (250.0 / 300.0));
        SetFlashNumber(movie, "_root.gtaWheelMY", g_mouseY.load() * (250.0 / 300.0));
    }
    uint8_t* h = SlotItem(p, kSlotHand);
    SetFlashNumber(movie, "_root.gtaWheelSel", g_wheelSel);
    // Emplacement de l'arme en main (0 = mains nues, -1 = arme sans emplacement).
    SetFlashNumber(movie, "_root.gtaWheelCur", !h ? 0 : (g_handSlot >= 1 ? g_handSlot : -1));
    SetFlashString(movie, "_root.gtaWheelL0", Tr("Mains nues", "Bare hands"));
    static const char* lp[] = { "", "_root.gtaWheelL1", "_root.gtaWheelL2", "_root.gtaWheelL3" };
    static const char* ip[] = { "", "_root.gtaWheelI1", "_root.gtaWheelI2", "_root.gtaWheelI3" };
    for (int k = 1; k <= 3; ++k) {
        WeaponDef* d = SlotRef(k);
        SetFlashString(movie, lp[k], d ? WeaponLabel(d) : "");
        SetFlashString(movie, ip[k], d && IconReady(d->icon) ? d->icon : "");
    }
    SetFlashString(movie, "_root.gtaWheelHand", h ? LabelOf(h) : Tr("Mains nues", "Bare hands"));
}

// ---- Armes posées dans les planques ---------------------------------------------------------
// En entrant dans une planque, quelques armes apparaissent au sol autour du point enregistré
// (F10) : nom unique (FUN_1401909E0, comme les armes restaurées d'une sauvegarde), création
// depuis la fiche (FUN_1405B7460), placement par matrice (FUN_140556AD0). Une fois par visite :
// il faut s'éloigner de 80 m pour qu'elles réapparaissent. Liste : [Armory] Floor= (noms de fiches
// séparés par des virgules).

uint8_t* SpawnWeaponAt(WeaponDef* d, float x, float y, float z, float yaw)
{
    if (!d || !d->ps) return nullptr;
    uint32_t sym = 0xFFFFFFFF;
    reinterpret_cast<void(__fastcall*)(void*, uint32_t*, const char*)>(g_base + kUniqueNameRva)(
        reinterpret_cast<void*>(g_base + kUniqueNameCtxRva), &sym, "SafehouseWeapon");
    auto* obj = reinterpret_cast<uint8_t* (__fastcall*)(uint32_t*, void*, uint32_t, void*, uint32_t, uint32_t)>(
        g_base + kSpawnObjectRva)(&sym, d->ps, 0, nullptr, 0, 0);
    if (!obj) return nullptr;
    const float c = cosf(yaw), s = sinf(yaw);
    float m[16] = {
        c, s, 0, 0,
        -s, c, 0, 0,
        0, 0, 1, 0,
        x, y, z, 1 };
    reinterpret_cast<void(__fastcall*)(void*, float*)>(g_base + kSetTransformRva)(obj, m);
    return obj;
}

// Le point de planque le plus proche (ou -1).
int NearestSafehouse(float x, float y, float radius)
{
    int best = -1;
    float bestD = radius * radius;
    for (size_t i = 0; i < g_safehouses.size(); ++i) {
        const float dx = x - g_safehouses[i].x, dy = y - g_safehouses[i].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD) { bestD = d2; best = static_cast<int>(i); }
    }
    return best;
}

void UpdateFloorWeapons()
{
    static std::vector<bool> stocked;
    if (stocked.size() != g_safehouses.size()) stocked.resize(g_safehouses.size(), false);
    if (!g_playerKnown.load()) return;
    const float px = g_playerX.load(), py = g_playerY.load();
    // Réarmement : loin de toutes les planques.
    for (size_t i = 0; i < g_safehouses.size(); ++i) {
        const float dx = px - g_safehouses[i].x, dy = py - g_safehouses[i].y;
        if (dx * dx + dy * dy > 80.f * 80.f) stocked[i] = false;
    }
    const int h = NearestSafehouse(px, py, 15.f);
    if (h < 0 || stocked[h] || !g_floorList[0]) return;
    // Les points enregistrés plusieurs fois au même endroit comptent comme une seule planque.
    for (size_t i = 0; i < g_safehouses.size(); ++i) {
        const float dx = g_safehouses[h].x - g_safehouses[i].x, dy = g_safehouses[h].y - g_safehouses[i].y;
        if (dx * dx + dy * dy < 15.f * 15.f) stocked[i] = true;
    }
    uint8_t* pl = Player();
    auto* node = pl ? *reinterpret_cast<uint8_t**>(pl + 0x58) : nullptr;
    if (!node) return;
    const float z = *reinterpret_cast<float*>(node + 0xB8);
    EnsureDefs();
    std::vector<WeaponDef*> list;
    char buf[sizeof(g_floorList)];
    strcpy_s(buf, g_floorList);
    char* ctx = nullptr;
    for (char* tok = strtok_s(buf, ",", &ctx); tok; tok = strtok_s(nullptr, ",", &ctx)) {
        while (*tok == ' ') ++tok;
        if (WeaponDef* d = DefByName(tok)) list.push_back(d);
    }
    // En arc de cercle autour du point de la planque, un peu au-dessus du sol (elles tombent).
    const float cx = g_safehouses[h].x, cy = g_safehouses[h].y;
    int n = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const float a = 6.2832f * static_cast<float>(i) / static_cast<float>(list.size());
        if (SpawnWeaponAt(list[i], cx + 1.4f * cosf(a), cy + 1.4f * sinf(a), z + 0.6f, a)) ++n;
    }
    LOG("armurerie : %d arme(s) posee(s) dans la planque %d", n, h + 1);
}
// ---- Sauvegarde des armes longues -----------------------------------------------------------
// Le jeu ne sauvegarde que l'arme en main et la ceinture : les 2 armes longues du mod sont
// écrites dans le .ini ([Loadout] Long1/Long2 + leur icône) et rechargées au démarrage.
void SaveLoadout()
{
    WritePrivateProfileStringA("Loadout", "Belt", g_belt ? g_belt->name : "", g_iniPath);
    WritePrivateProfileStringA("Loadout", "IconB", g_belt && g_belt->icon ? g_belt->icon : "", g_iniPath);
    for (int i = 0; i < 2; ++i) {
        char key[16];
        sprintf_s(key, "Long%d", i + 1);
        WritePrivateProfileStringA("Loadout", key, g_long[i] ? g_long[i]->name : "", g_iniPath);
        sprintf_s(key, "Icon%d", i + 1);
        WritePrivateProfileStringA("Loadout", key, g_long[i] && g_long[i]->icon ? g_long[i]->icon : "", g_iniPath);
    }
    LOG("armes : armes longues sauvegardees (%s / %s)", g_long[0] ? g_long[0]->label : "-", g_long[1] ? g_long[1]->label : "-");
}

void LoadLoadout()
{
    {
        char name[128], icon[96];
        GetPrivateProfileStringA("Loadout", "Belt", "", name, sizeof(name), g_iniPath);
        GetPrivateProfileStringA("Loadout", "IconB", "", icon, sizeof(icon), g_iniPath);
        if (name[0]) {
            if (WeaponDef* d = DefByName(name)) {
                g_belt = d;
                if (icon[0] && !d->icon) { d->icon = _strdup(icon); LoadIconPack(d->icon, nullptr); }
            }
        }
    }
    for (int i = 0; i < 2; ++i) {
        char key[16], name[128], icon[96];
        sprintf_s(key, "Long%d", i + 1);
        GetPrivateProfileStringA("Loadout", key, "", name, sizeof(name), g_iniPath);
        sprintf_s(key, "Icon%d", i + 1);
        GetPrivateProfileStringA("Loadout", key, "", icon, sizeof(icon), g_iniPath);
        if (!name[0]) continue;
        WeaponDef* d = DefByName(name);
        if (!d) continue;
        g_long[i] = d;
        if (icon[0] && !d->icon) {
            d->icon = _strdup(icon);
            LoadIconPack(d->icon, nullptr);
        }
    }
    LOG("armes : armes longues rechargees (%s / %s)", g_long[0] ? g_long[0]->label : "-", g_long[1] ? g_long[1]->label : "-");
}

// ---- Ralenti pendant la roue ----------------------------------------------------------------
// Échelle de temps de la simulation : float 0x203C958 (1.0 normal), celle que règle la commande
// de script set_simulation_timescale. On la baisse progressivement tant que la roue est ouverte,
// puis on remet la valeur d'avant. [Wheel] SlowMo=0.25 dans le .ini (1 = pas de ralenti).
constexpr uintptr_t kTimeScaleRva = 0x203C958;
float g_slowMo = 0.25f;
float g_savedTimeScale = -1.f;

void SlowMoStep(bool open)
{
    auto* ts = reinterpret_cast<float*>(g_base + kTimeScaleRva);
    if (open) {
        if (g_savedTimeScale < 0.f) g_savedTimeScale = *ts;
        if (g_slowMo >= 1.f || g_savedTimeScale <= 0.f) return;
        const float target = g_savedTimeScale * g_slowMo;
        *ts = (std::max)(target, *ts - 0.15f);
    } else if (g_savedTimeScale >= 0.f) {
        *ts = g_savedTimeScale;
        g_savedTimeScale = -1.f;
    }
}
void* g_hudMovie = nullptr;      // dernier « wrapper » Flash du HUD
void* HudMovieCached() { return g_hudMovie; }

// ---- Armurerie (garde-robe, ou roue en planque) ---------------------------------------------
// Deux niveaux : 0 = les 3 emplacements (« Ceinture : Pistolet .50 »...), 1 = les armes possibles
// pour l'emplacement choisi. A / Entrée valide, B / Échap revient (ou ferme), croix ou stick
// gauche haut-bas (ou flèches) pour parcourir. Dessiné par le Flash de l'écran qui l'a ouvert
// (_root.gtaArm*) : la garde-robe (ligne « ARMES ») ou le HUD (roue en planque, A).
// Pendant le menu, le jeu ne reçoit plus rien de la manette.
std::atomic<bool> g_armOpen{false};
int g_armLevel = 0;                   // 0 emplacements, 1 armes
int g_armSlot = 1;                    // 1 ceinture, 2 / 3 armes longues
int g_armIndex = 0;
std::vector<WeaponDef*> g_armList;    // niveau 1 ; nullptr = vider l'emplacement
void* g_armMovie = nullptr;           // « wrapper » Flash où dessiner (Movie* à +0x18)
bool g_armNeedRelease = true;         // ignorer la touche qui a ouvert le menu
constexpr WORD kPadUp = 0x0001, kPadDown = 0x0002, kPadA = 0x1000, kPadB = 0x2000;

const char* SlotTitle(int slot)
{
    return slot == 1 ? Tr("Ceinture", "Belt") : slot == 2 ? Tr("Arme longue 1", "Long gun 1") : Tr("Arme longue 2", "Long gun 2");
}

WeaponDef* g_pendingBelt = nullptr;   // arme choisie pour la ceinture dans un menu
bool g_pendingBeltSet = false;

WeaponDef* SlotDef(int slot)
{
    return SlotRef(slot);
}

void BuildWeaponList(int slot)
{
    EnsureDefs();
    g_armSlot = slot;
    g_armList.clear();
    g_armList.push_back(nullptr);
    const int want = slot == 1 ? 1 : 2;
    for (auto& d : g_defs) {
        if (!d.ps || (d.cls != want && d.cls != 3)) continue;
        bool dup = false;
        for (WeaponDef* e : g_armList) if (e && strcmp(WeaponLabel(e), WeaponLabel(&d)) == 0) dup = true;
        if (!dup) g_armList.push_back(&d);
    }
    WeaponDef* cur = SlotDef(slot);
    g_armIndex = 0;
    for (size_t i = 0; i < g_armList.size(); ++i) if (g_armList[i] == cur) g_armIndex = static_cast<int>(i);
}

// level 0 = choix de l'emplacement ; slot >= 1 : directement la liste d'armes de cet emplacement.
void OpenArmory(void* movie, int slot)
{
    g_armMovie = movie;
    g_armNeedRelease = true;
    if (slot >= 1) { g_armLevel = 1; BuildWeaponList(slot); }
    else { g_armLevel = 0; g_armIndex = 0; }
    g_armOpen = true;
    LOG("armurerie : ouverte (%s)", slot >= 1 ? SlotTitle(slot) : "emplacements");
}

// Au retour en jeu (mise à jour du HUD, menus fermés) : remplace l'arme de la ceinture.
void RunPendingBelt()
{
    g_pendingBeltSet = false;
    if (true) return;   // ceinture désormais gérée par le mod (plus d'objet à créer)
    uint8_t* p = Player();
    void* inv = PlayerInventory();
    if (!p || !inv) return;
    g_pendingBeltSet = false;
    if (uint8_t* belt = SlotItem(p, kSlot1H)) {
        RememberIcon(belt);
        HookUnEquip(inv, kSlot1H, 1);
        g_itemDef.erase(belt);
    }
    WeaponDef* d = g_pendingBelt;
    if (d && HolsterHand() && CreateInHand(d)) ToBelt();
    LOG("armurerie : ceinture -> %s", d ? d->label : "(vide)");
}

void AssignArmory()
{
    uint8_t* p = Player();
    void* inv = PlayerInventory();
    if (!p || !inv) return;
    WeaponDef* d = g_armList[g_armIndex];
    // Aucun objet n'est créé depuis un menu (la garde-robe tourne dans le fil de l'interface : y
    // créer une arme a planté le jeu). La ceinture est remplie au retour en jeu (RunPendingBelt),
    // l'icône d'une arme longue est apprise la première fois qu'on la sort.
    SlotRef(g_armSlot) = d;
    if (g_handSlot == g_armSlot) g_handSlot = -1;   // l'arme en main n'est plus celle de l'emplacement
    LOG("armurerie : %s -> %s", SlotTitle(g_armSlot), d ? d->label : "(vide)");
}

void PushArmory()
{
    void* movie = g_armMovie;
    if (!movie) return;
    SetFlashNumber(movie, "_root.gtaArmOpen", g_armOpen.load() ? 1 : 0);
    if (!g_armOpen.load()) return;
    static char rows[9][128];
    int count = 0;
    if (g_armLevel == 0) {
        SetFlashString(movie, "_root.gtaArmTitle", Tr("Armes", "Weapons"));
        SetFlashString(movie, "_root.gtaArmHint", Tr("A : choisir     B : fermer", "A: select     B: close"));
        count = 3;
        for (int i = 0; i < 3; ++i) {
            WeaponDef* d = SlotDef(i + 1);
            sprintf_s(rows[i], "%s : %s", SlotTitle(i + 1), WeaponLabel(d));
        }
        for (int i = 3; i < 9; ++i) rows[i][0] = 0;
        for (int r = 0; r < 9; ++r) {
            char path[48];
            sprintf_s(path, "_root.gtaArmR%d", r);
            SetFlashString(movie, path, rows[r]);
        }
        SetFlashNumber(movie, "_root.gtaArmSel", g_armIndex);
        return;
    }
    SetFlashString(movie, "_root.gtaArmTitle", SlotTitle(g_armSlot));
    SetFlashString(movie, "_root.gtaArmHint", UsingKbm() ? Tr("Entree : equiper     Echap : retour", "Enter: equip     Esc: back")
                                                         : Tr("A : equiper     B : retour", "A: equip     B: back"));
    constexpr int kRows = 9;
    count = static_cast<int>(g_armList.size());
    int top = g_armIndex - kRows / 2;
    if (top > count - kRows) top = count - kRows;
    if (top < 0) top = 0;
    for (int r = 0; r < kRows; ++r) {
        char path[48];
        sprintf_s(path, "_root.gtaArmR%d", r);
        const int i = top + r;
        const char* txt = i < count ? WeaponLabel(g_armList[i]) : "";
        SetFlashString(movie, path, txt);
    }
    SetFlashNumber(movie, "_root.gtaArmSel", g_armIndex - top);
}

// Retourne vrai si le menu est ouvert.
bool UpdateArmory(const PadState& pad, bool padOk)
{
    if (!g_armOpen.load()) return false;
    static ULONGLONG repeatAt = 0;
    static bool keyUp = false, keyDown = false, keyOk = true, keyBack = true;
    const WORD b = padOk ? pad.buttons : 0;
    const bool stickUp = padOk && pad.ly > 16000, stickDown = padOk && pad.ly < -16000;
    const bool up = (b & kPadUp) || stickUp || (GetAsyncKeyState(VK_UP) & 0x8000);
    const bool down = (b & kPadDown) || stickDown || (GetAsyncKeyState(VK_DOWN) & 0x8000);
    const bool ok = (b & kPadA) || (GetAsyncKeyState(VK_RETURN) & 0x8000);
    const bool back = (b & kPadB) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) || (GetAsyncKeyState(VK_BACK) & 0x8000);
    if (g_armNeedRelease) {
        // La touche qui a ouvert le menu ne doit pas aussi valider la première ligne.
        keyOk = true;
        keyBack = true;
        g_armNeedRelease = false;
    }
    const ULONGLONG now = GetTickCount64();
    const int count = g_armLevel == 0 ? 3 : static_cast<int>(g_armList.size());
    auto step = [&](bool held, bool& was, int dir) {
        if (held && (!was || now >= repeatAt)) {
            g_armIndex = (g_armIndex + dir + count) % count;
            repeatAt = now + (was ? 110 : 350);
        }
        was = held;
    };
    step(up, keyUp, -1);
    step(down, keyDown, +1);
    if (ok && !keyOk) {
        if (g_armLevel == 0) {
            g_armLevel = 1;
            BuildWeaponList(g_armIndex + 1);
        } else {
            AssignArmory();
            if (g_armMovie == HudMovieCached()) g_armOpen = false;   // depuis la roue : on ferme
            else { g_armIndex = g_armSlot - 1; g_armLevel = 0; }     // garde-robe : retour aux emplacements
        }
    }
    if (back && !keyBack) {
        if (g_armLevel == 1 && g_armMovie != HudMovieCached()) { g_armIndex = g_armSlot - 1; g_armLevel = 0; }
        else { g_armOpen = false; LOG("armurerie : fermee"); }
    }
    keyOk = ok;
    keyBack = back;
    PushArmory();
    return true;
}

// ---- Garde-robe : ligne « ARMES » -----------------------------------------------------------
// Le Flash de la garde-robe (wardrobe.bin modifié) appelle ExternalInterface.call("GTA_ARM_HOVER", 0/1)
// quand la ligne ARMES est surlignée. Les appels passent par UIGfxExternalInterface::Callback
// (vtable 0x1A19F80, entrée 1) : on y récupère l'écran (Movie*) et on garde les autres messages au jeu.
// Quand ARMES est surlignée, A est retiré au jeu et ouvre l'armurerie dans la garde-robe.
constexpr uintptr_t kExtIfaceVtblRva = 0x1A19F80;
using ExtCallbackFn = void(__fastcall*)(void* self, void* movie, const char* method, uint8_t* args, uint32_t argc);
ExtCallbackFn g_origExtCallback = nullptr;
std::atomic<bool> g_armHover{false};
std::atomic<ULONGLONG> g_armHoverTick{0};   // le Flash renvoie le signal toutes les ~0,5 s
bool ArmHoverActive() { return g_armHover.load() && GetTickCount64() - g_armHoverTick.load() < 1500; }
// Le 2e paramètre du rappel est déjà l'« enveloppe » de la scène (Movie* à +0x18), comme celle du HUD.
void* g_wardrobeMovie = nullptr;

void __fastcall HookExtCallback(void* self, void* movie, const char* method, uint8_t* args, uint32_t argc)
{
    if (method && strncmp(method, "GTA_", 4) == 0) {
        g_wardrobeMovie = movie;
        if (strcmp(method, "GTA_LANG") == 0 && args && argc >= 1) {
            const uint32_t type = *reinterpret_cast<uint32_t*>(args + 0x18);
            if ((type & 0x8F) == 6) {
                const char* str = (type >> 6) & 1 ? **reinterpret_cast<const char***>(args + 0x20)
                                                  : *reinterpret_cast<const char**>(args + 0x20);
                if (str) {
                    const bool fr = strcmp(str, "fr") == 0;
                    if (fr != g_langFr.load()) LOG("langue du jeu : %s", fr ? "francais" : "anglais");
                    g_langFr = fr;
                }
            }
            return;
        }
        if (strcmp(method, "GTA_LOG") == 0 && args && argc >= 1) {
            const uint32_t type = *reinterpret_cast<uint32_t*>(args + 0x18);
            if ((type & 0x8F) == 6) {
                const char* str = (type >> 6) & 1 ? **reinterpret_cast<const char***>(args + 0x20)
                                                  : *reinterpret_cast<const char**>(args + 0x20);
                if (str) LOG("flash : %s", str);
            }
            return;
        }
        if (strcmp(method, "GTA_ARM_TICK") == 0) {
            // Appelé à chaque image par la garde-robe, dans le fil de l'interface : on y pilote le menu.
            static bool iconLoaded = false;
            if (!iconLoaded) { iconLoaded = true; LoadIconPack("Icons_Weapon_9MM", nullptr); }
            if (args && argc >= 1) {
                const uint32_t type = *reinterpret_cast<uint32_t*>(args + 0x18) & 0x8F;
                const double v = type == 5 ? *reinterpret_cast<double*>(args + 0x20)
                               : type == 3 || type == 4 ? *reinterpret_cast<int32_t*>(args + 0x20) : 0.0;
                const bool was = g_armHover.load();
                g_armHover = v != 0.0;
                g_armHoverTick = GetTickCount64();
                if (was != g_armHover.load()) LOG("armurerie : ligne ARMES %s", g_armHover.load() ? "surlignee" : "quittee");
            }
            UpdateWardrobeArmory();
            return;
        }
        if (strcmp(method, "GTA_ARM_HOVER") == 0 && args && argc >= 1) {
            const uint32_t type = *reinterpret_cast<uint32_t*>(args + 0x18) & 0x8F;
            const double v = type == 5 ? *reinterpret_cast<double*>(args + 0x20)
                           : type == 3 || type == 4 ? *reinterpret_cast<int32_t*>(args + 0x20) : 0.0;
            const bool was = g_armHover.load();
            g_armHover = v != 0.0;
            g_armHoverTick = GetTickCount64();
            if (was != g_armHover.load()) LOG("armurerie : ligne ARMES %s", g_armHover.load() ? "surlignee" : "quittee");
        }
        return;
    }
    g_origExtCallback(self, movie, method, args, argc);
}

void HookWardrobeInterface()
{
    auto** slot = reinterpret_cast<void**>(g_base + kExtIfaceVtblRva + 8);
    DWORD old = 0;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) return;
    g_origExtCallback = reinterpret_cast<ExtCallbackFn>(*slot);
    *slot = reinterpret_cast<void*>(&HookExtCallback);
    VirtualProtect(slot, 8, old, &old);
    LOG("armurerie : messages des ecrans Flash interceptes");
}

// Appelé à chaque lecture de la manette par le jeu (fil du jeu) : pilote la garde-robe.
void UpdateWardrobeArmory()
{
    PadState pad{};
    const bool padOk = ReadPad(pad);
    if (g_armOpen.load() && g_armMovie == g_wardrobeMovie) { UpdateArmory(pad, padOk); return; }
    static bool aWas = true;
    const bool a = (padOk && (pad.buttons & kPadA)) || (GetAsyncKeyState(VK_RETURN) & 0x8000);
    if (ArmHoverActive() && !g_armOpen.load() && a && !aWas && g_wardrobeMovie) {
        OpenArmory(g_wardrobeMovie, 0);
        PushArmory();
    }
    aWas = a;
}

// ---- Clavier / souris -----------------------------------------------------------------------
// Mêmes gestes qu'à la manette, sur les touches du jeu : R (recharger / ramasser, l'équivalent de
// RB) maintenu ouvre la roue, un appui court reste au jeu ; la souris choisit dans la roue (caméra
// figée), relâcher R valide ; clic gauche = armurerie en planque ;
// 1 à 4 = mains nues, ceinture, armes longues (touches libres dans le jeu). Tab reste la carte.
// Le jeu lit le clavier par WM_KEYDOWN et la caméra par Raw Input (WM_INPUT) : la fenêtre du jeu
// est sous-classée pour retenir ces messages quand ils sont pour le mod.
WNDPROC g_origWndProc = nullptr;
HWND g_gameWnd = nullptr;
std::atomic<ULONGLONG> g_rDownSince{0};       // R maintenu depuis (0 = relâché)
std::atomic<bool> g_rReleased{false};         // R relâché (à traiter par la roue)
std::atomic<int> g_letKeys{0};                // messages clavier rejoués pour le jeu
std::atomic<int> g_mouseX{0}, g_mouseY{0};    // déplacement de la souris pendant la roue
std::atomic<bool> g_mouseLeft{false}, g_mouseRight{false};   // clics pendant la roue
std::atomic<bool> g_lastInputKbm{false};      // dernier appareil utilisé : clavier/souris
std::atomic<bool> g_armKeysHeld{false};       // touches du menu ARMES gardées jusqu'au relâchement

bool IsArmoryKey(WPARAM k)
{
    return k == VK_UP || k == VK_DOWN || k == VK_LEFT || k == VK_RIGHT || k == VK_RETURN || k == VK_ESCAPE || k == VK_BACK;
}

LRESULT CALLBACK ModWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR) {
        if (msg != WM_CHAR) g_lastInputKbm = true;
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        // Rejoués par le mod (appui court sur R, sortie d'arme) : pour le jeu.
        if (msg != WM_CHAR && g_letKeys.load() > 0 && (wp == 'R' || wp == VK_DOWN)) {
            --g_letKeys;
            return CallWindowProcW(g_origWndProc, w, msg, wp, lp);
        }
        // Menu ARMES ouvert (ou sa ligne surlignée pour Entrée) : flèches, Entrée, Échap pour le mod
        // seulement, jusqu'au relâchement après la fermeture du menu.
        if (msg != WM_CHAR && IsArmoryKey(wp) && (g_armOpen.load() || g_armKeysHeld.load() || (wp == VK_RETURN && ArmHoverActive()))) {
            if (down) g_armKeysHeld = true;
            else if (!g_armOpen.load()) g_armKeysHeld = false;
            return 0;
        }
        if (msg == WM_CHAR && (wp == '\r' || wp == 27 || wp == 8) && (g_armOpen.load() || g_armKeysHeld.load())) return 0;
        // R : décidé par le mod (roue, ou appui court rejoué au jeu).
        if ((msg != WM_CHAR && wp == 'R') || (msg == WM_CHAR && (wp == 'r' || wp == 'R'))) {
            if (msg == WM_KEYDOWN && !(lp & (1 << 30))) { g_rDownSince = GetTickCount64(); g_wheelUsed = false; }
            if (msg == WM_KEYUP) { g_rDownSince = 0; g_rReleased = true; }
            return 0;
        }
    } else if (msg == WM_INPUT) {
        RAWINPUT ri{};
        UINT size = sizeof(ri);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)
            && ri.header.dwType == RIM_TYPEMOUSE) {
            g_lastInputKbm = true;
            if (g_wheelOpen.load() || g_armOpen.load()) {
                // Roue ouverte : la souris choisit, la caméra ne bouge pas, pas de tir.
                if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) { g_mouseX += ri.data.mouse.lLastX; g_mouseY += ri.data.mouse.lLastY; }
                if (ri.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) g_mouseLeft = true;
                if (ri.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) g_mouseRight = true;
                return DefWindowProcW(w, msg, wp, lp);   // libère le message sans le donner au jeu
            }
        }
    } else if ((msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP)
               && (g_wheelOpen.load() || g_armOpen.load())) {
        return 0;
    }
    return CallWindowProcW(g_origWndProc, w, msg, wp, lp);
}

// Appelé à chaque image avec la fenêtre au premier plan (celle du jeu) : (re)pose le sous-classement
// si la fenêtre a changé (passage plein écran / fenêtré).
void HookGameWindow(HWND fg)
{
    if (!fg || (fg == g_gameWnd && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(fg, GWLP_WNDPROC)) == &ModWndProc)) return;
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(fg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ModWndProc)));
    g_gameWnd = fg;
    LOG("clavier/souris : fenetre du jeu %p sous-classee", fg);
}

// Appui de touche rejoué pour le jeu : enfoncée maintenant, relâchée après holdMs.
struct KeyTap { WPARAM vk; ULONGLONG upAt; };
std::vector<KeyTap> g_keyTaps;

LPARAM KeyParam(WPARAM vk, bool up)
{
    LPARAM p = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16);
    if (vk == VK_DOWN) p |= 1 << 24;                    // touche étendue
    if (up) p |= (1LL << 30) | (1LL << 31);
    return p;
}

void TapKey(WPARAM vk, ULONGLONG holdMs)
{
    if (!g_gameWnd) return;
    g_letKeys += 2;
    PostMessageW(g_gameWnd, WM_KEYDOWN, vk, KeyParam(vk, false));
    g_keyTaps.push_back({ vk, GetTickCount64() + holdMs });
}

// Clavier/souris : dernier appareil utilisé, ou pas de manette branchée.
bool UsingKbm()
{
    PadState pad{};
    return g_lastInputKbm.load() || !ReadPad(pad);
}

void UpdateKeyTaps()
{
    const ULONGLONG now = GetTickCount64();
    for (size_t i = 0; i < g_keyTaps.size();) {
        if (now < g_keyTaps[i].upAt) { ++i; continue; }
        PostMessageW(g_gameWnd, WM_KEYUP, g_keyTaps[i].vk, KeyParam(g_keyTaps[i].vk, true));
        g_keyTaps.erase(g_keyTaps.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

// Touches simples (sans roue) : F5 ceinture, F6 arme longue 1, F7 mains nues, F8 arme longue 2.
struct WeaponKey { int vk; int choice; bool down; };
WeaponKey g_weaponKeys[] = {
    { VK_F5, 1, false }, { VK_F6, 2, false }, { VK_F7, 0, false }, { VK_F8, 3, false },
};

void UpdateWheel(void* hud)
{
    PadState pad{};
    const bool padOk = ReadPad(pad);
    // RB : la roue ne s'ouvre qu'après g_wheelHold secondes de maintien (un appui court reste au jeu).
    static ULONGLONG rbSince = 0;
    const bool rb = padOk && (pad.buttons & kPadWheel);
    const ULONGLONG nowMs = GetTickCount64();
    if (!rb) rbSince = 0;
    else if (!rbSince) rbSince = nowMs;
    const bool rbHeld = rb && (g_wheelOpen.load() || nowMs - rbSince >= static_cast<ULONGLONG>(g_wheelHold * 1000.f));
    // Clavier : R maintenu (comme RB) ; un appui court est rejoué au jeu (recharger / ramasser).
    const ULONGLONG rSince = g_rDownSince.load();
    const bool rHeld = rSince && (g_wheelOpen.load() || nowMs - rSince >= static_cast<ULONGLONG>(g_wheelHold * 1000.f));
    static ULONGLONG rTapAt = 0;
    if (g_rReleased.exchange(false) && !g_wheelUsed.load() && !g_wheelOpen.load()) {
        if (SlotItem(Player(), kSlotHand) && PickupTargeted()) {
            // Ramassage avec une arme en main : la tenue est lâchée d'abord, puis R rejoué.
            g_holsterForPickup = true;
            rTapAt = nowMs + 200;
        } else {
            TapKey('R', 90);
        }
    }
    if (rTapAt && nowMs >= rTapAt) { rTapAt = 0; TapKey('R', 90); }
    UpdateKeyTaps();
    const bool held = rbHeld || rHeld;
    static bool wasOpen = false;
    void* movie = HudMovie(hud);
    g_hudMovie = movie;
    if (g_armOpen.load() && g_armMovie == movie) { UpdateArmory(pad, padOk); PushWheel(movie, false); return; }
    if (movie) SetFlashNumber(movie, "_root.gtaArmOpen", 0);
    if (!held && !wasOpen) PushWheel(movie, false);   // préchargement des icônes
    if (held) {
        if (!wasOpen) {
            g_wheelUsed = true;
            g_wheelSel = -1;
            g_mouseX = 0;
            g_mouseY = 0;
            g_mouseLeft = false;
            g_mouseRight = false;
            LOG("armes : roue ouverte");
        }
        // Choix au stick droit seulement (zone morte), ou aux flèches.
        if (padOk) {
            static int logged = 0;
            float x = pad.rx / 32767.f, y = pad.ry / 32767.f;
            if (x * x + y * y > 0.2f) {
                if (fabsf(x) > fabsf(y)) g_wheelSel = x < 0 ? 1 : 2;
                else g_wheelSel = y > 0 ? 0 : 3;
                if (logged++ < 20) LOG("armes : roue, stick (%.2f, %.2f) -> case %d", x, y, g_wheelSel);
            }
        }
        // Souris : comme un stick, le déplacement cumulé (borné) désigne la case.
        {
            float mx = static_cast<float>(g_mouseX.load()), my = static_cast<float>(g_mouseY.load());
            const float len = sqrtf(mx * mx + my * my);
            constexpr float kMax = 300.f;
            if (len > kMax) { mx *= kMax / len; my *= kMax / len; g_mouseX = static_cast<int>(mx); g_mouseY = static_cast<int>(my); }
            if (len > 60.f) {
                if (fabsf(mx) > fabsf(my)) g_wheelSel = mx < 0 ? 1 : 2;
                else g_wheelSel = my < 0 ? 0 : 3;
            }
        }
        // Y (manette) : icône suivante pour « mains nues », enregistrée dans le .ini.
        static bool yWas = true;
        const bool y = padOk && (pad.buttons & 0x8000);
        if (y && !yWas) {
            g_fistChoice = (g_fistChoice + 1) % static_cast<int>(sizeof(kFistChoices) / sizeof(kFistChoices[0]));
            char buf[8];
            sprintf_s(buf, "%d", g_fistChoice);
            WritePrivateProfileStringA("Wheel", "FistIcon", buf, g_iniPath);
            LOG("armes : icone mains nues %d = %s", g_fistChoice, FistIcon());
        }
        yWas = y;
        // Dans une planque : A (clic gauche) sur un emplacement ouvre l'armurerie.
        static bool aWas = true;
        const bool a = (padOk && (pad.buttons & kPadA)) || g_mouseLeft.exchange(false);
        if (a && !aWas && g_wheelSel >= 1 && InSafehouse()) {
            aWas = a;
            wasOpen = false;
            g_wheelOpen = false;
            SlowMoStep(false);
            PushWheel(movie, false);
            OpenArmory(movie, g_wheelSel);
            PushArmory();
            return;
        }
        aWas = a;
        g_wheelOpen = true;
        SlowMoStep(true);
        PushWheel(movie, true);
        SetFlashNumber(movie, "_root.gtaWheelArmory", InSafehouse() ? 1 : 0);
        wasOpen = true;
        return;
    }
    if (wasOpen) {
        wasOpen = false;
        g_wheelOpen = false;
        SlowMoStep(false);
        PushWheel(movie, false);
        LOG("armes : roue fermee, choix %d", g_wheelSel);
        if (g_wheelSel >= 0) SelectWeapon(g_wheelSel);
    }
    // Touches 1 à 4 : mains nues, ceinture, armes longues (libres dans le jeu au clavier).
    static bool numWas[4] = { true, true, true, true };
    for (int k = 0; k < 4; ++k) {
        const bool down = (GetAsyncKeyState('1' + k) & 0x8000) != 0;
        if (down && !numWas[k] && !g_armOpen.load()) SelectWeapon(k);
        numWas[k] = down;
    }
    // Raccourcis clavier F5..F8 (la croix de la manette reste au jeu : la roue suffit).
    for (auto& k : g_weaponKeys) {
        const bool down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        if (down && !k.down) SelectWeapon(k.choice);
        k.down = down;
    }
}

__declspec(noinline) void __fastcall HookHudUpdate(void* hud)
{
    g_origHudUpdate(hud);
    static std::atomic<int> once{0};
    if (once.exchange(1) == 0) LOG("armes : mise a jour du HUD active (fil %lu)", GetCurrentThreadId());
    if (uint8_t* pl = Player()) {
        if (auto* node = *reinterpret_cast<uint8_t**>(pl + 0x58)) {
            reinterpret_cast<void(__fastcall*)(void*)>(g_base + 0x191CF0)(node);
            g_playerX.store(*reinterpret_cast<float*>(node + 0xB0));
            g_playerY.store(*reinterpret_cast<float*>(node + 0xB4));
            g_playerKnown.store(true);
        }
    }
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return;
    HookGameWindow(fg);
    static bool loadoutLoaded = false;
    if (!loadoutLoaded && Player() && PlayerInventory()) {
        loadoutLoaded = true;
        char buf[32];
        GetPrivateProfileStringA("Wheel", "SlowMo", "0.25", buf, sizeof(buf), g_iniPath);
        g_slowMo = static_cast<float>(atof(buf));
        GetPrivateProfileStringA("Wheel", "HoldTime", "0.5", buf, sizeof(buf), g_iniPath);
        g_wheelHold = static_cast<float>(atof(buf));
        // Par défaut l'inventaire du mod repart vide à chaque lancement (moins de cas tordus) ;
        // [Loadout] KeepBetweenSessions=1 garde les armes d'une session à l'autre.
        const bool keep = GetPrivateProfileIntA("Loadout", "KeepBetweenSessions", 0, g_iniPath) != 0;
        if (keep) LoadLoadout();
        else LOG("armes : inventaire vide au lancement");
        // L'arme que le jeu a remise à la ceinture (sauvegarde) : gardée si on conserve l'inventaire.
        if (SlotItem(Player(), kSlot1H)) {
            WeaponDef* s = ClearStaging();
            if (keep && s && !g_belt) g_belt = s;
        }
        if (uint8_t* h = SlotItem(Player(), kSlotHand)) {
            WeaponDef* d = DefOf(h);
            for (int k = 1; k <= 3; ++k) if (d && SlotRef(k) == d) { g_handSlot = k; break; }
        }
    }
    RestoreAutoEquip();
    KeepHiddenWeapons();
    KeepCustomModels();
    WatchThrownWeapon();
    RunDroppedCleanup();
    if (g_holsterForPickup.exchange(false)) {
        // Ramassage : l'arme tenue est lâchée (comme dans le jeu), la nouvelle prendra son emplacement.
        uint8_t* h = SlotItem(Player(), kSlotHand);
        void* inv = PlayerInventory();
        if (h && inv) {
            const int k = g_handSlot;
            LOG("armes : ramassage, %s lachee", LabelOf(h));
            HookUnEquip(inv, kSlotHand, 0);
            g_pickupSlot = k;
        }
    }
    RunPendingCreate();
    RunPendingBelt();
    UpdateWheel(hud);
    if (loadoutLoaded) UpdateFloorWeapons();
    static WeaponDef* savedLong[3] = { nullptr, nullptr, nullptr };
    if (loadoutLoaded && (savedLong[0] != g_long[0] || savedLong[1] != g_long[1] || savedLong[2] != g_belt)) {
        savedLong[0] = g_long[0];
        savedLong[1] = g_long[1];
        savedLong[2] = g_belt;
        SaveLoadout();
    }
    static bool f9 = false, f10 = false;
    const bool f9d = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    const bool f10d = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f10d && !f10) RecordSafehouse();
    if (f9d && !f9) {
        if (g_kit != KitStep::Idle) {
            LOG("armurerie : kit deja en cours");
        } else if (!InSafehouse()) {
            LOG("armurerie : pas dans une planque (%.1f, %.1f) - F10 pour enregistrer celle-ci",
                g_playerX.load(), g_playerY.load());
        } else {
            LOG("armurerie : kit demande");
            g_kit = KitStep::ClearHand;
        }
    }
    f9 = f9d;
    f10 = f10d;
    RunKitStep();
    static uint8_t* last[2] = {};
    uint8_t* p = Player();
    uint8_t* now[2] = { SlotItem(p, kSlotHand), SlotItem(p, kSlot1H) };
    if (memcmp(now, last, sizeof(now)) != 0) {
        memcpy(last, now, sizeof(now));
        LOG("armes [changement] main=%s ceinture=%s", LabelOf(now[0]), LabelOf(now[1]));
    }
}

void HookGameXInput()
{
    HookImport("XINPUT9_1_0.dll", "XInputGetState", reinterpret_cast<void*>(&GameXInputGetState),
               reinterpret_cast<void**>(&g_gameXInput));
    LOG("armes : manette du jeu %s", g_gameXInput ? "interceptee (RB maintenu = roue)" : "non interceptee");
}

// Messages de débogage du jeu (FUN_140002110, vide en version commerciale : 18 octets puis du
// remplissage). On les renvoie vers le journal, filtrés sur les armes / attaches.
int __cdecl GameDebugPrint(const char* fmt, ...)
{
    if (!fmt) return 0;
    static const char* keys[] = { "Stow", "Attach", "Inventory", "Equip", "Weapon", "weapon", "stow", "attach" };
    bool want = false;
    for (const char* k : keys) if (strstr(fmt, k)) { want = true; break; }
    if (!want) return 0;
    static std::atomic<int> count{0};
    if (count.fetch_add(1) > 2000) return 0;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    LOG("jeu : %s", buf);
    return 0;
}

void HookGameDebugPrint()
{
    const uint8_t expect[18] = { 0x48,0x89,0x54,0x24,0x10,0x4C,0x89,0x44,0x24,0x18,0x33,0xC0,0x4C,0x89,0x4C,0x24,0x20,0xC3 };
    if (PatchAbsJumpN(g_base + 0x2110, expect, sizeof(expect), reinterpret_cast<void*>(&GameDebugPrint)))
        LOG("armes : messages de debogage du jeu rediriges");
}

// ---- Diagnostic : qui remet le fusil en main ? -------------------------------------------
// Point d'arrêt matériel (registres de débogage) sur les cases « cible main » et « cible dos »
// du ciblage du joueur. F11 l'arme (dans le fil du jeu on note l'adresse et le fil, le fil du
// plugin pose les registres), chaque écriture est notée avec l'adresse du code et les appelants.
std::atomic<uintptr_t> g_watchA{0}, g_watchB{0};
std::atomic<DWORD> g_watchThread{0};
std::atomic<int> g_watchHits{0};

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    auto* ctx = ep->ContextRecord;
    if (!(ctx->Dr6 & 0x3)) return EXCEPTION_CONTINUE_SEARCH;
    const int hit = g_watchHits.fetch_add(1);
    if (hit < 40) {
        const uintptr_t end = g_base + 0x3000000;
        char callers[256] = "";
        size_t len = 0;
        auto* sp = reinterpret_cast<uintptr_t*>(ctx->Rsp);
        int found = 0;
        for (int i = 0; i < 400 && found < 6; ++i) {
            const uintptr_t v = sp[i];
            if (v > g_base + 0x1000 && v < end) {
                len += sprintf_s(callers + len, sizeof(callers) - len, " %llx", static_cast<unsigned long long>(v - g_base));
                ++found;
            }
        }
        LOG("surveillance : ecriture %s (rip %llx) valeur main=%u dos=%u, pile :%s",
            (ctx->Dr6 & 1) ? "main" : "dos", static_cast<unsigned long long>(ctx->Rip - g_base),
            *reinterpret_cast<uint8_t*>(g_watchA.load()), *reinterpret_cast<uint8_t*>(g_watchB.load()), callers);
    }
    ctx->Dr6 = 0;
    if (hit >= 40) ctx->Dr7 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmWatch()
{
    uint8_t* comps = CharComponents(Player());
    if (!comps) return;
    auto* ts = *reinterpret_cast<uint8_t**>(comps + 0x140);
    if (!ts) return;
    auto* map = *reinterpret_cast<uint8_t**>(ts + 0x60);
    if (!map) return;
    g_watchA = reinterpret_cast<uintptr_t>(map + 8 + 0x11);
    g_watchB = reinterpret_cast<uintptr_t>(map + 8 + 0x2B);
    g_watchHits = 0;
    g_watchThread = GetCurrentThreadId();
    LOG("surveillance : demandee sur %p / %p (fil %lu)", reinterpret_cast<void*>(g_watchA.load()),
        reinterpret_cast<void*>(g_watchB.load()), GetCurrentThreadId());
}

// Appelé par le fil du plugin : pose les registres de débogage sur le fil du jeu.
void ApplyWatchIfRequested()
{
    const DWORD tid = g_watchThread.exchange(0);
    if (!tid) return;
    static bool vehAdded = false;
    if (!vehAdded) { AddVectoredExceptionHandler(1, WatchHandler); vehAdded = true; }
    HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!th) { LOG("surveillance : fil introuvable"); return; }
    SuspendThread(th);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(th, &c)) {
        c.Dr0 = g_watchA.load();
        c.Dr1 = g_watchB.load();
        // L0 + L1, écriture (RW=01), longueur 1 octet (LEN=00)
        c.Dr7 = (1ull << 0) | (1ull << 2) | (1ull << 16) | (1ull << 20);
        SetThreadContext(th, &c);
        LOG("surveillance : active");
    }
    ResumeThread(th);
    CloseHandle(th);
}

void InstallWeapons()
{
    HookGameDebugPrint();
    HookGameXInput();
    HookWardrobeInterface();
    int ok = 0;
    ok += Detour("equiper", kEquipRva, kEquipPro, sizeof(kEquipPro), reinterpret_cast<void*>(&HookEquip), g_origEquip);
    ok += Detour("retirer", kUnEquipRva, kUnEquipPro, sizeof(kUnEquipPro), reinterpret_cast<void*>(&HookUnEquip), g_origUnEquip);
    ok += Detour("ranger", kStowRva, kStowPro, sizeof(kStowPro), reinterpret_cast<void*>(&HookStow), g_origStow);
    ok += Detour("hud", kHudUpdateRva, kHudUpdatePro, sizeof(kHudUpdatePro), reinterpret_cast<void*>(&HookHudUpdate), g_origHudUpdate);
    LOG("armes : %d/4 detours installes (RB maintenu / Tab roue, F5 ceinture, F6/F8 armes longues, F7 mains nues, F9 kit, F10 planque)", ok);
    LoadArmory();
}

}  // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, void* outer)
{
    if (!g_realCreate) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\dinput8.dll");
        if (HMODULE h = LoadLibraryW(path))
            g_realCreate = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(h, "DirectInput8Create"));
    }
    return g_realCreate ? g_realCreate(inst, ver, riid, out, outer) : E_FAIL;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        g_log = _fsopen("SleepingDogsRemaster.log", "w", _SH_DENYNO);
        GetFullPathNameA("SleepingDogsRemaster.ini", MAX_PATH, g_iniPath, nullptr);
        LOG("Sleeping Dogs Remaster charge");
        nw::SetLog([](const char* s) { LOG("%s", s); });
        EnsureMinimapIni();
        InstallMinimap();
        InstallWeapons();
        if (SkipVanillaHudIntro()) LOG("hud : ancien ath masque des la sortie de menu");
        else LOG("hud : ancien ath toujours force par le jeu");
        if (GetPrivateProfileIntA("Debug", "LogFiles", 0, g_iniPath)) {
            HookImport("KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&LogCreateFileA), reinterpret_cast<void**>(&g_realCreateFileA));
            HookImport("KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(&LogCreateFileW), reinterpret_cast<void**>(&g_realCreateFileW));
            LOG("journal des fichiers ouverts : actif");
        }
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running = false;
    }
    return TRUE;
}
