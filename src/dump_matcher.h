#pragma once

#include "signal_analyzer.h"
#include "block_analyzer.h"
#include "protection_detector.h"
#include <string>
#include <vector>

// ============================================================
//  Résultat de l'appariement pour une paire de blocs
// ============================================================
struct BlockPair {
    int    idx1, idx2;          // index 1-based des blocs (dump1 / dump2)
    double dur1, dur2;          // durées en secondes
    double ratio;               // dur2 / dur1
    bool   structureCompat;     // structures compatibles (même type de contenu)
    bool   encodingCompat;      // codage L/S compatible (ratio similaire)
};

// ============================================================
//  Résultat global de l'appariement dump1 ↔ dump2
// ============================================================
struct DumpMatch {
    enum class Result {
        MATCHED,    // appariement solide, speed ratio fiable
        PARTIAL,    // appariement partiel (blocs manquants ou incohérences)
        FAILED      // impossibilité d'apparier (structure incompatible)
    };

    Result result              = Result::FAILED;
    int    matchedPairs        = 0;
    int    totalBlocks1        = 0;
    int    totalBlocks2        = 0;

    // Ratio de vitesse dump2/dump1 (médiane des ratios de durée)
    // Si < 1 : dump2 plus rapide ; si > 1 : dump2 plus lent
    double speedRatio          = 1.0;

    // Consistance du ratio sur tous les blocs (1 - CV).
    // 1.0 = ratio parfaitement constant, 0.0 = très variable.
    double speedConsistency    = 0.0;

    double confidence          = 0.0;

    std::vector<BlockPair> pairs;
    std::string            notes;
};

// Apparie les blocs de deux dumps et calcule le ratio de vitesse.
DumpMatch matchDumps(
    const std::vector<Block>&         blocks1,
    const std::vector<BlockAnalysis>& analyses1,
    const ProtectionAnalysis&         prot1,
    const std::vector<Block>&         blocks2,
    const std::vector<BlockAnalysis>& analyses2,
    const ProtectionAnalysis&         prot2);
