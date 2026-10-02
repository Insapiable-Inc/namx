# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This repo is a fork of [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) with a standalone cross-platform GUI added under `gui/`. The root-level code (`NAM/`, `tools/`, `Dependencies/`) is the upstream C++ DSP library. All GUI work lives exclusively in `gui/`.

```
nam-linux-gui/
├── NAM/           # Core DSP library: neural net inference (WaveNet, LSTM, ConvNet, Linear)
├── Dependencies/  # Git submodules: Eigen (linear algebra), nlohmann (JSON), AudioDSPTools (upstream tools only — the GUI reads WAVs with its own `WavReader`)
├── tools/         # Upstream CLI tools: loadmodel, benchmodel, run_tests
├── LICENSE        # Upstream MIT (NAM core) — the app itself is GPL-3.0-or-later, see gui/LICENSE
├── ROADMAP.md     # Public-release plan — phased checklist, worked one phase at a time
└── gui/           # Qt6 standalone player (all GUI work happens here)
    ├── build.sh           # One-shot Linux build script
    ├── build.ps1          # One-shot Windows build script (UTF-8 BOM, CRLF — keep it that way)
    ├── BUILD.md           # The real build/usage manual (per-distro deps, Windows, troubleshooting)
    ├── DSP_RESEARCH.md    # State-of-the-art options per effect (Phase 22), real-time flags, sources
    ├── CPP_GUIDELINES.md  # C++ style for gui/ (naming, auto, ownership, errors, formatting…) — follow it in new/touched code
    ├── .clang-format      # Formatting for gui/ (overrides the upstream root one)
    ├── CMakeLists.txt     # CMake build definition
    ├── presets/           # Personal .nampreset files — untracked on purpose, do not add to git
    └── src/
        ├── main.cpp       # QApplication setup (org/app name → QSettings path), Theme::apply
        ├── ui/            # Qt widgets
        ├── net/           # Tone3000Client (Qt Network; no widgets)
        ├── midi/          # MidiInput (ALSA / WinMM), MidiMapper (no widgets beyond the target pointers)
        └── dsp/           # AudioEngine, backends, effect processors (no Qt dependencies)
    ├── resources/         # resources.qrc, app icon (icons/), starter presets (presets/), TONE3000 brand assets (see its README)
    ├── packaging/         # desktop file, AppStream, mime, PKGBUILD, Windows .rc (no AppImage/Flatpak — decision 2026-09-17)
    └── tests/             # Tone3000ClientTest + mock_tone3000.py (-DNAM_BUILD_TESTS=ON)
```

## Release plan workflow

`ROADMAP.md` is the release plan: phases 0–19 with checkboxes, executed **in order, one phase at a time** (the exact order and the v1.0 tag point — after Phase 8 (packaging), which runs after Phase 12 — are in its "North star" section, which also records the product decisions: Petrucci X feature baseline, flat/no skeuomorphism, native Qt standalone, no DAW plugin, Linux first). Check items off as they land.

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

**Required packages (Arch):** `qt6-base jack2 alsa-lib cmake ninja`
**Required packages (Ubuntu 24.04+):** `qt6-base-dev libjack-jackd2-dev libasound2-dev cmake ninja-build g++-14` (C++23 → GCC 14; `CXX=g++-14` on 24.04)

Release builds default to `-march=native` via the `NAM_NATIVE_OPT` CMake option (ON). **CI/release builds must configure with `-DNAM_NATIVE_OPT=OFF`** → portable `-mavx2 -mfma`.

`cmake --install <build> --prefix /usr` installs the binary plus the desktop/mime/AppStream/icon files (app id `cl.pichulero.NAMPlayer`); `.github/workflows/release.yml` runs on `v*` tags (Ubuntu + Arch build and tests, Windows zip, source tarball, GitHub release). **No AppImage, Flatpak or other bundle** (user decision 2026-09-17): Linux users are assumed to have Qt 6 and JACK installed. `NAMPlayer --version`, `--help`, `--self-test-and-exit` (headless CI check) and `NAMPlayer file…` (opens .nam/.wav/.nampreset) are handled in `main.cpp`; the version comes from `PROJECT_VERSION` via the `NAM_VERSION` define.

