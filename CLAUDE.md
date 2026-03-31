# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This repo is a fork of [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) with a standalone Linux GUI added under `gui/`. The root-level code (`NAM/`, `tools/`, `Dependencies/`) is the upstream C++ DSP library. All GUI work lives exclusively in `gui/`.

```
nam-linux-gui/
├── NAM/           # Core DSP library: neural net inference (WaveNet, LSTM, ConvNet, Linear)
├── Dependencies/  # Git submodules: Eigen (linear algebra), AudioDSPTools (WAV I/O), nlohmann (JSON)
├── tools/         # Upstream CLI tools: loadmodel, benchmodel, run_tests
└── gui/           # Qt6 + JACK standalone player (all GUI work happens here)
    ├── build.sh           # One-shot build script
    ├── CMakeLists.txt     # CMake build definition
    └── src/               # All GUI C++ source
```

## Build

```bash
cd gui
bash build.sh          # Release (default) → gui/build-release/NAMPlayer
bash build.sh Debug    # Debug → gui/build-debug/NAMPlayer
```

The script initialises submodules automatically. Manual CMake:

```bash
cmake -S gui -B gui/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build gui/build-release --parallel $(nproc)
```

**Required packages (Arch):** `qt6-base jack2 cmake ninja`
**Required packages (Ubuntu 22.04+):** `qt6-base-dev libjack-jackd2-dev cmake ninja-build`

## Running

```bash
# PipeWire-JACK (modern distros) — just launch:
gui/build-release/NAMPlayer

# Classic jackd:
jackd -d alsa -r 44100 -p 256 &
gui/build-release/NAMPlayer
```

Connect ports with `jack_connect`, qjackctl, or qpwgraph:
```bash
jack_connect system:capture_1 NAMPlayer:input
jack_connect NAMPlayer:output_L system:playback_1
jack_connect NAMPlayer:output_R system:playback_2
```

## GUI architecture

The GUI is a Qt6 application using JACK for real-time audio. The key design constraint is **thread safety between the GUI thread and the JACK real-time audio thread**.

### Audio signal chain (in `AudioEngine::processAudio`)

```
JACK input (mono)
  → Input gain (smoothed)
  → [Tuner] → [Noise Gate] → [Compressor]
  → [Chorus 1] → [Flanger] → [Phaser]     ← all mono, pre-NAM
  → NAM DSP model
  → 3-band EQ (biquad, try_lock)
  → split to stereo (L = R = mono signal)
  → [Chorus 2] → [Tremolo] → [Delay] → [Reverb]  ← stereo, post-NAM
  → [IR convolution] (applied to both channels)
  → Output gain (smoothed)
  → JACK output_L + output_R (stereo)
```

### Thread-safety patterns

- **Simple parameters** (gains, effect enable flags, LFO rates, etc.) use `std::atomic<float>` / `std::atomic<bool>` — set from GUI thread, read lock-free in audio thread.
- **EQ coefficient updates** use `std::mutex mEQMutex` + `try_lock` in the audio thread (skips one block if GUI is writing — inaudible at normal buffer sizes).
- **NAM model swaps** use double-buffering (`mModelA`/`mModelB` + `mActiveModel` atomic) with `mModelMutex` held only during pointer swap, never during audio processing.
- **IR kernel swaps** follow the same double-buffer pattern inside `IRLoader`.
- **No allocations** happen in the audio callback.

### Key classes

| Class | File | Purpose |
|---|---|---|
| `AudioEngine` | `src/AudioEngine.h/cpp` | JACK client, owns all processors, runs signal chain |
| `MainWindow` | `src/MainWindow.h/cpp` | Qt UI, wires knobs/checkboxes to `AudioEngine` setters |
| `KnobWidget` | `src/KnobWidget.h/cpp` | `QDial` wrapper with float range, label, value display |
| `EQProcessor` | `src/EQProcessor.h/cpp` | 3-band biquad EQ (Audio EQ Cookbook) |
| `IRLoader` | `src/IRLoader.h/cpp` | FIR convolution cab sim, double-buffered kernel swap |
| `TunerProcessor` | `src/TunerProcessor.h/cpp` | Autocorrelation pitch detect, filled by audio thread / read by GUI timer |
| `NoiseGateProcessor` | `src/NoiseGateProcessor.h/cpp` | Envelope-follower gate |
| `CompressorProcessor` | `src/CompressorProcessor.h/cpp` | Feedforward peak compressor |
| `ChorusProcessor` | `src/ChorusProcessor.h/cpp` | Modulated delay; `processBlock` = mono, `processBlockStereo` = stereo (180° LFO offset) |
| `FlangerProcessor` | `src/FlangerProcessor.h/cpp` | Short modulated delay + feedback |
| `PhaserProcessor` | `src/PhaserProcessor.h/cpp` | 4-stage all-pass cascade + LFO |
| `TremoloProcessor` | `src/TremoloProcessor.h/cpp` | AM; stereo uses 90° LFO offset |
| `DelayProcessor` | `src/DelayProcessor.h/cpp` | Ping-pong stereo delay |
| `ReverbProcessor` | `src/ReverbProcessor.h/cpp` | Freeverb (8 comb + 4 all-pass, stereo) |

All effect processors inherit from `EffectBase` (`src/EffectBase.h`) and implement `setSampleRate`, `reset`, `processBlock` (mono), and optionally `processBlockStereo`.

### Preset system

Presets are JSON files (`.nampreset`) saved/loaded via Qt's `QJsonDocument`. They store all knob values and effect enable flags. No model/IR paths are saved — only processing parameters.

## NAM core library

`NAM/dsp.h` defines `nam::DSP`, the base class for all models. The key interface used by `AudioEngine`:

```cpp
model->Reset(sampleRate, maxBufferSize);
model->prewarm();  // run inference on silence to settle state
model->process(NAM_SAMPLE** inputs, NAM_SAMPLE** outputs, int numFrames);
```

`NAM_SAMPLE` is `float` when compiled with `-DNAM_SAMPLE_FLOAT` (set in `CMakeLists.txt`). Models are loaded via `nam::get_dsp(std::filesystem::path)` from `NAM/get_dsp.h`.

## CMake notes

- `CMAKE_AUTOMOC ON` — MOC runs automatically; add new Qt classes to `GUI_SOURCES` in `gui/CMakeLists.txt`.
- The clang language server will show false-positive errors for Qt types (`QWidget`, `QString`, etc.) because it doesn't see the CMake include paths. Ignore these; the actual compiler succeeds.
- `stdc++fs` is linked explicitly for `std::filesystem` on Linux.
