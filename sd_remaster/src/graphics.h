#pragma once
// Mod graphique : bloom (néons), ciel et exposition via le gestionnaire jour/nuit du moteur.

namespace gfx {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Réglages ([Graphics] du .ini). Multiplicateurs (1 = jeu d'origine), sauf l'exposition (ajoutée).
struct Settings {
    bool enabled = true;
    float bloomBoost = 1.0f, nightBloomBoost = 1.0f;          // intensité du halo des lumières
    float bloomThreshold = 1.0f, nightBloomThreshold = 1.0f;  // seuil : plus bas = plus de lumières qui rayonnent
    float bloomSaturation = 1.0f;                             // couleur du halo
    float skySaturation = 1.0f, skyBoost = 1.0f;              // ciel
    float exposure = 0.0f;                                    // luminosité globale
};

// À appeler à chaque image (fil du jeu).
void Update(const Settings& s);

}
