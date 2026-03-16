#define _CRT_SECURE_NO_WARNINGS
#include "wav_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// ============================================================
//  Helpers little-endian
// ============================================================

static void writeU16(FILE* f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}

static void writeU32(FILE* f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8)  & 0xFF),
                     static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 24) & 0xFF) };
    fwrite(b, 1, 4, f);
}

// ============================================================
//  writeWav
// ============================================================

bool writeWav(const char* path,
              const std::vector<float>& samples,
              uint32_t sampleRate)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    const uint32_t numSamples   = static_cast<uint32_t>(samples.size());
    const uint16_t numChannels  = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate     = sampleRate * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign   = numChannels * bitsPerSample / 8;
    const uint32_t dataBytes    = numSamples * blockAlign;
    const uint32_t riffSize     = 36 + dataBytes;  // taille du chunk RIFF (après "RIFF")

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    writeU32(f, riffSize);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    writeU32(f, 16);                // taille du chunk fmt (PCM = 16)
    writeU16(f, 1);                 // AudioFormat : PCM
    writeU16(f, numChannels);
    writeU32(f, sampleRate);
    writeU32(f, byteRate);
    writeU16(f, blockAlign);
    writeU16(f, bitsPerSample);

    // data chunk
    fwrite("data", 1, 4, f);
    writeU32(f, dataBytes);

    // Samples : float [-1,+1] → int16
    for (float s : samples) {
        const float clamped = std::max(-1.0f, std::min(1.0f, s));
        const int16_t pcm   = static_cast<int16_t>(clamped * 32767.0f);
        uint8_t b[2] = { static_cast<uint8_t>(pcm & 0xFF),
                         static_cast<uint8_t>((pcm >> 8) & 0xFF) };
        fwrite(b, 1, 2, f);
    }

    fclose(f);
    return true;
}
