#include <cstdio>
#include "wav_reader.h"
#include "source_detector.h"
#include "signal_analyzer.h"
#include "block_analyzer.h"
#include "protection_detector.h"

// ============================================================
//  Affiche les infos d'un fichier WAV chargé
// ============================================================
static void printInfo(const WavReader& reader, const char* filepath) {
    const WavInfo& info = reader.info();
    printf("=== %s ===\n", filepath);
    printf("  Canaux        : %u\n",   info.numChannels);
    printf("  Sample rate   : %u Hz\n", info.sampleRate);
    printf("  Bits/sample   : %u\n",   info.bitsPerSample);
    printf("  Nb samples    : %u\n",   info.numSamples);
    printf("  Durée         : %.3f s\n", info.durationSec);

    float vmin = 0.0f, vmax = 0.0f;
    for (float s : reader.samples()) {
        if (s < vmin) vmin = s;
        if (s > vmax) vmax = s;
    }
    printf("  Amplitude min : %.4f\n", vmin);
    printf("  Amplitude max : %.4f\n", vmax);
    printf("\n");
}

// ============================================================
//  Affiche le résultat de la détection de source
// ============================================================
static void printDetection(const DetectionResult& r) {
    printf("  Actifs        : %ld échantillons\n", r.activeCount);
    printf("  Transitions   : %ld flancs détectés\n", r.edgeCount);
    printf("  Ratio flancs  : %.2f%% des actifs (larg. moy. %.2f samples)\n",
           r.spikeRatio, r.avgSpikeWidth);

    switch (r.source) {
        case SourceType::PPI:      printf("  Source        : PPI (signal quasi-carré)\n\n");       break;
        case SourceType::CASSETTE: printf("  Source        : CASSETTE (signal analogique)\n\n");   break;
        default:                   printf("  Source        : INDÉTERMINÉ\n\n");                    break;
    }
}

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s fichier1.wav [fichier2.wav ...]\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        WavReader reader;
        if (!reader.load(argv[i])) {
            printf("ERREUR [%s] : %s\n\n", argv[i], reader.error().c_str());
            continue;
        }
        printInfo(reader, argv[i]);
        printDetection(detectSource(reader));

        const SegmentationResult seg = segmentSignal(reader);
        printf("  Blocs détectés : %zu  (seuil=%.2f, silence min=%.0f ms, bloc min=%.0f ms)\n",
               seg.blocks.size(),
               seg.params.silenceThreshold,
               seg.params.silenceMinDurSec * 1000.0,
               seg.params.blockMinDurSec   * 1000.0);

        // Analyser tous les blocs, puis détecter la protection au niveau du dump
        std::vector<BlockAnalysis> allAnalyses;
        allAnalyses.reserve(seg.blocks.size());
        for (const Block& b : seg.blocks) {
            printf("    [%2d] %8.3f s → %8.3f s  (%.3f s)  amp max=%.4f\n",
                   b.index, b.startSec, b.endSec, b.durationSec, b.maxAmplitude);

            const BlockAnalysis ba = analyzeBlock(reader, b);
            allAnalyses.push_back(ba);
            switch (ba.structure) {
                case BlockAnalysis::Structure::PILOT_DATA:
                    printf("         Pilote : %.0f Hz — %.3f s (%ld fronts)\n",
                           ba.pilotFreqHz, ba.pilotDurSec, ba.pilotEdgeCount);
                    if (ba.encodingValid)
                        printf("         Codage : S=%.1f L=%.1f samples (×%.2f)\n",
                               ba.shortHP, ba.longHP, ba.longHP / ba.shortHP);
                    if (ba.hasSyncPulse)
                        printf("         Sync   : pulse détecté\n");
                    if (ba.firstByteValid)
                        printf("         Octet1 : 0x%02X ✓\n", ba.firstByte);
                    else
                        printf("         Octet1 : non décodé\n");
                    printf("         Données: à partir de %.3f s\n", ba.dataStartSec);
                    break;
                case BlockAnalysis::Structure::PILOT_ONLY:
                    printf("         Pilote seul : %.0f Hz — %.3f s (%ld fronts)\n",
                           ba.pilotFreqHz, ba.pilotDurSec, ba.pilotEdgeCount);
                    break;
                case BlockAnalysis::Structure::DATA_ONLY:
                    printf("         Données seules (%ld fronts)\n", ba.totalEdgeCount);
                    break;
                default:
                    printf("         Structure indéterminée (%ld fronts)\n", ba.totalEdgeCount);
                    break;
            }
        }

        // Protection au niveau du dump
        const ProtectionAnalysis prot = detectProtection(allAnalyses,
                                                          reader.info().sampleRate);
        printf("  Protection     : ");
        switch (prot.type) {
            case ProtectionType::STANDARD_ROM:
                printf("Standard ROM  (confiance %.0f%%)\n", prot.confidence * 100);
                break;
            case ProtectionType::SPEEDLOCK:
                printf("Speedlock     (confiance %.0f%%)\n", prot.confidence * 100);
                break;
            case ProtectionType::BLEEPLOAD:
                printf("Bleepload     (confiance %.0f%%)\n", prot.confidence * 100);
                break;
            default:
                printf("Inconnue      (confiance max %.0f%%)\n", prot.confidence * 100);
                break;
        }
        printf("  Détails        : %s\n", prot.details.c_str());
        printf("\n");
    }

    return 0;
}
