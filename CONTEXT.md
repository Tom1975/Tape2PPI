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
│   └── main.cpp                      # Point d'entrée (modes 1 fichier / 2 fichiers / batch)
├── export/
│   ├── tape_to_ppi_converter.h       # Export émulateur — convertisseur causal (Phase 7 ✅)
│   └── tape_to_ppi_converter.cpp     # Implémentation sans dépendance externe
├── test/
│   ├── 10th_Frame__RERELEASE_KIXX.wav      # PPI, 44100 Hz, 8 bits
│   ├── k7 - 10th frames (US Gold - 1986).wav  # PPI, 44100 Hz, 8 bits
│   ├── k7 - Mach 3 (Loriciel - 1987).wav   # PPI, 44100 Hz, 8 bits
│   └── Mach 3 16ST.wav                      # CASSETTE, 48000 Hz, 16 bits stéréo
├── CONTEXT.md
└── CMakeLists.txt
```

### Compilation
```bash
g++ -std=c++17 -O2 -Isrc \
  src/main.cpp src/wav_reader.cpp src/source_detector.cpp \
  src/signal_analyzer.cpp src/block_analyzer.cpp src/protection_detector.cpp \
  src/dump_matcher.cpp src/signal_converter.cpp src/wav_writer.cpp \
  src/conversion_validator.cpp src/batch_processor.cpp \
  -o cpc_wav_analyzer
