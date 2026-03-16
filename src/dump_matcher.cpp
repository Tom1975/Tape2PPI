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
    const ProtectionAnalysis&         prot2)
{
    DumpMatch result;
    result.totalBlocks1 = static_cast<int>(blocks1.size());
    result.totalBlocks2 = static_cast<int>(blocks2.size());

    // ---- Vérifications préalables ----

    // Même protection détectée ?
    if (prot1.type != prot2.type &&
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

    // On appariera min(n1,n2) blocs séquentiellement.
    // Si les comptes diffèrent, on note une correspondance partielle.
    const int nPairs = std::min(n1, n2);
    const bool countMatch = (n1 == n2);

    // ---- Appariement séquentiel ----

    std::vector<double> ratios;
    int structIncompat = 0;
    int encIncompat    = 0;

    for (int i = 0; i < nPairs; ++i) {
        const Block&         b1  = blocks1[i];
        const BlockAnalysis& ba1 = analyses1[i];
        const Block&         b2  = blocks2[i];
        const BlockAnalysis& ba2 = analyses2[i];

        const double ratio = (b1.durationSec > 0.0)
                           ? b2.durationSec / b1.durationSec
                           : 1.0;

        // On ne comptabilise dans le ratio que les blocs avec une durée fiable
        // (> 0.5s pour éviter les très petits blocs bruités).
        if (b1.durationSec > 0.5 && b2.durationSec > 0.5)
            ratios.push_back(ratio);

        const bool sc = structuresCompat(ba1.structure, ba2.structure);
        const bool ec = encodingsCompat(ba1, ba2);
        if (!sc) ++structIncompat;
        if (!ec) ++encIncompat;

        BlockPair bp;
        bp.idx1           = ba1.blockIndex;
        bp.idx2           = ba2.blockIndex;
        bp.dur1           = b1.durationSec;
        bp.dur2           = b2.durationSec;
        bp.ratio          = ratio;
        bp.structureCompat = sc;
        bp.encodingCompat  = ec;
        result.pairs.push_back(bp);
    }

    result.matchedPairs = nPairs;

    if (ratios.empty()) {
        result.notes = "Aucun bloc valide pour calculer le ratio";
        return result;
    }

    // ---- Ratio de vitesse et consistance ----

    result.speedRatio       = median(ratios);
    const double cv         = coeffVar(ratios);
    result.speedConsistency = std::max(0.0, 1.0 - cv * 5.0);   // pénalité CV

    // ---- Confiance globale ----

    double conf = 0.0;

    // Même nombre de blocs
    if (countMatch)                                conf += 0.35;
    else if (std::abs(n1 - n2) == 1)              conf += 0.15;

    // Ratio de vitesse consistant (CV bas)
    if (cv < 0.02)        conf += 0.35;
    else if (cv < 0.05)   conf += 0.25;
    else if (cv < 0.10)   conf += 0.10;

    // Structures compatibles
    const double fracCompat = 1.0 - static_cast<double>(structIncompat) / nPairs;
    conf += fracCompat * 0.20;

    // Même protection
    if (prot1.type == prot2.type && prot1.type != ProtectionType::UNKNOWN)
        conf += 0.10;

    result.confidence = std::min(conf, 1.0);

    // ---- Résultat ----

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "ratio=×%.4f  consistance=%.0f%%  blocs=%d/%d appariés"
                  "  struct_incompat=%d  enc_incompat=%d",
                  result.speedRatio, result.speedConsistency * 100,
                  nPairs, std::max(n1, n2), structIncompat, encIncompat);
    result.notes = buf;

    if (result.confidence >= 0.70)
        result.result = DumpMatch::Result::MATCHED;
    else if (result.confidence >= 0.40)
        result.result = DumpMatch::Result::PARTIAL;
    else
        result.result = DumpMatch::Result::FAILED;

    return result;
}
