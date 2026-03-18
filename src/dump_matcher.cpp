#include "dump_matcher.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

// ============================================================
//  Utilitaires
// ============================================================

static double median(std::vector<double> v) {
    if (v.empty()) return 1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static double coeffVar(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    if (mean <= 0.0) return 0.0;
    double sq = 0.0;
    for (double x : v) sq += (x - mean) * (x - mean);
    return std::sqrt(sq / v.size()) / mean;
}

// Deux structures sont compatibles si le contenu est de même nature.
// Un bloc DATA_ONLY d'un côté peut correspondre à un PILOT_DATA de l'autre
// (certains loaders génèrent/suppriment le pilote selon le support).
static bool structuresCompat(BlockAnalysis::Structure s1,
                              BlockAnalysis::Structure s2) {
    using S = BlockAnalysis::Structure;
    if (s1 == S::UNKNOWN || s2 == S::UNKNOWN) return false;
    // DATA_ONLY et PILOT_DATA sont compatibles entre eux (données présentes dans les deux)
    if (s1 == S::PILOT_ONLY || s2 == S::PILOT_ONLY) return false;
    return true;
}

// Deux encodages sont compatibles si le ratio L/S est dans le même ordre
// de grandeur (tolérance ±30%).
static bool encodingsCompat(const BlockAnalysis& a, const BlockAnalysis& b) {
    if (!a.encodingValid || !b.encodingValid) return true;  // pas d'info = pas d'incompatibilité
    const double r1 = a.longHP / a.shortHP;
    const double r2 = b.longHP / b.shortHP;
    return std::fabs(r1 - r2) / r1 < 0.30;
}

// ============================================================
//  matchDumps
// ============================================================
DumpMatch matchDumps(
    const std::vector<Block>&         blocks1,
    const std::vector<BlockAnalysis>& analyses1,
    const ProtectionAnalysis&         prot1,
    const std::vector<Block>&         blocks2,
    const std::vector<BlockAnalysis>& analyses2,
    const ProtectionAnalysis&         prot2,
    bool                              skipProtectionCheck)
{
    DumpMatch result;
    result.totalBlocks1 = static_cast<int>(blocks1.size());
    result.totalBlocks2 = static_cast<int>(blocks2.size());

    // ---- Vérifications préalables ----

    // Même protection détectée ?
    if (!skipProtectionCheck &&
        prot1.type != prot2.type &&
        prot1.type != ProtectionType::UNKNOWN &&
        prot2.type != ProtectionType::UNKNOWN) {
        result.notes = "Protections différentes : " + prot1.name + " vs " + prot2.name;
        return result;
    }

    // Nombre de blocs identique ?
    const int n1 = result.totalBlocks1;
    const int n2 = result.totalBlocks2;

    if (n1 == 0 || n2 == 0) {
        result.notes = "Dump vide";
        return result;
    }

    // ---- Appariement séquentiel avec recherche d'offset optimal ----
    // On essaie de sauter jusqu'à MAX_SKIP blocs au début de chaque côté
    // pour gérer les blocs "parasites" qui n'ont pas de correspondant.
    // On garde l'alignement dont le CV des ratios de durée est le plus bas.

    static const int MAX_SKIP = 2;

    struct Alignment {
        int off1 = 0, off2 = 0;
        int nPairs = 0;
        double cv = 1e9;
        double speedRatio = 1.0;
        std::vector<double> ratios;
        std::vector<BlockPair> pairs;
        int structIncompat = 0;
        int encIncompat    = 0;
    };

    Alignment best;

    for (int o1 = 0; o1 <= MAX_SKIP; ++o1) {
        for (int o2 = 0; o2 <= MAX_SKIP; ++o2) {
            if (o1 + o2 > MAX_SKIP) continue; // limite le nombre total de blocs sautés
            const int avail1 = n1 - o1;
            const int avail2 = n2 - o2;
            if (avail1 <= 0 || avail2 <= 0) continue;
            const int np = std::min(avail1, avail2);
            if (np < 1) continue;

            Alignment a;
            a.off1   = o1;
            a.off2   = o2;
            a.nPairs = np;

            for (int i = 0; i < np; ++i) {
                const Block&         b1  = blocks1[o1 + i];
                const BlockAnalysis& ba1 = analyses1[o1 + i];
                const Block&         b2  = blocks2[o2 + i];
                const BlockAnalysis& ba2 = analyses2[o2 + i];

                const double ratio = (b1.durationSec > 0.0)
                                   ? b2.durationSec / b1.durationSec
                                   : 1.0;
                if (b1.durationSec > 0.5 && b2.durationSec > 0.5)
                    a.ratios.push_back(ratio);

                const bool sc = structuresCompat(ba1.structure, ba2.structure);
                const bool ec = encodingsCompat(ba1, ba2);
                if (!sc) ++a.structIncompat;
                if (!ec) ++a.encIncompat;

                BlockPair bp;
                bp.idx1            = ba1.blockIndex;
                bp.idx2            = ba2.blockIndex;
                bp.dur1            = b1.durationSec;
                bp.dur2            = b2.durationSec;
                bp.ratio           = ratio;
                bp.structureCompat = sc;
                bp.encodingCompat  = ec;
                a.pairs.push_back(bp);
            }

            if (a.ratios.empty()) continue;
            a.speedRatio = median(a.ratios);
            a.cv         = coeffVar(a.ratios);

            // Préférer l'alignement avec le CV le plus bas ;
            // en cas d'égalité, préférer celui qui saute le moins de blocs.
            const int skipPenalty    = o1 + o2;
            const int bestSkip       = best.off1 + best.off2;
            const bool betterCV      = a.cv < best.cv - 0.01;
            const bool sameCV        = std::fabs(a.cv - best.cv) < 0.01;
            const bool fewerSkips    = skipPenalty < bestSkip;
            if (betterCV || (sameCV && fewerSkips))
                best = std::move(a);
        }
    }

    if (best.ratios.empty()) {
        result.notes = "Aucun bloc valide pour calculer le ratio";
        return result;
    }

    result.pairs        = std::move(best.pairs);
    result.matchedPairs = best.nPairs;

    // ---- Ratio de vitesse et consistance ----

    result.speedRatio       = best.speedRatio;
    const double cv         = best.cv;
    result.speedConsistency = std::max(0.0, 1.0 - cv * 5.0);

    // ---- Confiance globale ----

    double conf = 0.0;

    // Nombre de blocs (après alignement)
    const bool countMatch = (n1 - best.off1 == n2 - best.off2);
    if (countMatch)                                conf += 0.35;
    else if (std::abs((n1 - best.off1) - (n2 - best.off2)) == 1) conf += 0.15;

    // Ratio de vitesse consistant (CV bas)
    if (cv < 0.02)        conf += 0.35;
    else if (cv < 0.05)   conf += 0.25;
    else if (cv < 0.10)   conf += 0.10;

    // Structures compatibles
    const double fracCompat = (best.nPairs > 0)
        ? 1.0 - static_cast<double>(best.structIncompat) / best.nPairs
        : 0.0;
    conf += fracCompat * 0.20;

    // Même protection
    if (prot1.type == prot2.type && prot1.type != ProtectionType::UNKNOWN)
        conf += 0.10;

    result.confidence = std::min(conf, 1.0);

    // ---- Résultat ----

    char buf[256];
    const char* offsetStr = (best.off1 > 0 || best.off2 > 0) ? " (offset)" : "";
    std::snprintf(buf, sizeof(buf),
                  "ratio=×%.4f  consistance=%.0f%%  blocs=%d/%d appariés"
                  "  struct_incompat=%d  enc_incompat=%d  off1=%d off2=%d%s",
                  result.speedRatio, result.speedConsistency * 100,
                  best.nPairs, std::max(n1, n2),
                  best.structIncompat, best.encIncompat,
                  best.off1, best.off2, offsetStr);
    result.notes = buf;

    if (result.confidence >= 0.70)
        result.result = DumpMatch::Result::MATCHED;
    else if (result.confidence >= 0.40)
        result.result = DumpMatch::Result::PARTIAL;
    else
        result.result = DumpMatch::Result::FAILED;

    return result;
}
