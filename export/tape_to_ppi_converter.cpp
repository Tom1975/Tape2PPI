// ============================================================
//  tape_to_ppi_converter.cpp
//
//  Convertisseur cassette → PPI pour émulateur Amstrad CPC.
//  Modélise l'interface analogique du 8255 PPI.
//
//  Dépendances : aucune (C++11 pur, <cmath> uniquement).
//
//  Généré par cpc_wav_analyzer (projet Tape2PPI).
// ============================================================

#include "tape_to_ppi_converter.h"

#include <algorithm>
#include <cmath>

// ============================================================
//  Constructeur
// ============================================================

TapeToPPIConverter::TapeToPPIConverter(uint32_t sampleRate)
    : m_sampleRate(sampleRate)
    , m_hpAlpha(0.0)
    , m_hpPrev(0.0f)
    , m_hpOut(0.0f)
    , m_hystFrac(0.05f)
    , m_state(1.0f)
    , m_rmsAlpha(0.0)
    , m_rmsEst(0.1f)
{
    setACCouplingHz(7.0);   // ~22 ms — condensateur de liaison typique CPC
    // Fenêtre RMS : ~10 ms
    m_rmsAlpha = std::exp(-1.0 / (sampleRate * 0.010));
}

// ============================================================
//  reset
// ============================================================

void TapeToPPIConverter::reset()
{
    m_hpPrev  = 0.0f;
    m_hpOut   = 0.0f;
    m_state   = 1.0f;
    m_rmsEst  = 0.1f;
}

// ============================================================
//  setACCouplingHz
// ============================================================

void TapeToPPIConverter::setACCouplingHz(double cutoffHz)
{
    // Filtre passe-haut du 1er ordre : alpha = exp(-2π·fc/fs)
    // La réponse de ce filtre est :  y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    m_hpAlpha = std::exp(-2.0 * M_PI * cutoffHz / m_sampleRate);
}

// ============================================================
//  setHysteresis
// ============================================================

void TapeToPPIConverter::setHysteresis(float frac)
{
    m_hystFrac = frac;
}

// ============================================================
//  process — traitement échantillon par échantillon (causal)
// ============================================================

float TapeToPPIConverter::process(float in)
{
    // --- 1. Filtre passe-haut (couplage AC) ---
    //   Supprime le DC offset et les dérives basses fréquences.
    //   Formule : y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    const float hp = static_cast<float>(m_hpAlpha) * (m_hpOut + in - m_hpPrev);
    m_hpPrev = in;
    m_hpOut  = hp;

    // --- 2. Mise à jour RMS locale (fenêtre exponentielle ~10 ms) ---
    const float a   = static_cast<float>(m_rmsAlpha);
    m_rmsEst = a * m_rmsEst + (1.0f - a) * (hp * hp);
    // rmsEst est la variance ; sqrt pour obtenir RMS
    const float rms  = std::sqrt(m_rmsEst);
    const float hyst = std::max(HYST_MIN, m_hystFrac * rms);

    // --- 3. Trigger de Schmitt ---
    if (hp >= hyst)
        m_state = 1.0f;
    else if (hp <= -hyst)
        m_state = -1.0f;
    // Zone morte [−hyst, +hyst] : on maintient l'état

    return m_state;
}
