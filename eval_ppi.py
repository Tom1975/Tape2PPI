#!/usr/bin/env python3
"""
Évaluation du modèle TCN contre les signaux PPI réels.

Métrique : classification S/L des intervalles entre transitions.
- Invariante à la durée du pilote (alignement automatique)
- Robuste au speed ratio (correction par médiane des ratios de pilote)
- Comparable à ce que fait le validateur C++ (comparaison bit à bit du codage)

Usage :
  python3 eval_ppi.py                    # évalue TCN + Schmitt trigger
  python3 eval_ppi.py --quick            # 5 blocs par jeu max (rapide)
"""

import json, os, sys
import numpy as np
import scipy.io.wavfile as wavfile
import torch
from train import TinyCpaTCN, schmitt_trigger

DATASET_DIR = 'out_dataset'
MODEL_PATH  = 'ml_env/cpc_model.pt'
WINDOW      = 128
BATCH_SIZE  = 8192

# Tolérance de classification S/L (±30% autour du threshold)
SL_TOLERANCE   = 0.20   # ±20% pour ±1 interval match
PILOT_TOL      = 0.30   # intervalle considéré "pilote" si dans ±30% de la médiane
MIN_PILOT_IVLS = 40     # minimum pour estimer le pilote

TRAIN_SOURCES = {
    'Mach 3 16ST.wav',
    'Bomb Jack Face A 16M.wav',
    'Bride of Frankenstein Face A 16M.wav',
    'Bridge-It Face A 16M.wav',
    'City Slicker Face A 16M.wav',
    '3D Grand Prix Face A 16M.wav',
    '3d boxing (Amsoft - 1985).wav',
}


def load_wav(path):
    sr, data = wavfile.read(path)
    if data.ndim == 2:
        data = data[:, 0]
    if   data.dtype == np.int16:  f = data.astype(np.float32) / 32768.0
    elif data.dtype == np.uint8:  f = (data.astype(np.float32) - 128.0) / 128.0
    else:                         f = data.astype(np.float32)
    return sr, f


def run_tcn(model, signal_f, window=128, batch_size=BATCH_SIZE):
    """Inférence TCN sur tout le signal."""
    hw = window // 2
    n = len(signal_f)
    padded = np.pad(signal_f, (hw, hw), mode='edge')
    windows = np.lib.stride_tricks.sliding_window_view(padded, window)[:n]
    out = np.empty(n, dtype=np.float32)
    with torch.no_grad():
        for start in range(0, n, batch_size):
            end = min(start + batch_size, n)
            x = torch.tensor(windows[start:end], dtype=torch.float32).unsqueeze(1)
            out[start:end] = (model(x) > 0).float().numpy()
    return out


def extract_intervals_us(binary_signal, sr, min_us=30.0):
    """Intervalles en µs entre transitions consécutives (filtre les artefacts)."""
    transitions = np.where(np.diff(binary_signal.astype(np.int8)) != 0)[0]
    if len(transitions) < 2:
        return np.array([])
    ivs = np.diff(transitions).astype(np.float64) * 1e6 / sr
    return ivs[ivs >= min_us]


def find_pilot_region(ivs, expected_us=None, tol=PILOT_TOL):
    """
    Retourne (pilot_med, pilot_end_idx, data_start_idx).
    Le pilote = séquence d'intervalles réguliers (tous similaires).
    Détection : fenêtre glissante de 40 intervalles, IQR/médiane < 0.3.
    """
    if len(ivs) < MIN_PILOT_IVLS:
        return None, 0, 0

    WINDOW_SIZE = 40
    best_pilot_end = 0
    pilot_med = None

    # Estimation initiale : médiane des 120 premiers intervalles
    n_est = min(120, len(ivs))
    med = np.median(ivs[:n_est])
    if expected_us is not None:
        # Validation : le ratio med/expected doit être raisonnable
        if not (0.5 < med / expected_us < 2.0):
            med = expected_us

    # Prolongement : fenêtre glissante
    for i in range(0, len(ivs) - WINDOW_SIZE):
        window = ivs[i:i + WINDOW_SIZE]
        wmed = np.median(window)
        if wmed < 1:
            break
        iqr = np.percentile(window, 75) - np.percentile(window, 25)
        if iqr / wmed < 0.30:
            best_pilot_end = i + WINDOW_SIZE
            pilot_med = wmed
        else:
            if best_pilot_end > 0 and i > best_pilot_end + 10:
                break  # pilot terminé

    if pilot_med is None or best_pilot_end < MIN_PILOT_IVLS:
        pilot_med = med
        best_pilot_end = 0

    return pilot_med, best_pilot_end


