#pragma once

// ============================================================
//  TapeToPPIConverter — convertisseur cassette → PPI temps réel
//
//  Modélise le circuit d'interface cassette de l'Amstrad CPC :
//    1. Couplage AC (suppression DC par filtre passe-haut IIR)
//    2. Trigger de Schmitt (comparateur avec hystérésis)
//
//  Usage :
//    TapeToPPIConverter conv(44100);
//    for chaque sample de la cassette :
//        float ppi = conv.process(sample);   // retourne ±1.0
//
//  Le signal de sortie est directement utilisable comme signal PPI :
//    +1.0  →  bit haut (PPI port A bit 7 = 1)
//    -1.0  →  bit bas  (PPI port A bit 7 = 0)
//
//  Paramètres matériels modélisés :
//    - Constante de temps AC : ~22 ms (typique condensateur de liaison cassette CPC)
//    - Hystérésis : ~5% du signal crête (Schmitt trigger interne)
// ============================================================

#include <cstdint>

class TapeToPPIConverter {
public:
    // sampleRate : fréquence d'échantillonnage du signal d'entrée (Hz)
    explicit TapeToPPIConverter(uint32_t sampleRate);

    // Réinitialise l'état interne (utile entre deux fichiers / entre deux faces)
    void reset();

    // Traite un sample d'entrée normalisé [-1.0, +1.0].
    // Retourne +1.0 ou -1.0 (niveau PPI).
    float process(float in);

    // Paramètres modifiables (optionnel — les valeurs par défaut conviennent
    // à la très grande majorité des enregistrements CPC standard)
    void setACCouplingHz(double cutoffHz);   // défaut ≈ 7 Hz
    void setHysteresis(float frac);          // défaut 0.05 (5% RMS adaptative)

private:
    uint32_t m_sampleRate;

    // Filtre passe-haut IIR (couplage AC) — y[n] = alpha*(y[n-1] + x[n] - x[n-1])
    double m_hpAlpha;     // coefficient filtre HP
    float  m_hpPrev;      // x[n-1]
    float  m_hpOut;       // y[n-1]

    // Schmitt trigger
    float  m_hystFrac;    // fraction de RMS locale
    float  m_state;       // sortie courante (+1 ou -1)

    // Estimateur RMS à décroissance exponentielle (fenêtre ~10 ms)
    double m_rmsAlpha;
    float  m_rmsEst;      // estimation courante de la RMS

    static constexpr float  HYST_MIN = 0.02f;  // hystérésis minimale absolue

    void updateAlpha();
};