```

### Utilisation
```bash
./cpc_wav_analyzer fichier.wav               # analyse individuelle
./cpc_wav_analyzer fichier1.wav fichier2.wav # analyse + appariement + conversion
./cpc_wav_analyzer --batch répertoire/       # traitement batch
```

---

## Plan des phases

### Phase 1 — Lecture et détection de source ✅
- Lecture WAV pur C/C++ (sans dépendance externe)
- Formats supportés : PCM 8/16/24/32 bits, mono/stéréo (downmix auto)
- Gestion des chunks inconnus (LIST, INFO, fact...)
- Normalisation float [-1.0, +1.0]
- Détection cassette vs PPI par normalisation enveloppe locale + analyse de dérivée :
  - PPI : spikeRatio > 2% ET avgSpikeWidth < 3 samples
  - CASSETTE : spikeRatio faible (signal sinusoïdal, dérivée lisse)

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
- Appariement séquentiel des blocs (même index)
- Speed ratio = médiane(dur2/dur1) sur les blocs > 0.5 s
- Consistance = 1 − CV×5 (coefficient de variation des ratios)
- Confiance : nombre de blocs (+0.35), CV bas (+0.35), structures compatibles (+0.20), même protection (+0.10)
- Résultat : MATCHED ≥ 0.70, PARTIAL ≥ 0.40, FAILED sinon

### Phase 6 — Conversion signal ✅
- **Cassette → PPI** : comparateur adaptatif (DC offset 50 ms + hystérésis 8% RMS locale 10 ms)
- **PPI → Cassette** : filtfilt IIR passe-bas (cutoff = 3× fréquence pilote) + renormalisation
- Écriture WAV PCM 16 bits signé mono (`wav_writer`)
- En mode 2 fichiers : conversion automatique si sources différentes, validation après conversion

### Phase 7 — Affinage, validation batch et export émulateur ✅
- **Validateur** (`conversion_validator`) : score par bloc comparant pilote (fréquence, durée),
  codage L/S et sync 0x16 du converti vs PPI de référence
- **Mode batch** (`--batch répertoire/`) : analyse tous les WAV, appariement automatique
  cassettes × PPI, conversion + validation, bilan global ; ignore les fichiers générés
- **Export émulateur** (`export/tape_to_ppi_converter.h/.cpp`) :
  - Classe `TapeToPPIConverter` causale, traitement échantillon par échantillon
  - Filtre passe-haut IIR (couplage AC, fc=7.2 Hz, τ=R318×C319=22 ms)
  - Deux étages Schmitt trigger à hystérésis fixe (schéma CPC 464/664/6128) :
    étage 1 (IC302 pins 5/6/7) ≈ 3%, étage 2 (IC302 pins 9/10/8) ≈ 12%
  - Aucune dépendance externe (C++11, `<cmath>` uniquement)

---

## Validation sur fichiers réels

### Phase 1 (détection source) — 4/4 ✓

| Fichier | Source réelle | Détecté |
|---------|--------------|---------|
| `k7 - 10th frames (US Gold - 1986).wav` | PPI | PPI ✓ |
| `k7 - Mach 3 (Loriciel - 1987).wav` | PPI | PPI ✓ |
| `Mach 3 16ST.wav` | CASSETTE | CASSETTE ✓ |
| `10th_Frame__RERELEASE_KIXX.wav` | PPI | PPI ✓ |

### Phase 3 (analyse de blocs) — résultats observés

| Fichier | Blocs | Bloc 1 pilote | Bloc 1 0x16 | Notes |
|---------|-------|--------------|-------------|-------|
| `10th_Frame__RERELEASE_KIXX.wav` | 5 | 760 Hz, S=15 L=30 | ✓ | Blocs 3-5 : données courtes, 0x16 non décodé |
| `Mach 3 16ST.wav` | 6 | 1412 Hz, S=8 L=18 | ✓ | Blocs 3-5 sans pilote (loader custom) |
| `k7 - 10th frames (US Gold - 1986).wav` | 5 | 735 Hz, S=16 L=30 | ✓ | Blocs 1-2 à 735 Hz, blocs 3-5 à 760 Hz |
| `k7 - Mach 3 (Loriciel - 1987).wav` | 6 | 1378 Hz, S=8 L=16 | ✓ | Blocs 2-5 sans pilote (loader custom) |

### Phase 5 (appariement) — résultats observés

| Paire | Résultat | Confiance | Speed ratio | Notes |
|-------|---------|-----------|-------------|-------|
| Mach 3 cassette ↔ PPI | APPARIÉ | 100% | ×1.0239 | PPI 2.4% plus lent |
| 10th Frame PPI ↔ PPI | APPARIÉ | 75% | ×1.0856 | Deux fichiers PPI — même source |

### Phase 7 (conversion + validation) — résultats observés

| Paire | Score conversion | Notes |
|-------|----------------|-------|
| Mach 3 16ST → PPI | 81% | Pilote 0% d'erreur, L/S err=12.5% |

### Test batch (mode `--batch test/`) — 13 fichiers

| Fichier | Source | Blocs | Protection |
|---------|--------|-------|------------|
| `10th_Frame__RERELEASE_KIXX.wav` | PPI | 5 | Standard ROM |
| `k7 - 10th frames (US Gold - 1986).wav` | PPI | 5 | Standard ROM |
| `k7 - Mach 3 (Loriciel - 1987).wav` | PPI | 6 | Speedlock |
| `Mach 3 16ST.wav` | CASSETTE | 6 | Speedlock |
| `Combat School.wav` | PPI | 8 | Standard ROM |
| `Combat School Demo.wav` | PPI | 3 | Standard ROM |
| `Combat School - Levels.wav` | PPI | 5 | — |
| `Gryzor.wav` | PPI | 6 | Standard ROM |
| `BB6BCSW.WAV` | PPI | 1 | — |
| `Big Box Face 6B 16ST.wav` | ? | 30 | Standard ROM |
| `Combat School Face A 16M.wav` | ? | 11 | — |
| `Combat School Face B 16M.wav` | ? | 11 | Standard ROM |
| `Gryzor 16M image + bloc 1 + bloc 2 data ok.wav` | ? | 5 | Standard ROM |

**Problèmes identifiés :**
- Les fichiers `16M` / `16ST` (enregistrements cassette 16-bit) sont classifiés `?` au lieu de `CASSETTE` → à corriger dans `source_detector`
- Faux match `Mach 3 16ST` ↔ `BB6BCSW.WAV` à ratio ×631 (absurde) — le filtre sur speed ratio doit rejeter les ratios hors [0.5, 2.0]

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

### Export émulateur (`export/tape_to_ppi_converter`)
- Classe causale (temps réel, pas de look-ahead)
- HP IIR : `y[n] = alpha*(y[n-1] + x[n] - x[n-1])`, cutoff 7.2 Hz (τ = R318×C319 = 22 ms)
- Étage 1 — Schmitt trigger fixe : hystérésis ≈ 3% (R310 = 470 kΩ, IC302 pins 5/6/7)
- Étage 2 — Schmitt trigger fixe : hystérésis ≈ 12% (R307 = 47 kΩ, IC302 pins 9/10/8)
- Note de polarité : 3 inversions au total (transistor + 2 comparateurs) → sortie inversée
- `setACCouplingHz(hz)`, `setHysteresis1(frac)`, `setHysteresis2(frac)` pour ajustement fin
