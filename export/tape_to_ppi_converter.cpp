// ============================================================
//  tape_to_ppi_converter.cpp
//
//  Convertisseur cassette → PPI pour émulateur Amstrad CPC.
//  Modélise fidèlement le circuit analogique d'interface cassette
//  (schéma CPC 464/664/6128).
//
//  Dépendances : aucune (C++11 pur, <cmath> uniquement).
//
//  Généré par cpc_wav_analyzer (projet Tape2PPI).
// ============================================================

#include "tape_to_ppi_converter.h"

#define _USE_MATH_DEFINES
#include <cmath>

// ============================================================
//  Constructeur
// ============================================================

TapeToPPIConverter::TapeToPPIConverter(uint32_t sampleRate)
    : m_sampleRate(sampleRate)
    , m_hpAlpha(0.0)
    , m_hpPrev(0.0f)
    , m_hpOut(0.0f)
    , m_hyst1(0.03f)    // R310=470K, R311||R312≈6.4K → 6.4/(470+6.4) ≈ 1.3%  × 2 ≈ 3%
    , m_state1(1.0f)
    , m_hyst2(0.12f)    // R307=47K,  R308||R309≈6.4K → 6.4/(47+6.4)  ≈ 12%
    , m_state2(1.0f)
{
    setACCouplingHz(7.2);   // τ = R318 × C319 = 1 MΩ × 0.022 µF = 22 ms → fc = 7.2 Hz
}

// ============================================================
//  reset
// ============================================================

void TapeToPPIConverter::reset()
{
    m_hpPrev  = 0.0f;
    m_hpOut   = 0.0f;
    m_state1  = 1.0f;
    m_state2  = 1.0f;
}

// ============================================================
//  setACCouplingHz
// ============================================================

void TapeToPPIConverter::setACCouplingHz(double cutoffHz)
{
    // Filtre passe-haut du 1er ordre : alpha = exp(-2π·fc/fs)
    // Réponse : y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    m_hpAlpha = std::exp(-2.0 * M_PI * cutoffHz / m_sampleRate);
}

// ============================================================
//  setHysteresis1 / setHysteresis2
// ============================================================

void TapeToPPIConverter::setHysteresis1(float hyst)
{
    m_hyst1 = hyst;
}

void TapeToPPIConverter::setHysteresis2(float hyst)
{
    m_hyst2 = hyst;
}

// ============================================================
//  process — traitement échantillon par échantillon (causal)
// ============================================================

float TapeToPPIConverter::process(float in)
{
    // --- 1. Filtre passe-haut (couplage AC — modélise C319 + R318) ---
    //   Supprime le DC offset et les dérives basses fréquences.
    //   Formule : y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    const float hp = static_cast<float>(m_hpAlpha) * (m_hpOut + in - m_hpPrev);
    m_hpPrev = in;
    m_hpOut  = hp;

    // --- 2. Étage 1 — Trigger de Schmitt (Q301 + IC302 pins 5/6/7) ---
    //   Le transistor KTC1815Y (émetteur commun) inverse le signal,
    //   puis le comparateur avec signal sur entrée − inverse à nouveau :
    //   double inversion → la condition de basculement reste identique
    //   à un comparateur non-inverseur sur le signal hp centré en 0.
    //   Hystérésis fixe ≈ 3 % dérivée de R310 = 470 kΩ.
    if      (hp >=  m_hyst1) m_state1 =  1.0f;
    else if (hp <= -m_hyst1) m_state1 = -1.0f;
    // Zone morte [−hyst1, +hyst1] : maintien de l'état

    // --- 3. Étage 2 — Trigger de Schmitt (IC302 pins 9/10/8) ---
    //   Entrée = sortie étage 1 (signal déjà carré ±1).
    //   Rôle : re-formateur / buffer de sortie vers le PPI.
    //   Hystérésis fixe ≈ 12 % dérivée de R307 = 47 kΩ.
    //   Comme |m_state1| = 1.0 >> m_hyst2, ce trigger suit m_state1
    //   sans délai ; il garantit toutefois l'immunité aux glitches
    //   transitoires sur le signal d'entrée.
    if      (m_state1 >=  m_hyst2) m_state2 =  1.0f;
    else if (m_state1 <= -m_hyst2) m_state2 = -1.0f;

    return m_state2;
}
