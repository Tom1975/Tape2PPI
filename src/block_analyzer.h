#pragma once

#include "wav_reader.h"
#include "signal_analyzer.h"

// ============================================================
//  Analyse interne d'un bloc : pilote, sync, données
// ============================================================
struct BlockAnalysis {
    int    blockIndex     = 0;
    long   totalEdgeCount = 0;   // transitions totales dans le bloc

    // Pilote
    bool   hasPilot       = false;
    double pilotStartSec  = 0.0;
    double pilotEndSec    = 0.0;
    double pilotDurSec    = 0.0;
    double pilotFreqHz    = 0.0;   // fréquence mesurée (Hz)
    long   pilotEdgeCount = 0;     // nombre de transitions dans le pilote

    // Données (région après le pilote)
    bool   hasData        = false;
    double dataStartSec   = 0.0;

    // Codage des bits de données (distribution bimodale S/L)
    bool   encodingValid  = false;
    double shortHP        = 0.0;   // demi-période courte (samples)
    double longHP         = 0.0;   // demi-période longue (samples)

    // Sync : pulse de sync + premier octet (0x16 attendu)
    bool    hasSyncPulse   = false;
    bool    firstByteValid = false;
    uint8_t firstByte      = 0;

    enum class Structure {
        UNKNOWN,        // signal trop court ou inanalysable
        PILOT_ONLY,     // pilote sans données détectées
        PILOT_DATA,     // pilote + données (structure CPC standard)
        DATA_ONLY       // données sans pilote (bloc de continuation ?)
    } structure = Structure::UNKNOWN;
};

// Analyse le contenu d'un bloc (pilote, structure, données).
BlockAnalysis analyzeBlock(const WavReader& reader, const Block& block);
