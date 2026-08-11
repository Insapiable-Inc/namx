# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This repo is a fork of [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) with a standalone cross-platform GUI added under `gui/`. The root-level code (`NAM/`, `tools/`, `Dependencies/`) is the upstream C++ DSP library. All GUI work lives exclusively in `gui/`.

```
nam-linux-gui/
├── NAM/           # Core DSP library: neural net inference (WaveNet, LSTM, ConvNet, Linear)
├── Dependencies/  # Git submodules: Eigen (linear algebra), AudioDSPTools (WAV I/O), nlohmann (JSON)
├── tools/         # Upstream CLI tools: loadmodel, benchmodel, run_tests
├── ROADMAP.md     # Public-release plan — phased checklist, worked one phase at a time
└── gui/           # Qt6 standalone player (all GUI work happens here)
    ├── build.sh           # One-shot Linux build script
    ├── build.ps1          # One-shot Windows build script (UTF-8 BOM, CRLF — keep it that way)
    ├── BUILD.md           # The real build/usage manual (per-distro deps, Windows, troubleshooting)
    ├── CMakeLists.txt     # CMake build definition
    ├── presets/           # Personal .nampreset files — untracked on purpose, do not add to git
    └── src/
        ├── main.cpp       # QApplication setup (org/app name → QSettings path), Theme::apply
        ├── ui/            # Qt widgets
        └── dsp/           # AudioEngine, backends, effect processors (no Qt dependencies)
```

## Release plan workflow

`ROADMAP.md` is the v1.0 release plan: phases 0–8 with checkboxes, executed **in order, one phase at a time**. Check items off as they land.

## Build

```bash
# Linux
cd gui
bash build.sh          # Release (default) → gui/build-release/NAMPlayer
bash build.sh Debug    # Debug → gui/build-debug/NAMPlayer

# Windows (PowerShell; needs MSVC + Qt6 + vcpkg — see gui/BUILD.md)
cd gui; ./build.ps1
```

The script initialises submodules automatically. Manual CMake:

```bash
cmake -S gui -B gui/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build gui/build-release --parallel $(nproc)
```

**Required packages (Arch):** `qt6-base jack2 cmake ninja`
**Required packages (Ubuntu 22.04+):** `qt6-base-dev libjack-jackd2-dev cmake ninja-build`

Release builds use `-march=native` (see the caveat in `gui/CMakeLists.txt` — must be dropped for distributable binaries).

## Running

```bash
# Linux, PipeWire-JACK (modern distros) — just launch:
gui/build-release/NAMPlayer

# Classic jackd:
jackd -d alsa -r 44100 -p 256 &
gui/build-release/NAMPlayer
```

JACK ports: `NAMPlayer:input`, `NAMPlayer:output_L`, `NAMPlayer:output_R`. Connect with `jack_connect`, qjackctl, or qpwgraph. Device/port routing is also selectable from dropdowns in the app, and connections persist via QSettings. On Windows the WASAPI backend picks devices directly — no server needed.

## GUI architecture

Qt6 application with a platform audio backend behind the `IAudioBackend` interface (`src/dsp/IAudioBackend.h`): `JackBackend` on Linux, `WasapiBackend` on Windows. Exactly one backend .cpp is compiled per platform (each defines `IAudioBackend::create()`); everything else is platform-agnostic. The key design constraint is **thread safety between the GUI thread and the real-time audio thread**.

### Signal chain

`AudioEngine` (implements `IAudioBackend::Callback`) owns all processors and one ordered `std::vector<EffectBase*> mChain`, iterated every callback. **Processing order = UI order** — the effect strip in `MainWindow` is built by iterating `chain()`.

```
mono input → input gain (smoothed)
  → Tuner → Gate → Compressor → Wah → Flanger → Phaser   (mono section)
  → NAM model → EQ
  → Chorus → Tremolo → Delay → Reverb                     (stereo-capable section)
  → IR convolution
  → output gain (smoothed) → stereo out (L, R)
```

### EffectBase dispatch

All effects inherit `EffectBase` (`src/dsp/EffectBase.h` — the header comment documents the full routing table). `process(L, R, n, inputIsStereo)` returns whether output is now stereo; the flag threads through the chain so each effect knows if L≠R. Effects implement:

- `processBlockMono` — mono in-place;
- `processBlockDual` — stereo input, mono effect per channel (**stateful effects must override with per-channel state**; the default reuses one state — see BUG-06);
- `processBlockStereoOut` — true stereo from mono (only if `supportsStereoOut()`).

`setSampleRate` is non-virtual; effects hook `onSampleRateChanged`.

### Thread-safety patterns

- **Simple parameters** (gains, enable flags, rates…) are `std::atomic<float>`/`std::atomic<bool>` — set from GUI thread, read lock-free in audio thread.
- **EQ coefficient updates** use a mutex + `try_lock` in the audio thread (skips one block if the GUI is writing).
- **NAM model swaps** are double-buffered (two model slots + atomic active pointer); the mutex is held only for the pointer swap, never during processing. **IR kernel swaps** follow the same pattern in `IRLoader`.
- **No allocations in the audio callback.**

### Key classes

