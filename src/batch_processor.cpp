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

static std::string convertedPath(const std::string& inputPath, const std::string& suffix) {
    fs::create_directories("converted");
    return "converted/" + fs::path(inputPath).stem().string() + suffix + ".wav";
}

// ============================================================
//  Helpers : appariement par nom
// ============================================================

static std::string toLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Normalise un nom pour comparaison : minuscules + suppression de la ponctuation
// Ex : "Dragon's Lair" → "dragons lair"   "3d-boxing" → "3d boxing"
static std::string normalizeName(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == ' ') r += static_cast<char>(std::tolower(c));
        else if (c == '-')               r += ' ';
        // apostrophes, points, etc. → supprimés
    }
    return r;
}

// Supprime les suffixes cassette typiques : " Face X 16M", " Face X 16ST", " 16M", " 16ST"
// Ex : "Game Face A 16M" → "Game"   /   "Game 16ST" → "Game"
static std::string stripCassetteSuffix(const std::string& stem) {
    // " Face X 16M" ou " Face X 16ST"
    size_t pos = stem.rfind(" Face ");
    if (pos != std::string::npos) {
        const std::string rest = stem.substr(pos + 6); // après " Face "
        if (rest.size() >= 4 && rest[1] == ' ') {
            const std::string tail = rest.substr(2);
            if (tail == "16M" || tail == "16ST")
                return stem.substr(0, pos);
        }
    }
    // " 16ST" ou " 16M"
    for (const char* sfx : {" 16ST", " 16M"}) {
        const std::string s(sfx);
        if (stem.size() >= s.size() && stem.compare(stem.size() - s.size(), s.size(), s) == 0)
            return stem.substr(0, stem.size() - s.size());
    }
    return stem;
}

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

    // --- 4. Appariement : priorité au suffixe "_PPI", fallback structurel ---
    struct MatchedPair {
        BatchEntry* cas;
        BatchEntry* ppi;
        DumpMatch   match;
        bool        byName; // apparié par nom (vs structurel)
    };
    std::vector<MatchedPair> matched;

    // Helper : base name sans extension
    auto baseName = [](const std::string& path) -> std::string {
        const fs::path p(path);
        std::string stem = p.stem().string(); // nom sans extension
        return stem;
    };

    std::vector<bool> ppiUsed(ppis.size(), false);
    std::vector<bool> casUsed(cassettes.size(), false);

    // Appariement par nom : cassette "X.wav" ↔ PPI "X_PPI.wav" ou "X_PPI_N.wav" (multi-parties)
    for (size_t ci = 0; ci < cassettes.size(); ++ci) {
        const std::string casBase  = baseName(cassettes[ci]->filepath);
        const std::string ppiPrefix = casBase + "_PPI";
        for (size_t pi = 0; pi < ppis.size(); ++pi) {
            if (ppiUsed[pi]) continue;
            const std::string ppiBase = baseName(ppis[pi]->filepath);
            bool isPpiMatch = (ppiBase == ppiPrefix);
            if (!isPpiMatch && ppiBase.size() > ppiPrefix.size() &&
                ppiBase.substr(0, ppiPrefix.size() + 1) == ppiPrefix + "_") {
                const std::string sfx = ppiBase.substr(ppiPrefix.size() + 1);
                isPpiMatch = !sfx.empty() &&
                             std::all_of(sfx.begin(), sfx.end(), ::isdigit);
            }
            if (!isPpiMatch) continue;
            const DumpMatch m = matchDumps(
                cassettes[ci]->seg.blocks, cassettes[ci]->analyses, cassettes[ci]->protection,
                ppis[pi]->seg.blocks,      ppis[pi]->analyses,      ppis[pi]->protection,
                /*skipProtectionCheck=*/true);
            printf("  MATCH (nom)  %s\n               ↔ %s\n               confiance=%.0f%%  ratio=×%.4f\n\n",
                   cassettes[ci]->filepath.c_str(), ppis[pi]->filepath.c_str(),
                   m.confidence * 100.0, m.speedRatio);
            matched.push_back({cassettes[ci], ppis[pi], m, true});
            casUsed[ci] = true;
            ppiUsed[pi] = true;
            // pas de break : autorise plusieurs fichiers _PPI_N pour la même cassette
        }
    }

    // Appariement par nom étendu : cassette "X Face A 16M.wav" ↔ PPI "X.wav"
    // Comparaison normalisée : minuscules + ponctuation ignorée
    // Ex : "Dragon's Lair Face B 16M" → "dragons lair" == "dragons lair" (Dragons lair.wav)
    for (size_t ci = 0; ci < cassettes.size(); ++ci) {
        if (casUsed[ci]) continue;
        const std::string casBase     = baseName(cassettes[ci]->filepath);
        const std::string casStripped = stripCassetteSuffix(casBase);
        if (normalizeName(casStripped) == normalizeName(casBase)) continue; // aucun suffixe supprimé
        const std::string casNorm = normalizeName(casStripped);
        for (size_t pi = 0; pi < ppis.size(); ++pi) {
            if (ppiUsed[pi]) continue;
            if (normalizeName(baseName(ppis[pi]->filepath)) == casNorm) {
                const DumpMatch m = matchDumps(
                    cassettes[ci]->seg.blocks, cassettes[ci]->analyses, cassettes[ci]->protection,
                    ppis[pi]->seg.blocks,      ppis[pi]->analyses,      ppis[pi]->protection,
                    /*skipProtectionCheck=*/true);
                if (m.speedRatio < 0.5 || m.speedRatio > 2.0) {
                    printf("  SKIP (ratio aberrant ×%.2f)  %s ↔ %s\n\n",
                           m.speedRatio,
                           cassettes[ci]->filepath.c_str(), ppis[pi]->filepath.c_str());
                    break;
                }
                printf("  MATCH (nom)  %s\n               ↔ %s\n               confiance=%.0f%%  ratio=×%.4f\n\n",
                       cassettes[ci]->filepath.c_str(), ppis[pi]->filepath.c_str(),
                       m.confidence * 100.0, m.speedRatio);
                matched.push_back({cassettes[ci], ppis[pi], m, true});
                casUsed[ci] = true;
                ppiUsed[pi] = true;
                break;
            }
        }
    }

    // Fallback structurel pour les fichiers non appariés par nom
    // (uniquement si le speed ratio est dans un intervalle raisonnable)
    for (size_t ci = 0; ci < cassettes.size(); ++ci) {
        if (casUsed[ci]) continue;
        for (size_t pi = 0; pi < ppis.size(); ++pi) {
            if (ppiUsed[pi]) continue;
            const DumpMatch m = matchDumps(
                cassettes[ci]->seg.blocks, cassettes[ci]->analyses, cassettes[ci]->protection,
                ppis[pi]->seg.blocks,      ppis[pi]->analyses,      ppis[pi]->protection);
            if (m.result != DumpMatch::Result::FAILED &&
                m.speedRatio >= 0.5 && m.speedRatio <= 2.0) {
                printf("  MATCH (struct)  %s\n                  ↔ %s\n                  confiance=%.0f%%  ratio=×%.4f\n\n",
                       cassettes[ci]->filepath.c_str(), ppis[pi]->filepath.c_str(),
                       m.confidence * 100.0, m.speedRatio);
                matched.push_back({cassettes[ci], ppis[pi], m, false});
                casUsed[ci] = true;
                ppiUsed[pi] = true;
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
        const std::string outPath = convertedPath(mp.cas->filepath, "_to_PPI");
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
        const ConversionQuality q = validateConversion(mp.ppi->analyses, convAnalyses, mp.match.speedRatio, mp.match.pairs);
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