`-DNAM_TONE3000_CLIENT_ID=t3k_pub_…` bakes the TONE3000 publishable key into the build (runtime override: QSettings `tone3000/clientId`, editable from the TONE3000 tab). `-DNAM_BUILD_TESTS=ON` adds `Tone3000ClientTest`, which spawns `tests/mock_tone3000.py` with `python3` and drives the whole client (PKCE sign-in over the loopback server, refresh, 429, lists, download + sidecar) headlessly:

```bash
cmake -S gui -B gui/build-test -DCMAKE_BUILD_TYPE=Debug -DNAM_BUILD_TESTS=ON
cmake --build gui/build-test --target Tone3000ClientTest && gui/build-test/Tone3000ClientTest
```

`DspGoldenTest` (same option) renders every effect over a fixed signal and compares a hash of the exact output bits — any DSP refactor that must not change the sound has to keep it green; re-record with `NAM_GOLDEN_PRINT=1` only when a change is meant to alter the sound.

`-DNAM_BUILD_BENCH=ON` adds `NAMPlayerBench` (`gui/bench/`): the DSP chain timed per stage against the block budget (`--model`, `--ir`, `--frames`, `--rate`, `--no-fast-tanh`, `--only`). The Phase 14 efficiency budget and reference numbers are in `gui/BUILD.md` → High CPU usage; re-run the bench after DSP changes. `--self-test-and-exit` also prints cold-start time and peak RSS.

## Running

```bash
# Linux, PipeWire-JACK (modern distros) — just launch:
gui/build-release/NAMPlayer

# Classic jackd:
jackd -d alsa -r 44100 -p 256 &
gui/build-release/NAMPlayer
```

JACK ports: `NAMPlayer:input`, `NAMPlayer:output_L`, `NAMPlayer:output_R`. Connect with `jack_connect`, qjackctl, or qpwgraph. Devices are also selectable from the status bar at the bottom of the window (click the In:/Out: items), and connections persist via QSettings. On Windows the WASAPI backend picks devices directly — no server needed.

## GUI architecture

Qt6 application with a platform audio backend behind the `IAudioBackend` interface (`src/dsp/IAudioBackend.h`): `JackBackend` on Linux, `WasapiBackend` on Windows. Exactly one backend .cpp is compiled per platform (each defines `IAudioBackend::create()`); everything else is platform-agnostic. The key design constraint is **thread safety between the GUI thread and the real-time audio thread**.

### Signal chain

`AudioEngine` (implements `IAudioBackend::Callback`) owns all processors and a **double-buffered chain** (two `std::vector<EffectBase*>` + atomic index) iterated every callback. **Processing order = UI order** — `MainWindow::rebuild_effect_strip` lays the cards out by iterating `get_chain()`, and the user can reorder by dragging cards. Each effect has a stable string ID (`AudioEngine::effect_table` — never rename); `set_chain_order(ids)` sanitizes (unknown IDs dropped, missing effects at default slot) and swaps RT-safely, flagging moved effects for an audio-thread `reset()` via `request_reset()`/`consume_pending_reset()`. All 16 effects reorder freely — nothing is pinned; only the Input/Output gain stages, the output limiter and the metronome sit outside the chain. New effect IDs are appended to the table at their default slot, and old sessions/presets get them there via the sanitizer. The default order:

```
mono input → input gain (smoothed)
  → Tuner → Transpose → Gate → Compressor → Wah → Flanger → Phaser → Drive   (mono section)
  → NAM model (sums a stereo input unless the Amplifier's Stereo switch is on)
  → Volume pedal (first in the effects loop) → EQ
  → Chorus → Tremolo → Delay → Reverb                              (stereo-capable)
  → Cabinet (one IR in the UI; the DSP has a second slot, its controls disabled)
  → output gain (smoothed) → output limiter → meter → + metronome → stereo out (L, R)
```

