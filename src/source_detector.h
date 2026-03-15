#pragma once

#include "wav_reader.h"

// ============================================================
//  Résultat de la détection de source
// ============================================================
enum class SourceType { UNKNOWN, PPI, CASSETTE };

struct DetectionResult {
    SourceType  source        = SourceType::UNKNOWN;
    long        activeCount   = 0;      // échantillons actifs (hors silence)
    long        edgeCount     = 0;      // nombre de transitions détectées
    double      spikeRatio    = 0.0;    // % d'actifs en flanc rapide
    double      avgSpikeWidth = 0.0;    // largeur moyenne d'un flanc (samples)
};

// Analyse le signal et retourne le résultat de détection.
// Applique une normalisation par enveloppe locale avant l'analyse,
// ce qui compense les variations d'amplitude dues aux protections.
DetectionResult detectSource(const WavReader& reader);
