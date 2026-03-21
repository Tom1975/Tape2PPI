# CPC WAV Analyzer — Contexte du projet

## Objectif
Outil C/C++ d'analyse et de comparaison de fichiers WAV issus de cassettes Amstrad CPC.
Deux sources d'enregistrement coexistent et doivent être traitées différemment.

## Sources de signal

| Source | Description | Forme du signal |
|--------|-------------|-----------------|
| **Cassette** | Enregistrement analogique d'une vraie cassette CPC | Sinusoïdal / analogique, flancs lents |
| **PPI** | Sampling de la sortie cassette du PPI (8255) | Quasi-carré, flancs quasi-instantanés |

La détection automatique de la source est nécessaire pour appliquer les bons traitements.

## Structure du projet

```
Tape2PPI/
├── src/
│   ├── wav_reader.h / .cpp           # Lecture WAV (Phase 1 ✅)
│   ├── source_detector.h / .cpp      # Détection cassette vs PPI (Phase 1 ✅)
│   ├── signal_analyzer.h / .cpp      # Segmentation en blocs (Phase 2 ✅)
│   ├── block_analyzer.h / .cpp       # Analyse interne des blocs (Phase 3 ✅)
│   ├── protection_detector.h / .cpp  # Détection de protection (Phase 4 ✅)
│   ├── dump_matcher.h / .cpp         # Appariement cassette ↔ PPI (Phase 5 ✅)
│   ├── signal_converter.h / .cpp     # Conversion signal (Phase 6+7 ✅)
│   ├── conversion_validator.h / .cpp # Validation qualité conversion (Phase 7 ✅)
│   ├── batch_processor.h / .cpp      # Traitement batch répertoire (Phase 7 ✅)
│   ├── wav_writer.h / .cpp           # Écriture WAV PCM 16 bits (Phase 6 ✅)
│   ├── dataset_exporter.h / .cpp     # Export paires de blocs pour entraînement ML (Phase 8 ✅)
│   └── main.cpp                      # Point d'entrée (modes 1 fichier / 2 fichiers / batch / export)
├── export/
│   ├── tape_to_ppi_converter.h       # Export émulateur — convertisseur causal algorithmique (Phase 7 ✅)
│   ├── tape_to_ppi_converter.cpp
│   ├── tcn_tape_to_ppi.h             # Export émulateur — convertisseur TCN (Phase 9 ✅)
│   ├── tcn_tape_to_ppi.cpp           # Inférence C++ pure, zéro dépendance externe
│   └── tcn_weights.h                 # Poids embarqués (auto-généré par export_weights.py)
├── tests/
│   └── test_tcn_inference.cpp        # Tests d'inférence TCN C++ (14/14 PASS)
├── train.py                          # Entraînement TCN PyTorch (Phase 9 ✅)
├── eval_ppi.py                       # Évaluation TCN vs PPI réels (Phase 9 ✅)
├── export_weights.py                 # Export poids → tcn_weights.h (intégré au CMake)
├── ml_env/
│   └── cpc_model.pt                  # Modèle entraîné (9 441 paramètres)
├── test/                             # Fichiers WAV de test (gérés par Git LFS)
│   ├── 10th_Frame.wav                      # CASSETTE, 44100 Hz, 16 bits
│   ├── 10th_Frame_PPI.wav                  # PPI correspondant
│   ├── 3D Grand Prix Face A 16M.wav        # CASSETTE, 44100 Hz, 16 bits
│   ├── 3D Grand Prix Face A 16M_PPI.wav    # PPI correspondant
│   ├── 3d boxing (Amsoft - 1985).wav       # CASSETTE, 44100 Hz
│   ├── 3d boxing (Amsoft - 1985)_PPI.wav  # PPI correspondant
│   ├── BB6BCSW.WAV                         # PPI correspondant (Big Box Face 6B)
│   ├── Big Box Face 6B 16ST.wav            # CASSETTE, 48000 Hz, 16 bits stéréo
│   ├── Bomb Jack Face A 16M.wav            # CASSETTE, 44100 Hz, 16 bits
│   ├── Bomb Jack.wav                       # PPI correspondant
│   ├── Bride of Frankenstein Face A 16M.wav # CASSETTE, 44100 Hz, 16 bits
│   ├── Bride of Frankenstein.wav           # PPI — structure différente (score ~50%)
│   ├── Bridge-It Face A 16M.wav            # CASSETTE, 44100 Hz, 16 bits
│   ├── bridge-it.wav                       # PPI correspondant
│   ├── City Slicker Face A 16M.wav         # CASSETTE, 44100 Hz, 16 bits (2 blocs parasites)
│   ├── City Slicker.wav                    # PPI correspondant (2 blocs)
│   ├── Combat School - Levels.wav          # PPI
│   ├── Combat School Demo.wav              # PPI
│   ├── Combat School Face A 16M.wav        # CASSETTE, 44100 Hz, 16 bits
│   ├── Combat School Face B 16M.wav        # CASSETTE, 44100 Hz, 16 bits (Face B)
│   ├── Combat School.wav                   # PPI correspondant
│   ├── Dan Dare Face A 16M.wav             # CASSETTE, 44100 Hz, 16 bits
│   ├── Dan Dare.wav                        # PPI correspondant
│   ├── Desert Fox Face A 16M.wav           # CASSETTE, 44100 Hz, 16 bits
│   ├── Desert Fox.wav                      # PPI correspondant
│   ├── Dragon's Lair Face B 16M.wav        # CASSETTE, 44100 Hz, 16 bits
│   ├── Dragons lair.wav                    # PPI correspondant
│   ├── Enduro Racer Face A 16M.wav         # CASSETTE, 44100 Hz, 16 bits
│   ├── Enduro Racer.wav                    # PPI correspondant
│   ├── Fairlight Face A 16M.wav            # CASSETTE, 44100 Hz, 16 bits
│   ├── Fairlight.wav                       # PPI correspondant
│   ├── Formula One Simulator Face A 16M.wav # CASSETTE, 44100 Hz, 16 bits
│   ├── Formula 1 Simulator.wav             # PPI (non apparié — nom trop différent)
│   ├── Future Knight Face A 16M.wav        # CASSETTE, 44100 Hz, 16 bits
│   ├── Future Knight.wav                   # PPI (non apparié — à vérifier)
│   ├── Golf Trophee Face A 16M.wav         # CASSETTE, 44100 Hz, 16 bits
│   ├── Golf.wav                            # PPI (non apparié — nom trop différent)
│   ├── Green Beret Face A 16M.wav          # CASSETTE, 44100 Hz, 16 bits
│   ├── Green Beret.wav                     # PPI correspondant
│   ├── Gryzor 16M image + bloc 1 + bloc 2 data ok.wav  # CASSETTE, 44100 Hz, 16 bits
│   ├── Gryzor Face B 16M.wav               # CASSETTE, 44100 Hz, 16 bits
│   ├── Gryzor.wav                          # PPI (non apparié — Face B, à vérifier)
│   ├── Highlander.wav                      # CASSETTE, 44100 Hz, 16 bits
│   ├── Highlander_PPI_1.wav                # PPI (partie 1/3)
│   ├── Highlander_PPI_2.wav                # PPI (partie 2/3)
│   ├── Highlander_PPI_3.wav                # PPI (partie 3/3)
│   ├── Mach 3 16ST.wav                     # CASSETTE, 48000 Hz, 16 bits stéréo
│   ├── Mach 3 16ST_PPI.wav                 # PPI correspondant
│   ├── Manic Miner 16M.wav                 # rôle à déterminer (cassette alternative ?)
│   ├── Manic Miner Face A 16M.wav          # CASSETTE, 44100 Hz, 16 bits
│   ├── Manic Miner.wav                     # PPI correspondant
│   ├── Oh Mummy Face A 16M.wav             # CASSETTE, 44100 Hz, 16 bits
│   ├── Oh Mummy.wav                        # PPI correspondant
│   ├── Revolution Face A 16M.wav           # CASSETTE, 44100 Hz, 16 bits
│   ├── Revolution.wav                      # PPI correspondant
│   ├── Roland in the Caves.wav             # PPI correspondant
│   ├── Roland On The Ropes 16M.wav         # CASSETTE, 44100 Hz, 16 bits
│   ├── Roland in the Caves Face A 16M.wav  # CASSETTE, 44100 Hz, 16 bits
│   ├── Roland on the ropes.wav             # PPI correspondant
│   ├── Silent Service Face A 16M.wav       # CASSETTE, 44100 Hz, 16 bits
│   ├── Silent Service.wav                  # PPI correspondant
│   ├── Sorcery Face A 16M.wav              # CASSETTE, 44100 Hz, 16 bits
│   ├── Sorcery Face B 16M.wav              # CASSETTE, 44100 Hz, 16 bits (Face B)
│   ├── Sorcery.wav                         # PPI correspondant
│   ├── Soul of a Robot Face A 16M.wav      # CASSETTE, 44100 Hz, 16 bits
│   ├── Soul of a robot.wav                 # PPI correspondant
│   ├── Sultan Maze.wav                     # PPI correspondant
│   ├── Sultan's Maze Face A 16M.wav        # CASSETTE, 44100 Hz, 16 bits
│   ├── Timeman One Face A 16M.wav          # CASSETTE, 44100 Hz, 16 bits
│   ├── Timeman One.wav                     # PPI correspondant
│   ├── Yie Ar Kung-Fu Face A 16M.wav       # CASSETTE, 44100 Hz, 16 bits
│   └── Yie ar Kung Fu.wav                  # PPI correspondant
├── converted/                        # Fichiers WAV générés (gitignored)
├── out_dataset/                      # Dataset ML exporté (gitignored)
├── elec.bmp                          # Schéma du circuit de lecture cassette CPC
├── CONTEXT.md
└── CMakeLists.txt
```

