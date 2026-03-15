#pragma once

#include "wav_reader.h"
#include <vector>

// ============================================================
//  Paramètres de segmentation
// ============================================================
struct SegmentationParams {
    float  silenceThreshold = 0.05f;   // niveau d'enveloppe en dessous duquel = silence
    double silenceMinDurSec = 0.100;   // durée minimale d'un silence pour couper (s)
    double blockMinDurSec   = 0.500;   // durée minimale d'un bloc (blocs plus courts = bruit ignoré)
};

// ============================================================
//  Bloc : segment de signal entre deux silences
// ============================================================
struct Block {
    int     index;          // numéro 1-based
    size_t  startSample;
    size_t  endSample;
    double  startSec;
    double  endSec;
    double  durationSec;
    float   maxAmplitude;
};

// ============================================================
//  Résultat de la segmentation
// ============================================================
struct SegmentationResult {
    std::vector<Block>  blocks;
    SegmentationParams  params;
};

// Découpe le signal en blocs séparés par des silences.
SegmentationResult segmentSignal(const WavReader& reader,
                                 const SegmentationParams& params = {});
