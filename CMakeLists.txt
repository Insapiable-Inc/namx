cmake_minimum_required(VERSION 3.16)
project(NAMPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# JACK is the Linux audio backend. On Windows we use WASAPI via the system
# DLLs (ole32, mmdevapi) — no external package needed.
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK REQUIRED jack)
endif()

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

# ---------------------------------------------------------------------------
# GUI sources (platform-agnostic)
# ---------------------------------------------------------------------------

set(GUI_SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/KnobWidget.cpp
    src/ui/KnobWidget.h
    src/ui/ToggleSwitch.cpp
    src/ui/ToggleSwitch.h
    src/ui/EffectShortcuts.cpp
    src/ui/EffectShortcuts.h
    src/ui/RackNavigator.cpp
    src/ui/RackNavigator.h
    src/ui/AudioStatusBar.cpp
    src/ui/AudioStatusBar.h
    src/ui/ParamRegistry.h
    src/ui/PresetSerializer.cpp
    src/ui/PresetSerializer.h
    src/ui/VUMeter.cpp
    src/ui/VUMeter.h
    src/ui/Theme.cpp
    src/ui/TunerDisplay.cpp
    src/ui/TunerDisplay.h
    src/ui/Theme.h
    src/dsp/AudioEngine.cpp
    src/dsp/AudioEngine.h
    src/dsp/IAudioBackend.h
    src/dsp/Log.cpp
    src/dsp/Log.h
    src/dsp/Constants.h
    src/dsp/EffectBase.h
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

# Per-platform backend. Each .cpp defines IAudioBackend::create(), exactly one
# of which is compiled per build, so the factory is unambiguous.
if(WIN32)
    list(APPEND GUI_SOURCES
        src/dsp/WasapiBackend.cpp
        src/dsp/WasapiBackend.h
    )
else()
    list(APPEND GUI_SOURCES
        src/dsp/JackBackend.cpp
        src/dsp/JackBackend.h
    )
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

add_executable(NAMPlayer ${GUI_SOURCES} ${NAM_SOURCES} ${WAV_SOURCES})

# On Windows we want a GUI app (no console window). Linux ignores WIN32.
set_target_properties(NAMPlayer PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(NAMPlayer PRIVATE
    ${NAM_ROOT}
    ${NAM_DEPS_PATH}/eigen
    ${NAM_DEPS_PATH}/nlohmann
    ${AUDIO_DSP_TOOLS_DIR}/dsp
    src/dsp
    src/ui
)

if(NOT WIN32)
    target_include_directories(NAMPlayer PRIVATE ${JACK_INCLUDE_DIRS})
endif()

target_compile_definitions(NAMPlayer PRIVATE
    NAM_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST
)

target_link_libraries(NAMPlayer PRIVATE
    Qt6::Core
    Qt6::Widgets
)

if(WIN32)
    # WASAPI lives in these system DLLs.
    target_link_libraries(NAMPlayer PRIVATE ole32 mmdevapi avrt)
    target_compile_definitions(NAMPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(NAMPlayer PRIVATE ${JACK_LIBRARIES})
endif()

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------

# Eigen (NAM inference + IR convolution) needs AVX2/FMA to vectorize fully;
# baseline x86-64 limits it to SSE2. NAM_NATIVE_OPT=ON (default) uses
# -march=native — fastest, but the binary is tied to the build machine's CPU.
# CI/release builds MUST use -DNAM_NATIVE_OPT=OFF, which targets portable
# AVX2+FMA (any x86-64 CPU from ~2013 on).
option(NAM_NATIVE_OPT "Optimize for the build machine's CPU (-march=native); OFF = portable -mavx2 -mfma" ON)

if(MSVC)
    # MSVC has no -march=native equivalent; /arch:AVX2 is both the fast and
    # the portable choice, so NAM_NATIVE_OPT has no effect here.
    target_compile_options(NAMPlayer PRIVATE
        /W3 /permissive- /EHsc
        $<$<CONFIG:Release>:/O2 /arch:AVX2>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
else()
    if(NAM_NATIVE_OPT)
        set(NAM_SIMD_FLAGS -march=native)
    else()
        set(NAM_SIMD_FLAGS -mavx2 -mfma)
    endif()
    target_compile_options(NAMPlayer PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        $<$<CONFIG:Release>:-O3 ${NAM_SIMD_FLAGS}>
        $<$<CONFIG:Debug>:-Og -g>
    )
endif()

# Suppress sign-compare warnings in third-party wav.cpp
if(NOT MSVC)
    set_source_files_properties(${WAV_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-sign-compare -Wno-error")
endif()

# stdc++fs is libstdc++-only — MSVC ships <filesystem> in the runtime.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(NAMPlayer PRIVATE stdc++fs)
endif()
