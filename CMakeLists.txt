cmake_minimum_required(VERSION 3.14)
project(cpc_wav_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Génération du header de poids TCN
# Régénéré automatiquement si ml_env/cpc_model.pt est plus récent
# que export/tcn_weights.h.
# Nécessite : ml_env/bin/python3 (environnement virtuel du projet)
# ============================================================
set(TCN_MODEL   "${CMAKE_SOURCE_DIR}/ml_env/cpc_model.pt")
set(TCN_WEIGHTS "${CMAKE_SOURCE_DIR}/export/tcn_weights.h")
set(TCN_SCRIPT  "${CMAKE_SOURCE_DIR}/export_weights.py")

add_custom_command(
    OUTPUT  "${TCN_WEIGHTS}"
    COMMAND "${CMAKE_SOURCE_DIR}/ml_env/bin/python3" "${TCN_SCRIPT}"
    DEPENDS "${TCN_MODEL}" "${TCN_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Génération de export/tcn_weights.h depuis cpc_model.pt"
)
add_custom_target(tcn_weights DEPENDS "${TCN_WEIGHTS}")

# ============================================================
# Analyseur principal
# ============================================================
add_executable(cpc_wav_analyzer
    src/main.cpp
    src/wav_reader.cpp
    src/source_detector.cpp
    src/signal_analyzer.cpp
    src/block_analyzer.cpp
    src/protection_detector.cpp
    src/dump_matcher.cpp
    src/signal_converter.cpp
    src/wav_writer.cpp
    src/conversion_validator.cpp
    src/batch_processor.cpp
    src/dataset_exporter.cpp
)

target_include_directories(cpc_wav_analyzer PRIVATE src)

# ============================================================
# Bibliothèque d'export (convertisseur TCN + algorithmique)
# ============================================================
add_library(tape_to_ppi STATIC
    export/tcn_tape_to_ppi.cpp
    export/tape_to_ppi_converter.cpp
)

target_include_directories(tape_to_ppi PUBLIC export)
add_dependencies(tape_to_ppi tcn_weights)

# ============================================================
# Test d'inférence TCN
# Usage : ./test_tcn_inference [chemin/vers/out_dataset]
# ============================================================
add_executable(test_tcn_inference
    tests/test_tcn_inference.cpp
    src/wav_reader.cpp
    src/signal_converter.cpp
)

target_include_directories(test_tcn_inference PRIVATE src)
target_link_libraries(test_tcn_inference PRIVATE tape_to_ppi)
