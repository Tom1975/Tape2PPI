#include "dataset_exporter.h"

#include "wav_reader.h"
#include "wav_writer.h"
#include "source_detector.h"
#include "signal_analyzer.h"
#include "block_analyzer.h"
#include "protection_detector.h"
#include "dump_matcher.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
//  Helpers
// ============================================================

static bool isGeneratedFile(const std::string& name) {
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
}

static std::string structureStr(BlockAnalysis::Structure s) {
    switch (s) {
        case BlockAnalysis::Structure::PILOT_DATA: return "PILOT_DATA";
        case BlockAnalysis::Structure::PILOT_ONLY: return "PILOT_ONLY";
        case BlockAnalysis::Structure::DATA_ONLY:  return "DATA_ONLY";
        default:                                   return "UNKNOWN";
    }
}

// Échappe les caractères spéciaux pour JSON
static std::string jsonStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

// Trouve un bloc par son index (1-based) dans un vecteur de blocs
static const Block* findBlock(const std::vector<Block>& blocks, int idx) {
    for (const Block& b : blocks)
        if (b.index == idx) return &b;
    return nullptr;
}

static const BlockAnalysis* findAnalysis(const std::vector<BlockAnalysis>& analyses, int idx) {
    for (const BlockAnalysis& a : analyses)
        if (a.blockIndex == idx) return &a;
    return nullptr;
}

// ============================================================
//  Structure interne
// ============================================================
struct ExportEntry {
    std::string filepath;
    WavReader   reader;
    DetectionResult            detection;
    SegmentationResult         seg;
    std::vector<BlockAnalysis> analyses;
    ProtectionAnalysis         protection;
};

