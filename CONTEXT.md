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
│   ├── wav_reader.h / .cpp       # Lecture WAV (Phase 1 ✅)
│   ├── source_detector.h / .cpp  # Détection cassette vs PPI (Phase 1 ✅)
│   ├── signal_analyzer.h / .cpp  # Segmentation en blocs (Phase 2 ✅)
│   ├── block_analyzer.h / .cpp   # Analyse interne des blocs (Phase 3 ✅)
│   └── main.cpp                  # Affichage des résultats
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
g++ -std=c++17 -O2 -Isrc src/main.cpp src/wav_reader.cpp src/source_detector.cpp \
    src/signal_analyzer.cpp src/block_analyzer.cpp -o cpc_wav_analyzer
./cpc_wav_analyzer fichier.wav [fichier2.wav ...]
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

### Phase 4 — Identification des blocs complexes 🔜
Détection de loaders spéciaux : Speedlock, Bleepload, etc.
Analyse heuristique de la forme d'onde pour les cas non standards.

### Phase 5 — Association cassette ↔ PPI 🔜
- Appairage des blocs entre les deux fichiers (par numéro, durée, structure)
- Vérification de similarité (corrélation temporelle, forme générale)
- Tolérance sur les durées (le PPI peut être légèrement plus rapide/lent)

### Phase 6 — Fonction de conversion 🔜
- Modélisation de la transformation cassette → PPI (ou inverse)
- La fonction doit être générique : applicable à tous les dumps du même type
- Objectif : pouvoir reconstruire un signal PPI depuis une cassette et vice-versa

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
