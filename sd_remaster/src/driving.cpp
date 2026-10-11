// Conduite plus réaliste. Le jeu recopie les réglages de chaque véhicule (fiche CarPhysicsDef) dans
// son véhicule Havok à l'apparition : on modifie donc directement le véhicule Havok de la voiture
// conduite par le joueur (valeurs d'origine gardées, remises à la sortie : la circulation garde son
// comportement). Les valeurs sont réécrites toutes les 0,5 s depuis l'origine (le jeu peut en
// rétablir certaines).
//
// Chemin (structures du SD-SDK, aucune fonction du SDK appelée) : joueur -> CharacterOccupantComponent
// -> véhicule -> PhysicsMoverInterface -> PhysicsWheeledVehicle -> VehicleInstance (hkpVehicleInstance) :
//   m_data (hkpVehicleData) : adhérence par roue, roulis / tangage / lacet, inertie, égaliseur ;
//   m_suspension (hkpVehicleDefaultSuspension) : ressorts, amortisseurs ;
//   m_brake (hkpVehicleDefaultBrake) : couple de freinage ;
//   m_velocityDamper (hkpVehicleDefaultVelocityDamper) : amortissement artificiel des rotations ;
//   PhysicsVehicle::mReorientActionRoll / Pitch (hkpReorientAction) : aides anti-tonneau.
// Chaque objet Havok est vérifié par son type RTTI avant d'y toucher.
#include "../../external/SD-SDK/sdhd_sdk.hh"
#include "driving.h"
#include "newweapons.h"

#include <cmath>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <string>
#include <mutex>
#include <vector>

using namespace UFG;

