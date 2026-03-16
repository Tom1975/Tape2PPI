#include "protection_detector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

// ============================================================
//  Utilitaires
// ============================================================

static double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Coefficient de variation (écart-type / moyenne) sur un vecteur.
// Mesure la dispersion relative — 0.0 = parfaitement constant.
static double cv(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    if (mean <= 0.0) return 0.0;
    double sq = 0.0;
    for (double x : v) sq += (x - mean) * (x - mean);
    return std::sqrt(sq / v.size()) / mean;
}

// ============================================================
//  detectProtection
// ============================================================
ProtectionAnalysis detectProtection(
    const std::vector<BlockAnalysis>& analyses,
    uint32_t /*sampleRate*/)
{
    ProtectionAnalysis result;

    if (analyses.empty()) {
        result.details = "Aucun bloc analysé";
        return result;
    }

    // ---- Collecte des statistiques sur l'ensemble des blocs ----

    const int totalBlocks    = static_cast<int>(analyses.size());
    int       dataOnlyCount  = 0;
    int       pilotCount     = 0;
    bool      firstBlockSync = false;

    std::vector<double> pilotFreqs;
    std::vector<double> pilotDurs;
    std::vector<double> lsRatios;

    for (const auto& ba : analyses) {
        using S = BlockAnalysis::Structure;

        if (ba.blockIndex == 1 && ba.firstByteValid)
            firstBlockSync = true;

        if (ba.structure == S::DATA_ONLY || ba.structure == S::UNKNOWN) {
            ++dataOnlyCount;
            continue;
        }

        if (ba.hasPilot) {
            ++pilotCount;
            pilotFreqs.push_back(ba.pilotFreqHz);
            pilotDurs.push_back(ba.pilotDurSec);
            if (ba.encodingValid && ba.shortHP > 0.0)
                lsRatios.push_back(ba.longHP / ba.shortHP);
        }
    }

    // Sans sync sur le bloc 1, on ne peut pas valider quoi que ce soit
    if (!firstBlockSync) {
        result.details = "Bloc 1 sans octet 0x16 — structure non conforme ou inconnue";
        return result;
    }

    if (pilotCount == 0) {
        result.details = "Aucun pilote détecté";
        return result;
    }

    // ---- Paramètres résumés ----

    const double medFreq    = median(pilotFreqs);
    const double medDur     = median(pilotDurs);
    const double medRatio   = lsRatios.empty() ? 2.0 : median(lsRatios);
    const double freqCV     = cv(pilotFreqs);    // consistance de la fréquence pilote
    const double dataFrac   = static_cast<double>(dataOnlyCount) / totalBlocks;

    // ---- Scoring ----
    // Chaque critère contribue à un score [0,1] pour chaque type connu.
    // On retient le type avec le meilleur score si celui-ci dépasse le seuil.

    double scoreStandard  = 0.0;
    double scoreSpeedlock = 0.0;

    // Fréquence pilote
    // Standard ROM : ~760 Hz (ou ~2000 Hz sur certains loaders ROM)
    if (medFreq >= 500.0 && medFreq <= 900.0)          scoreStandard  += 0.40;
    else if (medFreq >= 1800.0 && medFreq <= 2200.0)   scoreStandard  += 0.25;
    // Speedlock / turbo : typiquement 1000–1600 Hz
    if (medFreq > 1000.0 && medFreq < 2000.0)          scoreSpeedlock += 0.40;

    // Fraction de blocs DATA_ONLY
    // Standard : aucun (ou très peu) ; Speedlock : majorité
    if (dataFrac < 0.15)                                scoreStandard  += 0.30;
    if (dataFrac >= 0.50)                               scoreSpeedlock += 0.30;

    // Durée médiane du pilote
    // Standard : 2–5 s ; Speedlock : < 2 s (pilote court = gain vitesse)
    if (medDur >= 2.0 && medDur <= 6.0)                scoreStandard  += 0.20;
    if (medDur > 0.3  && medDur < 1.8)                 scoreSpeedlock += 0.20;

    // Ratio L/S
    // Les deux utilisent ×2 environ ; on donne un bonus de cohérence
    if (medRatio >= 1.8 && medRatio <= 2.3) {
        scoreStandard  += 0.10;
        scoreSpeedlock += 0.10;
    }

    // Consistance de la fréquence pilote entre les blocs
    // Speedlock est très régulier (tous les pilotes identiques)
    if (freqCV < 0.05)                                  scoreSpeedlock += 0.05;
    if (freqCV < 0.10)                                  scoreStandard  += 0.05;

    // ---- Décision ----

    static constexpr double THRESHOLD = 0.55;

    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "pilote=%.0f Hz  dur=%.2f s  L/S=×%.2f  DATA_ONLY=%d/%d",
                  medFreq, medDur, medRatio, dataOnlyCount, totalBlocks);
    std::string oss(buf);

    // Cap à 1.0 (plusieurs critères peuvent s'additionner au-delà)
    scoreStandard  = std::min(scoreStandard,  1.0);
    scoreSpeedlock = std::min(scoreSpeedlock, 1.0);

    if (scoreStandard >= THRESHOLD && scoreStandard >= scoreSpeedlock) {
        result.type       = ProtectionType::STANDARD_ROM;
        result.confidence = scoreStandard;
        result.name       = "Standard ROM";
        result.details    = oss;
    } else if (scoreSpeedlock >= THRESHOLD && scoreSpeedlock > scoreStandard) {
        result.type       = ProtectionType::SPEEDLOCK;
        result.confidence = scoreSpeedlock;
        result.name       = "Speedlock (ou compatible)";
        result.details    = oss;
    } else {
        result.type       = ProtectionType::UNKNOWN;
        result.confidence = std::max(scoreStandard, scoreSpeedlock);
        result.name       = "Inconnu";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  [scores: std=%.2f spd=%.2f]",
                      scoreStandard, scoreSpeedlock);
        result.details    = oss + buf;
    }

    return result;
}
