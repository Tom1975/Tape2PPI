// ============================================================
//  test_tcn_inference — validation du convertisseur TCN C++
//
//  Stratégie : compare la sortie TcnTapeToPPI avec la référence
//  Schmitt trigger C++ (signal_converter::convertToPPI),
//  qui est la cible exacte sur laquelle le modèle a été entraîné.
//
//  Tests effectués :
//    1. Sortie bipolaire stricte : uniquement -1.0 et +1.0
//    2. Signal nul (silence) → sortie -1.0 (duty ≈ 0%)
//    3. Précision vs Schmitt trigger sur blocs pilotes connus
//       (attendu > 99%, cohérent avec l'accuracy d'entraînement)
//
//  Dépend de out_dataset/ (généré par --export-dataset).
// ============================================================

#include "tcn_tape_to_ppi.h"
#include "wav_reader.h"
#include "signal_converter.h"

#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

// ---- Chargement WAV ----

static bool load_block(const std::string& path,
                       std::vector<float>& out_f,
                       std::vector<double>& out_d,
                       int& sr)
{
    WavReader r;
    if (!r.load(path)) return false;
    out_f = r.samples();
    out_d.assign(out_f.begin(), out_f.end());
    sr = static_cast<int>(r.info().sampleRate);
    return true;
}

// ---- Métriques ----

static double duty_pct(const std::vector<double>& v)
{
    int pos = 0;
    for (double x : v) if (x > 0.5) pos++;
    return 100.0 * pos / v.size();
}

static bool is_bipolar(const std::vector<double>& v)
{
    for (double x : v)
        if (x > -0.5 && x < 0.5) return false;
    return true;
}

// Précision TCN vs Schmitt trigger.
// Le Schmitt trigger (signal_converter) retourne 0.0/1.0.
// Le TCN retourne -1.0/+1.0.
// On aligne les deux conventions : Schmitt=1 ↔ TCN=+1.
// Si la polarité est inversée (3 inversions HW) on prend le meilleur des deux.
static double accuracy_vs_schmitt(const std::vector<double>& tcn,
                                  const std::vector<float>& schmitt_01)
{
    const int n = static_cast<int>(std::min(tcn.size(), schmitt_01.size()));
    int match_direct = 0, match_inv = 0;
    for (int i = 0; i < n; ++i) {
        int t = (tcn[i] > 0.0) ? 1 : 0;
        int s = (schmitt_01[i] > 0.5f) ? 1 : 0;
        if (t == s)       match_direct++;
        if (t == (1 - s)) match_inv++;
    }
    return 100.0 * std::max(match_direct, match_inv) / n;
}

// ---- Cadre de test ----

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* desc)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", desc);
    ok ? ++g_pass : ++g_fail;
}

// ============================================================
//  Tests
// ============================================================

static void test_silence()
{
    printf("\n--- Test silence ---\n");
    const int N = 44100;
    std::vector<double> silence(N, 0.0);
    TcnTapeToPPI conv;
    conv.Filtrer(silence);
    check(is_bipolar(silence),              "Sortie bipolaire stricte (±1.0)");
    check(duty_pct(silence) < 5.0,         "Silence → duty < 5%");
}

static void test_block(const char* label,
                       const std::string& path,
                       double acc_threshold = 98.0)
{
    printf("\n--- Test %s ---\n", label);
    std::vector<float>  samples_f;
    std::vector<double> samples_d;
    int sr = 0;
    if (!load_block(path, samples_f, samples_d, sr)) {
        printf("  [SKIP] Impossible de charger %s\n", path.c_str());
        return;
    }
    printf("  Chargé : %zu samples @ %d Hz (%.2f s)\n",
           samples_f.size(), sr, (double)samples_f.size() / sr);

    // Référence : Schmitt trigger C++ (signal_converter)
    std::vector<float> ref = convertToPPI(samples_f, sr);

    // Inférence TCN
    TcnTapeToPPI conv;
    conv.Filtrer(samples_d);

    double duty = duty_pct(samples_d);
    double acc  = accuracy_vs_schmitt(samples_d, ref);

    printf("  Duty cycle : %.1f%%  |  Précision vs Schmitt : %.2f%%\n", duty, acc);

    check(is_bipolar(samples_d),                    "Sortie bipolaire stricte (±1.0)");
    check(duty >= 40.0 && duty <= 60.0,             "Duty cycle dans [40%, 60%]");

    char buf[128];
    snprintf(buf, sizeof(buf), "Précision vs Schmitt >= %.0f%%", acc_threshold);
    check(acc >= acc_threshold, buf);
}

// ============================================================
//  Main
// ============================================================
int main(int argc, char** argv)
{
    const std::string dir = (argc > 1) ? argv[1] : "out_dataset";

    printf("============================================================\n");
    printf("  TcnTapeToPPI — tests d'inférence C++\n");
    printf("  Dataset : %s\n", dir.c_str());
    printf("============================================================\n");

    test_silence();

    // Bloc 0030 : Mach 3, Speedlock, PILOT_DATA (2.36 s @ 48000 Hz)
    test_block("Mach 3 pilote   (bloc 0030, Speedlock)",
               dir + "/block_0030_cassette.wav");

    // Bloc 0031 : Mach 3, Speedlock, PILOT_DATA (8.9 s @ 48000 Hz)
    test_block("Mach 3 pilote   (bloc 0031, Speedlock)",
               dir + "/block_0031_cassette.wav");

    // Bloc 0000 : 3D Grand Prix, Standard ROM, PILOT_DATA (22.5 s @ 44100 Hz)
    test_block("3D Grand Prix   (bloc 0000, Standard ROM)",
               dir + "/block_0000_cassette.wav");

    // Bloc 0010 : 3D Grand Prix, Standard ROM, PILOT_DATA
    test_block("3D Grand Prix   (bloc 0010, Standard ROM)",
               dir + "/block_0010_cassette.wav");

    printf("\n============================================================\n");
    printf("  Résultat : %d/%d tests passés\n", g_pass, g_pass + g_fail);
    printf("============================================================\n");
    return (g_fail == 0) ? 0 : 1;
}
