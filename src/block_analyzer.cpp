#include "block_analyzer.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdint>

// ============================================================
//  Paramètres
// ============================================================

// Intervalles à sauter en tête de bloc avant l'estimation
// (absorbe les transitoires d'enveloppe et glitches de début d'enregistrement)
static constexpr size_t PILOT_SKIP_LEADING  = 8;

// Nombre d'intervalles échantillonnés après le skip
// pour estimer la fréquence du pilote
static constexpr size_t PILOT_SAMPLE_COUNT  = 120;

// Seuil de régularité : IQR/médiane < ce seuil → signal régulier (pilote)
// Un pilote pur (PPI) a IQR/médiane ≈ 0.01-0.05.
// Un signal cassette analogique peut atteindre ~0.25 (wow/flutter).
// On tolère jusqu'à 0.30 pour couvrir les deux cas.
static constexpr double PILOT_REGULARITY    = 0.30;

// Tolérance sur la demi-période estimée pour prolonger le pilote dans le bloc
static constexpr double PILOT_TOLERANCE     = 0.25;   // ±25 %

// Nombre minimum de transitions pour valider un pilote
static constexpr size_t PILOT_MIN_EDGES     = 40;

// Fenêtre glissante pour la détection de fin de pilote :
// sur PILOT_WINDOW intervalles consécutifs, au moins PILOT_WIN_RATIO
// doivent être in-range pour que le pilote continue.
// Robuste aux outliers sporadiques (bruit, dropout cassette, faux ZC).
static constexpr size_t PILOT_WINDOW        = 40;
static constexpr double PILOT_WIN_RATIO     = 0.60;

// Seuil relatif d'activité : un sample est actif si son enveloppe rapide
// dépasse ce ratio de l'enveloppe lente locale (niveau ambiant).
// Permet de détecter des signaux faibles sans être aveuglé par le bruit
// entre deux plages actives.
static constexpr float  RELATIVE_ACTIVE     = 0.10f;   // 10 % du niveau local

// Plancher de l'enveloppe (évite la division par zéro lors de la normalisation)
static constexpr float  ENVELOPE_FLOOR      = 0.001f;

// Seuil de signe sur le signal normalisé (par enveloppe rapide)
static constexpr float  SIGN_THRESHOLD      = 0.05f;

// ============================================================
//  Enveloppe locale — two-pass peak-hold
//  decaySec contrôle la constante de temps.
// ============================================================
static std::vector<float> computeEnvelope(const std::vector<float>& s,
                                          uint32_t sr, float decaySec) {
    const size_t N = s.size();
    std::vector<float> env(N, 0.0f);
    const float decay = 1.0f - 1.0f / (static_cast<float>(sr) * decaySec);

    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = peak;
    }
    peak = 0.0f;
    for (size_t i = N; i-- > 0; ) {
        peak   = std::max(fabsf(s[i]), peak * decay);
        env[i] = std::max(env[i], peak);
    }
    return env;
}

// ============================================================
//  Détection des zero-crossings sur signal normalisé
//
//  Un sample est considéré actif si son enveloppe rapide (5 ms)
//  dépasse RELATIVE_ACTIVE × enveloppe lente (100 ms).
//  Cela permet de détecter des signaux faibles (protections qui
//  atténuent localement le signal) tout en ignorant le vrai bruit
//  de fond.
// ============================================================
static std::vector<size_t> detectZeroCrossings(const std::vector<float>& s,
                                                const std::vector<float>& envFast,
                                                const std::vector<float>& envSlow) {
    std::vector<size_t> crossings;
    int prevSign = 0;

    for (size_t i = 0; i < s.size(); ++i) {
        // Seuil d'activité relatif au niveau local
        if (envFast[i] < envSlow[i] * RELATIVE_ACTIVE) { prevSign = 0; continue; }

        const float norm = s[i] / std::max(envFast[i], ENVELOPE_FLOOR);
        const int   sign = (norm >  SIGN_THRESHOLD) ?  1
                         : (norm < -SIGN_THRESHOLD) ? -1 : 0;
        if (sign != 0) {
            if (prevSign != 0 && sign != prevSign)
                crossings.push_back(i);
            prevSign = sign;
        }
    }
    return crossings;
}

