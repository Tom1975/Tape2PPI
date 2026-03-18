#!/usr/bin/env python3
"""
TCN : conversion cassette -> PPI
Cible : Schmitt trigger adaptatif (identique au signal_converter C++).
Entraîné sur Standard ROM (3D Grand Prix) + Speedlock (Mach 3) pour la diversité.
Évaluation qualité : validateur C++ (intervalles en µs), pas sample-level vs PPI réel
  car la comparaison sample-level requiert une précision < T/10 ≈ 1.7 samples,
  impossible avec un rééchantillonnage global (speed_ratio médian, pas par bloc).
"""

import json, os
import numpy as np
import scipy.io.wavfile as wavfile
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader

# ============================================================
# Paramètres
# ============================================================
DATASET_DIR = 'out_dataset'
WINDOW      = 128    # échantillons de contexte (~3 ms @ 44100 Hz)
STEP        = 256    # sous-échantillonnage
BATCH_SIZE  = 1024
EPOCHS      = 30
LR          = 1e-3

# Blocs utilisables : Standard ROM (ratio ×0.54) + Speedlock Mach 3 (ratio ×1.02)
# Exclure 3D Boxing (ratio ×1.05, loader 0xEB non-standard)
USABLE_SOURCES = {'Standard ROM', 'Speedlock (ou compatible)'}

N_TRAIN_STD  = 22   # blocs Standard ROM pour l'entraînement (26 total)
N_TRAIN_SPD  = 4    # blocs Speedlock pour l'entraînement (6 total)

# ============================================================
# Audio
# ============================================================
def load_wav(path):
    sr, data = wavfile.read(path)
    if   data.dtype == np.int16:  f = data.astype(np.float32) / 32768.0
    elif data.dtype == np.uint8:  f = (data.astype(np.float32) - 128.0) / 128.0
    else:                         f = data.astype(np.float32)
    return sr, f

# ============================================================
# Schmitt trigger adaptatif
# Logique identique au signal_converter C++ :
#   - suppression DC par moyenne glissante (50 ms)
#   - hystérésis = 8% × RMS locale (10 ms)
# ============================================================
def schmitt_trigger(signal_f, sr, dc_ms=50, rms_ms=10, hyst_frac=0.08, hyst_min=0.02, silence_rms=0.02):
    n = len(signal_f)
    dc_win  = max(1, int(sr * dc_ms  / 1000))
    rms_win = max(1, int(sr * rms_ms / 1000))

    cumsum = np.cumsum(np.pad(signal_f, (dc_win, 0)))
    dc     = (cumsum[dc_win:] - cumsum[:-dc_win]) / dc_win
    s      = signal_f - dc[:n]

    sq_cum = np.cumsum(np.pad(s**2, (rms_win, 0)))
    rms    = np.sqrt((sq_cum[rms_win:] - sq_cum[:-rms_win]) / rms_win)[:n]
    hyst   = np.maximum(hyst_frac * rms, hyst_min)

    out   = np.zeros(n, dtype=np.float32)
    state = 0.0
    for i in range(n):
        if rms[i] < silence_rms:
            state = 0.0
        elif s[i] > hyst[i]:
            state = 1.0
        elif s[i] < -hyst[i]:
            state = 0.0
        out[i] = state
    return out

# ============================================================
# Dataset PyTorch
# ============================================================
class BlockDataset(Dataset):
    def __init__(self, blocks, directory, window, step):
        hw = window // 2
        Xs, ys = [], []
        for b in blocks:
            _, cas = load_wav(os.path.join(directory, b['cassette_file']))
            sr  = b['cassette_sample_rate']
            tgt = schmitt_trigger(cas, sr)
            for i in range(hw, len(tgt) - hw, step):
                Xs.append(cas[i - hw : i + hw])
                ys.append(tgt[i])
        self.X = torch.tensor(np.array(Xs), dtype=torch.float32).unsqueeze(1)
        self.y = torch.tensor(np.array(ys),  dtype=torch.float32)

    def __len__(self):             return len(self.y)
    def __getitem__(self, idx):    return self.X[idx], self.y[idx]