| Class | File | Purpose |
|---|---|---|
| `AudioEngine` | `src/dsp/AudioEngine.h/cpp` | Owns backend + processors, runs the chain, VU peaks |
| `IAudioBackend` | `src/dsp/IAudioBackend.h` | Backend interface: start/stop, device enumeration, port routing |
| `JackBackend` | `src/dsp/JackBackend.h/cpp` | Linux JACK/PipeWire-JACK backend |
| `WasapiBackend` | `src/dsp/WasapiBackend.h/cpp` | Windows WASAPI shared-mode backend (float32 mix format only) |
| `NamProcessor` | `src/dsp/NamProcessor.h/cpp` | NAM model hosting; independent L/R instances for stereo |
| `IRLoader` / `IRProcessor` | `src/dsp/IRLoader.h/cpp` | FIR cab sim, energy-normalized, double-buffered kernel swap |
| `EQProcessor` | `src/dsp/EQProcessor.h/cpp` | 3-band biquad EQ (Audio EQ Cookbook) |
| `TunerProcessor` | `src/dsp/TunerProcessor.h/cpp` | NSDF pitch detect; audio thread fills, GUI timer reads |
| `NoiseGateProcessor`, `CompressorProcessor`, `DynamicWahProcessor` | `src/dsp/…` | Mono dynamics (envelope gate, feedforward comp, envelope wah) |
| `ChorusProcessor`, `FlangerProcessor`, `PhaserProcessor`, `TremoloProcessor` | `src/dsp/…` | Modulation effects; stereo variants use LFO phase offsets |
| `DelayProcessor` | `src/dsp/DelayProcessor.h/cpp` | Ping-pong stereo delay |
| `ReverbProcessor` | `src/dsp/ReverbProcessor.h/cpp` | Freeverb (8 comb + 4 all-pass, stereo) |
| `MainWindow` | `src/ui/MainWindow.h/cpp` | All UI assembly, engine wiring, presets, QSettings persistence |
| `KnobWidget` | `src/ui/KnobWidget.h/cpp` | `QDial` wrapper: float range, scroll step, label |
| `ToggleSwitch` | `src/ui/ToggleSwitch.h/cpp` | Custom-painted on/off switch |
| `Theme` | `src/ui/Theme.h/cpp` | Fusion + dark palette + app stylesheet; per-card accent categories |
| `EffectShortcuts` | `src/ui/EffectShortcuts.h/cpp` | Keyboard toggles for effects (user has Latin American layout) |
| `Constants.h` / `GUIConstants.h` | `src/dsp/` / `src/ui/` | DSP param ranges (used) / UI sizes (currently dead — ROADMAP Phase 5) |

### Parameter registry

`MainWindow::registerParameters()` is the **single source of truth for every user-facing
parameter**: one `knobParam`/`toggleParam` line per control builds `mParams`
(`ParamDesc { group, key, widget, defaults, inPreset }`) and wires the widget to the
engine via a lambda connect. Preset save/load are single loops over this table; Phase 3
session persistence and future MIDI mapping consume it too. **When adding an effect
parameter, register it here — never add hand-written connects, slots, or preset keys.**
`group`+`key` mirror the preset JSON schema (empty group = top-level key).

### Persistence

- **QSettings** (org `pichulero.cl`, app name set in `main.cpp` — drives the config path): last model/IR/preset paths, audio device IDs, JACK port connections, window geometry, and the full registry state under `session/<group>/<key>` (saved on exit + debounced 1 s after any widget change; restored on launch through the widgets). First run auto-connects `system:capture_1`/`playback_{1,2}`.
- **Presets** are `.nampreset` JSON files via `QJsonDocument`, schema v2: all registry parameters plus `version`, `namEnabled`, and embedded `namPath`/`irPath` (applied on load when the file exists; otherwise a warning, current model/IR kept). v1 files (no `version`) still load. Loading applies values through the widgets, so the UI and engine can't drift.

## NAM core library

`NAM/dsp.h` defines `nam::DSP`, the base class for all models. The key interface used by `NamProcessor`:

```cpp
model->Reset(sampleRate, maxBufferSize);
model->prewarm();  // run inference on silence to settle state
model->process(NAM_SAMPLE** inputs, NAM_SAMPLE** outputs, int numFrames);
```

`NAM_SAMPLE` is `float` when compiled with `-DNAM_SAMPLE_FLOAT` (set in `gui/CMakeLists.txt`, along with `-DNAM_ENABLE_A2_FAST` for the fast-path WaveNet). Models are loaded via `nam::get_dsp(std::filesystem::path)` from `NAM/get_dsp.h`. A model `Reset` to buffer size N must never be fed more than N frames — the core only asserts, and Release strips asserts. `NamProcessor` guards this: `prepareBuffer` re-`Reset()`s live models when the max grows, and `process()` bypasses oversized blocks.

## CMake notes

- `CMAKE_AUTOMOC ON` — MOC runs automatically; add new Qt classes to `GUI_SOURCES` in `gui/CMakeLists.txt`.
- Backend selection is in `CMakeLists.txt`: `WasapiBackend.cpp` on WIN32, `JackBackend.cpp` otherwise. On Windows the target is `WIN32_EXECUTABLE` (no console — `std::cerr` output is invisible there).
- The clang language server shows false-positive errors for Qt types because it doesn't see the CMake include paths. Ignore these; the actual compiler succeeds.
- `stdc++fs` is linked explicitly on Linux for `std::filesystem`.