static bool analyzeEntry(const std::string& path, ExportEntry& e) {
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
//  exportDataset
// ============================================================
void exportDataset(const std::string& inputDir, const std::string& outputDir)
{
    // --- Créer le répertoire de sortie ---
    try { fs::create_directories(outputDir); }
    catch (...) {
        printf("ERREUR : impossible de créer %s\n", outputDir.c_str());
        return;
    }

    // --- 1. Lister les fichiers WAV source ---
    std::vector<std::string> wavFiles;
    try {
        for (const auto& entry : fs::directory_iterator(inputDir)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            const std::string ext  = entry.path().extension().string();
            if ((ext == ".wav" || ext == ".WAV" || ext == ".Wav") && !isGeneratedFile(name))
                wavFiles.push_back(entry.path().string());
        }
    } catch (const fs::filesystem_error& ex) {
        printf("ERREUR répertoire [%s] : %s\n", inputDir.c_str(), ex.what());
        return;
    }

    if (wavFiles.empty()) {
        printf("Aucun fichier WAV trouvé dans %s\n", inputDir.c_str());
        return;
    }

    std::sort(wavFiles.begin(), wavFiles.end());
    printf("══════════════════════════════════════════\n");
    printf("  EXPORT DATASET : %zu fichier(s) dans %s\n", wavFiles.size(), inputDir.c_str());
    printf("  Sortie : %s\n", outputDir.c_str());
    printf("══════════════════════════════════════════\n\n");

    // --- 2. Analyser tous les fichiers ---
    std::vector<ExportEntry> entries;
    entries.reserve(wavFiles.size());
    for (const auto& path : wavFiles) {
        ExportEntry e;
        printf("  Analyse : %s\n", path.c_str());
        if (analyzeEntry(path, e)) {
            const char* src = (e.detection.source == SourceType::CASSETTE) ? "CASSETTE" :
                              (e.detection.source == SourceType::PPI)      ? "PPI"      : "?";
            printf("    → %s  %zu blocs  prot=%s\n",
                   src, e.seg.blocks.size(), e.protection.name.c_str());
            entries.push_back(std::move(e));
        }
    }
    printf("\n");

    // --- 3. Séparer cassettes / PPI ---
    std::vector<ExportEntry*> cassettes, ppis;
    for (auto& e : entries) {
        if      (e.detection.source == SourceType::CASSETTE) cassettes.push_back(&e);
        else if (e.detection.source == SourceType::PPI)      ppis.push_back(&e);
    }

    printf("  Cassettes : %zu  /  PPI : %zu\n\n", cassettes.size(), ppis.size());
    if (cassettes.empty() || ppis.empty()) {
        printf("Pas de paire cassette/PPI possible.\n");
        return;
    }

    // --- 4. Apparier toutes les paires cassette × PPI ---
    struct MatchedPair { ExportEntry* cas; ExportEntry* ppi; DumpMatch match; };
    std::vector<MatchedPair> matched;

    for (ExportEntry* cas : cassettes) {
        for (ExportEntry* ppi : ppis) {
            const DumpMatch m = matchDumps(
                cas->seg.blocks, cas->analyses, cas->protection,
                ppi->seg.blocks, ppi->analyses, ppi->protection);
            if (m.result != DumpMatch::Result::FAILED) {
                printf("  MATCH  %s\n"
                       "         ↔ %s\n"
                       "         confiance=%.0f%%  ratio=×%.4f  %d blocs appariés\n\n",
                       cas->filepath.c_str(), ppi->filepath.c_str(),
                       m.confidence * 100.0, m.speedRatio, m.matchedPairs);
                matched.push_back({cas, ppi, m});
            }
        }
    }

    if (matched.empty()) {
        printf("Aucune paire appariée — aucun bloc à exporter.\n");
        return;
    }

    // --- 5. Exporter les blocs appariés ---
    int blockId        = 0;
    int skippedBlocks  = 0;

    // Fichier JSON ouvert en écriture directe
    const std::string jsonPath = outputDir + "/dataset.json";
    FILE* jf = fopen(jsonPath.c_str(), "w");
    if (!jf) {
        printf("ERREUR : impossible d'écrire %s\n", jsonPath.c_str());
        return;
    }
    fprintf(jf, "{\n  \"blocks\": [\n");
    bool firstJson = true;

    printf("══════════════════════════════════════════\n");
    printf("  EXPORT DES BLOCS\n");
    printf("══════════════════════════════════════════\n");

    for (MatchedPair& mp : matched) {
        const std::string casName = fs::path(mp.cas->filepath).filename().string();
        const std::string ppiName = fs::path(mp.ppi->filepath).filename().string();

        for (const BlockPair& bp : mp.match.pairs) {
            // Localiser les blocs dans les deux dumps
            const Block*         casBlock    = findBlock(mp.cas->seg.blocks, bp.idx1);
            const BlockAnalysis* casAnalysis = findAnalysis(mp.cas->analyses, bp.idx1);
            const Block*         ppiBlock    = findBlock(mp.ppi->seg.blocks, bp.idx2);
            const BlockAnalysis* ppiAnalysis = findAnalysis(mp.ppi->analyses, bp.idx2);

            if (!casBlock || !casAnalysis || !ppiBlock || !ppiAnalysis) {
                printf("  [SKIP] bloc %d↔%d introuvable\n", bp.idx1, bp.idx2);
                ++skippedBlocks;
                continue;
            }

            // Extraire les échantillons du bloc
            const auto& casSamples = mp.cas->reader.samples();
            const auto& ppiSamples = mp.ppi->reader.samples();

            const std::vector<float> casChunk(
                casSamples.begin() + casBlock->startSample,
                casSamples.begin() + casBlock->endSample);
            const std::vector<float> ppiChunk(
                ppiSamples.begin() + ppiBlock->startSample,
                ppiSamples.begin() + ppiBlock->endSample);

            // Noms des fichiers de sortie
            char prefix[32];
            std::snprintf(prefix, sizeof(prefix), "block_%04d", blockId);
            const std::string casOut = outputDir + "/" + prefix + "_cassette.wav";
            const std::string ppiOut = outputDir + "/" + prefix + "_ppi.wav";

            const bool casOk = writeWav(casOut.c_str(), casChunk, mp.cas->reader.info().sampleRate);
            const bool ppiOk = writeWav(ppiOut.c_str(), ppiChunk, mp.ppi->reader.info().sampleRate);

            if (!casOk || !ppiOk) {
                printf("  [ERREUR] écriture bloc %04d\n", blockId);
                ++skippedBlocks;
                continue;
            }

            // Métadonnées JSON
            if (!firstJson) fprintf(jf, ",\n");
            firstJson = false;

            fprintf(jf,
                "    {\n"
                "      \"id\": \"%04d\",\n"
                "      \"cassette_file\": \"%s_cassette.wav\",\n"
                "      \"ppi_file\": \"%s_ppi.wav\",\n"
                "      \"cassette_sample_rate\": %u,\n"
                "      \"ppi_sample_rate\": %u,\n"
                "      \"speed_ratio\": %.6f,\n"
                "      \"protection\": \"%s\",\n"
                "      \"structure\": \"%s\",\n"
                "      \"has_pilot\": %s,\n"
                "      \"pilot_freq_hz\": %.1f,\n"
                "      \"cassette_duration_sec\": %.3f,\n"
                "      \"ppi_duration_sec\": %.3f,\n"
                "      \"source_cassette\": \"%s\",\n"
                "      \"source_ppi\": \"%s\",\n"
                "      \"block_index_cassette\": %d,\n"
                "      \"block_index_ppi\": %d\n"
                "    }",
                blockId,
                prefix, prefix,
                mp.cas->reader.info().sampleRate,
                mp.ppi->reader.info().sampleRate,
                mp.match.speedRatio,
                jsonStr(mp.cas->protection.name).c_str(),
                structureStr(casAnalysis->structure).c_str(),
                casAnalysis->hasPilot ? "true" : "false",
                casAnalysis->pilotFreqHz,
                casBlock->durationSec,
                ppiBlock->durationSec,
                jsonStr(casName).c_str(),
                jsonStr(ppiName).c_str(),
                bp.idx1,
                bp.idx2);

            printf("  [%04d]  %-14s  bloc %d↔%d  %.1fs  %s\n",
                   blockId,
                   mp.cas->protection.name.c_str(),
                   bp.idx1, bp.idx2,
                   casBlock->durationSec,
                   structureStr(casAnalysis->structure).c_str());

            ++blockId;
        }
    }

    fprintf(jf, "\n  ],\n  \"total_blocks\": %d\n}\n", blockId);
    fclose(jf);

    // --- 6. Bilan ---
    printf("\n══════════════════════════════════════════\n");
    printf("  BILAN EXPORT\n");
    printf("══════════════════════════════════════════\n");
    printf("  Blocs exportés : %d\n", blockId);
    if (skippedBlocks > 0)
        printf("  Blocs ignorés  : %d\n", skippedBlocks);
    printf("  dataset.json   : %s\n\n", jsonPath.c_str());
}
