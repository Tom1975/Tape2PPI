#pragma once

#include <cstdint>
#include <vector>

// Écrit un fichier WAV PCM 16 bits signé mono.
// samples : valeurs normalisées [-1.0, +1.0], clampées à la conversion.
// Retourne true si succès.
bool writeWav(const char* path,
              const std::vector<float>& samples,
              uint32_t sampleRate);