# ============================================================
# Modèle : TCN léger
# ============================================================
class TinyCpaTCN(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv1d( 1, 16, kernel_size=9, padding=4), nn.ReLU(),
            nn.Conv1d(16, 32, kernel_size=9, padding=4), nn.ReLU(),
            nn.Conv1d(32, 16, kernel_size=9, padding=4), nn.ReLU(),
        )
        self.head = nn.Linear(16, 1)

    def forward(self, x):
        feats  = self.conv(x)
        center = feats[:, :, feats.shape[2] // 2]
        return self.head(center).squeeze(-1)

# ============================================================
# Évaluation
# ============================================================
def evaluate(model, loader):
    model.eval()
    correct = total = 0
    with torch.no_grad():
        for xb, yb in loader:
            preds = (model(xb) > 0).float()
            correct += (preds == yb).sum().item()
            total   += len(yb)
    return correct / total * 100.0

# ============================================================
# Main
# ============================================================
if __name__ == '__main__':
    with open(os.path.join(DATASET_DIR, 'dataset.json')) as f:
        meta = json.load(f)

    std_blocks = [b for b in meta['blocks'] if b['protection'] == 'Standard ROM']
    spd_blocks = [b for b in meta['blocks'] if b['protection'] == 'Speedlock (ou compatible)']

    print(f"Blocs Standard ROM  : {len(std_blocks)}  (source : 3D Grand Prix)")
    print(f"Blocs Speedlock     : {len(spd_blocks)}  (source : Mach 3)")

    train_blocks = std_blocks[:N_TRAIN_STD] + spd_blocks[:N_TRAIN_SPD]
    test_blocks  = std_blocks[N_TRAIN_STD:] + spd_blocks[N_TRAIN_SPD:]
    print(f"\nTrain : {len(train_blocks)} blocs  /  Test : {len(test_blocks)} blocs\n")

    print("Chargement train...", flush=True)
    train_ds = BlockDataset(train_blocks, DATASET_DIR, WINDOW, STEP)
    print(f"  {len(train_ds):,} fenêtres  |  ratio positif : {train_ds.y.mean():.3f}")

    print("Chargement test...", flush=True)
    test_ds = BlockDataset(test_blocks, DATASET_DIR, WINDOW, STEP)
    print(f"  {len(test_ds):,} fenêtres  |  ratio positif : {test_ds.y.mean():.3f}\n")

    train_dl = DataLoader(train_ds, batch_size=BATCH_SIZE, shuffle=True,  num_workers=0)
    test_dl  = DataLoader(test_ds,  batch_size=BATCH_SIZE, shuffle=False, num_workers=0)

    model     = TinyCpaTCN()
    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    criterion = nn.BCEWithLogitsLoss()

    print(f"Modèle : {sum(p.numel() for p in model.parameters()):,} paramètres")
    print(f"Entraînement : {EPOCHS} époques\n")
    print(f"{'Époque':>6}  {'Loss':>8}  {'Acc train':>10}  {'Acc test':>9}")
    print("-" * 42)

    for epoch in range(1, EPOCHS + 1):
        model.train()
        total_loss = 0.0
        for xb, yb in train_dl:
            optimizer.zero_grad()
            loss = criterion(model(xb), yb)
            loss.backward()
            optimizer.step()
            total_loss += loss.item() * len(yb)
        avg_loss = total_loss / len(train_ds)

        if epoch % 5 == 0 or epoch == 1:
            acc_tr = evaluate(model, train_dl)
            acc_te = evaluate(model, test_dl)
            print(f"{epoch:6d}  {avg_loss:8.4f}  {acc_tr:9.2f}%  {acc_te:8.2f}%", flush=True)

    torch.save(model.state_dict(), 'ml_env/cpc_model.pt')
    print("\nModèle sauvegardé : ml_env/cpc_model.pt")