### Conventions de nommage des paires
L'appariement cassette ↔ PPI supporte plusieurs conventions (par ordre de priorité) :
1. `Name_PPI.wav` pour le PPI de `Name.wav`
2. `Name.wav` (PPI) pour `Name Face A 16M.wav` (cassette) — suffixes ` Face X 16M` / ` 16ST` supprimés, apostrophes/tirets normalisés (ex : `Dragon's Lair Face B 16M` ↔ `Dragons lair`)
3. Fallback structurel si le speed ratio est dans [0.5, 2.0]

### Compilation (CMake — recommandé)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Génère : build/cpc_wav_analyzer, build/test_tcn_inference, build/libtape_to_ppi.a
# tcn_weights.h est auto-régénéré si ml_env/cpc_model.pt est plus récent
```

### Compilation manuelle (analyseur seul)
```bash
g++ -std=c++17 -O2 -Isrc \
  src/main.cpp src/wav_reader.cpp src/source_detector.cpp \
  src/signal_analyzer.cpp src/block_analyzer.cpp src/protection_detector.cpp \
  src/dump_matcher.cpp src/signal_converter.cpp src/wav_writer.cpp \
  src/conversion_validator.cpp src/batch_processor.cpp src/dataset_exporter.cpp \
  -o cpc_wav_analyzer
