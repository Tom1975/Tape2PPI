#include "batch_processor.h"

#include "wav_reader.h"
#include "source_detector.h"
#include "signal_analyzer.h"
#include "block_analyzer.h"
#include "protection_detector.h"
#include "dump_matcher.h"
#include "signal_converter.h"
#include "conversion_validator.h"
#include "wav_writer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
//  Structure interne : analyse complète d'un fichier
// ============================================================
struct BatchEntry {
    std::string filepath;
    WavReader   reader;
    DetectionResult       detection;
    SegmentationResult    seg;
    std::vector<BlockAnalysis> analyses;
    ProtectionAnalysis    protection;
};

static bool analyzeEntry(const std::string& path, BatchEntry& e) {
    e.filepath = path;
    if (!e.reader.load(path)) {
        printf("  ERREUR [%s] : %s\n", path.c_str(), e.reader.error().c_str());
        return false;
    }
    e.detection  = detectSource(e.reader);
    e.seg        = segmentSignal(e.reader);
    e.analyses.reserve(e.seg.blocks.size());
    for (const Block& b : e.seg.blocks)
        e.analyses.push_back(analyzeBlock(e.reader, b));
    e.protection = detectProtection(e.analyses, e.reader.info().sampleRate);
    return true;
}

// ============================================================
//  runBatch
// ============================================================

void runBatch(const std::string& directory)
{
    // --- 1. Lister les fichiers WAV (hors fichiers générés par l'outil) ---
    // Suffixes exclus : fichiers produits par une conversion précédente
    auto isGenerated = [](const std::string& name) {
        static const char* suffixes[] = {
            "_to_PPI.wav", "_to_cassette.wav",
            "_converted_to_PPI.wav", "_converted_to_cassette.wav",
            nullptr
        };
        for (int i = 0; suffixes[i]; ++i) {
            const std::string s(suffixes[i]);
            if (name.size() >= s.size() &&
                name.compare(name.size() - s.size(), s.size(), s) == 0)
                return true;
        }
        return false;
    };

    std::vector<std::string> wavFiles;
    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            const std::string stem = entry.path().filename().string();
            const std::string ext  = entry.path().extension().string();
            if ((ext == ".wav" || ext == ".WAV" || ext == ".Wav") && !isGenerated(stem))
                wavFiles.push_back(entry.path().string());
        }
    } catch (const fs::filesystem_error& ex) {
        printf("ERREUR répertoire [%s] : %s\n", directory.c_str(), ex.what());
        return;
    }

    if (wavFiles.empty()) {
        printf("Aucun fichier WAV trouvé dans %s\n", directory.c_str());
        return;
    }

    std::sort(wavFiles.begin(), wavFiles.end());
    printf("══════════════════════════════════════════\n");
    printf("  BATCH : %zu fichier(s) WAV dans %s\n", wavFiles.size(), directory.c_str());
    printf("══════════════════════════════════════════\n\n");

    // --- 2. Analyser tous les fichiers ---
    std::vector<BatchEntry> entries;
    entries.reserve(wavFiles.size());
    for (const auto& path : wavFiles) {
        BatchEntry e;
        printf("  Analyse : %s\n", path.c_str());
        if (analyzeEntry(path, e)) {
            const char* srcStr = (e.detection.source == SourceType::CASSETTE) ? "CASSETTE" :
                                 (e.detection.source == SourceType::PPI)      ? "PPI"      : "?";
            printf("    → %s  %zu blocs  prot=%s\n",
                   srcStr, e.seg.blocks.size(), e.protection.name.c_str());
            entries.push_back(std::move(e));
        }
    }
    printf("\n");

    // --- 3. Séparer cassettes / PPI ---
    std::vector<BatchEntry*> cassettes, ppis;
    for (auto& e : entries) {
        if (e.detection.source == SourceType::CASSETTE) cassettes.push_back(&e);
        else if (e.detection.source == SourceType::PPI) ppis.push_back(&e);
    }

    printf("  Cassettes : %zu  /  PPI : %zu\n\n", cassettes.size(), ppis.size());

    if (cassettes.empty() || ppis.empty()) {
        printf("Pas de paire cassette/PPI possible.\n");
        return;
    }

    // --- 4. Appariement toutes paires cassette × PPI ---
    struct MatchedPair {
        BatchEntry* cas;
        BatchEntry* ppi;
        DumpMatch   match;
    };
    std::vector<MatchedPair> matched;

    for (BatchEntry* cas : cassettes) {
        for (BatchEntry* ppi : ppis) {
            const DumpMatch m = matchDumps(
                cas->seg.blocks, cas->analyses, cas->protection,
                ppi->seg.blocks, ppi->analyses, ppi->protection);
            if (m.result != DumpMatch::Result::FAILED) {
                printf("  MATCH  %s\n         ↔ %s\n         confiance=%.0f%%  ratio=×%.4f\n\n",
                       cas->filepath.c_str(), ppi->filepath.c_str(),
                       m.confidence * 100.0, m.speedRatio);
                matched.push_back({cas, ppi, m});
            }
        }
    }

    if (matched.empty()) {
        printf("Aucune paire appariée.\n");
        return;
    }

    // --- 5. Conversion + validation pour chaque paire ---
    int   totalPairs      = 0;
    int   goodPairs       = 0;
    double sumScore       = 0.0;

    for (MatchedPair& mp : matched) {
        ++totalPairs;
        printf("══════════════════════════════════════════\n");
        printf("  CONVERSION  %s\n             → PPI\n",
               mp.cas->filepath.c_str());
        printf("══════════════════════════════════════════\n");

        const ConversionParams params = extractConversionParams(
            mp.cas->analyses, mp.match);

        // Conversion cassette → PPI
        const std::vector<float> converted = convertToPPI(
            mp.cas->reader.samples(),
            mp.cas->reader.info().sampleRate);

        // Écriture fichier converti
        const std::string outPath = mp.cas->filepath + "_to_PPI.wav";
        if (writeWav(outPath.c_str(), converted, mp.cas->reader.info().sampleRate))
            printf("  Écrit : %s\n", outPath.c_str());
        else
            printf("  ERREUR écriture %s\n", outPath.c_str());

        // Analyse du signal converti (en mémoire via WavReader rechargé)
        WavReader convReader;
        if (!convReader.load(outPath)) {
            printf("  ERREUR relecture converti : %s\n", convReader.error().c_str());
            continue;
        }

        const SegmentationResult convSeg  = segmentSignal(convReader);
        std::vector<BlockAnalysis> convAnalyses;
        convAnalyses.reserve(convSeg.blocks.size());
        for (const Block& b : convSeg.blocks)
            convAnalyses.push_back(analyzeBlock(convReader, b));

        // Validation : converti vs PPI de référence
        const ConversionQuality q = validateConversion(mp.ppi->analyses, convAnalyses, mp.match.speedRatio);
        printConversionQuality(q);

        sumScore  += q.overallScore;
        if (q.overallScore >= 0.70) ++goodPairs;
    }

    // --- 6. Bilan global ---
    printf("══════════════════════════════════════════\n");
    printf("  BILAN BATCH\n");
    printf("══════════════════════════════════════════\n");
    printf("  Paires converties : %d\n", totalPairs);
    printf("  Paires bonnes     : %d  (score ≥ 70%%)\n", goodPairs);
    if (totalPairs > 0)
        printf("  Score moyen       : %.0f%%\n", sumScore / totalPairs * 100.0);
    printf("\n");
}
