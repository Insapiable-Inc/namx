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
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/KnobWidget.cpp
    src/ui/KnobWidget.h
    src/ui/ToggleSwitch.cpp
    src/ui/ToggleSwitch.h
    src/ui/GUIConstants.h
    src/dsp/AudioEngine.cpp
    src/dsp/AudioEngine.h
    src/dsp/Constants.h
    src/dsp/KnobRanges.h
    src/dsp/EffectBase.h
    src/dsp/Effects.h
    src/dsp/IRLoader.cpp
    src/dsp/IRLoader.h
    src/dsp/IRProcessor.h
    src/dsp/NamProcessor.cpp
    src/dsp/NamProcessor.h
    src/dsp/EQProcessor.cpp
    src/dsp/EQProcessor.h
    src/dsp/TunerProcessor.cpp
    src/dsp/TunerProcessor.h
    src/dsp/NoiseGateProcessor.cpp
    src/dsp/NoiseGateProcessor.h
    src/dsp/CompressorProcessor.cpp
    src/dsp/CompressorProcessor.h
    src/dsp/ChorusProcessor.cpp
    src/dsp/ChorusProcessor.h
    src/dsp/FlangerProcessor.cpp
    src/dsp/FlangerProcessor.h
    src/dsp/PhaserProcessor.cpp
    src/dsp/PhaserProcessor.h
    src/dsp/TremoloProcessor.cpp
    src/dsp/TremoloProcessor.h
    src/dsp/DelayProcessor.cpp
    src/dsp/DelayProcessor.h
    src/dsp/ReverbProcessor.cpp
    src/dsp/ReverbProcessor.h
    src/dsp/DynamicWahProcessor.cpp
    src/dsp/DynamicWahProcessor.h
)

add_executable(NAMPlayer ${GUI_SOURCES} ${NAM_SOURCES} ${WAV_SOURCES})

target_include_directories(NAMPlayer PRIVATE
    ${NAM_ROOT}
    ${NAM_DEPS_PATH}/eigen
    ${NAM_DEPS_PATH}/nlohmann
    ${AUDIO_DSP_TOOLS_DIR}/dsp
    ${JACK_INCLUDE_DIRS}
    src/dsp
    src/ui
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