Cards have a main row and optional ▾ detail rows (`MainWindow::add_detail_row`) for advanced knobs; those knobs register through `late_knob` (deferred `knob_param`) so they don't need named members.

### EffectBase dispatch

All effects inherit `EffectBase` (`src/dsp/EffectBase.h` — the header comment documents the full routing table). `process(l, r, n, input_is_stereo)` returns whether output is now stereo; the flag threads through the chain so each effect knows if L≠R. Effects implement:

- `process_block_mono` — mono in-place;
- `process_block_dual` — stereo input, mono effect per channel (**stateful effects must override with per-channel state**; the default reuses one state — see BUG-06);
- `process_block_stereo_out` — true stereo from mono (only if `supports_stereo_out()`).

`set_sample_rate` is non-virtual; effects hook `on_sample_rate_changed`.

### Thread-safety patterns

- **Simple parameters** (gains, enable flags, rates…) are `std::atomic<float>`/`std::atomic<bool>` — set from GUI thread, read lock-free in audio thread.
- **EQ coefficient updates** use a mutex + `try_lock` in the audio thread (skips one block if the GUI is writing).
- **NAM model swaps, IR kernel swaps and chain reorders** all go through `RtSlot<T>` (`src/dsp/RtSlot.h`): the GUI publishes a freshly built object, the audio thread binds it once per block (`acquire()` … `end_block()` on every exit path), and the previous object is parked on a retired list and freed only after the audio thread has provably completed a block that began after the swap. Publishing never blocks and back-to-back publishes are safe; `AudioEngine::reclaim_retired()` frees eagerly when the backend is stopped. Never `delete` or mutate a published object from the GUI thread.
- **No allocations in the audio callback.**

### Key classes

