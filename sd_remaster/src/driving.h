#pragma once
// Conduite plus réaliste : réglages Havok du véhicule conduit par le joueur (adhérence, roulis,
// inertie, suspension, freins, aides à la stabilité), remis à l'origine à la sortie du véhicule.

namespace drv {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// VehicleLookComponent::OnUpdateMaterial (0x5880B0) vient de reconstruire les surcharges de matériau du
// composant d'affichage : peinture du mod reposée si ce véhicule a été repeint.
void OnVehicleLook(void* drawable);

struct Settings {
    bool enabled = true;
    float grip = 0.92f;          // adhérence des pneus (x origine)
    float rearGrip = 0.95f;      // adhérence en plus à l'arrière (x grip) : l'arrière décroche un peu avant
    float bodyRoll = 2.5f;       // roulis / tangage de la caisse dus aux forces des pneus (x origine, max 1)
    float antiRoll = 0.5f;       // aides anti-tonneau du jeu (x origine)
    float suspension = 0.85f;    // raideur des ressorts (x origine)
    float damping = 0.9f;        // amortisseurs (x origine)
    float spinDamping = 0.6f;    // freinage artificiel des rotations (x origine) : glisses possibles
    float equalizer = 0.5f;      // égalisation de l'adhérence entre les roues (x origine)
    float extraTorque = 0.5f;    // couple de virage ajouté par le jeu (x origine)
    float brakes = 0.85f;        // force de freinage (x origine)
    float topSpeed = 1.35f;      // bridage de vitesse du jeu (130 km/h sur toutes les voitures, x origine)
    // Tuning du modèle conduit (garage) : puissance, freins, adhérence en plus, nitro, peinture.
    float engine = 1.f;          // couple moteur (x origine)
    float vmaxMul = 1.f;         // niveau moteur : vitesse max du véhicule (x), par la transmission et le bridage
    float tuneBrakes = 1.f;      // x en plus des freins
    float tuneGrip = 1.f;        // x en plus de l'adhérence
    bool nitro = false;          // nitro installée
    int nitroLevel = 0;          // niveau de nitro (0 à 3)
    int paintId = -1;            // peinture : rang dans la palette du mod (-1 : celle d'origine)
    char forModel[32] = "";      // clé du véhicule pour lequel le tuning a été lu (peinture appliquée seulement à lui)
};

// Véhicule conduit : clé du tuning (ce véhicule précis : modèle + nom de l'objet, "" à pied) et nom du modèle.
const char* CurrentModel();
const char* CurrentModelName();

// Bridage de vitesse d'origine du véhicule conduit (km/h, 0 à pied).
float OrigGovernorKmh();

// Vitesse max du véhicule conduit sans tuning (km/h, 0 à pied) : bridage général ou limite de la boîte
// (régime max au dernier rapport), la plus basse des deux.
float BaseVmaxKmh();

// Palette de peintures du mod (couleur libre appliquée sur une copie de la peinture du véhicule).
int PaletteCount();
const char* PaletteName(int i, bool fr);

// Vitesse du véhicule conduit (km/h, 0 à pied).
float CurrentSpeedKmh();

// Flammes de la nitro : l'échappement du véhicule conduit crache l'effet de feu FlameSource.
void SetNitroFlames(bool on);
void SetFlameSource(int effectId);

// Nitro en cours (couple x mul, vitesse de pointe relevée) : appelé à chaque image.
void SetBoost(bool on, float mul);

// À appeler à chaque image (fil du jeu) avec le personnage du joueur.
void Update(void* player, const Settings& s);

}  // namespace drv
