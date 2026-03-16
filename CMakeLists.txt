cmake_minimum_required(VERSION 3.14)
project(cpc_wav_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
)

target_include_directories(cpc_wav_analyzer PRIVATE src)
