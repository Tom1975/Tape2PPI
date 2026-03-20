#include "source_detector.h"

#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================
//  Paramètres
// ============================================================

// Enveloppe locale : constante de temps (two-pass peak-hold)
// 50 ms → suit les variations lentes d'amplitude (protections, modulation)
// sans déformer les flancs individuels (~0.01–0.5 ms)
static constexpr float  ENVELOPE_DECAY_SEC = 0.05f;

// En dessous de ce niveau d'enveloppe : zone de silence, ignorée
static constexpr float  ACTIVE_THRESHOLD   = 0.05f;

// Plancher anti-division-par-zéro lors de la normalisation
static constexpr float  ENVELOPE_FLOOR     = 0.01f;

// Seuil de dérivée sur signal normalisé :
//   PPI    → transition en 1–2 samples → |d| ≈ 2.0
//   Cassette (flanc 10–50 samples) → |d| << 0.7
static constexpr float  DERIV_SPIKE        = 0.7f;

// Si le ratio de samples en flanc rapide dépasse ce seuil → PPI
static constexpr double SPIKE_RATIO_PPI    = 2.0;   // %

// Largeur maximale d'un flanc pour qu'il soit considéré PPI
static constexpr double AVG_WIDTH_PPI      = 3.0;   // samples

// ============================================================
//  Enveloppe locale (two-pass peak-hold à décroissance exponentielle)
//
//  Passe avant  : attack instantané, décroissance lente
//  Passe arrière : même dans l'autre sens, on prend le max des deux.
//  Résultat : enveloppe symétrique, O(N), sans tableau de déque.
// ============================================================
static std::vector<float> computeEnvelope(const std::vector<float>& s, uint32_t sampleRate) {
    const size_t N = s.size();
    std::vector<float> env(N, 0.0f);

    const float decay = 1.0f - 1.0f / (static_cast<float>(sampleRate) * ENVELOPE_DECAY_SEC);

    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak  = std::max(fabsf(s[i]), peak * decay);
        env[i] = peak;
    }

    peak = 0.0f;
    for (size_t i = N; i-- > 0; ) {
        peak  = std::max(fabsf(s[i]), peak * decay);
        env[i] = std::max(env[i], peak);
    }

    return env;
}

// ============================================================
//  detectSource
//
//  Algorithme en deux étapes :
//
//  1. Normalisation par enveloppe locale
//     s_norm[i] = s[i] / max(env[i], FLOOR)
//     → compense l'amplitude variable (protections, sous-modulation)
//
//  2. Analyse de la dérivée sur le signal normalisé
//     d[i] = |s_norm[i] - s_norm[i-1]|
//     PPI    : d ≈ 2.0 sur 1–2 samples, 0 ailleurs → spikeRatio élevé, width faible
//     Cassette : d << 0.7 partout (flancs lents) → spikeRatio ≈ 0
// ============================================================
DetectionResult detectSource(const WavReader& reader) {
    const auto& s  = reader.samples();
    const size_t N = s.size();

    DetectionResult result;
    if (N == 0) return result;

    // --- Étape 1 : enveloppe locale ---
    const std::vector<float> env = computeEnvelope(s, reader.info().sampleRate);

    // --- Étape 2 : dérivée sur signal normalisé ---
    long  activeCount = 0;
    long  spikeCount  = 0;   // samples avec |d| > DERIV_SPIKE
    long  totalSpikes = 0;   // nombre de flancs distincts
    long  totalWidth  = 0;   // somme des largeurs de flancs
    long  spikeWidth  = 0;   // largeur du flanc en cours
    bool  inSpike     = false;
    float prevNorm    = 0.0f;

    for (size_t i = 0; i < N; ++i) {
        if (env[i] < ACTIVE_THRESHOLD) {
            // Silence : clore un flanc éventuel
            if (inSpike) {
                ++totalSpikes;
                totalWidth += spikeWidth;
                spikeWidth  = 0;
                inSpike     = false;
            }
            prevNorm = 0.0f;
            continue;
        }

        const float norm = s[i] / std::max(env[i], ENVELOPE_FLOOR);
        const float d    = (activeCount > 0) ? fabsf(norm - prevNorm) : 0.0f;
        prevNorm = norm;
        ++activeCount;

        if (d > DERIV_SPIKE) {
            ++spikeCount;
            ++spikeWidth;
            inSpike = true;
        } else {
            if (inSpike) {
                ++totalSpikes;
                totalWidth += spikeWidth;
                spikeWidth  = 0;
                inSpike     = false;
            }
        }
    }

    if (inSpike) {
        ++totalSpikes;
        totalWidth += spikeWidth;
    }

    result.edgeCount     = totalSpikes;
    result.activeCount   = activeCount;

    if (activeCount == 0) return result;

    result.spikeRatio    = static_cast<double>(spikeCount)  / activeCount * 100.0;
    result.avgSpikeWidth = (totalSpikes > 0)
                           ? static_cast<double>(totalWidth) / totalSpikes
                           : 0.0;

    if (result.spikeRatio > SPIKE_RATIO_PPI && result.avgSpikeWidth < AVG_WIDTH_PPI)
        result.source = SourceType::PPI;
    else if (result.activeCount > 0)
        result.source = SourceType::CASSETTE;
    // sinon : aucun sample actif → UNKNOWN

    return result;
}
