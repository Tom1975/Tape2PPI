#define _USE_MATH_DEFINES
#include "signal_converter.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

// ============================================================
//  extractConversionParams
// ============================================================

ConversionParams extractConversionParams(
    const std::vector<BlockAnalysis>& analyses,
    const DumpMatch&                  match)
{
    ConversionParams p;
    p.speedRatio = match.speedRatio;

    // Fréquence pilote médiane sur les blocs ayant un pilote
    std::vector<double> freqs;
    for (const auto& ba : analyses)
        if (ba.hasPilot && ba.pilotFreqHz > 0.0)
            freqs.push_back(ba.pilotFreqHz);

    if (!freqs.empty()) {
        std::sort(freqs.begin(), freqs.end());
        p.pilotFreqHz = freqs[freqs.size() / 2];
    }

    p.valid = (match.result != DumpMatch::Result::FAILED);
    return p;
}

// ============================================================
//  convertToPPI — comparateur adaptatif (DC offset + hystérésis)
// ============================================================
//
//  Étape 1 — Suppression DC (biais d'offset cassette) :
//    Moyenne glissante symétrique sur DC_WIN_MS (50 ms).
//    Implémentée via une somme préfixe pour être O(N).
//    But : centrer le signal autour de 0 quelle que soit la dérive DC
//    (head DC offset, amplificateur, etc.).
//
//  Étape 2 — Hystérésis adaptative :
//    RMS locale sur RMS_WIN_MS (10 ms), hystérésis = HYST_FRAC * RMS_locale.
//    Borne inférieure HYST_MIN pour les zones de silence.
//    But : s'adapter aux variations d'amplitude (protections qui changent
//    le niveau), sans déclencher sur le bruit résiduel.

static constexpr double DC_WIN_MS  = 50.0;   // fenêtre DC (ms)
static constexpr double RMS_WIN_MS = 10.0;   // fenêtre RMS (ms)
static constexpr float  HYST_FRAC  = 0.08f;  // hystérésis = 8% de la RMS locale
static constexpr float  HYST_MIN   = 0.02f;  // hystérésis minimale absolue
static constexpr float  SILENCE_RMS = 0.02f; // en dessous : silence → sortie 0

std::vector<float> convertToPPI(const std::vector<float>& src,
                                uint32_t sampleRate)
{
    const size_t N = src.size();
    if (N == 0) return {};

    // --- Étape 1 : suppression DC par moyenne glissante symétrique ---
    const size_t dcHalf = static_cast<size_t>(sampleRate * DC_WIN_MS / 1000.0 / 2.0);

    // Somme préfixe
    std::vector<double> prefix(N + 1, 0.0);
    for (size_t i = 0; i < N; ++i)
        prefix[i + 1] = prefix[i] + src[i];

    std::vector<float> dc(N);   // signal centré
    for (size_t i = 0; i < N; ++i) {
        const size_t lo  = (i > dcHalf) ? i - dcHalf : 0;
        const size_t hi  = std::min(i + dcHalf + 1, N);
        const double mean = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
        dc[i] = src[i] - static_cast<float>(mean);
    }

    // --- Étape 2 : hystérésis adaptative (RMS locale) ---
    const size_t rmsHalf = static_cast<size_t>(sampleRate * RMS_WIN_MS / 1000.0 / 2.0);

    // Somme préfixe des carrés
    std::vector<double> prefixSq(N + 1, 0.0);
    for (size_t i = 0; i < N; ++i)
        prefixSq[i + 1] = prefixSq[i] + static_cast<double>(dc[i]) * dc[i];

    std::vector<float> out(N);
    float state = 1.0f;

    for (size_t i = 0; i < N; ++i) {
        const size_t lo  = (i > rmsHalf) ? i - rmsHalf : 0;
        const size_t hi  = std::min(i + rmsHalf + 1, N);
        const double rms = std::sqrt((prefixSq[hi] - prefixSq[lo]) / static_cast<double>(hi - lo));

        // Silence : signal sous le seuil → émettre 0 (préserve les silences pour
        // la segmentation aval, évite que le Schmitt trigger maintienne ±1 au repos)
        if (static_cast<float>(rms) < SILENCE_RMS) {
            out[i] = 0.0f;
            continue;
        }

        const float  hyst = std::max(HYST_MIN, HYST_FRAC * static_cast<float>(rms));

        if (dc[i] >= hyst)
            state = 1.0f;
        else if (dc[i] <= -hyst)
            state = -1.0f;
        // zone morte : on maintient l'état précédent

        out[i] = state;
    }

    return out;
}

// ============================================================
//  convertToCassette — filtre passe-bas zéro-phase
// ============================================================
//  Filtre IIR du premier ordre (RC) appliqué en avant puis en arrière
//  pour un déphasage nul (filtfilt).
//  alpha = exp(-2π·fc/fs)  (coefficient de rétroaction).

std::vector<float> convertToCassette(const std::vector<float>& src,
                                     uint32_t sampleRate,
                                     double cutoffHz)
{
    if (src.empty()) return {};

    const double alpha = std::exp(-2.0 * M_PI * cutoffHz / sampleRate);
    const float  a     = static_cast<float>(alpha);
    const float  b     = 1.0f - a;

    const size_t N = src.size();
    std::vector<float> tmp(N);
    std::vector<float> out(N);

    // Passe avant
    float y = src[0];
    for (size_t i = 0; i < N; ++i) {
        y = a * y + b * src[i];
        tmp[i] = y;
    }

    // Passe arrière
    y = tmp[N - 1];
    for (size_t i = N; i-- > 0; ) {
        y = a * y + b * tmp[i];
        out[i] = y;
    }

    // Normalisation d'amplitude
    float maxIn  = 0.0f;
    float maxOut = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        maxIn  = std::max(maxIn,  std::fabs(src[i]));
        maxOut = std::max(maxOut, std::fabs(out[i]));
    }
    if (maxOut > 0.0f && maxIn > 0.0f) {
        const float gain = maxIn / maxOut;
        for (float& s : out) s *= gain;
    }

    return out;
}