```

### Utilisation
```bash
./cpc_wav_analyzer fichier.wav               # analyse individuelle
./cpc_wav_analyzer fichier1.wav fichier2.wav # analyse + appariement + conversion
./cpc_wav_analyzer --batch répertoire/       # traitement batch
./cpc_wav_analyzer --export-dataset src/ out/ # export paires de blocs (dataset ML)
# Les WAV générés sont toujours écrits dans converted/ (créé automatiquement, gitignored)
```

### Intégration émulateur (libtape_to_ppi)
```cpp
#include "tcn_tape_to_ppi.h"          // convertisseur TCN
#include "tape_to_ppi_converter.h"    // convertisseur algorithmique (Schmitt trigger HW)

TcnTapeToPPI conv;
conv.Filtrer(samples);  // in-place, vector<double> [-1.0,+1.0] → [-1.0,+1.0]
```
Lier avec `-Iexport export/tcn_tape_to_ppi.cpp export/tape_to_ppi_converter.cpp`
(ou via `target_link_libraries(... tape_to_ppi)` avec CMake).

---

## Plan des phases

### Phase 1 — Lecture et détection de source ✅
- Lecture WAV pur C/C++ (sans dépendance externe)
- Formats supportés : PCM 8/16/24/32 bits, mono/stéréo (downmix auto)
- Gestion des chunks inconnus (LIST, INFO, fact...)
- Normalisation float [-1.0, +1.0]
- Détection cassette vs PPI par normalisation enveloppe locale + analyse de dérivée :
  - PPI : spikeRatio > 2% ET avgSpikeWidth < 3 samples
  - CASSETTE : `activeCount > 0` (signal actif mais pas de spikes → sinusoïdal)
  - UNKNOWN : signal trop faible (aucun sample actif)
- **Correction** : le test utilisait `edgeCount > 0` pour CASSETTE — or les signaux
  sinusoïdaux purs ne dépassent jamais le seuil DERIV_SPIKE=0.7, donc edgeCount=0.
  Correction : `activeCount > 0` → les fichiers 16M/16ST sont désormais classifiés CASSETTE.

### Phase 2 — Segmentation ✅
- Enveloppe locale (two-pass peak-hold, 5 ms) pour détecter silences/activité
- Seuil de silence relatif à l'amplitude max du fichier (robuste aux signaux faibles)
- Paramètres : silenceThreshold=0.05, silenceMinDur=100 ms, blockMinDur=500 ms

### Phase 3 — Identification des blocs standards ✅
- **Détection zero-crossings** : normalisation par enveloppe rapide (5 ms) / lente (100 ms)
  — robuste aux signaux faibles (protections qui atténuent le niveau)
- **Estimation pilote** : IQR/médiane sur 120 intervalles après 8 skip → signal régulier si IQR/med < 0.30
- **Prolongement pilote** : fenêtre glissante 40 intervalles, seuil 60% in-range
  — robuste aux outliers (wow/flutter cassette, faux ZC isolés)
- **Raffinement pilotEnd** : avance jusqu'au vrai premier intervalle hors-plage pilote
- **Codage des bits** : distribution bimodale S (court) / L (long) sur 200 intervalles
  — ratio L/S attendu entre 1.4 et 3.0
- **Octet de sync** : scan des 32 premiers alignements × 4 combinaisons (short=0/1, MSB/LSB first)
  → cherche 0x16

**Règle sync** : obligatoire pour le **bloc 1** de chaque dump, optionnel ensuite
(les protections Speedlock, Bleepload, etc. n'ont pas forcément de sync sur les blocs suivants).

### Phase 4 — Détection de protection ✅
- Scoring multi-critères : fréquence pilote, durée pilote, fraction DATA_ONLY, ratio L/S
- Types reconnus : **Standard ROM** (pilote 500–900 Hz, durée 2–5 s, peu de DATA_ONLY)
  et **Speedlock** (pilote 1000–2000 Hz, durée < 1.8 s, majorité DATA_ONLY)
- Si aucun score ≥ 0.55 : protection **INCONNUE** (pas de forçage)
- Sync bloc 1 obligatoire pour valider quoi que ce soit

### Phase 5 — Appariement cassette ↔ PPI ✅
- Appariement séquentiel des blocs avec recherche d'offset (MAX_SKIP=2)
- Speed ratio = médiane(dur_ppi/dur_cassette) sur les blocs > 0.5 s
- Consistance = 1 − CV×5 (coefficient de variation des ratios)
- Confiance : nombre de blocs (+0.35), CV bas (+0.35), structures compatibles (+0.20), même protection (+0.10)
- Résultat : MATCHED ≥ 0.70, PARTIAL ≥ 0.40, FAILED sinon
- **Correction tiebreaker** : à CV égal, préférer l'alignement avec le plus de paires
  (avant : préférait moins de sauts, ce qui sélectionnait des alignements dégénérés à 1 paire
  avec CV=0 trivial au détriment d'alignements à 2 paires consistants — ex : City Slicker ratio ×405)

### Phase 6 — Conversion signal ✅
- **Cassette → PPI** : comparateur adaptatif (DC offset 50 ms + hystérésis 8% RMS locale 10 ms)
- **PPI → Cassette** : filtfilt IIR passe-bas (cutoff = 3× fréquence pilote) + renormalisation
- Écriture WAV PCM 16 bits signé mono (`wav_writer`)
- En mode 2 fichiers : conversion automatique si sources différentes, validation après conversion
- Les fichiers générés sont écrits dans `converted/` (créé automatiquement, gitignored)

### Phase 7 — Affinage, validation batch et export émulateur ✅
- **Validateur** (`conversion_validator`) : score par bloc comparant pilote (fréquence, durée),
  codage S/L en µs avec correction du speed ratio, et sync 0x16 du converti vs PPI de référence
  — comparaison en µs (pas en samples) pour s'affranchir des différences de sample rate entre
  cassette (48000 Hz) et PPI (44100 Hz) ; `sampleRate` ajouté dans `BlockAnalysis`
- **Correction** : `validateConversion` prend maintenant un paramètre `pairs` (issu du dump_matcher)
  pour aligner correctement les blocs converti/référence via leurs indices (idx1/idx2) au lieu
  d'un appariement séquentiel aveugle — critique pour les cassettes avec blocs parasites (City Slicker)
- **Mode batch** (`--batch répertoire/`) : analyse tous les WAV, appariement automatique
  cassettes × PPI (par nom puis structurel), conversion + validation, bilan global ;
  ignore les fichiers générés (suffixes `_to_PPI.wav`, `_to_cassette.wav`)
- **Export émulateur** (`export/tape_to_ppi_converter.h/.cpp`) :
  - Classe `TapeToPPIConverter` causale, traitement échantillon par échantillon
  - Filtre passe-haut IIR (couplage AC, fc=7.2 Hz, τ=R318×C319=22 ms)
  - Deux étages Schmitt trigger à hystérésis fixe (schéma CPC 464/664/6128) :
    étage 1 (IC302 pins 5/6/7) ≈ 3%, étage 2 (IC302 pins 9/10/8) ≈ 12%
  - Aucune dépendance externe (C++11, `<cmath>` uniquement)

### Phase 8 — Export dataset ML ✅
- **Mode `--export-dataset src/ out/`** (`dataset_exporter`) :
  - Scanne un répertoire, apparie les paires cassette/PPI (même logique que batch)
  - Pour chaque bloc apparié : `block_NNNN_cassette.wav` + `block_NNNN_ppi.wav`
  - `dataset.json` : métadonnées complètes par bloc (protection, structure, sample rates,
    speed ratio, durée, fréquence pilote, fichiers source, indices de blocs)
  - Dataset courant : 14 jeux, ~183 paires de blocs (mis à jour avec nouveaux jeux)

### Phase 9 — Modèle TCN + export C++ ✅
- **Entraînement** (`train.py`) :
  - Modèle : TCN léger 9 441 paramètres (3× Conv1d 1→16→32→16, k=9 + Linear 16→1)
  - Cible : Schmitt trigger adaptatif C++ (signal_converter) — supervision directe par PPI réel
    impossible (précision sub-sample requise < T/10 ≈ 1.7 samples, incompatible avec speed_ratio global)
  - Split par jeu (jamais les mêmes jeux en train et test) :
    - **Train** : Mach 3, Bomb Jack, Bride of Frankenstein, Bridge-It, City Slicker, 3D Grand Prix, 3d boxing
    - **Test** : Dan Dare, Desert Fox, Dragon's Lair, Combat School
  - Résultats : **99.79% accuracy test** après 30 époques (~15 min CPU)
  - Modèle sauvegardé : `ml_env/cpc_model.pt`
- **Export poids** (`export_weights.py`) :
  - Génère `export/tcn_weights.h` (poids embarqués, ~37 KB)
  - Intégré au CMake : régénération automatique si `cpc_model.pt` est modifié
- **Inférence C++** (`export/tcn_tape_to_ppi.h/.cpp`) :
  - Classe `TcnTapeToPPI`, interface batch in-place : `void Filtrer(std::vector<double>&)`
  - Entrée : signal cassette normalisé [-1.0, +1.0]
  - Sortie : signal PPI bipolaire (-1.0 / +1.0), in-place
  - Aucune dépendance externe (C++11, `<vector>` uniquement)
  - Précision vs Schmitt trigger C++ : **99.5–99.9%** selon le bloc
- **Tests** (`tests/test_tcn_inference.cpp`) :
  - 14/14 tests passés : bipolaire, duty cycle, précision vs Schmitt ≥ 98%
  - Blocs testés : Mach 3 Speedlock (48 kHz) + 3D Grand Prix Standard ROM (44,1 kHz)
  - Indices dataset : bloc 0000/0010 = 3D Grand Prix, bloc 0030/0031 = Mach 3
- **Évaluation vs PPI réels** (`eval_ppi.py`) :
  - Métrique : classification S/L des intervalles entre transitions (robuste au speed ratio et
    à la durée du pilote, comparable au validateur C++)
  - Usage : `python3 eval_ppi.py` (tous blocs) ou `python3 eval_ppi.py --quick` (5 blocs/jeu)
  - Résultats Phase 10 (28 jeux, 132 blocs quick, dataset 339 blocs) :
    **TCN 95.9%, Schmitt 95.5%, Δ = +0.4%** — TCN dépasse Schmitt pour la première fois
    - TRAIN (7 jeux) : TCN 95.5% / Schmitt 95.9% (Δ -0.3%)
    - TEST (21 jeux) : TCN 96.0% / Schmitt 95.4% (Δ **+0.6%**) — généralisation validée
    - Gains TCN vs Schmitt : Desert Fox +3.1%, Silent Service +4.2%
    - Cas difficiles : Dan Dare 70.9%, Highlander 83.8% (appariement multi-parties)
    - Green Beret 89.5% — bloc 0196 à 75% (Schmitt 86.7%) → candidat re-training
  - Note : lancer avec `PYTHONIOENCODING=utf-8` sur Windows (sinon crash sur le résumé UTF-8)

---

## Validation sur fichiers réels

### Détection source — résultats actuels

| Fichier | Source réelle | Détecté |
|---------|--------------|---------|
| `Mach 3 16ST.wav` | CASSETTE | CASSETTE ✓ |
| `3D Grand Prix Face A 16M.wav` | CASSETTE | CASSETTE ✓ |
| `Combat School Face A 16M.wav` | CASSETTE | CASSETTE ✓ |
| `Mach 3 16ST_PPI.wav` | PPI | PPI ✓ |
| `Combat School.wav` | PPI | PPI ✓ |
| `Future Knight Face A 16M.wav` | CASSETTE | **PPI ✗** — fausse détection |
| `Gryzor Face B 16M.wav` | CASSETTE | **PPI ✗** — fausse détection |
| `10th_Frame.wav` | CASSETTE (?) | **PPI** — peut-être vraiment un PPI |

Les fichiers 16 bits enregistrés depuis cassette (signaux sinusoïdaux purs) sont
correctement classifiés CASSETTE depuis la correction `activeCount > 0`.
Exception : `Future Knight Face A 16M.wav` et `Gryzor Face B 16M.wav` sont détectés PPI
malgré leur suffixe — signal atypique (forte distorsion ou enregistrement non standard).

### Appariement batch — résultats sur test/ (~72 fichiers)

| Paire | Mode | Speed ratio | Score |
|-------|------|-------------|-------|
| Mach 3 16ST ↔ Mach 3 16ST_PPI | nom (_PPI) | ×1.024 | ~81% |
| 3D Grand Prix ↔ 3D Grand Prix_PPI | nom (_PPI) | ×0.546 | ~85%+ |
| 10th Frame ↔ 10th Frame_PPI | nom (_PPI) | — | non apparié (les deux détectés PPI) |
| City Slicker ↔ City Slicker | nom (normalisé) | ×1.054 | ~90% |
| Dragon's Lair ↔ Dragons lair | nom (normalisé) | ×0.999 | ~90% |
| Bomb Jack, Bridge-It, Desert Fox, Dan Dare, Combat School… | nom (normalisé) | ~×1.0 | appariés |
| Enduro Racer, Fairlight, Green Beret | nom (normalisé) | ~×1.0 | appariés |
| Oh Mummy, Roland On The Ropes, Yie Ar Kung-Fu | nom (normalisé) | ~×1.04 | 90–100% |
| Timeman One, Soul of a Robot, Silent Service, Sorcery Face B | nom (normalisé) | ~×1.04 | 80–95% |
| Sultan's Maze ↔ Sultan Maze | nom (normalisé) | ×1.033 | 98% |
| Bride of Frankenstein | nom (normalisé) | ×1.86 | ~52% (version différente) |
| Revolution | nom (normalisé) | ×0.81 | ~20% (version différente, 5 vs 26 blocs) |
| Sorcery Face A | nom (normalisé) | ×31.31 | SKIP (10 blocs cassette vs 2 blocs PPI — incompatible) |
| Manic Miner 16M ↔ Manic Miner | nom (normalisé) | ×1.021 | 45% (enreg. alternatif) |
| Roland in the Caves | nom (normalisé) | ~×1.04 | apparié après renommage PPI |
| Big Box Face 6B ↔ BB6BCSW | — | — | non apparié — noms trop différents (à renommer) |
| Highlander ↔ _PPI_1/2/3 | nom (_PPI_N) | — | 3 appariements multi-parties |
| Formula One Simulator, Golf Trophee | — | — | non appariés (noms trop différents) |

### Cas particuliers connus

- **City Slicker** : la cassette contient 2 blocs parasites avant les données (blocs 0 et 1),
  le PPI n'en a pas. L'appariement était faussé (ratio ×405) jusqu'au fix du tiebreaker dans
  `dump_matcher` (morePairs > fewerSkips à CV égal) et du fix du validateur (pairs mapping).
- **Bride of Frankenstein** : cassette à 1470 Hz / sync 0xFF ; PPI à 735 Hz / sync 0x16 —
  les deux fichiers semblent correspondre à des versions différentes du jeu. Score ~50%, normal.
- **Dragon's Lair** : `Dragon's Lair Face B 16M.wav` n'était pas apparié par nom car l'apostrophe
  empêchait la comparaison. Résolu par `normalizeName()` qui supprime la ponctuation.
