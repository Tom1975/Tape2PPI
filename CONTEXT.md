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
cpc_wav_analyzer/
├── src/
│   ├── wav_reader.h / .cpp      # Lecture WAV (Phase 1 ✅)
│   ├── signal_analyzer.h / .cpp # Analyse du signal (Phase 2 🔜)
│   └── source_detector.h / .cpp # Détection cassette vs PPI (Phase 1 ✅ dans main.cpp)
├── tests/
│   ├── sample_cassette.wav      # Fichier de test cassette réel (à placer ici)
│   └── sample_ppi.wav           # Fichier de test PPI réel (à placer ici)
├── CONTEXT.md                   # Ce fichier
└── CMakeLists.txt
```

## Plan des phases

### Phase 1 — Lecture et détection de source ✅
- Lecture WAV pur C/C++ (sans dépendance externe)
- Formats supportés : PCM 8/16/24/32 bits, mono/stéréo (downmix auto)
- Gestion des chunks inconnus (LIST, INFO, fact...)
- Normalisation float [-1.0, +1.0]
- Détection cassette vs PPI :
  - Ratio de samples en zone intermédiaire (flancs lents = cassette)
  - Fallback bimodalité pour signal carré parfait (PPI)
  - Seuils à valider/ajuster sur vrais fichiers

### Phase 2 — Segmentation 🔜
- Détection des **silences** (seuil d'amplitude configurable + durée minimale)
- Découpage du signal en **blocs numérotés**
- Pour chaque bloc : position début/fin (samples + secondes), durée, amplitude max

### Phase 3 — Identification des blocs standards
Reconnaissance des structures CPC classiques dans chaque bloc :
- Pilote (tonalité répétitive, fréquence ~2000 Hz)
- Bit de sync + octet de sync (0x16)
- Données
- Trailer bits

### Phase 4 — Identification des blocs complexes
Détection de loaders spéciaux : Speedlock, Bleepload, etc.
Analyse heuristique de la forme d'onde pour les cas non standards.

### Phase 5 — Association cassette ↔ PPI
- Appairage des blocs entre les deux fichiers (par numéro, durée, structure)
- Vérification de similarité (corrélation temporelle, forme générale)
- Tolérance sur les durées (le PPI peut être légèrement plus rapide/lent)

### Phase 6 — Fonction de conversion
- Modélisation de la transformation cassette → PPI (ou inverse)
- La fonction doit être générique : applicable à tous les dumps du même type
- Objectif : pouvoir reconstruire un signal PPI depuis une cassette et vice-versa

## État actuel (Phase 1 complète)

### Ce qui est implémenté
- `src/wav_reader.h / .cpp` : lecture complète, normalisée, sans warnings fread
- `src/source_detector.h / .cpp` : détection cassette vs PPI (refactoré depuis main.cpp)
- `main.cpp` : affichage des métadonnées + appel source_detector

### Validation sur fichiers réels (terminée)

| Fichier | Source réelle | Détecté | |
|---------|--------------|---------|---|
| `k7 - 10th frames (US Gold - 1986).wav` | PPI | PPI | ✓ |
| `k7 - Mach 3 (Loriciel - 1987).wav` | PPI | PPI | ✓ |
| `Mach 3 16ST.wav` | CASSETTE | CASSETTE | ✓ |
| `10th_Frame__RERELEASE_KIXX.wav` | PPI | PPI | ✓ |

**Résultat : 4/4 correct.**

**Algorithme de détection** : normalisation par enveloppe locale (50 ms) + analyse de dérivée.
- PPI    : spikeRatio > 2% ET avgSpikeWidth < 3 samples
- CASSETTE : spikeRatio faible (signal sinusoïdal, dérivée lisse)

### Compilation
```bash
g++ -std=c++17 -O2 -Isrc main.cpp src/wav_reader.cpp src/source_detector.cpp -o cpc_wav_analyzer
./cpc_wav_analyzer fichier.wav
```

## Notes techniques importantes

### Format WAV CPC typique
- Sample rate : 44100 Hz (parfois 22050 Hz pour les vieux dumps)
- Bits : 16 bits signé PCM le plus souvent
- Durée typique d'un bloc pilote CPC : ~2-5 secondes
- Fréquence du pilote CPC : ~2000 Hz (période ~22 samples à 44100 Hz)
- Octet de sync : 0x16 répété, suivi de 0x24

### Heuristique détection PPI (à valider)
- `transitionRatio < 5%` ET `avgEdgeDuration < 3 samples` → PPI
- `samplesStable > 95%` (aucun front détecté) → PPI parfait
- Sinon → Cassette

### Points d'attention Phase 2
- Les silences inter-blocs CPC peuvent être très courts (~10-50 ms)
- Certains loaders non-standard n'ont pas de silence entre les blocs
- Prévoir un paramètre de seuil de silence configurable (ex: 100 ms par défaut)