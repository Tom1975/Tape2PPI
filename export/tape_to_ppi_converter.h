#pragma once

// ============================================================
//  TapeToPPIConverter — convertisseur cassette → PPI temps réel
//
//  Modélise fidèlement le circuit d'interface cassette du CPC
//  (schéma électronique Amstrad CPC 464/664/6128) :
//
//    1. Couplage AC — C319 (0.022 µF) + R318 (1 MΩ)
//         Filtre passe-haut IIR, τ = R×C = 22 ms → fc ≈ 7.2 Hz
//
//    2. Amplificateur transistor KTC1815Y (émetteur commun, inverseur)
//         Modélisé implicitement dans la polarité du trigger de Schmitt
//
//    3. Trigger de Schmitt — IC302 LA6324 1/4, pins 5/6/7
//         Hystérésis fixe dérivée de R310 = 470 kΩ (feedback)
//         et R311 ‖ R312 ≈ 6.4 kΩ → hyst ≈ 3 % du signal
//
//    4. Trigger de Schmitt — IC302 LA6324 1/4, pins 9/10/8
//         Hystérésis fixe dérivée de R307 = 47 kΩ (feedback)
//         et R308 ‖ R309 ≈ 6.4 kΩ → hyst ≈ 12 % du signal
//         Agit comme buffer re-formateur sur le signal déjà carré
//
//  Note de polarité : le circuit réel effectue 3 inversions
//  (transistor + 2 comparateurs). La sortie est donc inversée
//  par rapport à l'entrée ; tenir compte de la convention
//  PPI port A bit 7 de l'émulateur cible.
//
//  Usage :
//    TapeToPPIConverter conv(44100);
//    for chaque sample de la cassette :
//        float ppi = conv.process(sample);   // retourne ±1.0
//
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

    // Paramètres modifiables (optionnel — les valeurs par défaut correspondent
    // aux valeurs mesurées sur le schéma CPC)
    void setACCouplingHz(double cutoffHz);  // défaut ≈ 7.2 Hz  (τ = 22 ms)
    void setHysteresis1(float hyst);        // défaut 0.03  (étage 1, R310=470K)
    void setHysteresis2(float hyst);        // défaut 0.12  (étage 2, R307=47K)

private:
    uint32_t m_sampleRate;

    // Filtre passe-haut IIR (couplage AC) — y[n] = alpha*(y[n-1] + x[n] - x[n-1])
    double m_hpAlpha;   // coefficient filtre HP
    float  m_hpPrev;    // x[n-1]
    float  m_hpOut;     // y[n-1]

    // Étage 1 — Schmitt trigger (Q301 + IC302 pins 5/6/7)
    float  m_hyst1;     // seuil absolu normalisé  (~R310 feedback)
    float  m_state1;    // sortie étage 1 (+1 ou -1)

    // Étage 2 — Schmitt trigger (IC302 pins 9/10/8)
    float  m_hyst2;     // seuil absolu normalisé  (~R307 feedback)
    float  m_state2;    // sortie étage 2 (+1 ou -1)

    void updateAlpha();
};
