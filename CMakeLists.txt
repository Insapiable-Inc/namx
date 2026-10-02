cmake_minimum_required(VERSION 3.20) # CXX_STANDARD 23
project(NAMPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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
    # MIDI input: the ALSA sequencer (Phase 16).
    pkg_check_modules(ALSA REQUIRED alsa)
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
    src/ui/MidiMappingsDialog.cpp
    src/ui/MidiMappingsDialog.h
    src/midi/MidiMessage.h
    src/midi/MidiInput.cpp
    src/midi/MidiInput.h
    src/midi/MidiMapper.cpp
    src/midi/MidiMapper.h
    src/ui/VUMeter.cpp
    src/ui/VUMeter.h
    src/ui/GainReductionMeter.cpp
    src/ui/GainReductionMeter.h
    src/ui/Theme.cpp
    src/ui/TunerDisplay.cpp
    src/ui/TunerDisplay.h
    src/ui/Theme.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/dsp/AudioEngine.cpp
    src/dsp/AudioEngine.h
    src/dsp/IAudioBackend.h
    src/dsp/Log.cpp
    src/dsp/Log.h
    src/dsp/Constants.h
    src/dsp/EffectBase.h
    src/dsp/IRLoader.cpp
    src/dsp/IRLoader.h
    src/dsp/IRProcessor.cpp
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
    src/dsp/Metronome.cpp
    src/dsp/DriveProcessor.cpp
    src/dsp/DriveProcessor.h
    src/dsp/DoublerProcessor.cpp
    src/dsp/DoublerProcessor.h
    src/dsp/VolumeProcessor.cpp
    src/dsp/VolumeProcessor.h
    src/dsp/OutputLimiter.cpp
    src/dsp/OutputLimiter.h
    src/dsp/Common.h
    src/dsp/DelayLine.h
    src/dsp/Denormals.h
    src/dsp/PitchShifter.h
    src/dsp/TransposeProcessor.h
    src/dsp/Metronome.h
)

# Per-platform backend. Each .cpp defines IAudioBackend::create(), exactly one
# of which is compiled per build, so the factory is unambiguous.
if(WIN32)
    list(APPEND GUI_SOURCES
        src/dsp/WasapiBackend.cpp
        src/dsp/WasapiBackend.h
        packaging/NAMPlayer.rc   # exe icon + version block
        src/midi/WinMidiInput.cpp
    )
else()
    list(APPEND GUI_SOURCES
        src/dsp/JackBackend.cpp
        src/dsp/JackBackend.h
        src/midi/AlsaMidiInput.cpp
    )
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

add_executable(NAMPlayer ${GUI_SOURCES} ${NAM_SOURCES} ${WAV_SOURCES})

# Warnings (CPP_GUIDELINES.md → Types and casts) apply to our code only: third-party
# sources compiled into our targets are silenced, their headers included as SYSTEM.
# Errors on GCC/Clang; MSVC stays at /W4 without /WX until a Windows build is clean.
if(MSVC)
    set(NAM_WARNING_FLAGS /W4)
    set(NAM_THIRD_PARTY_FLAGS /w)
else()
    set(NAM_WARNING_FLAGS -Wall -Wextra -Wconversion -Wsign-conversion -Wsuggest-override -Werror)
    set(NAM_THIRD_PARTY_FLAGS -w)
endif()
set_source_files_properties(${NAM_SOURCES} ${WAV_SOURCES} PROPERTIES COMPILE_OPTIONS "${NAM_THIRD_PARTY_FLAGS}")