- **Revolution** : cassette 5 blocs Standard ROM vs PPI 26 blocs Speedlock — versions différentes,
  appariement à 20% / ratio ×0.81, non exploitable.
- **Sorcery** : Face A (10 blocs cassette) incompatible avec le PPI (2 blocs) — ratio ×31,
  SKIP automatique. Face B (2 blocs) s'apparie correctement à 90%. Le PPI `Sorcery.wav`
  correspond probablement à la Face B uniquement.
- **Highlander** : cassette (`Highlander.wav`) = 24 blocs ; PPI en 3 fichiers de 6 blocs chacun
  (`Highlander_PPI_1/2/3.wav`). Supporté depuis le fix `_PPI_N` dans `batch_processor` et
  `dataset_exporter` — 3 paires distinctes générées. Les blocs sont probablement corrects mais
  répartis dans un ordre différent entre cassette et dumps PPI (chargements successifs).
- **Big Box Face 6B** : PPI = `BB6BCSW.WAV` (1 seul bloc) vs cassette 30 blocs — structures
  incompatibles pour l'appariement. À renommer en `Big Box Face 6B 16ST_PPI.wav` pour clarté.
- **Manic Miner** : deux enregistrements cassette (`16M` et `Face A 16M`) pour un seul PPI.
  `Manic Miner 16M.wav` s'apparie à 45% (PARTIAL), `Manic Miner Face A 16M.wav` reste sans PPI.

