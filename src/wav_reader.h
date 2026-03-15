#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================
//  WavInfo — métadonnées extraites du header WAV
// ============================================================
struct WavInfo {
    uint16_t numChannels    = 0;
    uint32_t sampleRate     = 0;
    uint16_t bitsPerSample  = 0;
    uint32_t numSamples     = 0;   // par canal
    double   durationSec    = 0.0;
};

// ============================================================
//  WavReader
//  Lit un fichier WAV PCM (8/16/24/32 bits, mono ou stéréo).
//  Les samples sont normalisés en float [-1.0, +1.0].
//  Pour un fichier stéréo, un downmix vers mono est effectué.
// ============================================================
class WavReader {
public:
    // Charge le fichier. Retourne true en cas de succès.
    bool load(const std::string& filepath);

    // Métadonnées
    const WavInfo& info() const { return m_info; }

    // Samples normalisés (mono, float [-1,+1])
    const std::vector<float>& samples() const { return m_samples; }

    // Dernier message d'erreur
    const std::string& error() const { return m_error; }

private:
    WavInfo             m_info;
    std::vector<float>  m_samples;
    std::string         m_error;

    // Lecture des chunks RIFF
    bool parseRiff(FILE* f);
    bool parseFmt (FILE* f, uint32_t chunkSize);
    bool parseData(FILE* f, uint32_t chunkSize);

    // Normalisation d'un sample brut selon la profondeur
    float normalizeSample(int32_t raw) const;

    // Lecture little-endian
    static uint16_t readU16(FILE* f);
    static uint32_t readU32(FILE* f);
    static int32_t  readI24(FILE* f); // 3 octets signés
};
