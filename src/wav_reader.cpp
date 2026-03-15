#include "wav_reader.h"

#include <cstdio>
#include <cstring>
#include <cmath>

// ============================================================
//  Helpers little-endian
// ============================================================
uint16_t WavReader::readU16(FILE* f) {
    uint8_t b[2] = {};
    if (fread(b, 1, 2, f) != 2) return 0;
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

uint32_t WavReader::readU32(FILE* f) {
    uint8_t b[4] = {};
    if (fread(b, 1, 4, f) != 4) return 0;
    return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

int32_t WavReader::readI24(FILE* f) {
    uint8_t b[3] = {};
    if (fread(b, 1, 3, f) != 3) return 0;
    // Reconstruction signée sur 32 bits
    int32_t val = static_cast<int32_t>(b[0] | (b[1] << 8) | (b[2] << 16));
    if (val & 0x800000)          // bit de signe 24 bits
        val |= 0xFF000000;       // extension de signe
    return val;
}

// ============================================================
//  Normalisation sample → float [-1, +1]
// ============================================================
float WavReader::normalizeSample(int32_t raw) const {
    switch (m_info.bitsPerSample) {
        case 8:  return (static_cast<float>(raw) - 128.0f) / 128.0f;
        case 16: return static_cast<float>(raw) / 32768.0f;
        case 24: return static_cast<float>(raw) / 8388608.0f;   // 2^23
        case 32: return static_cast<float>(raw) / 2147483648.0f; // 2^31
        default: return 0.0f;
    }
}

// ============================================================
//  Parsing du chunk "fmt "
// ============================================================
bool WavReader::parseFmt(FILE* f, uint32_t chunkSize) {
    if (chunkSize < 16) {
        m_error = "Chunk fmt trop petit";
        return false;
    }

    uint16_t audioFormat = readU16(f);
    if (audioFormat != 1) {
        m_error = "Format non supporté (seul PCM=1 est accepté, trouvé: "
                  + std::to_string(audioFormat) + ")";
        return false;
    }

    m_info.numChannels   = readU16(f);
    m_info.sampleRate    = readU32(f);
    /* byteRate  */         readU32(f); // ignoré
    /* blockAlign */        readU16(f); // ignoré
    m_info.bitsPerSample = readU16(f);

    if (m_info.bitsPerSample != 8  && m_info.bitsPerSample != 16 &&
        m_info.bitsPerSample != 24 && m_info.bitsPerSample != 32) {
        m_error = "Profondeur non supportée: " + std::to_string(m_info.bitsPerSample);
        return false;
    }

    if (m_info.numChannels == 0) {
        m_error = "Nombre de canaux invalide";
        return false;
    }

    // Consommer les octets restants du chunk fmt (extensions éventuelles)
    if (chunkSize > 16)
        fseek(f, static_cast<long>(chunkSize - 16), SEEK_CUR);

    return true;
}

// ============================================================
//  Parsing du chunk "data"
// ============================================================
bool WavReader::parseData(FILE* f, uint32_t chunkSize) {
    const uint32_t bytesPerSample = m_info.bitsPerSample / 8;
    const uint32_t frameSize      = bytesPerSample * m_info.numChannels;

    if (frameSize == 0) {
        m_error = "frameSize nul — fmt non parsé avant data ?";
        return false;
    }

    const uint32_t numFrames = chunkSize / frameSize;
    m_info.numSamples = numFrames;
    m_info.durationSec = static_cast<double>(numFrames) / m_info.sampleRate;

    m_samples.reserve(numFrames);

    for (uint32_t i = 0; i < numFrames; ++i) {
        float mono = 0.0f;

        for (uint16_t ch = 0; ch < m_info.numChannels; ++ch) {
            int32_t raw = 0;

            switch (m_info.bitsPerSample) {
                case 8:
                    raw = static_cast<int32_t>(fgetc(f));
                    break;
                case 16:
                    raw = static_cast<int16_t>(readU16(f)); // cast signé
                    break;
                case 24:
                    raw = readI24(f);
                    break;
                case 32: {
                    uint32_t u = readU32(f);
                    raw = static_cast<int32_t>(u);
                    break;
                }
            }

            mono += normalizeSample(raw);
        }

        // Downmix : moyenne des canaux
        m_samples.push_back(mono / static_cast<float>(m_info.numChannels));
    }

    return true;
}

// ============================================================
//  Parsing RIFF global — itère sur les chunks
// ============================================================
bool WavReader::parseRiff(FILE* f) {
    // Header RIFF
    char tag[4];
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "RIFF", 4) != 0) {
        m_error = "Pas un fichier RIFF";
        return false;
    }
    readU32(f); // fileSize (ignoré)

    char wave[4];
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) {
        m_error = "Pas un fichier WAVE";
        return false;
    }

    bool fmtFound  = false;
    bool dataFound = false;

    // Itération sur les sous-chunks
    while (!feof(f)) {
        char chunkId[4];
        if (fread(chunkId, 1, 4, f) != 4) break;

        uint32_t chunkSize = readU32(f);

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            if (!parseFmt(f, chunkSize)) return false;
            fmtFound = true;
        }
        else if (memcmp(chunkId, "data", 4) == 0) {
            if (!fmtFound) {
                m_error = "Chunk data trouvé avant fmt";
                return false;
            }
            if (!parseData(f, chunkSize)) return false;
            dataFound = true;
            break; // On s'arrête après le premier chunk data
        }
        else {
            // Chunk inconnu (LIST, INFO, fact, etc.) → on saute
            fseek(f, static_cast<long>(chunkSize), SEEK_CUR);
        }
    }

    if (!fmtFound)  { m_error = "Chunk fmt absent"; return false; }
    if (!dataFound) { m_error = "Chunk data absent"; return false; }

    return true;
}

// ============================================================
//  Point d'entrée public
// ============================================================
bool WavReader::load(const std::string& filepath) {
    m_samples.clear();
    m_error.clear();
    m_info = WavInfo{};

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        m_error = "Impossible d'ouvrir le fichier : " + filepath;
        return false;
    }

    bool ok = parseRiff(f);
    fclose(f);
    return ok;
}
