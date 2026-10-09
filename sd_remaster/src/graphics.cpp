// Mod graphique : réglages du gestionnaire jour/nuit du moteur (UFG::TimeOfDayManager, 0x2163510) :
// bloom HDR (halo des lumières : seuils, intensité, saturation), saturation / luminosité du ciel,
// exposition. Les néons rayonnent davantage la nuit (marchés de nuit, rues).
//
// Structure du SD-SDK (external/SD-SDK), valide pour notre exe ; aucune fonction du SDK appelée.
// Garde-fou : la latitude doit être celle de Hong Kong (~22°, en radians) et l'heure entre 0 et 24 h, sinon
// on ne touche à rien.
#include "../../external/SD-SDK/sdhd_sdk.hh"
#include "graphics.h"

#include <cmath>
#include <cstdarg>

using namespace UFG;

namespace gfx {
namespace {

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

TimeOfDayManager* Tod() { return reinterpret_cast<TimeOfDayManager*>(SDK::gBaseAddress + 0x2163510); }

// Une valeur du moteur pilotée par le mod : la valeur d'origine est reprise chaque fois que le jeu
// l'écrit lui-même (elle diffère alors de ce que le mod y avait mis), puis on y applique le réglage.
struct Driven {
    float orig = 0.f, written = NAN;
    void Apply(float& field, float value)
    {
        if (std::isnan(written) || field != written) orig = field;
        field = value;
        written = value;
    }
    void Restore(float& field)
    {
        if (!std::isnan(written) && field == written) field = orig;
        written = NAN;
    }
};

Driven g_bloomNear, g_bloomFar, g_bloomBoost, g_bloomSat, g_skySat, g_skyBoost, g_exposure;

}  // namespace

void SetLog(LogFn fn) { g_logFn = fn; }

void Update(const Settings& s)
{
    TimeOfDayManager* t = Tod();
    static int state = 0;   // 0 à vérifier, 1 valide, -1 refusé
    if (state == 0) {
        // Latitude en radians : Hong Kong, 22,3° = 0,39 rad.
        const bool ok = t->m_LocalLatitude > 0.30f && t->m_LocalLatitude < 0.48f
            && t->m_SecondsSinceMidnight >= 0.f && t->m_SecondsSinceMidnight <= 86400.f;
        Log("graphismes : jour/nuit latitude %.3f rad, heure %.2f h, bloom seuils %.3f/%.3f (distances %.1f/%.1f) intensite %.3f "
            "saturation %.3f, ciel saturation %.3f luminosite %.3f, exposition %.3f (EV %.3f / %.3f / %.3f)",
            t->m_LocalLatitude, t->m_SecondsSinceMidnight / 3600.f, t->mBloomNearThreshold, t->mBloomFarThreshold,
            t->mBloomNearDistance, t->mBloomFarDistance, t->mBloomBoost, t->mBloomSaturation, t->mSkySaturation,
            t->mSkyBoost, t->mExposure, t->mVisualTreatmentEV, t->mMeteringEV, t->mTodEV);
        state = ok ? 1 : -1;
        if (!ok) { Log("graphismes : structure jour/nuit non reconnue, reglages graphiques desactives"); return; }
    }
    if (state < 0) return;

    if (!s.enabled) {
        g_bloomNear.Restore(t->mBloomNearThreshold);
        g_bloomFar.Restore(t->mBloomFarThreshold);
        g_bloomBoost.Restore(t->mBloomBoost);
        g_bloomSat.Restore(t->mBloomSaturation);
        g_skySat.Restore(t->mSkySaturation);
        g_skyBoost.Restore(t->mSkyBoost);
        g_exposure.Restore(t->mExposure);
        return;
    }

    // Part de nuit : 1 entre 20 h et 5 h, 0 entre 7 h et 18 h, fondu entre les deux.
    const float h = t->m_SecondsSinceMidnight / 3600.f;
    float night;
    if (h >= 20.f || h < 5.f) night = 1.f;
    else if (h >= 7.f && h < 18.f) night = 0.f;
    else if (h < 7.f) night = (7.f - h) / 2.f;
    else night = (h - 18.f) / 2.f;

    const float boost = s.bloomBoost + (s.nightBloomBoost - s.bloomBoost) * night;
    const float thresh = s.bloomThreshold + (s.nightBloomThreshold - s.bloomThreshold) * night;
    g_bloomNear.Apply(t->mBloomNearThreshold, g_bloomNear.orig * thresh);
    g_bloomFar.Apply(t->mBloomFarThreshold, g_bloomFar.orig * thresh);
    g_bloomBoost.Apply(t->mBloomBoost, g_bloomBoost.orig * boost);
    g_bloomSat.Apply(t->mBloomSaturation, g_bloomSat.orig * s.bloomSaturation);
    g_skySat.Apply(t->mSkySaturation, g_skySat.orig * s.skySaturation);
    g_skyBoost.Apply(t->mSkyBoost, g_skyBoost.orig * s.skyBoost);
    g_exposure.Apply(t->mExposure, g_exposure.orig + s.exposure);

    static ULONGLONG nextLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= nextLog) {
        nextLog = now + 60000;
        Log("graphismes : %.1f h (nuit %.0f %%), bloom intensite %.3f -> %.3f, seuil %.3f -> %.3f",
            h, night * 100.f, g_bloomBoost.orig, t->mBloomBoost, g_bloomNear.orig, t->mBloomNearThreshold);
    }
}

}  // namespace gfx