---

## Notes techniques importantes

### Format WAV CPC typique
- Sample rate : 44100 Hz (parfois 48000 Hz pour les enregistrements récents)
- Bits : 8 bits (PPI) ou 16 bits (cassette) le plus souvent
- Amplitude cassette : peut être faible (~0.5) à cause des protections — ne pas confondre avec du bruit

### Codage des bits de données
- Chaque bit = 2 demi-périodes consécutives de même type (paires SS ou LL)
- S (court) ≈ L/2, ratio typique L/S ≈ 2.0 (peut varier de 1.4 à 3.0)
- Pas d'ordre de bits ni de polarité standard — varie selon le loader
- La fréquence du pilote peut varier fortement : de ~700 Hz (PPI lent) à ~2000 Hz+ (Speedlock)
- Les fréquences pilote observées : 735, 760, 1378, 1412 Hz selon le jeu et la protection

### Structure d'un bloc CPC standard
```
[PILOTE : ~2-5 s de signal régulier]
[→ Premier intervalle hors-plage = début des données]
[OCTET(S) 0x16 : sync byte(s), décodables avec les params S/L du pilote]
[DONNÉES : codage bimodal S/L]
```
Pas de pulse de sync séparé systématique — dans certains loaders, 0x16 commence directement.

### Blocs sans pilote (loaders non-standards)
- Certains loaders (Speedlock, Bleepload, loaders custom) n'ont pas de pilote sur les blocs de données
- La segmentation par silences les détecte quand même comme blocs
- Structure marquée `DATA_ONLY` — normal, pas une erreur

