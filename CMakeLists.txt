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

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# TONE3000 publishable key (client_id for the OAuth flows). Not a secret — it
# only identifies the app — but it belongs to whoever ships the build:
#   cmake -DNAM_TONE3000_CLIENT_ID=t3k_pub_… 
# Users can also paste one at runtime (Library → TONE3000).
set(NAM_TONE3000_CLIENT_ID "" CACHE STRING "TONE3000 publishable key baked into the build")

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
    src/ui/AudioStatusBar.cpp
    src/ui/AudioStatusBar.h
    src/ui/LibraryIndex.cpp
    src/ui/LibraryIndex.h
    src/ui/LibraryPanel.cpp
    src/ui/LibraryPanel.h
    src/ui/LibraryDock.cpp
    src/ui/LibraryDock.h
    src/ui/Tone3000Panel.cpp
    src/ui/Tone3000Panel.h
    src/net/Tone3000Client.cpp
    src/net/Tone3000Client.h
    resources/resources.qrc
    src/ui/ParamRegistry.h
    src/ui/PresetSerializer.cpp
    src/ui/PresetSerializer.h
    src/ui/PresetBar.cpp
    src/ui/PresetBar.h
    src/ui/TempoBar.cpp
    src/ui/TempoBar.h
    src/ui/TempoSync.h
    src/ui/VUMeter.cpp
    src/ui/VUMeter.h
    src/ui/GainReductionMeter.cpp
    src/ui/GainReductionMeter.h
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
        packaging/NAMPlayer.rc   # exe icon + version block
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
    src/net
)

if(NOT WIN32)
    target_include_directories(NAMPlayer PRIVATE ${JACK_INCLUDE_DIRS})
endif()

target_compile_definitions(NAMPlayer PRIVATE
    NAM_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST
    NAM_TONE3000_CLIENT_ID="${NAM_TONE3000_CLIENT_ID}"
    NAM_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(NAMPlayer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
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

# ---------------------------------------------------------------------------
# Install (cmake --install <build> --prefix /usr): binary + desktop integration.
# Packaging files live in packaging/, icons in resources/icons (see
# packaging/README.md). Windows deploys with windeployqt instead.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS NAMPlayer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    set(NAM_APP_ID "cl.pichulero.NAMPlayer")
    install(FILES packaging/${NAM_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/${NAM_APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/${NAM_APP_ID}.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    foreach(size 16 32 48 64 128 256 512)
        install(FILES resources/icons/NAMPlayer-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${NAM_APP_ID}.png)
    endforeach()
    install(FILES resources/icons/NAMPlayer.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${NAM_APP_ID}.svg)
endif()

# ---------------------------------------------------------------------------
# Tests (opt-in): -DNAM_BUILD_TESTS=ON → `ctest` or run the binaries directly.
# ---------------------------------------------------------------------------
option(NAM_BUILD_TESTS "Build the test executables" OFF)
if(NAM_BUILD_TESTS)
    enable_testing()
    add_executable(Tone3000ClientTest
        tests/Tone3000ClientTest.cpp
        src/net/Tone3000Client.cpp
        src/net/Tone3000Client.h
        src/dsp/Log.cpp
    )
    target_include_directories(Tone3000ClientTest PRIVATE src/net src/dsp)
    target_compile_definitions(Tone3000ClientTest PRIVATE
        MOCK_SERVER_PY="${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_tone3000.py")
    target_link_libraries(Tone3000ClientTest PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
    add_test(NAME Tone3000ClientTest COMMAND Tone3000ClientTest)
endif()

# ---------------------------------------------------------------------------
# Benchmark (opt-in): -DNAM_BUILD_BENCH=ON → NAMPlayerBench, the chain timer
# that checks the Phase 14 efficiency budget (see bench/NAMPlayerBench.cpp).
# ---------------------------------------------------------------------------
option(NAM_BUILD_BENCH "Build the NAMPlayerBench chain benchmark" OFF)
if(NAM_BUILD_BENCH)
    set(BENCH_DSP_SOURCES ${GUI_SOURCES})
    list(FILTER BENCH_DSP_SOURCES INCLUDE REGEX "^src/dsp/.*\\.cpp$")
    list(FILTER BENCH_DSP_SOURCES EXCLUDE REGEX "(AudioEngine|JackBackend|WasapiBackend)\\.cpp$")
    add_executable(NAMPlayerBench bench/NAMPlayerBench.cpp ${BENCH_DSP_SOURCES} ${NAM_SOURCES} ${WAV_SOURCES})
    target_include_directories(NAMPlayerBench PRIVATE
        ${NAM_ROOT} ${NAM_DEPS_PATH}/eigen ${NAM_DEPS_PATH}/nlohmann ${AUDIO_DSP_TOOLS_DIR}/dsp src/dsp)
    target_compile_definitions(NAMPlayerBench PRIVATE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
    target_link_libraries(NAMPlayerBench PRIVATE Qt6::Core) # Log.cpp: QLoggingCategory
    if(MSVC)
        target_compile_options(NAMPlayerBench PRIVATE /O2 /arch:AVX2 /EHsc)
    else()
        target_compile_options(NAMPlayerBench PRIVATE -O3 ${NAM_SIMD_FLAGS})
    endif()
endif()