| Class | File | Purpose |
|---|---|---|
| `AudioEngine` | `src/dsp/AudioEngine.h/cpp` | Owns backend + processors, runs the chain, VU peaks |
| `IAudioBackend` | `src/dsp/IAudioBackend.h` | Backend interface: start/stop, device enumeration, port routing |
| `JackBackend` | `src/dsp/JackBackend.h/cpp` | Linux JACK/PipeWire-JACK backend |
| `WasapiBackend` | `src/dsp/WasapiBackend.h/cpp` | Windows WASAPI shared-mode backend (float32 mix format only) |
| `NamProcessor` | `src/dsp/NamProcessor.h/cpp` | NAM model hosting; independent L/R instances for stereo |
| `WavReader` | `src/dsp/WavReader.h/cpp` | Strict mono WAV reader for IRs (PCM 16/24/32, float 32/64, extensible); every length checked before reading — damaged files are errors (`tests/WavReaderTest`) |
| `IRLoader` / `IRProcessor` | `src/dsp/IRLoader.h/cpp`, `IRProcessor.h/cpp` | FIR cab sim, energy-normalized, RtSlot kernel swap; `IRProcessor` = two loaders (slots A/B) with level/pan/phase/blend + cuts — the UI exposes slot A only (advanced options commented out 2026-10-01; slot B is neither restored nor loaded from presets) |
| `EQProcessor` | `src/dsp/EQProcessor.h/cpp` | 4-band semi-parametric biquad EQ + low/high cut (Audio EQ Cookbook) |
| `TunerProcessor` | `src/dsp/TunerProcessor.h/cpp` | NSDF pitch detect; audio thread fills, GUI timer reads |
| `TunerDisplay` | `src/ui/TunerDisplay.h/cpp` | TU-3-style readout: LED ladder, glowing seven-segment note, signed cents |
| `NoiseGateProcessor`, `CompressorProcessor`, `DynamicWahProcessor` | `src/dsp/…` | Mono dynamics (envelope gate, feedforward comp, envelope wah) |
| `ChorusProcessor`, `FlangerProcessor`, `PhaserProcessor`, `TremoloProcessor` | `src/dsp/…` | Modulation effects; stereo variants use LFO phase offsets |
| `DelayProcessor` | `src/dsp/DelayProcessor.h/cpp` | Ping-pong stereo delay |
| `ReverbProcessor` | `src/dsp/ReverbProcessor.h/cpp` | Freeverb (8 comb + 4 all-pass, stereo) + pre-delay, wet cuts (shimmer in the DSP, its knob disabled) |
| `DriveProcessor`, `VolumeProcessor`, `TransposeProcessor` | `src/dsp/…` | Phase 17 stages: TS-style drive, volume pedal with mid-point, ±12 st transpose |
| `PitchShifter` | `src/dsp/PitchShifter.h` | Two-tap Hann delay-line shifter with WSOLA re-alignment (Transpose; Shimmer and Crystals are in the DSP with their knobs disabled) |
| `OutputLimiter` | `src/dsp/OutputLimiter.h/cpp` | Output-stage look-ahead limiter (not an effect; registry key `limiter`) |
| `MainWindow` | `src/ui/MainWindow.h/cpp` | Window assembly, parameter registry, effect strip + DnD reorder, session state |
| `ParamRegistry.h` | `src/ui/` | `ParamDesc` — one entry per user-facing parameter (shared by MainWindow + PresetSerializer) |
| `PresetBar` | `src/ui/PresetBar.h/cpp` | Bar above the rack: preset name menu, ‹ ›, `*`, Save/Save As, quick slots 1–8 (owns `presets/slots`), A/B buttons; MainWindow owns the preset state (`preset_path` + baseline) |
| `PresetSerializer` | `src/ui/PresetSerializer.h/cpp` | Preset schema v2 JSON ↔ registry/engine (v1 files load too) |
| `SettingsDialog` | `src/ui/SettingsDialog.h/cpp` | Settings window (Ctrl+,): Audio / MIDI / Library / Tuner / Shortcuts pages, instant apply; drives the same objects as the rest of the UI (`SettingsTargets`); the Shortcuts page is built from the window's menus and `QShortcut`s (each needs a `whatsThis`) |
| `AudioStatusBar` | `src/ui/AudioStatusBar.h/cpp` | Bottom status bar: backend state, rate/buffer/latency, device pickers; persists IDs, emits `device_selection_changed` |
| `LibraryIndex` | `src/ui/LibraryIndex.h/cpp` | Library scan: worker thread over the root folders, metadata cache (`AppDataLocation/library-index.json`), `QFileSystemWatcher` rescans; `LibraryEntry` carries a pre-built `summary`/`details` per file |
| `LibraryPanel` | `src/ui/LibraryPanel.h/cpp` | Local library tab: Models/IRs/Presets sections, search, favorites, recents, sort, two-line list; emits `activated(kind, path)`, owner reports back via `set_loaded_path` |
| `LibraryDock` | `src/ui/LibraryDock.h/cpp` | Dock content: Library / TONE3000 tab switch over `LibraryPanel` + `Tone3000Panel` |
| `Tone3000Client` | `src/net/Tone3000Client.h/cpp` | TONE3000 API: OAuth 2.0 + PKCE via system browser + loopback `QTcpServer` (`http://localhost:51731/callback`), Bearer requests with pre-emptive/401 refresh, 5-min listing cache, 429 back-off, model download → `download_dir()/<creator>/<tone>/` + `.t3k.json` sidecar, disk-cached images. Env: `NAM_TONE3000_API_BASE`, `NAM_TONE3000_NO_BROWSER` |
| `Tone3000Panel` | `src/ui/Tone3000Panel.h/cpp` | TONE3000 tab: setup (no key) → splash (signed out) → bounded lists (Trending/Latest/Favorites/Downloads/Mine) + "Browse TONE3000" (select_tone flow) → tone detail with per-model download/load; emits `load_requested(kind, path)` |
| `TempoBar` | `src/ui/TempoBar.h/cpp` | Tempo + metronome strip: BPM `NumberField`, Tap, play (Space), `BeatLeds` accent editor, meter/subdivision/sound/count-in menus, level/pan; emits `bpm_changed`; `poll()` from the meter timer |
| `NumberField` | `src/ui/NumberField.h/cpp` | Drag / scroll / click-to-type numeric readout with a unit (tempo BPM, tuner A4 reference) |
| `TempoSync.h` | `src/ui/` | Note-value table (index order is a preset contract — append only) + ms/Hz conversions; `MainWindow::add_tempo_sync`/`push_synced` wire the Sync switches |
| `Metronome` | `src/dsp/Metronome.h/cpp` | RT click generator mixed after the output gain (not an effect, not metered); samples synthesized in `set_sample_rate`; settings are atomics |
| `MidiInput` | `src/midi/MidiInput.h/cpp` + `AlsaMidiInput.cpp` (Linux) / `WinMidiInput.cpp` (Windows) | Platform MIDI input; backend thread → queued `message_received(MidiMessage)`; devices by name, hot-plug, enable set in `midi/disabledDevices` |
| `MidiMapper` | `src/midi/MidiMapper.h/cpp` | Mappings (CC/note/program × absolute/relative/toggle/momentary, range) → registry knobs/toggles (registered automatically by `knob_param`/`toggle_param`) and actions; Learn; `.nammidi` files; clock → tempo. Tested by `tests/MidiMapperTest` |
| `MidiMappingsDialog` | `src/ui/MidiMappingsDialog.h/cpp` | MIDI → Mappings… window (devices, table, per-row learn, load/save/defaults) |
| `VUMeter` | `src/ui/VUMeter.h/cpp` | Vertical LED bar-graph peak meter (glowing segments, green/yellow/red zones, peak-hold LED; drawn with `LedPaint.h`, shared with `TunerDisplay`); `set_peak(linear)` on a 66 dB scale, time-based ballistics, repaints only on movement (fed at ~60 fps, paused while minimized) |
| `GainReductionMeter` | `src/ui/GainReductionMeter.h/cpp` | Compressor/Gate reduction readout in the rows' stereo slot; fed from `take_gain_reduction_db()` |
| `Log.h` | `src/dsp/` | `nam.audio` logging category — use `qCWarning(lc_audio, …)`, never `std::cerr` (invisible on Windows) |
| `KnobWidget` | `src/ui/KnobWidget.h/cpp` | `QDial` wrapper: float range, scroll step, label |
| `ToggleSwitch` | `src/ui/ToggleSwitch.h/cpp` | Custom-painted on/off switch |
| `Theme` | `src/ui/Theme.h/cpp` | Fusion + dark palette + app stylesheet; per-effect accents (`tag_card(card, fx_id)`) + chrome color accessors — **all colors live here** |
| `EffectShortcuts` | `src/ui/EffectShortcuts.h/cpp` | Keyboard toggles for effects (user has Latin American layout) |
| `Constants.h` | `src/dsp/` | DSP parameter ranges and defaults for all effects |