// ============================================================
//  Estimation adaptative de la fréquence du pilote
//
//  On échantillonne les PILOT_SAMPLE_COUNT premiers intervalles,
//  puis on calcule médiane et IQR.
//  Si IQR/médiane < PILOT_REGULARITY → c'est un signal régulier
//  (pilote), et la médiane est la demi-période estimée.
//
//  Retourne 0 si le début du bloc n'est pas régulier (pas de pilote).
// ============================================================
static double estimatePilotHP(const std::vector<double>& iv) {
    if (iv.size() < PILOT_SKIP_LEADING + PILOT_MIN_EDGES) return 0.0;

    const size_t offset = PILOT_SKIP_LEADING;
    const size_t N = std::min(PILOT_SAMPLE_COUNT, iv.size() - offset);
    if (N < PILOT_MIN_EDGES) return 0.0;

    std::vector<double> sample(iv.begin() + offset, iv.begin() + offset + N);
    std::sort(sample.begin(), sample.end());

    const double median = sample[N / 2];
    const double q1     = sample[N / 4];
    const double q3     = sample[3 * N / 4];
    const double iqr    = q3 - q1;

    if (median <= 0.0 || (iqr / median) > PILOT_REGULARITY)
        return 0.0;

    return median;
}

// ============================================================
//  Estimation du codage bimodal des bits de données
//
//  Prend jusqu'à N_SAMPLE intervalles depuis startIdx, filtre les
//  outliers, cherche la coupure la plus franche (grand gap) dans la
//  distribution triée. Retourne les médianes S (court) et L (long).
//  Retourne {0,0} si le signal n'est pas bimodal (données non standard).
// ============================================================
struct BitEncoding { double shortHP = 0.0; double longHP = 0.0; };

static BitEncoding estimateBitEncoding(const std::vector<double>& iv,
                                        size_t startIdx,
                                        double pilotHP) {
    static constexpr size_t N_SAMPLE = 200;

    // On garde les intervalles dans [0.1×pilotHP, 4×pilotHP]
    const double minIv = pilotHP * 0.10;
    const double maxIv = pilotHP * 4.00;

    std::vector<double> sample;
    sample.reserve(N_SAMPLE);
    for (size_t i = startIdx; i < iv.size() && sample.size() < N_SAMPLE; ++i)
        if (iv[i] >= minIv && iv[i] <= maxIv)
            sample.push_back(iv[i]);

    if (sample.size() < 20) return {};
    std::sort(sample.begin(), sample.end());

    const size_t N = sample.size();

    // Largest gap → frontière entre les deux modes
    size_t split  = 0;
    double maxGap = 0.0;
    for (size_t i = 1; i < N; ++i) {
        const double gap = sample[i] - sample[i - 1];
        if (gap > maxGap) { maxGap = gap; split = i; }
    }

    // Les deux groupes doivent avoir une taille minimale
    if (split < 4 || split > N - 4) return {};

    const double sHP = sample[split / 2];
    const double lHP = sample[split + (N - split) / 2];

    // Ratio L/S attendu entre 1.4 et 3.0 (CPC standard : ×2)
    if (lHP < 1.4 * sHP || lHP > 3.0 * sHP) return {};

    return {sHP, lHP};
}

// ============================================================
//  Tentative de décodage d'un octet (8 bits = 16 demi-périodes)
//
//  Chaque bit = 2 demi-périodes consécutives de même type.
//  On essaie les 4 combinaisons : {short=0 ou 1} × {MSB ou LSB first}.
//  Retourne l'octet si toutes les paires sont cohérentes, -1 sinon.
// ============================================================
static int tryDecodeByte(const std::vector<double>& iv, size_t start,
                          double threshold, bool shortIsZero, bool msbFirst) {
    if (start + 16 > iv.size()) return -1;
    uint8_t byte = 0;
    for (int b = 0; b < 8; ++b) {
        const bool aShort = (iv[start + 2 * b]     < threshold);
        const bool bShort = (iv[start + 2 * b + 1] < threshold);
        if (aShort != bShort) return -1;   // paire incohérente → erreur
        const int bit = (aShort == shortIsZero) ? 0 : 1;
        if (msbFirst) byte = static_cast<uint8_t>((byte << 1) | bit);
        else          byte = static_cast<uint8_t>(byte | (bit << b));
    }
    return byte;
}

