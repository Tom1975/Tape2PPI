#pragma once

#include <string>

// Exporte les paires de blocs alignés (cassette ↔ PPI) pour l'entraînement ML.
//
// Pour chaque paire de fichiers appariés dans inputDir, exporte :
//   block_NNNN_cassette.wav  — segment cassette
//   block_NNNN_ppi.wav       — segment PPI correspondant
//   dataset.json             — métadonnées (protection, sample rates, speed ratio...)
//
// Seuls les blocs effectivement appariés (DumpMatch.pairs) sont exportés.
void exportDataset(const std::string& inputDir, const std::string& outputDir);