### Naming

Code follows `gui/CPP_GUIDELINES.md` (Phase 20, 2026-10-01): `snake_case` functions,
variables and members with no prefixes, `UPPER_CASE` constants, `PascalCase` types; no
parameter or local reuses a member's name (`new_<x>` / `current_<x>` / `<x>_per_channel`).
Settings keys, preset JSON keys and effect IDs are data and keep their spelling
(`effectOrder`, `tone3000/clientId`, …). `gui/.clang-tidy` encodes the naming rules.

### Parameter registry

`MainWindow::register_parameters()` is the **single source of truth for every user-facing
parameter**: one `knob_param`/`toggle_param` line per control builds `params`
(`ParamDesc { group, key, widget, defaults, in_preset }`) and wires the widget to the
engine via a lambda connect. Preset save/load are single loops over this table; Phase 3
session persistence and future MIDI mapping consume it too. **When adding an effect
parameter, register it here — never add hand-written connects, slots, or preset keys.**
`group`+`key` mirror the preset JSON schema (empty group = top-level key).
`in_preset = false` keeps a parameter session-only (the tuner): presets neither save nor
apply it, and it doesn't count toward the preset `*`.

### Loading files

Every way a model/IR/preset gets loaded — library click, ‹ › arrows, `Library → Import…`
dialogs, drag-and-drop onto the window, session restore, preset references — goes through
`MainWindow::load_model_file` / `load_ir_file` / `load_preset_file`. They skip a reload of the
already-loaded path, persist `files/last*`, bump the library recents and mark the loaded
entry, and refresh the Amplifier/Cabinet hero rows (`update_hero_cards`). Don't call
`AudioEngine::load_model`/`load_ir` from the UI directly.