def sl_accuracy(ivs_pred, ivs_ref, pilot_med_pred, pilot_med_ref):
    """
    Compare les séquences S/L après le pilote.

    - Speed ratio corrigé par le ratio des médianes de pilote
    - Intervalles classifiés S (court) ou L (long) selon threshold = 1.5 × pilote
    - Comparaison séquentielle sur min(n_pred, n_ref) paires
    - Retourne (pct_match, n_compared)
    """
    if pilot_med_pred is None or pilot_med_ref is None or pilot_med_ref == 0:
        return 0.0, 0

    # Speed ratio = ratio des médianes de pilote
    speed_ratio = pilot_med_pred / pilot_med_ref  # pred/ref

    # Threshold S/L : 1.5 × demi-période pilote
    sl_thresh_pred = pilot_med_pred * 1.5
    sl_thresh_ref  = pilot_med_ref  * 1.5

    # Classification
    def classify(ivs, thresh):
        sl = np.where(ivs < thresh, 0, 1)  # 0=S, 1=L
        return sl

    sl_p = classify(ivs_pred, sl_thresh_pred)
    sl_r = classify(ivs_ref,  sl_thresh_ref)

    n = min(len(sl_p), len(sl_r))
    if n == 0:
        return 0.0, 0

    match = (sl_p[:n] == sl_r[:n]).sum() / n * 100.0
    return float(match), n


def compare_pilot_freq(ivs_pred, ivs_ref, sr_pred, sr_ref):
    """
    Compare les fréquences de pilote.
    Retourne (freq_pred_hz, freq_ref_hz, match_pct_tol20).
    """
    if len(ivs_pred) < 10 or len(ivs_ref) < 10:
        return 0.0, 0.0, 0.0
    f_pred = 1e6 / (2 * np.median(ivs_pred[:200]))
    f_ref  = 1e6 / (2 * np.median(ivs_ref[:200]))
    rel    = abs(f_pred - f_ref) / f_ref if f_ref > 0 else 1.0
    return float(f_pred), float(f_ref), float(rel < 0.20) * 100.0


def evaluate_block(model, block, directory):
    """
    Évalue un bloc. Retourne dict avec scores ou None si fichier manquant.
    """
    cas_path = os.path.join(directory, block['cassette_file'])
    ppi_path = os.path.join(directory, block['ppi_file'])
    if not os.path.exists(cas_path) or not os.path.exists(ppi_path):
        return None

    sr_cas, cas = load_wav(cas_path)
    sr_ppi, ppi = load_wav(ppi_path)

    # TCN et Schmitt sur cassette
    tcn_out     = run_tcn(model, cas)
    schmitt_out = schmitt_trigger(cas, sr_cas)

    # Binariser PPI réel
    ppi_bin = (ppi > 0.0).astype(np.float32)

    # Intervalles
    ivs_tcn     = extract_intervals_us(tcn_out,     sr_cas)
    ivs_schmitt = extract_intervals_us(schmitt_out, sr_cas)
    ivs_ppi     = extract_intervals_us(ppi_bin,     sr_ppi)

    if len(ivs_ppi) < MIN_PILOT_IVLS:
        return None

    # Fréquence pilote attendue (µs)
    pilot_freq_hz = block.get('pilot_freq_hz', 735.0)
    speed_ratio   = block['speed_ratio']
    if speed_ratio <= 0 or not block.get('has_pilot', True):
        return None  # bloc DATA_ONLY sans pilote : pas comparable
    expected_ppi_med = 1e6 / (2 * pilot_freq_hz * speed_ratio)  # cassette pilot en µs

    # Pilote dans chaque signal
    pilot_tcn,     pe_tcn     = find_pilot_region(ivs_tcn,     expected_ppi_med)
    pilot_schmitt, pe_schmitt = find_pilot_region(ivs_schmitt, expected_ppi_med)
    expected_ref_med = 1e6 / (2 * pilot_freq_hz)
    pilot_ppi, pe_ppi = find_pilot_region(ivs_ppi, expected_ref_med)

    # Données après pilote
    data_tcn     = ivs_tcn[pe_tcn:]
    data_schmitt = ivs_schmitt[pe_schmitt:]
    data_ppi     = ivs_ppi[pe_ppi:]

    # Score S/L
    score_tcn,     n_tcn     = sl_accuracy(data_tcn,     data_ppi, pilot_tcn,     pilot_ppi)
    score_schmitt, n_schmitt = sl_accuracy(data_schmitt, data_ppi, pilot_schmitt, pilot_ppi)

    # Fréquence pilote
    f_tcn,     f_ppi, freq_ok_tcn     = compare_pilot_freq(ivs_tcn[:pe_tcn or 200],     ivs_ppi[:pe_ppi or 200], sr_cas, sr_ppi)
    f_schmitt, _,     freq_ok_schmitt = compare_pilot_freq(ivs_schmitt[:pe_schmitt or 200], ivs_ppi[:pe_ppi or 200], sr_cas, sr_ppi)

    return {
        'score_tcn':     score_tcn,
        'score_schmitt': score_schmitt,
        'n_tcn':         n_tcn,
        'n_schmitt':     n_schmitt,
        'pilot_tcn_hz':  f_tcn,
        'pilot_ppi_hz':  f_ppi,
        'pilot_end_tcn': pe_tcn,
        'pilot_end_ppi': pe_ppi,
        'speed_ratio':   block['speed_ratio'],
    }


