#include "signal_analyzer.h"

#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================
//  Enveloppe locale (two-pass peak-hold à décroissance exponentielle)
//  decaySec : constante de temps — fenêtre courte (5 ms) pour
//  détecter rapidement les débuts/fins de bloc.
// ============================================================
static std::vector<float> computeEnvelope(const std::vector<float>& s,
                                          uint32_t sampleRate,
                                          float decaySec = 0.005f) {
    const size_t N = s.size();
    std::vector<float> env(N, 0.0f);

    const float decay = 1.0f - 1.0f / (static_cast<float>(sampleRate) * decaySec);

    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = peak;
    }
    peak = 0.0f;
    for (size_t i = N; i-- > 0; ) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = std::max(env[i], peak);
    }

    return env;
}

// ============================================================
//  segmentSignal
// ============================================================
SegmentationResult segmentSignal(const WavReader& reader,
                                 const SegmentationParams& params) {
    const auto&    s  = reader.samples();
    const size_t   N  = s.size();
    const uint32_t sr = reader.info().sampleRate;

    SegmentationResult result;
    result.params = params;

    if (N == 0) return result;

    // --- Étape 1 : enveloppe courte (5 ms) ---
    const std::vector<float> env = computeEnvelope(s, sr);

    // --- Étape 2 : détection des silences valides ---
    // Un silence est valide s'il dure >= silenceMinDurSec.
    struct Span { size_t start, end; };
    std::vector<Span> silences;

    const size_t minSilenceSamples =
        static_cast<size_t>(params.silenceMinDurSec * sr);

    size_t silStart = 0;
    bool   inSil    = false;

    for (size_t i = 0; i <= N; ++i) {
        const bool silent = (i < N) && (env[i] < params.silenceThreshold);

        if (!inSil && silent) {
            silStart = i;
            inSil    = true;
        } else if (inSil && (!silent || i == N)) {
            if (i - silStart >= minSilenceSamples)
                silences.push_back({silStart, i});
            inSil = false;
        }
    }

    // --- Étape 3 : construction des blocs entre les silences ---
    // Les silences sont des séparateurs ; les régions entre eux sont des blocs.
    std::vector<size_t> blockStarts = {0};
    std::vector<size_t> blockEnds;

    for (const auto& sil : silences) {
        blockEnds.push_back(sil.start);
        blockStarts.push_back(sil.end);
    }
    blockEnds.push_back(N);

    int blockIndex = 1;
    for (size_t i = 0; i < blockStarts.size(); ++i) {
        const size_t start = blockStarts[i];
        const size_t end   = blockEnds[i];

        if (end <= start) continue;

        // Amplitude max dans le bloc
        float maxAmp = 0.0f;
        for (size_t j = start; j < end; ++j)
            maxAmp = std::max(maxAmp, fabsf(s[j]));

        // Ignorer les régions trop silencieuses (ex: silence de début/fin de fichier)
        if (maxAmp < params.silenceThreshold) continue;

        // Ignorer les blocs trop courts (bruit dans une plage de silence)
        const double durSec = static_cast<double>(end - start) / sr;
        if (durSec < params.blockMinDurSec) continue;

        Block b;
        b.index        = blockIndex++;
        b.startSample  = start;
        b.endSample    = end;
        b.startSec     = static_cast<double>(start) / sr;
        b.endSec       = static_cast<double>(end)   / sr;
        b.durationSec  = b.endSec - b.startSec;
        b.maxAmplitude = maxAmp;
        result.blocks.push_back(b);
    }

    return result;
}
