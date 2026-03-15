#include "block_analyzer.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

// ============================================================
//  Paramètres
// ============================================================

// Intervalles à sauter en tête de bloc avant l'estimation
// (absorbe les transitoires d'enveloppe et glitches de début d'enregistrement)
static constexpr size_t PILOT_SKIP_LEADING  = 8;

// Nombre d'intervalles échantillonnés après le skip
// pour estimer la fréquence du pilote
static constexpr size_t PILOT_SAMPLE_COUNT  = 120;

// Seuil de régularité : IQR/médiane < ce seuil → signal régulier (pilote)
// Un pilote pur a IQR/médiane ≈ 0.01 ; de la donnée mixte dépasse 0.3
static constexpr double PILOT_REGULARITY    = 0.20;

// Tolérance sur la demi-période estimée pour prolonger le pilote dans le bloc
static constexpr double PILOT_TOLERANCE     = 0.25;   // ±25 %

// Nombre minimum de transitions pour valider un pilote
static constexpr size_t PILOT_MIN_EDGES     = 40;

// Nombre de transitions hors-plage tolérées avant de clore le pilote
static constexpr size_t MAX_OUT_STREAK      = 3;

// Seuils de normalisation
static constexpr float  ACTIVE_THRESHOLD    = 0.05f;
static constexpr float  ENVELOPE_FLOOR      = 0.01f;
static constexpr float  SIGN_THRESHOLD      = 0.05f;

// ============================================================
//  Enveloppe locale (two-pass peak-hold, 5 ms)
// ============================================================
static std::vector<float> computeEnvelope(const std::vector<float>& s, uint32_t sr) {
    const size_t N = s.size();
    std::vector<float> env(N, 0.0f);
    const float decay = 1.0f - 1.0f / (static_cast<float>(sr) * 0.005f);

    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = peak;
    }
    peak = 0.0f;
    for (size_t i = N; i-- > 0; ) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = std::max(env[i], peak);
    }
    return env;
}

// ============================================================
//  Détection des zero-crossings sur signal normalisé
// ============================================================
static std::vector<size_t> detectZeroCrossings(const std::vector<float>& s,
                                                const std::vector<float>& env) {
    std::vector<size_t> crossings;
    int prevSign = 0;

    for (size_t i = 0; i < s.size(); ++i) {
        if (env[i] < ACTIVE_THRESHOLD) { prevSign = 0; continue; }

        const float norm = s[i] / std::max(env[i], ENVELOPE_FLOOR);
        const int   sign = (norm >  SIGN_THRESHOLD) ?  1
                         : (norm < -SIGN_THRESHOLD) ? -1 : 0;
        if (sign != 0) {
            if (prevSign != 0 && sign != prevSign)
                crossings.push_back(i);
            prevSign = sign;
        }
    }
    return crossings;
}

// ============================================================
//  Estimation adaptative de la fréquence du pilote
//
//  On échantillonne les PILOT_SAMPLE_COUNT premiers intervalles,
//  puis on calcule médiane et IQR.
//  Si IQR/médiane < PILOT_REGULARITY → c'est un signal régulier
//  (pilote), et la médiane est la demi-période estimée.
//
//  Retourne 0 si le début du bloc n'est pas régulier (pas de pilote).
// ============================================================
static double estimatePilotHP(const std::vector<double>& iv) {
    if (iv.size() < PILOT_SKIP_LEADING + PILOT_MIN_EDGES) return 0.0;

    const size_t offset = PILOT_SKIP_LEADING;
    const size_t N = std::min(PILOT_SAMPLE_COUNT, iv.size() - offset);
    if (N < PILOT_MIN_EDGES) return 0.0;

    std::vector<double> sample(iv.begin() + offset, iv.begin() + offset + N);
    std::sort(sample.begin(), sample.end());

    const double median = sample[N / 2];
    const double q1     = sample[N / 4];
    const double q3     = sample[3 * N / 4];
    const double iqr    = q3 - q1;

    if (median <= 0.0 || (iqr / median) > PILOT_REGULARITY)
        return 0.0;   // début irrégulier → pas de pilote

    return median;
}