namespace drv {
namespace {

LogFn g_logFn = nullptr;

void Log(const char* fmt, ...)
{
    if (!g_logFn) return;
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_logFn(buf);
}

bool Ok(const void* p, size_t n) { return p && nw::IsReadable(p, n); }

// Nom RTTI (".?AVhkpVehicleData@@") d'un objet polymorphe, ou "".
const char* RttiName(const void* obj)
{
    if (!Ok(obj, 8)) return "";
    auto** vtbl = *static_cast<void** const*>(obj);
    if (!Ok(vtbl - 1, 8)) return "";
    auto* col = static_cast<const uint8_t*>(vtbl[-1]);
    if (!Ok(col, 0x18)) return "";
    const uint32_t tdRva = *reinterpret_cast<const uint32_t*>(col + 0x0C);
    auto* td = reinterpret_cast<const char*>(SDK::gBaseAddress + tdRva);
    if (!Ok(td, 0x40)) return "";
    return td + 0x10;
}

bool Is(const void* obj, const char* name) { return strcmp(RttiName(obj), name) == 0; }

struct VelocityDamper {   // hkpVehicleDefaultVelocityDamper
    void* vtbl;
    u16 memSize;
    i16 refCount;
    f32 normalSpinDamping;
    f32 collisionSpinDamping;
    f32 collisionThreshold;
};

// Une valeur modifiée : origine gardée, valeur écrite = origine x facteur (bornée).
struct Field {
    float* p;
    float orig;
    float mul;
    float maxV;
    float written = NAN;
};

struct Car {
    void* vehicle = nullptr;           // hkpVehicleInstance
    std::vector<Field> fields;
    // Couple : m_maxTorque et la courbe de couple du jeu (mTorqueTable), réglés (tuning) puis
    // multipliés pendant la nitro.
    std::vector<float*> torque;
    std::vector<float> torqueBase;     // valeurs réglées hors nitro
    float* governor = nullptr;         // bridage de vitesse (km/h)
    // Bridage : le jeu le change lui-même selon la situation (20 km/h par moments) et le rétablit sans
    // cesse. Le mod suit la valeur du jeu (prise chaque fois qu'elle diffère de ce qu'il a écrit) et
    // écrit valeur du jeu x multiplicateur, à chaque image.
    float govGame = 0.f;               // dernière valeur posée par le jeu
    float govGameMax = 0.f;            // la plus haute vue (statistiques)
    float govMul = 1.f;
    float govWritten = -1.f;
    float gearVmax = 0.f;              // limite de la boîte d'origine (km/h)
    float topSpeed = 1.f;
    float baseVmax = 0.f;              // vitesse max sans tuning (km/h)
    float* primary = nullptr;          // rapport de pont (transmission) : nitro
    float primaryBase = 0.f;
    VehicleLookComponent* look = nullptr;
    // Peinture : surcharge de matériau « StateBlock.<nom>_VehicleLook » posée par le jeu sur le
    // composant d'affichage (VehicleLookComponent::OnUpdateMaterial, 0x5880B0, nom tiré au hasard dans
    // la liste de peintures du modèle). On déplace son lien de ressource vers une autre peinture.
    uint8_t* paintEntry = nullptr;     // entrée de surcharge (0x68 octets) ; lien de ressource à +0x08
    std::vector<std::string> paints;   // noms des peintures du modèle (diagnostic)
    uint8_t* paintCopy = nullptr;      // copie de la peinture d'origine, couleur du mod
    int paintNow = -2;                 // peinture posée par le mod (-2 : aucune)
    uint8_t* paintOrigData = nullptr;  // peinture d'origine (ressource, uid)
    u32 paintOrigUid = 0;
    uint8_t* obj = nullptr;            // objet du véhicule (registre des peintures)
    u32 objName = 0;
    PhysicsMoverInterface* mover = nullptr; // vitesse
    VehicleEffectsComponent* effects = nullptr;   // effets du véhicule (flammes de la nitro)
    char model[32] = "";
    char modelName[64] = "";
};
Car g_car;
ULONGLONG g_nextWrite = 0;

// Remise à l'origine : seulement les valeurs encore telles que le mod les a écrites (si le véhicule a
// été détruit entre-temps, sa mémoire n'est plus la sienne : on n'y écrit rien).
void Write(Car& c, bool restore)
{
    for (auto& f : c.fields) {
        if (!Ok(f.p, 4)) continue;
        if (restore) {
            if (*f.p == f.written) *f.p = f.orig;
            continue;
        }
        float v = f.orig * f.mul;
        if (f.maxV > 0.f && v > f.maxV && f.orig <= f.maxV) v = f.maxV;
        *f.p = v;
        f.written = v;
    }
}

void Add(Car& c, float& field, float mul, float maxV = 0.f) { c.fields.push_back({ &field, field, mul, maxV }); }

template <typename F> F GameFn(uptr rva);
PhysicsMoverInterface* g_lastMover = nullptr;
struct Car;
void FindPaint(Car& c, SimObjectVehicle* veh);
bool SetPaint(Car& c, int index);

PhysicsWheeledVehicle* PlayerCar(void* player, SimObjectVehicle** outVeh = nullptr)
{
    auto* ch = static_cast<SimObjectCharacter*>(player);
    if (!Ok(ch, sizeof(SimObjectCharacter)) || !Ok(ch->m_Components.p, 0x10 * (Character_CharacterOccupantComponent + 1))) return nullptr;
    auto* occ = ch->GetCharacterOccupant();
    if (!Ok(occ, sizeof(CharacterOccupantComponent))) return nullptr;
    auto* veh = occ->GetCurrentVehicle();
    if (!Ok(veh, sizeof(SimObjectVehicle)) || !Ok(veh->m_Components.p, 0x10 * (Vehicle_PhysicsMoverInterface + 1))) return nullptr;
    auto* mover = veh->GetPhysicsMoverInterface();
    if (!Ok(mover, sizeof(PhysicsMoverInterface))) return nullptr;
    auto* pv = mover->mPhysicsVehicle;
    if (!Ok(pv, sizeof(PhysicsVehicle)) || !Ok(pv->mDnaDefinition.mData, 0x20) || !(pv->IsPhysicsCar() || pv->IsPhysicsBike())) return nullptr;
    auto* wv = static_cast<PhysicsWheeledVehicle*>(pv);
    if (outVeh) *outVeh = veh;
    g_lastMover = mover;
    return Ok(wv, sizeof(PhysicsWheeledVehicle)) ? wv : nullptr;
}

// Composant d'apparence du véhicule (VehicleLookComponent, type 0xCC...) : couleurs (teintes).
VehicleLookComponent* LookOf(SimObjectVehicle* veh)
{
    if (!veh) return nullptr;
    const u32 n = veh->m_Components.size;
    if (n == 0 || n > 64 || !Ok(veh->m_Components.p, n * sizeof(SimComponentHolder))) return nullptr;
    for (u32 i = 0; i < n; ++i) {
        SimComponent* c = veh->m_Components.p[i].m_pComponent;
        const u32 uid = veh->m_Components.p[i].m_TypeUID;
        if (!c || (uid & 0xFE000000) != 0xCC000000) continue;
        if (Ok(c, sizeof(VehicleLookComponent))) return reinterpret_cast<VehicleLookComponent*>(c);
    }
    return nullptr;
}

void Capture(Car& c, PhysicsWheeledVehicle* wv, SimObjectVehicle* veh, const Settings& sIn)
{
    // Moto : seulement le tuning (moteur, freins, adhérence, nitro, vitesse de pointe). Les réglages de
    // conduite réaliste (roulis, aides anti-tonneau, rotations, suspension) la feraient tomber.
    Settings s = sIn;
    const bool bike = wv->IsPhysicsBike();
    if (bike) {
        s.grip = s.rearGrip = 1.f;
        s.bodyRoll = s.antiRoll = s.suspension = s.damping = s.spinDamping = s.equalizer = s.extraTorque = 1.f;
        s.brakes = 1.f;
    }
    c = {};
    c.mover = g_lastMover;
    if (VehicleEffectsComponent* fx = veh->GetVehicleEffects(); Ok(fx, sizeof(VehicleEffectsComponent)) && Is(fx, ".?AVVehicleEffectsComponent@UFG@@"))
        c.effects = fx;
    auto* vi = wv->mHavokVehicle;
    if (!Ok(vi, sizeof(hkpVehicleInstance))) return;
    c.vehicle = vi;
    std::string what;
    // Clé du tuning : ce véhicule précis (nom de l'objet sur la carte, symbole à +0x48) et son modèle.
    const u32 objName = Ok(veh, 0x50) ? *reinterpret_cast<u32*>(reinterpret_cast<uint8_t*>(veh) + 0x48) : 0;
    char baseKey[32];
    snprintf(baseKey, sizeof(baseKey), "%016llX-%08X", static_cast<unsigned long long>(wv->mDnaDefinition.mNameUID), objName);
    // Plusieurs véhicules du même modèle portent le même nom d'objet : la clé est attribuée à chaque
    // véhicule de la partie, dans l'ordre où le joueur les prend (le premier « M », puis « M#2 », ...).
    // Un autre exemplaire du modèle n'hérite donc plus du tuning d'un véhicule déjà pris.
    {
        struct Claim { uint8_t* obj; void* vtbl; std::string key; };
        static std::vector<Claim> claims;
        auto* o = reinterpret_cast<uint8_t*>(veh);
        void* vt = Ok(o, 8) ? *reinterpret_cast<void**>(o) : nullptr;
        const Claim* mine = nullptr;
        for (const auto& cl : claims)
            if (cl.obj == o && cl.vtbl == vt && cl.key.compare(0, strlen(baseKey), baseKey) == 0) { mine = &cl; break; }
        if (!mine) {
            std::string key;
            for (int n = 1; n < 100; ++n) {
                key = baseKey;
                if (n > 1) key += "#" + std::to_string(n);
                bool used = false;
                for (const auto& cl : claims) if (cl.key == key) { used = true; break; }
                if (!used) break;
            }
            if (claims.size() > 400) claims.erase(claims.begin());
            claims.push_back({ o, vt, key });
            mine = &claims.back();
        }
        snprintf(c.model, sizeof(c.model), "%s", mine->key.c_str());
    }
    if (auto* nm = GameFn<const char* (__fastcall*)(u32)>(0x16F0C0)(objName); Ok(nm, 2))
        Log("conduite : vehicule \"%s\" (cle %s)", nm, c.model);
    if (Ok(wv->mParams, sizeof(VehicleParams)) && Ok(wv->mParams->modelName.mData, 4))
        snprintf(c.modelName, sizeof(c.modelName), "%s", wv->mParams->modelName.mData);
    if (VehicleLookComponent* look = LookOf(veh)) {
        const qColour* t = look->mColourTints;
        if (Ok(t, 4 * sizeof(qColour))) {
            Log("conduite : %s (%s), peinture %d vitres %d, teintes (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)",
                c.modelName, c.model, look->mVehiclePaintID, look->mVehicleGlassID, t[0].r, t[0].g, t[0].b,
                t[1].r, t[1].g, t[1].b, t[2].r, t[2].g, t[2].b, t[3].r, t[3].g, t[3].b);

        } else {
            Log("conduite : %s (%s), peinture %d, pas de teintes", c.modelName, c.model, look->mVehiclePaintID);
        }
        c.look = look;
    } else {
        Log("conduite : %s (%s), pas de composant d'apparence", c.modelName, c.model);
    }
    if (auto* d = vi->m_data; Is(d, ".?AVhkpVehicleData@@") && Ok(d, sizeof(hkpVehicleData))) {
        const int n = d->m_wheelParams.m_size;
        if (n > 0 && n <= 8 && Ok(d->m_wheelParams.m_data, n * sizeof(hkpVehicleData::WheelComponentParams))) {
            for (int i = 0; i < n; ++i) {
                auto& w = d->m_wheelParams.m_data[i];
                const float g = s.grip * s.tuneGrip * (w.m_axle != 0 ? s.rearGrip : 1.f);
                Add(c, w.m_friction, g);
                Add(c, w.m_maxFriction, g);
            }
        }
        Log("conduite : origine roulis %.3f tangage %.3f lacet %.3f couple+ %.3f inertie %.2f/%.2f/%.2f egaliseur %.2f adherence %.2f (%d roues)",
            d->m_torqueRollFactor, d->m_torquePitchFactor, d->m_torqueYawFactor, d->m_extraTorqueFactor,
            d->m_chassisUnitInertiaYaw, d->m_chassisUnitInertiaRoll, d->m_chassisUnitInertiaPitch, d->m_frictionEqualizer,
            n > 0 && d->m_wheelParams.m_data ? d->m_wheelParams.m_data[0].m_friction : 0.f, n);
        Add(c, d->m_torqueRollFactor, s.bodyRoll, 1.f);
        Add(c, d->m_torquePitchFactor, s.bodyRoll, 1.f);
        Add(c, d->m_extraTorqueFactor, s.extraTorque);
        Add(c, d->m_frictionEqualizer, s.equalizer);
        what += " pneus";
    }
    if (auto* su = reinterpret_cast<hkpVehicleDefaultSuspension*>(vi->m_suspension); Is(su, ".?AVhkpVehicleDefaultSuspension@@")) {
        const int n = su->m_wheelSpringParams.m_size;
        if (n > 0 && n <= 8 && Ok(su->m_wheelSpringParams.m_data, n * sizeof(hkpVehicleDefaultSuspension::WheelSpringSuspensionParameters))) {
            auto& w0 = su->m_wheelSpringParams.m_data[0];
            Log("conduite : origine ressort %.1f amortisseurs %.2f / %.2f", w0.m_strength, w0.m_dampingCompression, w0.m_dampingRelaxation);
            for (int i = 0; i < n; ++i) {
                auto& w = su->m_wheelSpringParams.m_data[i];
                Add(c, w.m_strength, s.suspension);
                Add(c, w.m_dampingCompression, s.damping);
                Add(c, w.m_dampingRelaxation, s.damping);
            }
            what += " suspension";
        }
    } else {
        Log("conduite : suspension de type %s, non modifiee", RttiName(vi->m_suspension));
    }
    // Freins du jeu (UFG::Brakes, dérivé du frein Havok par défaut).
    if (auto* br = reinterpret_cast<hkpVehicleDefaultBrake*>(vi->m_brake); Is(br, ".?AVhkpVehicleDefaultBrake@@") || Is(br, ".?AVBrakes@UFG@@")) {
        const int n = br->m_wheelBrakingProperties.m_size;
        if (n > 0 && n <= 8 && Ok(br->m_wheelBrakingProperties.m_data, n * sizeof(hkpVehicleDefaultBrake::WheelBrakingProperties))) {
            for (int i = 0; i < n; ++i) Add(c, br->m_wheelBrakingProperties.m_data[i].m_maxBreakingTorque, s.brakes * s.tuneBrakes);
            what += " freins";
        }
    } else {
        Log("conduite : freins de type %s, non modifies", RttiName(vi->m_brake));
    }
    if (auto* vd = reinterpret_cast<VelocityDamper*>(vi->m_velocityDamper); Is(vd, ".?AVhkpVehicleDefaultVelocityDamper@@")) {
        Log("conduite : origine amortissement des rotations %.2f (collision %.2f)", vd->normalSpinDamping, vd->collisionSpinDamping);
        Add(c, vd->normalSpinDamping, s.spinDamping);
        what += " rotations";
    } else {
        Log("conduite : amortisseur de rotation de type %s, non modifie", RttiName(vi->m_velocityDamper));
    }
    for (hkpReorientAction* ra : { wv->mReorientActionRoll, wv->mReorientActionPitch }) {
        if (!Is(ra, ".?AVhkpReorientAction@@")) continue;
        Add(c, ra->m_strength, s.antiRoll);
        Add(c, ra->m_damping, s.antiRoll);
        what += " anti-tonneau";
    }
    FindPaint(c, veh);
    // Moteur du jeu (UFG::Engine) : couple (tuning, nitro) et vitesse de pointe (nitro).
    if (auto* en = reinterpret_cast<Engine*>(vi->m_engine); Is(en, ".?AVEngine@UFG@@") && Ok(en, sizeof(Engine))) {
        const int nt = en->mTorqueTable.size;
        const bool table = nt > 0 && nt <= 64 && Ok(en->mTorqueTable.p, nt * sizeof(f32));
        Log("conduite : origine couple %.0f (courbe %d points%s%.0f), regime %.0f-%.0f, bridage %.0f km/h", en->m_maxTorque, nt,
            table ? ", max " : "", table ? *std::max_element(en->mTorqueTable.p, en->mTorqueTable.p + nt) : 0.f,
            en->m_minRPM, en->m_maxRPM, en->mGovernorSpeedKPH);
        Add(c, en->m_maxTorque, s.engine);
        c.torque.push_back(&en->m_maxTorque);
        c.torqueBase.push_back(en->m_maxTorque * s.engine);
        if (table) {
            for (int i = 0; i < nt; ++i) {
                Add(c, en->mTorqueTable.p[i], s.engine);
                c.torque.push_back(&en->mTorqueTable.p[i]);
                c.torqueBase.push_back(en->mTorqueTable.p[i] * s.engine);
            }
        }
        // Résistance du moteur selon la vitesse (UFG::Engine : de ResistanceMin à ResistanceMax entre
        // ResistanceMinSpeed et ResistanceMaxSpeed) : un limiteur doux (la moto plafonnait puis ralentissait).
        // Vitesses repoussées et résistance réduite avec le niveau du moteur.
        Log("conduite : resistance moteur %.3f -> %.3f entre %.0f et %.0f ; facteurs regime %.3f / %.3f / %.3f",
            en->mEngineResistanceMin, en->mEngineResistanceMax, en->mEngineResistanceMinSpeed, en->mEngineResistanceMaxSpeed,
            en->m_resistanceFactorAtMinRPM, en->m_resistanceFactorAtOptRPM, en->m_resistanceFactorAtMaxRPM);
        {
            const float k = (std::max)(0.2f, s.vmaxMul * s.topSpeed);
            Add(c, en->mEngineResistanceMinSpeed, k);
            Add(c, en->mEngineResistanceMaxSpeed, k);
            Add(c, en->mEngineResistanceMin, 1.f / k);
            Add(c, en->mEngineResistanceMax, 1.f / k);
        }
        // Bridage : réglage général x niveau du moteur.
        c.governor = &en->mGovernorSpeedKPH;
        c.govGame = c.govGameMax = en->mGovernorSpeedKPH;
        c.govMul = s.topSpeed * s.vmaxMul;
        c.topSpeed = s.topSpeed;
        what += " moteur";
        // Boîte : au régime max sur le dernier rapport, la vitesse est plafonnée (la moto ne dépassait pas
        // 105 km/h quel que soit le couple). Le niveau du moteur allonge le rapport de pont d'autant
        // (le couple est augmenté d'autant par ailleurs : mêmes reprises).
        float gearVmax = 0.f;
        auto* tr = reinterpret_cast<hkpVehicleDefaultTransmission*>(vi->m_transmission);
        if ((Is(tr, ".?AVTransmission@UFG@@") || Is(tr, ".?AVhkpVehicleDefaultTransmission@@")) && Ok(tr, sizeof(hkpVehicleDefaultTransmission))) {
            const int ng = tr->m_gearsRatio.m_size;
            const float radius = Ok(vi->m_data, sizeof(hkpVehicleData)) && vi->m_data->m_wheelParams.m_size > 0 && Ok(vi->m_data->m_wheelParams.m_data, sizeof(hkpVehicleData::WheelComponentParams))
                ? vi->m_data->m_wheelParams.m_data[0].m_radius : 0.f;
            if (ng > 0 && ng <= 12 && Ok(tr->m_gearsRatio.m_data, ng * sizeof(f32)) && tr->m_primaryTransmissionRatio > 0.f && radius > 0.f) {
                const float top = tr->m_gearsRatio.m_data[ng - 1];
                if (top > 0.f) gearVmax = en->m_maxRPM / (top * tr->m_primaryTransmissionRatio) * radius * 6.2831853f / 60.f * 3.6f;
            }
            Log("conduite : boite %d rapports, pont %.2f, dernier %.2f, roue %.2f m -> %.0f km/h au regime max",
                ng, tr->m_primaryTransmissionRatio, ng > 0 && tr->m_gearsRatio.m_data ? tr->m_gearsRatio.m_data[ng - 1] : 0.f, radius, gearVmax);
            Add(c, tr->m_primaryTransmissionRatio, 1.f / (std::max)(0.2f, s.vmaxMul));
            c.primary = &tr->m_primaryTransmissionRatio;
            c.primaryBase = tr->m_primaryTransmissionRatio / (std::max)(0.2f, s.vmaxMul);
            what += " boite";
        }
        // Air : la puissance nécessaire croît comme la traînée x v³. Avec le couple x vmaxMul (même
        // régime, boîte allongée : puissance x vmaxMul), traînée / vmaxMul² -> vitesse max x vmaxMul.
        auto* ae = reinterpret_cast<hkpVehicleDefaultAerodynamics*>(vi->m_aerodynamics);
        if ((Is(ae, ".?AVVehicleAerodynamics@UFG@@") || Is(ae, ".?AVhkpVehicleDefaultAerodynamics@@")) && Ok(ae, sizeof(hkpVehicleDefaultAerodynamics))) {
            Log("conduite : air : trainee %.3f, surface %.2f, densite %.2f", ae->m_dragCoefficient, ae->m_frontalArea, ae->m_airDensity);
            Add(c, ae->m_dragCoefficient, 1.f / ((std::max)(0.2f, s.vmaxMul) * (std::max)(0.2f, s.vmaxMul)));
            what += " air";
        } else {
            Log("conduite : aerodynamique de type %s, non modifiee", RttiName(vi->m_aerodynamics));
        }
        c.gearVmax = gearVmax;
    } else {
        Log("conduite : moteur de type %s, non modifie", RttiName(vi->m_engine));
    }
    Log("conduite : %s %p regle (%d valeurs :%s)", bike ? "moto" : "vehicule", vi, static_cast<int>(c.fields.size()), what.c_str());
}

bool g_boost = false;
float g_boostMul = 1.f;

// Palette : couleur principale (valeurs 0..15 du bloc de peinture) et seconde (16..31), proches des
// peintures du jeu (« WhiteDarkerPaint » : 0,63).
struct PaletteColor { const char* fr; const char* en; float r, g, b; };
const PaletteColor kPalette[] = {
    { "Blanc", "White", 0.80f, 0.80f, 0.80f },
    { "Blanc perle", "Pearl white", 0.72f, 0.70f, 0.64f },
    { "Argent", "Silver", 0.50f, 0.51f, 0.53f },
    { "Gris", "Grey", 0.22f, 0.22f, 0.23f },
    { "Noir", "Black", 0.015f, 0.015f, 0.017f },
    { "Rouge", "Red", 0.55f, 0.02f, 0.02f },
    { "Rouge sombre", "Dark red", 0.22f, 0.01f, 0.012f },
    { "Orange", "Orange", 0.75f, 0.20f, 0.01f },
    { "Jaune", "Yellow", 0.78f, 0.58f, 0.02f },
    { "Or", "Gold", 0.55f, 0.38f, 0.10f },
    { "Vert citron", "Lime green", 0.30f, 0.62f, 0.03f },
    { "Vert", "Green", 0.03f, 0.30f, 0.07f },
    { "Vert foncé", "Dark green", 0.01f, 0.09f, 0.03f },
    { "Bleu ciel", "Sky blue", 0.18f, 0.42f, 0.70f },
    { "Bleu", "Blue", 0.02f, 0.10f, 0.52f },
    { "Bleu nuit", "Midnight blue", 0.008f, 0.015f, 0.10f },
    { "Violet", "Purple", 0.22f, 0.03f, 0.42f },
    { "Rose", "Pink", 0.80f, 0.18f, 0.42f },
    { "Marron", "Brown", 0.16f, 0.07f, 0.03f },
    { "Bronze", "Bronze", 0.36f, 0.20f, 0.08f },
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

constexpr u32 kStateBlockType = 0x4D04C7F2;     // type de ressource « StateBlock »
constexpr u32 kVehicleLookParam = 0xC6D01635;   // paramètre de matériau de la surcharge

template <typename F> F GameFn(uptr rva) { return reinterpret_cast<F>(SDK::gBaseAddress + rva); }

// Composant d'affichage (CompositeDrawableComponent) d'un objet du jeu, comme le fait le jeu.
struct PaintReg { uint8_t* obj; u32 name; uint8_t* copy; uint8_t* orig; u32 uid; void* vtbl; };
std::vector<PaintReg> g_paintReg;
std::recursive_mutex g_paintLock;   // le jeu peut reconstruire un affichage hors de la boucle du HUD

PaintReg* FindReg(uint8_t* obj, u32 name)
{
    for (auto& r : g_paintReg) if (r.obj == obj && r.name == name && Ok(obj, 8) && *reinterpret_cast<void**>(obj) == r.vtbl) return &r;
    return nullptr;
}

uint8_t* DrawableOfObj(uint8_t* obj)
{
    if (!Ok(obj, 0x70)) return nullptr;
    const u16 f = *reinterpret_cast<u16*>(obj + 0x4C);
    uint8_t* comps = *reinterpret_cast<uint8_t**>(obj + 0x68);
    if (((f >> 14) & 1) || static_cast<i16>(f) < 0) return Ok(comps, 0xE8) ? *reinterpret_cast<uint8_t**>(comps + 0xE0) : nullptr;
    if ((f >> 13) & 1) return Ok(comps, 0x98) ? *reinterpret_cast<uint8_t**>(comps + 0x90) : nullptr;
    return GameFn<uint8_t* (__fastcall*)(void*, u32)>(0x190A60)(obj, *reinterpret_cast<u32*>(SDK::gBaseAddress + 0x20212FC));
}

void FindPaint(Car& c, SimObjectVehicle* veh)
{
    auto* obj = reinterpret_cast<uint8_t*>(veh);
    if (!Ok(obj, 0x70)) return;
    c.obj = obj;
    c.objName = *reinterpret_cast<u32*>(obj + 0x48);
    // Liste des peintures : propriété (symbole 0x240D590) du jeu de propriétés de l'objet.
    uint8_t* sop = *reinterpret_cast<uint8_t**>(obj + 0x50);
    uint8_t* props = nullptr;
    if (Ok(sop, 0xA0)) {
        props = *reinterpret_cast<uint8_t**>(sop + 0x90);
        if (!props) props = *reinterpret_cast<uint8_t**>(sop + 0x98);
    }
    if (props) {
        auto* list = GameFn<uint8_t* (__fastcall*)(void*, void*, int)>(0x1E9ED0)(props, reinterpret_cast<void*>(SDK::gBaseAddress + 0x240D590), 1);
        if (Ok(list, 0x30) && *reinterpret_cast<int*>(list + 0x18) == 0xC) {
            const u32 n = *reinterpret_cast<u32*>(list + 0x28);
            for (u32 i = 0; i < n && i < 64; ++i) {
                auto* name = GameFn<const char* (__fastcall*)(void*, u32)>(0x1E9B20)(list, i);
                if (Ok(name, 2)) c.paints.emplace_back(name);
            }
        }
    }
    // Entrée de surcharge : la première de type StateBlock sur le composant d'affichage (la 2e : vitres).
    uint8_t* dc = DrawableOfObj(obj);
    if (Ok(dc, 0x80)) {
        const int count = *reinterpret_cast<int*>(dc + 0x78);
        for (int i = 0; i < count && i < 16; ++i) {
            uint8_t* e = dc + 0x80 + i * 0x68;
            if (!Ok(e, 0x68)) break;
            if (*reinterpret_cast<u32*>(e + 0x28) == kStateBlockType) {
                c.paintEntry = e;
                // Origine gardée à la première rencontre de l'entrée (le véhicule est re-réglé à chaque
                // changement de tuning : la peinture lue alors peut déjà être celle du mod).
                static std::vector<std::pair<uint8_t*, std::pair<uint8_t*, u32>>> seen;
                bool known = false;
                for (auto& k : seen) if (k.first == e) { c.paintOrigData = k.second.first; c.paintOrigUid = k.second.second; known = true; }
                if (!known) {
                    c.paintOrigData = *reinterpret_cast<uint8_t**>(e + 0x18);
                    c.paintOrigUid = *reinterpret_cast<u32*>(e + 0x20);
                    if (seen.size() > 64) seen.erase(seen.begin());
                    seen.push_back({ e, { c.paintOrigData, c.paintOrigUid } });
                }
                c.paintNow = *reinterpret_cast<uint8_t**>(e + 0x18) == c.paintOrigData ? -2 : -3;   // -3 : peinture du mod déjà posée
                // Véhicule déjà repeint : la peinture d'origine est celle gardée au registre (l'entrée a
                // pu être reconstruite avec la copie du mod déjà posée).
                std::lock_guard<std::recursive_mutex> lock(g_paintLock);
                if (PaintReg* reg = FindReg(c.obj, c.objName)) {
                    c.paintOrigData = reg->orig;
                    c.paintOrigUid = reg->uid;
                    c.paintNow = *reinterpret_cast<uint8_t**>(e + 0x18) == reg->orig ? -2 : -3;
                }
                break;
            }
        }
    }
    // Diagnostic : contenu de la peinture actuelle (StateBlock : valeurs, en-têtes {décalage, taille}).
    if (c.paintEntry) {
        auto* sb = *reinterpret_cast<Illusion::StateBlock**>(c.paintEntry + 0x18);
        if (Ok(sb, sizeof(Illusion::StateBlock)) && sb->mDataByteSize <= 0x400 && sb->mNumValues <= 64) {
            const float* f = reinterpret_cast<const float*>(sb->GetBlockData());
            const auto* hd = sb->GetBlockHeader();
            if (Ok(f, sb->mDataByteSize) && Ok(hd, sb->mNumValues * 4)) {
                std::string vals, hdrs;
                char b[48];
                for (u32 i = 0; i < sb->mDataByteSize / 4 && i < 48; ++i) { snprintf(b, sizeof(b), " %.3g", f[i]); vals += b; }
                for (u32 i = 0; i < sb->mNumValues; ++i) { snprintf(b, sizeof(b), " %u/%u", hd[i].dataOffset, hd[i].size); hdrs += b; }
                Log("conduite : bloc peinture %08X : %u octets, %u valeurs, parent %08X ; en-tetes%s ; donnees%s",
                    sb->mNameUID, sb->mDataByteSize, sb->mNumValues, sb->mParentUID, hdrs.c_str(), vals.c_str());
            }
        }
    }
    std::string names;
    for (auto& p : c.paints) names += " " + p;
    Log("conduite : %zu peinture(s) :%s ; surcharge %s", c.paints.size(), names.c_str(), c.paintEntry ? "trouvee" : "absente");
}

// Déplace le lien de ressource de la surcharge de peinture vers une autre ressource StateBlock.
bool RelinkEntry(uint8_t* entry, uint8_t* data, u32 uid)
{
    if (!entry || !Ok(entry, 0x68) || !Ok(data, 0x30)) return false;
    struct Node { Node* prev; Node* next; };
    auto* h = reinterpret_cast<Node*>(entry + 0x08);                 // qResourceHandle
    auto** hData = reinterpret_cast<uint8_t**>(entry + 0x18);
    auto* hUid = reinterpret_cast<u32*>(entry + 0x20);
    if (*hData == data) return true;
    if (!Ok(h->prev, 16) || !Ok(h->next, 16)) return false;
    // Retiré de la liste des liens de l'ancienne ressource, ajouté à celle de la nouvelle (+0x20).
    h->prev->next = h->next;
    h->next->prev = h->prev;
    auto* head = reinterpret_cast<Node*>(&reinterpret_cast<UFG::qResourceData*>(data)->mResourceHandles);
    h->next = head->next;
    h->prev = head;
    head->next->prev = h;
    head->next = h;
    *hData = data;
    *hUid = uid;
    return true;
}

bool RelinkPaint(Car& c, uint8_t* data, u32 uid) { return RelinkEntry(c.paintEntry, data, uid); }

// Registre des voitures repeintes : le jeu reconstruit les surcharges de matériau d'un véhicule
// (VehicleLookComponent::OnUpdateMaterial, 0x5880B0) quand il change d'affichage (sortie du
// véhicule, distance) : la peinture d'origine revenait. Reposée à chaque reconstruction.
// Objet vérifié par son nom (+0x48) : une mémoire réutilisée par un autre véhicule n'est pas repeinte.

uint8_t* PaintEntryOf(uint8_t* dc)
{
    if (!Ok(dc, 0x80)) return nullptr;
    const int count = *reinterpret_cast<int*>(dc + 0x78);
    for (int i = 0; i < count && i < 16; ++i) {
        uint8_t* e = dc + 0x80 + i * 0x68;
        if (!Ok(e, 0x68)) break;
        if (*reinterpret_cast<u32*>(e + 0x28) == kStateBlockType) return e;
    }
    return nullptr;
}

// Copie privée de la peinture d'origine (bloc StateBlock), hors de l'inventaire du jeu : seule cette
// voiture la voit. Liens internes remis à zéro (nœuds sur eux-mêmes, liste des liens vide).
uint8_t* MakePaintCopy(uint8_t* orig)
{
    auto* sb = reinterpret_cast<Illusion::StateBlock*>(orig);
    if (!Ok(sb, sizeof(Illusion::StateBlock)) || sb->mDataByteSize > 0x400 || sb->mNumValues > 64) return nullptr;
    const size_t head = (sizeof(Illusion::StateBlock) + 15) & ~size_t(15);
    const size_t size = head + ((sb->mDataByteSize + 15) & ~u32(15)) + sb->mNumValues * sizeof(Illusion::StateBlock::StateBlockHeader);
    if (!Ok(orig, size)) return nullptr;
    auto* copy = static_cast<uint8_t*>(_aligned_malloc(size + 16, 16));
    if (!copy) return nullptr;
    memcpy(copy, orig, size);
    auto* c = reinterpret_cast<Illusion::StateBlock*>(copy);
    auto* rd = static_cast<UFG::qResourceData*>(c);
    // (nœud d'arbre : laissé tel quel, la copie n'est dans aucun arbre)
    auto* lst = reinterpret_cast<void**>(&rd->mResourceHandles);
    lst[0] = lst;
    lst[1] = lst;
    auto* node = static_cast<UFG::qNode<Illusion::StateBlock>*>(c);
    node->mPrev = node;
    node->mNext = node;
    return copy;
}

// Peinture de la palette sur la voiture ; index < 0 : celle d'origine.
bool SetPaint(Car& c, int index)
{
    std::lock_guard<std::recursive_mutex> lock(g_paintLock);
    PaintReg* reg = FindReg(c.obj, c.objName);
    if (index < 0) {
        if (reg) { *reg = g_paintReg.back(); g_paintReg.pop_back(); }
        return c.paintOrigData && RelinkPaint(c, c.paintOrigData, c.paintOrigUid);
    }
    if (index >= kPaletteCount || !c.paintOrigData) return false;
    if (!c.paintCopy && reg) c.paintCopy = reg->copy;   // copie déjà faite pour ce véhicule
    if (!c.paintCopy) c.paintCopy = MakePaintCopy(c.paintOrigData);
    if (!c.paintCopy) { Log("conduite : copie de la peinture impossible"); return false; }
    auto* sb = reinterpret_cast<Illusion::StateBlock*>(c.paintCopy);
    auto* f = reinterpret_cast<float*>(sb->GetBlockData());
    const PaletteColor& p = kPalette[index];
    if (sb->mDataByteSize >= 32) {
        f[0] = p.r; f[1] = p.g; f[2] = p.b;               // couleur principale
        f[4] = p.r * 0.98f; f[5] = p.g * 0.98f; f[6] = p.b * 0.98f;   // seconde
    }
    const bool ok = RelinkPaint(c, c.paintCopy, c.paintOrigUid);
    if (ok && c.obj) {
        if (reg) { reg->copy = c.paintCopy; reg->orig = c.paintOrigData; reg->uid = c.paintOrigUid; }
        else {
            if (g_paintReg.size() >= 48) g_paintReg.erase(g_paintReg.begin());
            g_paintReg.push_back({ c.obj, c.objName, c.paintCopy, c.paintOrigData, c.paintOrigUid, *reinterpret_cast<void**>(c.obj) });
        }
    }
    Log("conduite : peinture %s%s", p.fr, ok ? "" : " (echec)");
    return ok;
}

}  // namespace

void SetLog(LogFn fn) { g_logFn = fn; }

void OnVehicleLook(void* drawable)
{
    auto* dc = static_cast<uint8_t*>(drawable);
    std::lock_guard<std::recursive_mutex> lock(g_paintLock);
    if (g_paintReg.empty() || !Ok(dc, 0x80)) return;
    auto* obj = *reinterpret_cast<uint8_t**>(dc + 0x28);
    if (!Ok(obj, 0x50)) return;
    PaintReg* reg = FindReg(obj, *reinterpret_cast<u32*>(obj + 0x48));
    if (!reg) return;
    if (uint8_t* e = PaintEntryOf(dc); e && *reinterpret_cast<uint8_t**>(e + 0x18) != reg->copy) {
        RelinkEntry(e, reg->copy, reg->uid);
        static int logged = 0;
        if (logged++ < 8) Log("conduite : peinture du mod reposee sur %p apres reconstruction", obj);
    }
}

const char* CurrentModel() { return g_car.vehicle ? g_car.model : ""; }
const char* CurrentModelName() { return g_car.vehicle ? g_car.modelName : ""; }
int PaletteCount() { return kPaletteCount; }
const char* PaletteName(int i, bool fr) { return i >= 0 && i < kPaletteCount ? (fr ? kPalette[i].fr : kPalette[i].en) : ""; }

float OrigGovernorKmh() { return g_car.vehicle ? g_car.govGameMax : 0.f; }
float BaseVmaxKmh()
{
    if (!g_car.vehicle) return 0.f;
    const float gov = g_car.govGameMax * g_car.topSpeed;
    return g_car.gearVmax > 0.f ? (std::min)(gov, g_car.gearVmax) : gov;
}

float CurrentSpeedKmh()
{
    if (!g_car.vehicle || !Ok(g_car.mover, sizeof(PhysicsMoverInterface))) return 0.f;
    return fabsf(g_car.mover->mForwardSpeed) * 3.6f;
}

// Flammes de la nitro, au pot d'échappement : les effets d'échappement du véhicule (8 propre, 9 sale)
// sont posés sur les repères des pots. Pendant l'injection, leurs effets liés (mBoundEffect, 4 vitesses)
// prennent l'effet de feu d'un autre effet du véhicule ([Nitro] FlameEffect, 20 = moteur en feu par
// défaut), puis l'échappement est relancé ; à la fin, effets d'origine remis et échappement relancé.
// Table d'état des effets : composant + 0xB8 + n*0x18 (état : 3 allumé, 4 éteint), +0xC0 : liste des
// effets (arbre à +0x28, parcours 0x170B20 / 0x1714C0) ; effet : +0x30 4 effets liés.
// TurnOnEffect 0x68E3A0 / TurnOffEffect 0x68D890 (composant, numéro), vérifiées dans cet exe.
constexpr int kExhaustEffects[2] = { 8, 9 };
int g_flameSource = 20;
struct SavedBound { uint8_t* node; int ids[4]; };
std::vector<SavedBound> g_flameSaved;
bool g_flamesOn = false;
bool g_exhaustWasOn[2] = {};
VehicleEffectsComponent* g_flamesFx = nullptr;

template <typename Fn> void ForEffectNodes(VehicleEffectsComponent* fx, int id, Fn&& fn)
{
    auto* base = reinterpret_cast<uint8_t*>(fx);
    auto* list = *reinterpret_cast<uint8_t**>(base + 0xC0 + id * 0x18);
    if (!Ok(list, 0x40)) return;
    auto first = GameFn<uint8_t* (__fastcall*)(void*)>(0x170B20);
    auto next = GameFn<uint8_t* (__fastcall*)(void*, void*)>(0x1714C0);
    int guard = 0;
    for (uint8_t* n = first(list + 0x28); n && guard < 16; n = next(list + 0x28, n), ++guard)
        if (Ok(n, 0x70)) fn(n);
}

int& EffectState(VehicleEffectsComponent* fx, int id) { return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(fx) + 0xB8 + id * 0x18); }

void SetNitroFlames(bool on)
{
    VehicleEffectsComponent* fx = g_car.vehicle ? g_car.effects : nullptr;
    auto turnOn = GameFn<void(__fastcall*)(void*, int)>(0x68E3A0);
    auto turnOff = GameFn<void(__fastcall*)(void*, int)>(0x68D890);
    if (g_flamesOn && (!on || fx != g_flamesFx)) {
        const bool alive = Ok(g_flamesFx, sizeof(VehicleEffectsComponent)) && Is(g_flamesFx, ".?AVVehicleEffectsComponent@UFG@@");
        if (alive) {
            for (int e = 0; e < 2; ++e) turnOff(g_flamesFx, kExhaustEffects[e]);
            for (auto& sv : g_flameSaved) if (Ok(sv.node, 0x40)) memcpy(sv.node + 0x30, sv.ids, sizeof(sv.ids));
            for (int e = 0; e < 2; ++e) if (g_exhaustWasOn[e]) turnOn(g_flamesFx, kExhaustEffects[e]);
        }
        g_flameSaved.clear();
        g_flamesOn = false;
        g_flamesFx = nullptr;
    }
    if (!on || g_flamesOn || !Ok(fx, sizeof(VehicleEffectsComponent))) return;
    // Effet de feu à poser : le premier effet lié de l'effet source.
    // Source choisie, puis les autres feux du jeu (moteur détruit, véhicule détruit, feux arrière / milieu /
    // avant) : tous les véhicules n'ont pas les mêmes (la moto n'a pas le 20).
    int fire = 0;
    const int sources[] = { g_flameSource, 20, 21, 27, 26, 25, 22 };
    for (int src : sources) {
        ForEffectNodes(fx, src, [&](uint8_t* n) {
            for (int i = 0; i < 4 && !fire; ++i) if (int v = reinterpret_cast<int*>(n + 0x30)[i]; v != 0 && v != -1) fire = v;
        });
        if (fire) break;
    }
    // Effets pas encore préparés sur ce véhicule (le jeu les charge au premier allumage) : effet de feu
    // déjà trouvé sur un autre véhicule, sinon le feu arrière est préparé (allumé puis éteint aussitôt).
    static int knownFire = 0;
    if (!fire && knownFire) fire = knownFire;
    if (!fire && EffectState(fx, 27) != 3) {
        GameFn<void(__fastcall*)(void*, int)>(0x68E3A0)(fx, 27);
        GameFn<void(__fastcall*)(void*, int)>(0x68D890)(fx, 27);
        ForEffectNodes(fx, 27, [&](uint8_t* n) {
            for (int i = 0; i < 4 && !fire; ++i) if (int v = reinterpret_cast<int*>(n + 0x30)[i]; v != 0 && v != -1) fire = v;
        });
    }
    if (fire) knownFire = fire;
    if (!fire) {
        static bool logged = false;
        if (!logged) { logged = true; Log("conduite : pas d'effet de feu %d sur ce vehicule (flammes de nitro impossibles)", g_flameSource); }
        return;
    }
    g_flameSaved.clear();
    for (int e = 0; e < 2; ++e) {
        g_exhaustWasOn[e] = EffectState(fx, kExhaustEffects[e]) == 3;
        ForEffectNodes(fx, kExhaustEffects[e], [&](uint8_t* n) {
            SavedBound sv{ n, {} };
            memcpy(sv.ids, n + 0x30, sizeof(sv.ids));
            g_flameSaved.push_back(sv);
            for (int i = 0; i < 4; ++i) reinterpret_cast<int*>(n + 0x30)[i] = fire;
        });
    }
    if (g_flameSaved.empty()) return;
    for (int e = 0; e < 2; ++e) turnOff(fx, kExhaustEffects[e]);
    turnOn(fx, kExhaustEffects[0]);   // échappement relancé : il crache le feu
    g_flamesOn = true;
    g_flamesFx = fx;
}

void SetFlameSource(int id) { if (id > 0 && id < 40) g_flameSource = id; }

void SetBoost(bool on, float mul)
{
    g_boost = on;
    g_boostMul = mul;
}

// Voitures repeintes : vérifiées 4 fois par seconde. Le jeu remet la peinture d'origine sur l'entrée de
// surcharge quand le véhicule change d'affichage (sortie du véhicule, distance), sans toujours passer
// par OnUpdateMaterial ; la peinture du mod est reposée. Objet vérifié (table virtuelle et nom) : une
// mémoire réutilisée par autre chose n'est pas touchée, l'entrée est alors oubliée.
void RefreshPaints()
{
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 250;
    std::lock_guard<std::recursive_mutex> lock(g_paintLock);
    for (size_t i = 0; i < g_paintReg.size();) {
        PaintReg& r = g_paintReg[i];
        if (!Ok(r.obj, 0x70) || *reinterpret_cast<void**>(r.obj) != r.vtbl || *reinterpret_cast<u32*>(r.obj + 0x48) != r.name) {
            r = g_paintReg.back();
            g_paintReg.pop_back();
            continue;
        }
        if (uint8_t* e = PaintEntryOf(DrawableOfObj(r.obj)); e && *reinterpret_cast<uint8_t**>(e + 0x18) != r.copy) {
            const bool ok = RelinkEntry(e, r.copy, r.uid);
            static int logged = 0;
            if (logged++ < 8) Log("conduite : peinture du mod reposee sur %p%s", r.obj, ok ? "" : " (echec)");
        }
        ++i;
    }
}

void Update(void* player, const Settings& s)
{
    RefreshPaints();
    SimObjectVehicle* veh = nullptr;
    PhysicsWheeledVehicle* wv = s.enabled && player ? PlayerCar(player, &veh) : nullptr;
    void* vi = wv ? wv->mHavokVehicle : nullptr;
    if (vi != g_car.vehicle) {
        if (g_car.vehicle) {
            Write(g_car, true);
            if (g_car.governor && Ok(g_car.governor, 4) && *g_car.governor == g_car.govWritten) *g_car.governor = g_car.govGame;

            Log("conduite : vehicule %p rendu a l'origine", g_car.vehicle);
            g_car = {};
        }
        if (wv && vi) Capture(g_car, wv, veh, s);
        g_nextWrite = 0;
    }
    if (!g_car.vehicle) return;
    // Nitro : couple multiplié et vitesse de pointe relevée tant qu'elle est active (chaque image).
    static bool boosting = false;
    const bool boost = g_boost && s.nitro;
    if (boost || boosting) {
        for (size_t i = 0; i < g_car.torque.size(); ++i) {
            float* t = g_car.torque[i];
            if (!Ok(t, 4)) continue;
            *t = boost ? g_car.torqueBase[i] * g_boostMul : g_car.torqueBase[i];
            for (auto& f : g_car.fields) if (f.p == t) f.written = *t;
        }
        // Pendant l'injection, la boîte s'allonge aussi (sinon le régime max plafonne la vitesse).
        if (g_car.primary && Ok(g_car.primary, 4)) {
            *g_car.primary = boost ? g_car.primaryBase / 1.3f : g_car.primaryBase;
            for (auto& f : g_car.fields) if (f.p == g_car.primary) f.written = *g_car.primary;
        }
    }
    boosting = boost;
    // Bridage réécrit à chaque image : le jeu le remet à sa valeur d'origine en permanence (le code du
    // moteur, 0x483C00, coupe le couple au-delà ; la moto restait à 105 km/h malgré le tuning).
    if (g_car.governor && Ok(g_car.governor, 4)) {
        const float cur = *g_car.governor;
        if (cur != g_car.govWritten) {
            g_car.govGame = cur;
            if (cur > g_car.govGameMax) g_car.govGameMax = cur;
        }
        const float want = g_car.govGame * g_car.govMul * (boost ? 1.35f : 1.f);
        *g_car.governor = want;
        g_car.govWritten = want;
    }
    // Peinture choisie (la voiture la garde en sortant : c'est sa nouvelle couleur).
    // Réglages lus pour un autre véhicule (le précédent, à la prise d'un véhicule) : peinture pas touchée.
    if (strcmp(s.forModel, g_car.model) == 0
        && (s.paintId >= 0 || g_car.paintNow >= 0 || g_car.paintNow == -3) && s.paintId != g_car.paintNow) {
        Log("conduite : peinture %d -> %d (%s)", g_car.paintNow, s.paintId, g_car.model);
        if (SetPaint(g_car, s.paintId)) g_car.paintNow = s.paintId;
        else g_car.paintNow = s.paintId;   // pas de nouvel essai à chaque image
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextWrite) return;
    g_nextWrite = now + 500;
    if (!boost) Write(g_car, false);
}

}  // namespace drv