// ============================================================
//  analyzeBlock
// ============================================================
BlockAnalysis analyzeBlock(const WavReader& reader, const Block& block) {
    const auto&    s  = reader.samples();
    const uint32_t sr = reader.info().sampleRate;

    BlockAnalysis result;
    result.blockIndex = block.index;
    result.sampleRate = sr;

    if (block.endSample <= block.startSample) return result;

    // Extraire les samples du bloc
    const std::vector<float> bs(s.begin() + block.startSample,
                                 s.begin() + block.endSample);

    // Enveloppe rapide (5 ms) pour la normalisation instantanée du signal,
    // enveloppe lente (100 ms) pour estimer le niveau ambiant local.
    const std::vector<float>  envFast = computeEnvelope(bs, sr, 0.005f);
    const std::vector<float>  envSlow = computeEnvelope(bs, sr, 0.100f);
    const std::vector<size_t> zc      = detectZeroCrossings(bs, envFast, envSlow);

    result.totalEdgeCount = static_cast<long>(zc.size());

    if (zc.size() < PILOT_MIN_EDGES + 1) {
        result.structure = BlockAnalysis::Structure::UNKNOWN;
        return result;
    }

    // --- Intervalles inter-transitions ---
    const size_t NI = zc.size() - 1;
    std::vector<double> iv(NI);
    for (size_t i = 0; i < NI; ++i)
        iv[i] = static_cast<double>(zc[i + 1] - zc[i]);

    // --- Estimation adaptative de la demi-période du pilote ---
    const double pilotHP = estimatePilotHP(iv);

    if (pilotHP <= 0.0) {
        // Le début du bloc est irrégulier → pas de pilote détecté
        result.hasData      = true;
        result.dataStartSec = block.startSec;
        result.structure    = BlockAnalysis::Structure::DATA_ONLY;
        return result;
    }

    // --- Prolongement du pilote dans le bloc ---
    // Fenêtre glissante de PILOT_WINDOW intervalles : le pilote s'étend tant
    // que la fraction in-range reste >= PILOT_WIN_RATIO.
    // Robuste aux outliers sporadiques (bruit, dropout cassette, faux ZC isolés)
    // tout en stoppant net quand le signal devient modulé (section données).
    const double minHP = pilotHP * (1.0 - PILOT_TOLERANCE);
    const double maxHP = pilotHP * (1.0 + PILOT_TOLERANCE);

    const size_t extStart = std::min(PILOT_SKIP_LEADING, NI);
    const size_t avail    = (NI > extStart) ? NI - extStart : 0;
    size_t pilotEnd       = NI;   // par défaut : tout le bloc est pilote

    if (avail > PILOT_WINDOW) {
        // Initialise la fenêtre sur les PILOT_WINDOW premiers intervalles
        size_t winIn = 0;
        for (size_t i = extStart; i < extStart + PILOT_WINDOW; ++i)
            if (iv[i] >= minHP && iv[i] <= maxHP) ++winIn;

        // Glissement jusqu'à trouver une fenêtre irrégulière
        for (size_t i = extStart + PILOT_WINDOW; i <= NI; ++i) {
            if ((double)winIn / PILOT_WINDOW < PILOT_WIN_RATIO) {
                pilotEnd = i - PILOT_WINDOW;
                break;
            }
            if (i < NI) {
                if (iv[i - PILOT_WINDOW] >= minHP && iv[i - PILOT_WINDOW] <= maxHP) --winIn;
                if (iv[i]                >= minHP && iv[i]                <= maxHP) ++winIn;
            }
        }
    }

    // Compter les intervalles in-range dans la zone pilote retenue
    size_t inRange = 0;
    for (size_t i = extStart; i < pilotEnd; ++i)
        if (iv[i] >= minHP && iv[i] <= maxHP) ++inRange;

    if (inRange < PILOT_MIN_EDGES) {
        result.hasData      = true;
        result.dataStartSec = block.startSec;
        result.structure    = BlockAnalysis::Structure::DATA_ONLY;
        return result;
    }

    // --- Fréquence mesurée (médiane des intervalles in-range) ---
    std::vector<double> validIv;
    validIv.reserve(pilotEnd - extStart);
    for (size_t i = extStart; i < pilotEnd; ++i)
        if (iv[i] >= minHP && iv[i] <= maxHP)
            validIv.push_back(iv[i]);

    std::sort(validIv.begin(), validIv.end());
    const double medianHP = validIv[validIv.size() / 2];

    result.hasPilot       = true;
    result.pilotStartSec  = block.startSec + static_cast<double>(zc[extStart]) / sr;
    result.pilotEndSec    = block.startSec + static_cast<double>(zc[pilotEnd]) / sr;
    result.pilotDurSec    = result.pilotEndSec - result.pilotStartSec;
    result.pilotFreqHz    = static_cast<double>(sr) / (2.0 * medianHP);
    result.pilotEdgeCount = static_cast<long>(pilotEnd);

    if (pilotEnd < NI) {
        result.hasData      = true;
        result.dataStartSec = block.startSec + static_cast<double>(zc[pilotEnd]) / sr;
        result.structure    = BlockAnalysis::Structure::PILOT_DATA;
    } else {
        result.structure    = BlockAnalysis::Structure::PILOT_ONLY;
        return result;
    }

    // --- Détection du pulse de sync et du premier octet ---
    // Raffiner pilotEnd : avancer jusqu'au premier intervalle clairement
    // hors-plage pilote. La fenêtre glissante retourne le début de la fenêtre
    // qui a échoué — il peut rester des intervalles pilote valides avant la
    // vraie transition.
    while (pilotEnd < NI && iv[pilotEnd] >= minHP && iv[pilotEnd] <= maxHP)
        ++pilotEnd;

    // Mettre à jour le temps de fin de pilote avec la position affinée
    if (pilotEnd < zc.size())
        result.pilotEndSec = block.startSec + static_cast<double>(zc[pilotEnd]) / sr;
    result.pilotDurSec = result.pilotEndSec - result.pilotStartSec;

    // Signal de sync : présence d'un intervalle court juste après le pilote.
    // Purement informatif — le scan commence toujours depuis pilotEnd car
    // certains loaders intègrent le sync dans le premier octet (0x16 CPC).
    if (pilotEnd < NI && iv[pilotEnd] < pilotHP * 0.75)
        result.hasSyncPulse = true;

    // Estimation du codage bimodal depuis la zone données (pilotEnd = premier
    // intervalle hors-plage pilote, qu'il soit sync ou premier bit de données).
    const BitEncoding enc = estimateBitEncoding(iv, pilotEnd, pilotHP);
    if (enc.shortHP <= 0.0) return result;

    result.encodingValid = true;
    result.shortHP       = enc.shortHP;
    result.longHP        = enc.longHP;
    const double thr     = (enc.shortHP + enc.longHP) / 2.0;

    // Cherche 0x16 sur les 32 premiers alignements possibles × 4 combinaisons.
    // On scanne depuis pilotEnd (sans skip) pour couvrir :
    //   - aucun sync pulse  : 0x16 démarre à pilotEnd
    //   - 1 intervalle sync : 0x16 démarre à pilotEnd+1
    //   - 1 bit sync complet: 0x16 démarre à pilotEnd+2
    for (size_t off = pilotEnd; off <= pilotEnd + 32 && !result.firstByteValid; ++off) {
        for (bool shortIsZero : {true, false}) {
            for (bool msbFirst : {true, false}) {
                if (tryDecodeByte(iv, off, thr, shortIsZero, msbFirst) == 0x16) {
                    result.firstByteValid = true;
                    result.firstByte      = 0x16;
                }
            }
        }
    }

    return result;
}