// ============================================================
//  analyzeBlock
// ============================================================
BlockAnalysis analyzeBlock(const WavReader& reader, const Block& block) {
    const auto&    s  = reader.samples();
    const uint32_t sr = reader.info().sampleRate;

    BlockAnalysis result;
    result.blockIndex = block.index;

    if (block.endSample <= block.startSample) return result;

    // Extraire les samples du bloc
    const std::vector<float> bs(s.begin() + block.startSample,
                                 s.begin() + block.endSample);

    // Enveloppe + zero-crossings
    const std::vector<float>  env = computeEnvelope(bs, sr);
    const std::vector<size_t> zc  = detectZeroCrossings(bs, env);

    result.totalEdgeCount = static_cast<long>(zc.size());

    if (zc.size() < PILOT_MIN_EDGES + 1) {
        result.structure = BlockAnalysis::Structure::UNKNOWN;
        return result;
    }

    // --- Intervalles inter-transitions ---
    const size_t NI = zc.size() - 1;
    std::vector<double> iv(NI);
    for (size_t i = 0; i < NI; ++i)
        iv[i] = static_cast<double>(zc[i + 1] - zc[i]);

    // --- Estimation adaptative de la demi-période du pilote ---
    const double pilotHP = estimatePilotHP(iv);

    if (pilotHP <= 0.0) {
        // Le début du bloc est irrégulier → pas de pilote détecté
        result.hasData      = true;
        result.dataStartSec = block.startSec;
        result.structure    = BlockAnalysis::Structure::DATA_ONLY;
        return result;
    }

    // --- Prolongement du pilote dans le bloc ---
    const double minHP = pilotHP * (1.0 - PILOT_TOLERANCE);
    const double maxHP = pilotHP * (1.0 + PILOT_TOLERANCE);

    size_t pilotEnd  = 0;
    size_t inRange   = 0;
    size_t outStreak = 0;

    for (size_t i = 0; i < NI; ++i) {
        if (iv[i] >= minHP && iv[i] <= maxHP) {
            ++inRange;
            outStreak = 0;
        } else {
            ++outStreak;
            if (outStreak > MAX_OUT_STREAK) {
                pilotEnd = i - MAX_OUT_STREAK;
                break;
            }
        }
    }

    if (pilotEnd == 0 && inRange >= PILOT_MIN_EDGES)
        pilotEnd = NI;   // tout le bloc est pilote

    if (inRange < PILOT_MIN_EDGES) {
        result.hasData      = true;
        result.dataStartSec = block.startSec;
        result.structure    = BlockAnalysis::Structure::DATA_ONLY;
        return result;
    }

    // --- Fréquence mesurée (médiane des intervalles in-range) ---
    std::vector<double> validIv;
    validIv.reserve(pilotEnd);
    for (size_t i = 0; i < pilotEnd; ++i)
        if (iv[i] >= minHP && iv[i] <= maxHP)
            validIv.push_back(iv[i]);

    std::sort(validIv.begin(), validIv.end());
    const double medianHP = validIv[validIv.size() / 2];

    result.hasPilot       = true;
    result.pilotStartSec  = block.startSec + static_cast<double>(zc[0])       / sr;
    result.pilotEndSec    = block.startSec + static_cast<double>(zc[pilotEnd]) / sr;
    result.pilotDurSec    = result.pilotEndSec - result.pilotStartSec;
    result.pilotFreqHz    = static_cast<double>(sr) / (2.0 * medianHP);
    result.pilotEdgeCount = static_cast<long>(pilotEnd);

    if (pilotEnd < NI) {
        result.hasData      = true;
        result.dataStartSec = block.startSec + static_cast<double>(zc[pilotEnd]) / sr;
        result.structure    = BlockAnalysis::Structure::PILOT_DATA;
    } else {
        result.structure    = BlockAnalysis::Structure::PILOT_ONLY;
    }

    return result;
}