### Paramètres clés de `block_analyzer.cpp`
```
PILOT_SKIP_LEADING = 8      // intervalles ignorés en début de bloc
PILOT_SAMPLE_COUNT = 120    // intervalles analysés pour estimer le pilote
PILOT_REGULARITY   = 0.30   // IQR/médiane max pour valider un pilote
PILOT_TOLERANCE    = 0.25   // ±25% tolérance sur la demi-période
PILOT_WINDOW       = 40     // taille fenêtre glissante pour fin de pilote
PILOT_WIN_RATIO    = 0.60   // fraction min in-range dans la fenêtre
PILOT_MIN_EDGES    = 40     // transitions min pour valider un pilote
```

### Paramètres clés de `signal_converter.cpp`
```
DC_WIN_MS    = 50 ms   // fenêtre suppression DC (moyenne glissante symétrique)
RMS_WIN_MS   = 10 ms   // fenêtre estimation RMS locale (hystérésis adaptative)
HYST_FRAC    = 0.08    // hystérésis = 8% de la RMS locale
HYST_MIN     = 0.02    // hystérésis minimale absolue
SILENCE_RMS  = 0.02    // en dessous : silence → sortie 0 (préserve les silences inter-blocs)
```

**Note** : `wav_reader` utilise le canal gauche uniquement pour les fichiers stéréo (fidèle
au câblage hardware CPC : seule la pointe du jack stéréo est connectée à l'entrée EAR).

### Schéma électronique de référence
- `elec.bmp` : schéma du circuit de lecture cassette du CPC (chemin du signal de l'entrée EAR
  jusqu'au PPI 8255) — référence pour les valeurs R/C, les étages Schmitt trigger (IC302),
  le couplage AC (R318/C319) et la topologie des inversions de polarité.

### Export émulateur (`export/tape_to_ppi_converter`)
- Classe causale (temps réel, pas de look-ahead)
- HP IIR : `y[n] = alpha*(y[n-1] + x[n] - x[n-1])`, cutoff 7.2 Hz (τ = R318×C319 = 22 ms)
- Étage 1 — Schmitt trigger fixe : hystérésis ≈ 3% (R310 = 470 kΩ, IC302 pins 5/6/7)
- Étage 2 — Schmitt trigger fixe : hystérésis ≈ 12% (R307 = 47 kΩ, IC302 pins 9/10/8)
- Note de polarité : 3 inversions au total (transistor + 2 comparateurs) → sortie inversée
- `setACCouplingHz(hz)`, `setHysteresis1(frac)`, `setHysteresis2(frac)` pour ajustement fin

### Régression accuracy TCN (époques 25-30)
La régression de ~0.3% entre les époques 25 et 30 est un artefact du LR fixe (1e-3) :
à >99% accuracy, les fluctuations stochastiques (~1000 samples) font osciller la courbe.
Piste d'amélioration : LR scheduler (ReduceLROnPlateau ou CosineAnnealing) + sauvegarde
du meilleur modèle (pas encore implémenté).

### Évaluation TCN vs PPI réels — méthodologie (`eval_ppi.py`)
La comparaison sample-à-sample cassette↔PPI est impossible (voir note dans `train.py`).
La métrique correcte : **classification S/L des intervalles** entre transitions.
- Extraction des intervalles en µs de la sortie TCN et du PPI binarisé (`> 0.0`)
- Détection automatique de la fin du pilote par fenêtre glissante (IQR/médiane < 0.30)
- Speed ratio estimé par ratio des médianes de pilote (TCN vs PPI)
- Seuil S/L = 1.5 × demi-période pilote ; comparaison séquentielle des bits après pilote
- Métrique invariante à la durée du pilote (pas de comparaison globale depuis t=0)

### Jeux non appariés dans test/ (noms trop différents)
- `Formula One Simulator Face A 16M.wav` ↔ `Formula 1 Simulator.wav` — à renommer
- `Golf Trophee Face A 16M.wav` ↔ `Golf.wav` — à renommer ou ajouter alias
- `Future Knight Face A 16M.wav` ↔ `Future Knight.wav` — devrait s'apparier (à débugger)
- `Gryzor Face B 16M.wav` ↔ `Gryzor.wav` — Face B, structure peut-être différente
