cmake_minimum_required(VERSION 3.16)
project(NAMPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Find JACK
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED jack)

# NAM core sources
set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NAM_DEPS_PATH "${NAM_ROOT}/Dependencies")

file(GLOB_RECURSE NAM_SOURCES
    "${NAM_ROOT}/NAM/*.cpp"
    "${NAM_ROOT}/NAM/*.c"
)

# AudioDSPTools wav sources for IR loading
set(AUDIO_DSP_TOOLS_DIR "${NAM_DEPS_PATH}/AudioDSPTools")
set(WAV_SOURCES "${AUDIO_DSP_TOOLS_DIR}/dsp/wav.cpp")

# GUI sources
set(GUI_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/Constants.h
    src/AudioEngine.cpp
    src/AudioEngine.h
    src/KnobWidget.cpp
    src/KnobWidget.h
    src/IRLoader.cpp
    src/IRLoader.h
    src/EQProcessor.cpp
    src/EQProcessor.h
    src/EffectBase.h
    src/TunerProcessor.cpp
    src/TunerProcessor.h
    src/NoiseGateProcessor.cpp
    src/NoiseGateProcessor.h
    src/CompressorProcessor.cpp
    src/CompressorProcessor.h
    src/ChorusProcessor.cpp
    src/ChorusProcessor.h
    src/FlangerProcessor.cpp
    src/FlangerProcessor.h
    src/PhaserProcessor.cpp
    src/PhaserProcessor.h
    src/TremoloProcessor.cpp
    src/TremoloProcessor.h
    src/DelayProcessor.cpp
    src/DelayProcessor.h
    src/ReverbProcessor.cpp
    src/ReverbProcessor.h
    src/ToggleSwitch.cpp
    src/ToggleSwitch.h
    src/PitchShiftProcessor.cpp
    src/PitchShiftProcessor.h
)

add_executable(NAMPlayer ${GUI_SOURCES} ${NAM_SOURCES} ${WAV_SOURCES})

target_include_directories(NAMPlayer PRIVATE
    ${NAM_ROOT}
    ${NAM_DEPS_PATH}/eigen
    ${NAM_DEPS_PATH}/nlohmann
    ${AUDIO_DSP_TOOLS_DIR}/dsp
    ${JACK_INCLUDE_DIRS}
    src
)

target_compile_definitions(NAMPlayer PRIVATE
    NAM_SAMPLE_FLOAT
)

target_link_libraries(NAMPlayer PRIVATE
    Qt6::Core
    Qt6::Widgets
    ${JACK_LIBRARIES}
)

target_compile_options(NAMPlayer PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-Og -g>
)

# Suppress sign-compare warnings in third-party wav.cpp
set_source_files_properties(${WAV_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare -Wno-error")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(NAMPlayer PRIVATE stdc++fs)
endif()
