#pragma once

#include "wav_reader.h"
#include "dump_matcher.h"
#include "block_analyzer.h"
#include "source_detector.h"

#include <vector>

// ============================================================
//  Paramètres de conversion extraits de l'analyse des dumps
// ============================================================
struct ConversionParams {
    double speedRatio    = 1.0;   // dur2/dur1 (vitesse relative)
    double pilotFreqHz   = 800.0; // fréquence pilote médiane (pour le LPF)
    bool   valid         = false;
};

// Extrait les paramètres de conversion depuis les analyses de dump.
ConversionParams extractConversionParams(
    const std::vector<BlockAnalysis>& analyses,
    const DumpMatch&                  match);

// ============================================================
//  Conversions signal
// ============================================================

// Cassette → PPI : suppression DC adaptative + comparateur Schmitt adaptatif.
// Produit un signal quasi-carré ±1.
// sampleRate : fréquence d'échantillonnage (nécessaire pour les fenêtres temporelles).
std::vector<float> convertToPPI(const std::vector<float>& src,
                                uint32_t sampleRate);

// PPI → Cassette : filtre passe-bas zéro-phase (forward + backward IIR).
// cutoffHz : fréquence de coupure (typiquement 3× fréquence pilote).
std::vector<float> convertToCassette(const std::vector<float>& src,
                                     uint32_t sampleRate,
                                     double cutoffHz);
