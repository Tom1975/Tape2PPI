#pragma once

#include "block_analyzer.h"
#include "signal_analyzer.h"

#include <string>
#include <vector>

// ============================================================
//  Qualité de conversion pour un bloc individuel
// ============================================================
struct BlockConversionQuality {
    int  blockIndex       = 0;

    // --- Pilote ---
    bool pilotRefFound    = false;   // pilote détecté dans la référence PPI
    bool pilotConvFound   = false;   // pilote détecté dans le signal converti
    double pilotFreqErr   = 0.0;     // erreur relative (%) sur la fréquence pilote
    double pilotDurErr    = 0.0;     // erreur relative (%) sur la durée pilote

    // --- Codage S/L ---
    bool encodingRefValid  = false;
    bool encodingConvValid = false;
    double shortHPErr      = 0.0;    // erreur relative (%) sur S
    double longHPErr       = 0.0;    // erreur relative (%) sur L
    double lsRatioErr      = 0.0;    // erreur relative (%) sur le ratio L/S

    // --- Sync 0x16 ---
    bool syncRef  = false;
    bool syncConv = false;

    // --- Score bloc [0, 1] ---
    // 1.0 = conversion parfaite sur ce bloc
    double score  = 0.0;

    // Détails lisibles
    std::string details;
};

// ============================================================
//  Qualité de conversion globale pour une paire de dumps
// ============================================================
struct ConversionQuality {
    std::vector<BlockConversionQuality> blocks;

    int    validBlocks    = 0;   // blocs ayant un pilote de référence
    int    goodBlocks     = 0;   // blocs avec score ≥ 0.70
    double overallScore   = 0.0; // moyenne des scores (blocs valides)

    std::string summary;
};

// Compare le signal converti (analysé) avec le PPI de référence.
// refAnalyses   : analyses du PPI de référence (ground truth)
// convAnalyses  : analyses du signal cassette converti en PPI
ConversionQuality validateConversion(
    const std::vector<BlockAnalysis>& refAnalyses,
    const std::vector<BlockAnalysis>& convAnalyses);

// Affiche le rapport de validation sur stdout.
void printConversionQuality(const ConversionQuality& q);
