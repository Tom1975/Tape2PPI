#pragma once

#include <string>

// Lance le traitement batch d'un répertoire :
// - Analyse tous les fichiers .wav/.WAV
// - Détecte source (cassette / PPI) pour chacun
// - Tente d'apparier toutes les paires cassette × PPI
// - Pour chaque paire appariée : convertit cassette → PPI,
//   valide la conversion bloc par bloc, affiche les résultats
// - Affiche un bilan global en fin de traitement
void runBatch(const std::string& directory);
