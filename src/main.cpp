#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "wav_reader.h"
#include "source_detector.h"
#include "signal_analyzer.h"
#include "block_analyzer.h"
#include "protection_detector.h"
#include "dump_matcher.h"

// ============================================================
//  Structure regroupant l'analyse complète d'un fichier
// ============================================================
struct DumpInfo {
    std::string           filepath;
    WavReader             reader;
    DetectionResult       detection;
    SegmentationResult    seg;
    std::vector<BlockAnalysis> analyses;
    ProtectionAnalysis    protection;
};

// ============================================================
//  Analyse complète d'un fichier (sans affichage)
// ============================================================
static bool analyzeDump(const char* filepath, DumpInfo& out) {
    out.filepath = filepath;
    if (!out.reader.load(filepath)) {
        printf("ERREUR [%s] : %s\n\n", filepath, out.reader.error().c_str());
        return false;
    }
    out.detection  = detectSource(out.reader);
    out.seg        = segmentSignal(out.reader);
    out.analyses.reserve(out.seg.blocks.size());
    for (const Block& b : out.seg.blocks)
        out.analyses.push_back(analyzeBlock(out.reader, b));
    out.protection = detectProtection(out.analyses, out.reader.info().sampleRate);
    return true;
}

// ============================================================
//  Affichage détaillé d'un dump
// ============================================================
static void printDump(const DumpInfo& d) {
    const WavInfo& info = d.reader.info();
    printf("=== %s ===\n", d.filepath.c_str());
    printf("  Canaux        : %u\n",    info.numChannels);
    printf("  Sample rate   : %u Hz\n", info.sampleRate);
    printf("  Bits/sample   : %u\n",    info.bitsPerSample);
    printf("  Durée         : %.3f s\n", info.durationSec);

    switch (d.detection.source) {
        case SourceType::PPI:
            printf("  Source        : PPI (signal quasi-carré)\n\n"); break;
        case SourceType::CASSETTE:
            printf("  Source        : CASSETTE (signal analogique)\n\n"); break;
        default:
            printf("  Source        : INDÉTERMINÉ\n\n"); break;
    }

    printf("  Blocs détectés : %zu\n", d.seg.blocks.size());
    for (size_t i = 0; i < d.seg.blocks.size(); ++i) {
        const Block&         b  = d.seg.blocks[i];
        const BlockAnalysis& ba = d.analyses[i];
        printf("    [%2d] %8.3f s → %8.3f s  (%.3f s)  amp max=%.4f\n",
               b.index, b.startSec, b.endSec, b.durationSec, b.maxAmplitude);
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

    printf("  Protection     : ");
    switch (d.protection.type) {
        case ProtectionType::STANDARD_ROM:
            printf("Standard ROM  (confiance %.0f%%)\n", d.protection.confidence * 100); break;
        case ProtectionType::SPEEDLOCK:
            printf("Speedlock     (confiance %.0f%%)\n", d.protection.confidence * 100); break;
        case ProtectionType::BLEEPLOAD:
            printf("Bleepload     (confiance %.0f%%)\n", d.protection.confidence * 100); break;
        default:
            printf("Inconnue      (confiance max %.0f%%)\n", d.protection.confidence * 100); break;
    }
    printf("  Détails        : %s\n", d.protection.details.c_str());
    printf("\n");
}

// ============================================================
//  Affichage de l'appariement de deux dumps
// ============================================================
static void printMatch(const DumpInfo& d1, const DumpInfo& d2, const DumpMatch& m) {
    printf("══════════════════════════════════════════\n");
    printf("  APPARIEMENT  %s  ↔  %s\n",
           d1.filepath.c_str(), d2.filepath.c_str());
    printf("══════════════════════════════════════════\n");

    switch (m.result) {
        case DumpMatch::Result::MATCHED:
            printf("  Résultat      : APPARIÉ      (confiance %.0f%%)\n",
                   m.confidence * 100); break;
        case DumpMatch::Result::PARTIAL:
            printf("  Résultat      : PARTIEL      (confiance %.0f%%)\n",
                   m.confidence * 100); break;
        default:
            printf("  Résultat      : ÉCHEC        (confiance %.0f%%)\n",
                   m.confidence * 100); break;
    }

    printf("  Blocs         : %d/%d appariés\n", m.matchedPairs,
           std::max(m.totalBlocks1, m.totalBlocks2));
    printf("  Speed ratio   : ×%.4f  (dump2 est %.1f%% %s rapide)\n",
           m.speedRatio,
           std::fabs(m.speedRatio - 1.0) * 100.0,
           m.speedRatio < 1.0 ? "plus" : "moins");
    printf("  Consistance   : %.0f%%\n", m.speedConsistency * 100);
    printf("  Détails       : %s\n\n", m.notes.c_str());

    if (!m.pairs.empty()) {
        printf("  %-5s  %-10s  %-10s  %s\n",
               "Bloc", "Dur1 (s)", "Dur2 (s)", "Ratio");
        for (const BlockPair& bp : m.pairs) {
            printf("  [%2d↔%2d]  %8.3f    %8.3f   ×%.4f%s\n",
                   bp.idx1, bp.idx2, bp.dur1, bp.dur2, bp.ratio,
                   bp.structureCompat ? "" : "  ⚠ struct incompatible");
        }
    }
    printf("\n");
}

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s fichier1.wav [fichier2.wav]\n", argv[0]);
        printf("  1 fichier  : analyse individuelle\n");
        printf("  2 fichiers : analyse + appariement\n");
        return 1;
    }

    // Mode analyse individuelle (1 ou plusieurs fichiers)
    if (argc != 3) {
        for (int i = 1; i < argc; ++i) {
            DumpInfo d;
            if (analyzeDump(argv[i], d))
                printDump(d);
        }
        return 0;
    }

    // Mode comparaison (exactement 2 fichiers)
    DumpInfo d1, d2;
    if (!analyzeDump(argv[1], d1) || !analyzeDump(argv[2], d2))
        return 1;

    printDump(d1);
    printDump(d2);

    const DumpMatch m = matchDumps(
        d1.seg.blocks, d1.analyses, d1.protection,
        d2.seg.blocks, d2.analyses, d2.protection);
    printMatch(d1, d2, m);

    return 0;
}
