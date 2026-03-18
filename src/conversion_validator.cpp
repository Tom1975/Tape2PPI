#include "conversion_validator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================
//  validateConversion
// ============================================================

ConversionQuality validateConversion(
    const std::vector<BlockAnalysis>& refAnalyses,
    const std::vector<BlockAnalysis>& convAnalyses,
    double speedRatio)
{
    ConversionQuality q;

    const int n = static_cast<int>(std::min(refAnalyses.size(), convAnalyses.size()));

    for (int i = 0; i < n; ++i) {
        const BlockAnalysis& ref  = refAnalyses[i];
        const BlockAnalysis& conv = convAnalyses[i];

        BlockConversionQuality bq;
        bq.blockIndex = ref.blockIndex;

        bq.pilotRefFound  = ref.hasPilot;
        bq.pilotConvFound = conv.hasPilot;

        bq.encodingRefValid  = ref.encodingValid;
        bq.encodingConvValid = conv.encodingValid;

        bq.syncRef  = ref.firstByteValid;
        bq.syncConv = conv.firstByteValid;

        // --- Score : on n'évalue que les blocs avec un pilote de référence ---
        // (blocs DATA_ONLY de référence = pas de vérité terrain sur le pilote)

        if (!ref.hasPilot) {
            // Bloc sans pilote de référence : on vérifie juste qu'on ne génère pas un faux pilote
            bq.score   = (conv.hasPilot) ? 0.5 : 1.0;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Pas de pilote de référence%s",
                          conv.hasPilot ? " — FAUX PILOTE converti" : "");
            bq.details = buf;
            q.blocks.push_back(bq);
            continue;
        }

        ++q.validBlocks;

        double score = 0.0;

        // --- Pilote trouvé ? (0.30 pts) ---
        if (conv.hasPilot) {
            score += 0.30;

            // Erreur fréquence pilote (0.25 pts)
            // Le signal converti garde la cadence de la cassette :
            //   fréquence attendue = ref_ppi_freq × speedRatio
            {
                const double expectedFreq = ref.pilotFreqHz * speedRatio;
                bq.pilotFreqErr = std::fabs(conv.pilotFreqHz - expectedFreq) / expectedFreq * 100.0;
            }
            if      (bq.pilotFreqErr < 2.0)  score += 0.25;
            else if (bq.pilotFreqErr < 5.0)  score += 0.18;
            else if (bq.pilotFreqErr < 10.0) score += 0.10;

            // Erreur durée pilote (0.10 pts)
            // Durée attendue dans le converti = ref_ppi_dur / speedRatio
            if (ref.pilotDurSec > 0.0) {
                const double expectedDur = ref.pilotDurSec / speedRatio;
                bq.pilotDurErr = std::fabs(conv.pilotDurSec - expectedDur) / expectedDur * 100.0;
                if      (bq.pilotDurErr < 5.0)  score += 0.10;
                else if (bq.pilotDurErr < 15.0) score += 0.05;
            }
        }

        // --- Codage S/L (0.25 pts) ---
        // Comparaison en µs avec correction du speed ratio :
        //   ref_µs / speedRatio donne la durée attendue dans le signal converti.
        // Cela évite l'artefact de quantisation dû aux sample rates différents
        // (ex : L=16@44100Hz vs L=18@48000Hz ont le même sens physique).
        if (ref.encodingValid && conv.encodingValid && ref.sampleRate > 0 && conv.sampleRate > 0) {
            const double refSµs  = ref.shortHP  / ref.sampleRate  * 1e6 / speedRatio;
            const double refLµs  = ref.longHP   / ref.sampleRate  * 1e6 / speedRatio;
            const double convSµs = conv.shortHP / conv.sampleRate * 1e6;
            const double convLµs = conv.longHP  / conv.sampleRate * 1e6;

            bq.shortHPErrUs = std::fabs(convSµs - refSµs) / refSµs * 100.0;
            bq.longHPErrUs  = std::fabs(convLµs - refLµs) / refLµs * 100.0;

            const double maxErr = std::max(bq.shortHPErrUs, bq.longHPErrUs);
            if      (maxErr < 5.0)  score += 0.25;
            else if (maxErr < 10.0) score += 0.15;
            else if (maxErr < 20.0) score += 0.08;
        } else if (!ref.encodingValid) {
            // Pas de vérité terrain sur le codage : pas de pénalité
            score += 0.15;
        }

        // --- Sync 0x16 (0.15 pts) ---
        if (ref.firstByteValid) {
            if (conv.firstByteValid)
                score += 0.15;
            // sinon : pénalité implicite (pas de bonus)
        } else {
            score += 0.08;  // pas de référence → demi-bonus
        }

        bq.score = std::min(score, 1.0);

        // Texte de synthèse
        char buf[256];
        if (conv.hasPilot) {
            std::snprintf(buf, sizeof(buf),
                "pilote=%.0f Hz (err=%.1f%%)  S err=%.1f%% L err=%.1f%%  sync=%s",
                conv.pilotFreqHz, bq.pilotFreqErr,
                bq.shortHPErrUs, bq.longHPErrUs,
                conv.firstByteValid ? "OK" : "NON");
        } else {
            std::snprintf(buf, sizeof(buf), "PILOTE NON DÉTECTÉ");
        }
        bq.details = buf;

        if (bq.score >= 0.70) ++q.goodBlocks;
        q.blocks.push_back(bq);
    }

    // Score global (blocs avec pilote de référence uniquement)
    if (q.validBlocks > 0) {
        double sum = 0.0;
        for (const auto& bq : q.blocks)
            if (bq.pilotRefFound) sum += bq.score;
        q.overallScore = sum / q.validBlocks;
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "Score global : %.0f%%  (%d/%d blocs bons)",
        q.overallScore * 100.0, q.goodBlocks, q.validBlocks);
    q.summary = buf;

    return q;
}

// ============================================================
//  printConversionQuality
// ============================================================

void printConversionQuality(const ConversionQuality& q) {
    printf("  Qualité de conversion (cassette → PPI) :\n");
    printf("  %-5s  %-6s  %-14s  %-22s  %-8s  %s\n",
           "Bloc", "Score", "Pilote (err%)", "S/L err µs", "Sync", "Détails");

    for (const auto& bq : q.blocks) {
        if (!bq.pilotRefFound) {
            printf("  [%2d]  —      (pas de pilote de ref)  %s\n",
                   bq.blockIndex, bq.details.c_str());
            continue;
        }
        printf("  [%2d]  %5.0f%%  %s\n",
               bq.blockIndex,
               bq.score * 100.0,
               bq.details.c_str());
    }

    printf("\n  %s\n\n", q.summary.c_str());
}