def main():
    quick = '--quick' in sys.argv

    print(f"Chargement du modèle : {MODEL_PATH}")
    model = TinyCpaTCN()
    model.load_state_dict(torch.load(MODEL_PATH, map_location='cpu', weights_only=True))
    model.eval()

    with open(os.path.join(DATASET_DIR, 'dataset.json')) as f:
        meta = json.load(f)

    blocks = meta['blocks']
    print(f"{len(blocks)} blocs dans le dataset\n")

    from collections import defaultdict
    results_by_game = defaultdict(list)

    # Si --quick : limite à 5 blocs par jeu
    if quick:
        by_game = defaultdict(list)
        for b in blocks:
            by_game[b['source_cassette']].append(b)
        blocks = [b for game_blocks in by_game.values() for b in game_blocks[:5]]
        print(f"Mode --quick : {len(blocks)} blocs\n")

    for idx, block in enumerate(blocks):
        src = block['source_cassette']
        res = evaluate_block(model, block, DATASET_DIR)
        if res is None:
            print(f"  [SKIP] {os.path.basename(src)[:40]} {block['cassette_file']} — données insuffisantes")
            continue
        results_by_game[src].append(res)
        status = "TRAIN" if src in TRAIN_SOURCES else "TEST "
        print(f"  [{status}] {os.path.basename(src):45s}  {block['cassette_file']}"
              f"  TCN {res['score_tcn']:5.1f}%  Schmitt {res['score_schmitt']:5.1f}%"
              f"  n={res['n_tcn']:5d}"
              f"  pilote: cas={res['pilot_tcn_hz']:.0f}Hz ppi={res['pilot_ppi_hz']:.0f}Hz",
              flush=True)

    # Bilan par jeu
    print("\n" + "═" * 90)
    print(f"  {'JEU':45s}  {'TCN':>7}  {'Schmitt':>8}  {'Δ':>6}  {'blocs':>5}  {'split':>5}")
    print("─" * 90)

    all_weighted_tcn = all_weighted_sch = total_w = 0.0
    for src in sorted(results_by_game):
        vals = results_by_game[src]
        weights = [max(v['n_tcn'], 1) for v in vals]
        tot = sum(weights)
        avg_tcn = sum(v['score_tcn']     * w for v, w in zip(vals, weights)) / tot
        avg_sch = sum(v['score_schmitt'] * w for v, w in zip(vals, weights)) / tot
        split   = "TRAIN" if src in TRAIN_SOURCES else "TEST "
        delta   = avg_tcn - avg_sch
        sign    = "+" if delta >= 0 else ""
        print(f"  [{split}] {os.path.basename(src):45s}  {avg_tcn:6.1f}%  {avg_sch:6.1f}%  {sign}{delta:5.1f}%  {len(vals):5d}")
        all_weighted_tcn += avg_tcn * tot
        all_weighted_sch += avg_sch * tot
        total_w += tot

    print("─" * 90)
    if total_w > 0:
        g_tcn = all_weighted_tcn / total_w
        g_sch = all_weighted_sch / total_w
        delta = g_tcn - g_sch
        sign = "+" if delta >= 0 else ""
        print(f"  {'GLOBAL':52s}  {g_tcn:6.1f}%  {g_sch:6.1f}%  {sign}{delta:5.1f}%")

    # TRAIN vs TEST
    print()
    for label, test_fn in [("TRAIN", lambda s: s in TRAIN_SOURCES),
                            ("TEST ", lambda s: s not in TRAIN_SOURCES)]:
        games = [s for s in results_by_game if test_fn(s)]
        vals_all = [v for s in games for v in results_by_game[s]]
        if not vals_all:
            continue
        weights = [max(v['n_tcn'], 1) for v in vals_all]
        tot = sum(weights)
        avg_tcn = sum(v['score_tcn']     * w for v, w in zip(vals_all, weights)) / tot
        avg_sch = sum(v['score_schmitt'] * w for v, w in zip(vals_all, weights)) / tot
        delta = avg_tcn - avg_sch
        sign = "+" if delta >= 0 else ""
        print(f"  [{label}]  {len(games)} jeux  —  TCN {avg_tcn:.1f}%  Schmitt {avg_sch:.1f}%  Δ {sign}{delta:.1f}%")


if __name__ == '__main__':
    main()
