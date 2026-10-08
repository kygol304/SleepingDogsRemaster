#pragma once
// Nouvelles armes : modèles 3D créés en mémoire (aucun fichier du jeu modifié) et affichés à la
// place du modèle d'origine sur les seules armes du mod (variantes « nom#xxx »).
#include <cstdint>

namespace nw {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// À appeler à chaque image (fil du jeu) : termine le chargement des textures.
void Update();

// Crée (une fois) le modèle décrit par un fichier .skm. Renvoie son uid, 0 en cas d'échec.
uint32_t LoadModel(const char* path);

// Masque les pièces d'origine de l'objet et y attache le modèle uid (sans effet si déjà fait,
// sauf pour re-masquer les pièces que le jeu aurait réaffichées). Vrai si le modèle a été ajouté.
bool ApplyModel(void* simObject, uint32_t uid);

// Nombre de pièces (liaisons rigides) visibles / au total sur l'objet (diagnostic).
void BindingVisibility(void* simObject, int* visible, int* total);

// Lance-roquettes : copie modifiée de la fiche d'arme (WeaponTypeInfo) d'origine, jamais libérée.
// Les fiches d'origine ne sont pas touchées. nullptr si la fiche ne paraît pas valide.
struct RocketParams { float speedMul, gravityMul, damageMul; uint32_t explosion; int clip; };
// Icône du mod (PNG) chargée comme texture nommée « name » : -1 échec, 1 en cours, 2 prête.
int IconState(const char* name, const char* pngPath);

// Emplacement du ShaderResourceView d'une texture du jeu (par son nom), nullptr si non chargée.
void** TextureSrvSlot(const char* name);
// ShaderResourceView d'une icône du mod prête, nullptr sinon. AddRefSrv : référence en plus.
void* IconSrv(const char* name);
void AddRefSrv(void* srv);
// Icône du mod aux proportions d'une texture du jeu (variante recadrée si elles diffèrent), nullptr
// tant qu'elle n'est pas prête. TextureSize : dimensions d'une texture du jeu chargée.
void* IconSrvFor(const char* name, const char* pngPath, uint32_t targetW, uint32_t targetH);
bool TextureSize(const char* name, uint32_t* w, uint32_t* h);

// Emplacement de la fiche d'arme (WeaponTypeInfo*) dans le composant d'arme d'un objet, ou nullptr.
void** WeaponInfoSlot(void* simObject);
// Type d'explosion du dernier mode de tir d'une fiche d'arme (0 si illisible).
uint32_t LastModeExplosion(const void* weaponInfo);
// Ajoute (si absent) au tableau des explosions du jeu un type « ourSym » : copie de « baseSym »
// agrandie, avec l'effet de la plus grosse explosion. Renvoie ourSym, ou 0 en cas d'échec.
// prefer : types dont on préfère l'effet visuel (dans l'ordre), sinon la plus grosse explosion.
uint32_t EnsureRocketExplosion(uint32_t ourSym, uint32_t baseSym, float radiusMul, const char* (*symbolName)(uint32_t),
                               const uint32_t* prefer, int nPrefer);
void SetRocketExplosion(void* weaponInfo, uint32_t sym);
void* MakeRocketWeaponInfo(void* srcInfo, const RocketParams& rp, const char* (*symbolName)(uint32_t));

}