# On Windows we want a GUI app (no console window). Linux ignores WIN32.
set_target_properties(NAMPlayer PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(NAMPlayer SYSTEM PRIVATE
    ${NAM_ROOT}
    ${NAM_DEPS_PATH}/eigen
    ${NAM_DEPS_PATH}/nlohmann
    ${AUDIO_DSP_TOOLS_DIR}/dsp
)
target_include_directories(NAMPlayer PRIVATE
    src/dsp
    src/ui
    src/net
    src/midi
)

if(NOT WIN32)
    target_include_directories(NAMPlayer PRIVATE ${JACK_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS})
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
    target_link_libraries(NAMPlayer PRIVATE ole32 mmdevapi avrt winmm)
    target_compile_definitions(NAMPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(NAMPlayer PRIVATE ${JACK_LIBRARIES} ${ALSA_LIBRARIES})
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
        ${NAM_WARNING_FLAGS} /permissive- /EHsc
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
        ${NAM_WARNING_FLAGS}
        $<$<CONFIG:Release>:-O3 ${NAM_SIMD_FLAGS}>
        $<$<CONFIG:Debug>:-Og -g>
    )
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
    target_compile_options(Tone3000ClientTest PRIVATE ${NAM_WARNING_FLAGS})
    target_compile_definitions(Tone3000ClientTest PRIVATE
        MOCK_SERVER_PY="${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_tone3000.py")
    target_link_libraries(Tone3000ClientTest PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
    add_test(NAME Tone3000ClientTest COMMAND Tone3000ClientTest)

    add_executable(MidiMapperTest
        tests/MidiMapperTest.cpp
        src/midi/MidiMapper.cpp
        src/midi/MidiMapper.h
        src/ui/KnobWidget.cpp
        src/ui/KnobWidget.h
        src/ui/ToggleSwitch.cpp
        src/ui/ToggleSwitch.h
        src/ui/Theme.cpp
    )
    target_include_directories(MidiMapperTest PRIVATE src/midi src/ui src/dsp)
    target_compile_options(MidiMapperTest PRIVATE ${NAM_WARNING_FLAGS})
    target_link_libraries(MidiMapperTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME MidiMapperTest COMMAND MidiMapperTest)

    # Bit-exact effect outputs (tests/DspGoldenTest.cpp): guards DSP refactors.
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(Threads REQUIRED)
    set(GOLDEN_DSP_SOURCES ${GUI_SOURCES})
    list(FILTER GOLDEN_DSP_SOURCES INCLUDE REGEX "^src/dsp/.*\\.cpp$")
    list(FILTER GOLDEN_DSP_SOURCES EXCLUDE REGEX "(AudioEngine|JackBackend|WasapiBackend|NamProcessor)\\.cpp$")
    add_executable(DspGoldenTest tests/DspGoldenTest.cpp ${GOLDEN_DSP_SOURCES} ${WAV_SOURCES})
    target_include_directories(DspGoldenTest SYSTEM PRIVATE ${NAM_DEPS_PATH}/eigen ${AUDIO_DSP_TOOLS_DIR}/dsp)
    target_include_directories(DspGoldenTest PRIVATE src/dsp)
    target_compile_definitions(DspGoldenTest PRIVATE NAM_SAMPLE_FLOAT)
    target_compile_options(DspGoldenTest PRIVATE ${NAM_WARNING_FLAGS})
    target_link_libraries(DspGoldenTest PRIVATE Qt6::Core Qt6::Test Threads::Threads)
    add_test(NAME DspGoldenTest COMMAND DspGoldenTest)
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
    target_include_directories(NAMPlayerBench SYSTEM PRIVATE
        ${NAM_ROOT} ${NAM_DEPS_PATH}/eigen ${NAM_DEPS_PATH}/nlohmann ${AUDIO_DSP_TOOLS_DIR}/dsp)
    target_include_directories(NAMPlayerBench PRIVATE src/dsp)
    target_compile_definitions(NAMPlayerBench PRIVATE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
    target_link_libraries(NAMPlayerBench PRIVATE Qt6::Core) # Log.cpp: QLoggingCategory
    if(MSVC)
        target_compile_options(NAMPlayerBench PRIVATE ${NAM_WARNING_FLAGS} /O2 /arch:AVX2 /EHsc)
    else()
        target_compile_options(NAMPlayerBench PRIVATE ${NAM_WARNING_FLAGS} -O3 ${NAM_SIMD_FLAGS})
    endif()
endif()
