#pragma once

#include "block_analyzer.h"
#include <string>
#include <vector>

// ============================================================
//  Type de protection identifié au niveau du dump
// ============================================================
enum class ProtectionType {
    STANDARD_ROM,   // Chargeur ROM standard CPC (~760 Hz, tous blocs pilotés)
    SPEEDLOCK,      // Speedlock / turbo haute vitesse (>1000 Hz, blocs DATA_ONLY)
    BLEEPLOAD,      // Bleepload (reconnaissance future)
    UNKNOWN         // Structure non reconnue ou confiance insuffisante
};

// ============================================================
//  Résultat de l'analyse de protection (niveau dump)
// ============================================================
struct ProtectionAnalysis {
    ProtectionType type       = ProtectionType::UNKNOWN;
    double         confidence = 0.0;   // fraction 0.0–1.0
    std::string    name;               // nom lisible
    std::string    details;            // observations clés
};

// Analyse la liste des blocs d'un dump pour identifier la protection.
// Si la confiance est insuffisante, retourne UNKNOWN.
ProtectionAnalysis detectProtection(
    const std::vector<BlockAnalysis>& analyses,
    uint32_t sampleRate);