### TONE3000 constraints (free non-commercial tier)

Only the bounded list endpoints (trending, latest, favorited, downloaded, created) and
the OAuth prompt flows are allowed — **never call `/tones/search`**; catalog search is
the browser-based `select_tone` flow, which returns a `tone_id` to `auth_finished`.
Branding rules are implemented deliberately: full logo on the splash and list header
before the compact T3K mark appears (Browse button, hero-row origin mark); every card
shows image, title, gear type, format and creator; the signed-in user is visible.
The TONE3000 download folder is always scanned by `LibraryIndex` (`with_tone3000_dir`),
and entries carry `t3k_tone_id`/`t3k_model_id` from the sidecar, which `PresetSerializer`'s
caller writes into the preset's `tone3000` object for cross-machine resolution
(`MainWindow::resolve_tone3000_model`).

### Persistence

- **QSettings** (org `pichulero.cl`, app name set in `main.cpp` — drives the config path): last model/IR/preset paths, audio device IDs, JACK port connections, window geometry + dock state (`window/state`), the library (`library/{model,ir,preset}Roots`, `favorites`, `recents`, `section`, `mode`, `sort`, `tab`, `tone3000Dir`), TONE3000 (`tone3000/clientId`, `accessToken`, `refreshToken`, `expiresAt`, `user/*`, `list`), and the full registry state plus `session/effectOrder` under `session/` (saved on exit + debounced 1 s after any widget change; restored on launch through the widgets). First run auto-connects `system:capture_1`/`playback_{1,2}`.
- **Presets** (8 starters in `gui/resources/presets/`, seeded once into `<presets>/Starter`) are `.nampreset` JSON files via `QJsonDocument`, schema v2: all registry parameters plus `version`, `namEnabled`, `effectOrder` (stable-ID list), embedded `namPath`/`irPath` (applied on load when the file exists; otherwise a warning, current model/IR kept), and an optional `tone3000` object (`model`/`ir` → `toneId`, `modelId`, `title`, `creator`) used to find or fetch a missing file. v1 files (no `version`) still load. Loading applies values through the widgets, so the UI and engine can't drift.

## NAM core library

`NAM/dsp.h` defines `nam::DSP`, the base class for all models. The key interface used by `NamProcessor`:

```cpp
model->Reset(sampleRate, maxBufferSize);
model->prewarm();  // run inference on silence to settle state
model->process(NAM_SAMPLE** inputs, NAM_SAMPLE** outputs, int numFrames);
```

`NAM_SAMPLE` is `float` when compiled with `-DNAM_SAMPLE_FLOAT` (set in `gui/CMakeLists.txt`, along with `-DNAM_ENABLE_A2_FAST` for the fast-path WaveNet). Models are loaded via `nam::get_dsp(std::filesystem::path)` from `NAM/get_dsp.h`. A model `Reset` to buffer size N must never be fed more than N frames — the core only asserts, and Release strips asserts. `NamProcessor` guards this: `prepare_buffer` re-`Reset()`s live models when the max grows, and `process()` bypasses oversized blocks.

## CMake notes

- `CMAKE_AUTOMOC ON` — MOC runs automatically; add new Qt classes to `GUI_SOURCES` in `gui/CMakeLists.txt`.
- Backend selection is in `CMakeLists.txt`: `WasapiBackend.cpp` on WIN32, `JackBackend.cpp` otherwise. On Windows the target is `WIN32_EXECUTABLE` (no console — `std::cerr` output is invisible there).
- The clang language server shows false-positive errors for Qt types because it doesn't see the CMake include paths. Ignore these; the actual compiler succeeds.
- `stdc++fs` is linked explicitly on Linux for `std::filesystem`.
