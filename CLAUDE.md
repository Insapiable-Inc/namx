# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

NAM-X, a standalone cross-platform Neural Amp Modeler player (Qt 6). The NAM DSP core and
the other code dependencies are git submodules under `external/`; Qt, JACK/PipeWire and
ALSA come from the OS. (Until 2026-10-04 the app lived under `gui/` in a fork of
NeuralAmpModelerCore — older notes in ROADMAP/AUDIT still say `gui/…`.)

```
namx/
├── CMakeLists.txt     # CMake build definition
├── build.sh           # One-shot Linux build script
├── build.ps1          # One-shot Windows build script (UTF-8 BOM — keep it)
├── BUILD.md           # The real build/usage manual (per-distro deps, Windows, troubleshooting)
├── ROADMAP.md         # Public-release plan — phased checklist, worked one phase at a time
├── DSP_RESEARCH.md    # State-of-the-art options per effect (Phase 22), real-time flags, sources
├── CPP_GUIDELINES.md  # C++ style (naming, auto, ownership, errors, formatting…) — follow it in new/touched code
├── .clang-format      # Formatting for src/, tests/, bench/
├── LICENSE            # GPL-3.0-or-later text, verbatim (GitHub detects it); COPYRIGHT = our notice; LICENSE-MIT = the core's MIT text (+ pre-2026-10-01 app code)
├── presets/           # Personal .nampreset files — untracked on purpose, do not add to git
├── src/
│   ├── main.cpp       # QApplication setup (org/app name → QSettings path), Theme::apply
│   ├── ui/            # Qt widgets
│   ├── midi/          # MidiInput (ALSA / WinMM), MidiMapper (no widgets beyond the target pointers)
│   └── dsp/           # AudioEngine, backends, effect processors (no Qt dependencies)
├── resources/         # resources.qrc, app icon (icons/), starter presets (presets/)
├── packaging/         # desktop file, AppStream, mime, Windows .rc, THIRD-PARTY-NOTICES.md + licenses/ (installed with the bundled deps' license texts — CMake component `notices`) (AppImage built by release.yml — the only Linux release, user 2026-10-04; no Flatpak, Snap or AUR package)
├── tests/             # MidiMapperTest, DspGoldenTest, WavReaderTest, NamProcessorTest, LibraryIndexTest, ThemeFileTest (-DNAM_BUILD_TESTS=ON)
├── bench/             # namx-bench, instructions.py (CPU regression) (-DNAM_BUILD_BENCH=ON)
└── external/          # Git submodules (CMake: EXTERNAL_DIR, NAM_ROOT, NAM_DEPS_PATH)
    ├── NeuralAmpModelerCore/  # NAM core — our fork (Insapiable-Inc), branch `namx` = upstream 0.6.0 (0b3d3c9, merged 2026-10-18) + our weight-check fix; brings Eigen, nlohmann/json, AudioDSPTools in its Dependencies/
    ├── signalsmith-linear/    # MIT FFT for the cab convolution
    ├── signalsmith-stretch/   # MIT pitch shifter for the Shimmers
    └── chowdsp_wdf/           # BSD-3 wave digital filters for the Drive
```

The core fork lives locally in `~/Git/namx-core` (remotes: `origin` = the Insapiable-Inc
fork, `upstream` = sdatkinson). A core change goes there on `namx`, gets pushed, then the
submodule is bumped here. Keep the core at a version whose sound we've checked (golden test,
by ear) — updating it is its own step.

## Release plan workflow

`ROADMAP.md` is the release plan: phases 0–19 with checkboxes, executed **in order, one phase at a time** (the exact order and the tag point — **v0.0.0**, the first release (user, 2026-10-04), after bug fixes (26), cleanup (28), benchmarking (27) and packaging (8) — are in its "North star" section, which also records the product decisions: Petrucci X feature baseline, flat/no skeuomorphism, native Qt standalone, no DAW plugin, Linux first). Check items off as they land.

## Build

```bash
# Linux
bash build.sh          # Release (default) → build-release/namx
bash build.sh Debug    # Debug → build-debug/namx

# Windows (PowerShell; needs MSVC + Qt6 — see BUILD.md)
./build.ps1
```

The script initialises submodules automatically. Manual CMake:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel $(nproc)
```

**Required packages (Arch):** `qt6-base jack2 alsa-lib cmake ninja`
**Required packages (Ubuntu 24.04+):** `qt6-base-dev libjack-jackd2-dev libasound2-dev cmake ninja-build g++-14` (C++23 → GCC 14; `CXX=g++-14` on 24.04)

Release builds default to `-march=native` via the `NAM_NATIVE_OPT` CMake option (ON). **CI/release builds must configure with `-DNAM_NATIVE_OPT=OFF`** → portable `-mavx2 -mfma`.

`cmake --install <build> --prefix /usr` installs the binary plus the desktop/mime/AppStream/icon files (app id `io.github.Insapiable_Inc.namx`); `.github/workflows/release.yml` runs on `v*` tags (Ubuntu + Arch build and tests, Windows zip, source tarball, GitHub release). **Linux distribution:** the AppImage only (user, 2026-10-04); no Flatpak (manifest removed — in git history at 0438cc5), no Snap, no AUR package (the PKGBUILD was removed). Bundles must never include `libjack`. `namx --version`, `--help`, `--self-test-and-exit` (headless CI check) and `namx file…` (opens .nam/.wav/.nampreset) are handled in `main.cpp`; the version comes from `PROJECT_VERSION` via the `NAM_VERSION` define.

`-DNAM_BUILD_TESTS=ON` adds `MidiMapperTest`, `DspGoldenTest`, `WavReaderTest`, `NamProcessorTest`, `LibraryIndexTest` and `ThemeFileTest` (all headless):

```bash
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug -DNAM_BUILD_TESTS=ON
cmake --build build-test && build-test/MidiMapperTest && build-test/WavReaderTest
```

**TONE3000** (the community catalog integration) was removed on 2026-10-02 — the user
wasn't convinced by it; the working integration lives on the `experiment/tone3000`
branch. Don't reintroduce it on `main` without asking.

`DspGoldenTest` (same option) renders every effect over a fixed signal and compares a signature of the output (per quarter second and channel: RMS plus level/phase at 440.5 Hz and 2 kHz) against `tests/dsp-golden.txt`, within 0.1 % of the window's RMS — any DSP refactor that must not change the sound has to keep it green. Deliberately not bit-exact: glibc versions round `tanf` & co. differently (CI's Ubuntu vs. local Arch), while a one-sample shift or a missed `reset()` still fails. Re-record (command in the test's header comment) only when a change is meant to alter the sound.

`-DNAM_BUILD_BENCH=ON` adds `namx-bench` (`bench/`): the DSP chain timed per stage against the block budget (`--model`, `--ir`, `--frames`, `--rate`, `--no-fast-tanh`, `--only`). The Phase 14 efficiency budget and reference numbers are in `BUILD.md` → High CPU usage; re-run the bench after DSP changes. `--self-test-and-exit` also prints cold-start time and peak RSS.

## Running

```bash
# Linux, PipeWire-JACK (modern distros) — just launch:
build-release/namx

# Classic jackd:
jackd -d alsa -r 44100 -p 256 &
build-release/namx
```

JACK ports: `NAM-X:input`, `NAM-X:output_L`, `NAM-X:output_R`. Connect with `jack_connect`, qjackctl, or qpwgraph. Devices are also selectable from the status bar at the bottom of the window (click the In:/Out: items), and connections persist via QSettings. On Windows the WASAPI backend picks devices directly — no server needed.

## GUI architecture

Qt6 application with a platform audio backend behind the `IAudioBackend` interface (`src/dsp/IAudioBackend.h`): `JackBackend` on Linux, `WasapiBackend` on Windows. Exactly one backend .cpp is compiled per platform (each defines `IAudioBackend::create()`); everything else is platform-agnostic. The key design constraint is **thread safety between the GUI thread and the real-time audio thread**.

### Signal chain

`AudioEngine` (implements `IAudioBackend::Callback`) owns all processors and a **double-buffered chain** (two `std::vector<EffectBase*>` + atomic index) iterated every callback. **Processing order = UI order** — `MainWindow::rebuild_effect_strip` lays the cards out by iterating `get_chain()`, and the user can reorder by dragging cards. Each effect has a stable string ID (`AudioEngine::effect_table` — never rename); `set_chain_order(ids)` sanitizes (unknown IDs dropped, missing effects at default slot) and swaps RT-safely, flagging moved effects for an audio-thread `reset()` via `request_reset()`/`consume_pending_reset()`. All 16 effects reorder freely — nothing is pinned; only the Input/Output gain stages, the output limiter and the metronome sit outside the chain. New effect IDs are appended to the table at their default slot, and old sessions/presets get them there via the sanitizer. The default order:

```
mono input → input gain (smoothed)
  → Tuner → Transpose → Gate → Compressor → Wah → Flanger → Phaser → Drive   (mono section)
  → NAM model (a stereo input runs one model per channel — never summed to mono)
  → Volume pedal (first in the effects loop) → EQ
  → Chorus → Tremolo → Delay → Reverb                              (stereo-capable)
  → Cabinet (one IR in the UI; the DSP has a second slot, its controls disabled)
  → output gain (smoothed) → meter (guitar) → + metronome → output limiter → stereo out (L, R)
```

Cards have a main row and optional ▾ detail rows (`MainWindow::add_detail_row`) for advanced knobs; those knobs register through `late_knob` (deferred `knob_param`) so they don't need named members.

### EffectBase dispatch

All effects inherit `EffectBase` (`src/dsp/EffectBase.h` — the header comment documents the full routing table). `process(l, r, n, input_is_stereo)` returns whether output is now stereo; the flag threads through the chain so each effect knows if L≠R. Effects implement:

- `process_block_mono` — mono in-place;
- `process_block_dual` — stereo input, mono effect per channel (**stateful effects must override with per-channel state**; the default reuses one state — see BUG-06);
- `process_block_stereo_out` — true stereo from mono (only if `supports_stereo_out()`).

- `sync_right_channel` — called by `process` on the first stereo-input block after mono ones (only the left state advanced): copy the left state to the right where cheap, clear it where not (review item 19; effects that override `process` wholesale — EQ, NAM — run the edge themselves).

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
| `WasapiBackend` | `src/dsp/WasapiBackend.h/cpp` | Windows WASAPI backend: shared mode (float32 mix format only) or opt-in exclusive mode (native format search + conversion, minimum period, falls back to shared; `audio/exclusive`). Tested on Windows by the user (2026-10-18); no Windows machine here, so changes are compile-checked with MinGW |
| `NamProcessor` | `src/dsp/NamProcessor.h/cpp` | NAM model hosting; independent L/R instances for stereo; a model runs at its training rate (48 kHz when unstated) — at another engine rate it sits behind AudioDSPTools' Lanczos `ResamplingContainer` (via `ModelResampler.h`), like the official plugin; Input (`nam/input`) before the model; output = loudness normalization × the user's Output trim (`nam/output`); both 0 dB by default |
| `WavReader` | `src/dsp/WavReader.h/cpp` | Strict mono WAV reader for IRs (PCM 16/24/32, float 32/64, extensible); every length checked before reading — damaged files are errors (`tests/WavReaderTest`) |
| `IRLoader` / `IRProcessor` | `src/dsp/IRLoader.h/cpp`, `IRProcessor.h/cpp` | Cab convolution: 64 direct taps + uniformly partitioned FFT convolution (overlap-save, `signalsmith-linear` FFT — the `external/signalsmith-linear` submodule), zero added latency, IRs up to 1 s (longer ones faded out); energy-normalized, RtSlot kernel swap; `IRProcessor` = two loaders (slots A/B) with level/pan/phase/blend + cuts — the UI exposes slot A only, with its Level on the row (`ir/levelA`, ±20 dB, ramped per block; restored 2026-10-04 — the other advanced options stay commented out since 2026-10-01; slot B is neither restored nor loaded from presets) |
| `EQProcessor` | `src/dsp/EQProcessor.h/cpp` | 4-band semi-parametric biquad EQ + low/high cut (Audio EQ Cookbook) |
| `TunerProcessor` | `src/dsp/TunerProcessor.h/cpp` | NSDF pitch detect; audio thread fills, GUI timer reads; Mute (on by default, `tuner/mute`, saved in presets like `tuner/enabled`) silences the output while it runs |
| `TunerDisplay` | `src/ui/TunerDisplay.h/cpp` | TU-3-style readout: LED ladder, glowing seven-segment note, signed cents |
| `NoiseGateProcessor`, `CompressorProcessor`, `DynamicWahProcessor` | `src/dsp/…` | Mono dynamics (envelope gate, feedforward comp, envelope wah) |
| `ChorusProcessor`, `FlangerProcessor`, `PhaserProcessor`, `TremoloProcessor` | `src/dsp/…` | Modulation effects; stereo variants use LFO phase offsets |
| `Phase90Circuit` | `src/dsp/Phase90Circuit.h` | The Phaser: MXR Phase 90 circuit model (polimi-ispl paper's MIT MATLAB in closed form — JFET channel resistance from the LFO and Vds); Feedback solved in-loop, capped at 90 % (runs away near 98 %); chosen by ear 2026-10-04, replacing the digital phaser + Vibe |
| `DelayProcessor` | `src/dsp/DelayProcessor.h/cpp` | Stereo delay: two independent lines (Time / R Time, each syncable); ping-pong Cross-feed in the DSP, no knob |
| `ReverbProcessor` | `src/dsp/ReverbProcessor.h/cpp`, `ReverbAlgorithms.h` | Plate (Dattorro), Hall and Room (8- / 16-line FDNs) + pre-delay (tempo-syncable), wet cuts, level-matched types; Decay keeps its 0–100 preset key `roomSize` and shows 0.2–10 s (`decay_seconds`); Shimmer (octave-up feedback, `StretchShifter`) |
| `DriveProcessor`, `VolumeProcessor`, `TransposeProcessor` | `src/dsp/…` | Phase 17 stages: Tube Screamer drive, volume pedal with mid-point, ±12 st transpose |
| `TubeScreamerClipper` | `src/dsp/TubeScreamerClipper.h` | The Drive's clipping stage: the TS808/TS9 op-amp + diode circuit as a wave digital filter (`external/chowdsp_wdf`, BSD-3), 4× oversampled with half-band FIRs (small latency, reported). Replaced the digital TS approximation ("Classic") 2026-10-04 — approved by ear |
| `FilterBankShifter` | `src/dsp/FilterBankShifter.h` | Transpose's shifter: complex gammatone bank (2 bands/ERB) → per-band rotation at (ratio − 1) × its spectral peak's frequency → Hohmann delay/phase-aligned resynthesis; ~20 ms at the low E, ~4 ms above; band state in 8-lane blocks so the loop vectorizes. Chosen by ear 2026-10-03 (the variants tried are listed in its header) |
| `StretchShifter` | `src/dsp/StretchShifter.h` | Octave shifter for the tails (Reverb Shimmer, Delay Shimmer — keys `reverb/shimmer`, `delay/shimmer`; the Delay's was `crystals` until 2026-10-03): Signalsmith Stretch (`external/signalsmith-stretch`, MIT) at its default 120 ms block, accurate pitch, 150 ms latency — the Delay feeds it 150 ms earlier in the line so the Delay's octave repeats land on time. Replaced the WSOLA `PitchShifter` (deleted 2026-10-03) |
| `OutputLimiter` | `src/dsp/OutputLimiter.h/cpp` | Output-stage look-ahead limiter (not an effect; registry key `limiter`) |
| `MainWindow` | `src/ui/MainWindow.h/cpp` | Window assembly, parameter registry, effect strip + DnD reorder, session state; the status bar's red middle zone is for problems worth seeing — post with `set_notice(key, text, tip)`, clear with an empty text (first user: model/device sample-rate mismatch) |
| `ParamRegistry.h` | `src/ui/` | `ParamDesc` — one entry per user-facing parameter (shared by MainWindow + PresetSerializer) |
| `PresetBar` | `src/ui/PresetBar.h/cpp` | Bar above the rack: preset name menu, ‹ ›, `*`, Save/Save As, quick slots 1–8 (owns `presets/slots`), A/B buttons; MainWindow owns the preset state (`preset_path` + baseline) |
| `PresetSerializer` | `src/ui/PresetSerializer.h/cpp` | Preset schema v2 JSON ↔ registry/engine (v1 files load too) |
| `SettingsDialog` | `src/ui/SettingsDialog.h/cpp` | Settings window (Ctrl+,): Audio / MIDI / Library / Tuner / Shortcuts pages, instant apply; drives the same objects as the rest of the UI (`SettingsTargets`); the Shortcuts page is built from the window's menus and `QShortcut`s (each needs a `whatsThis`) |
| `AudioStatusBar` | `src/ui/AudioStatusBar.h/cpp` | Bottom status bar: backend state, rate/buffer/latency (round trip = server I/O + `AudioEngine::processing_latency()`, from each effect's `latency_ms()` and the limiter; breakdown in the tooltip), device pickers; persists IDs, emits `device_selection_changed` |
| `LibraryIndex` | `src/ui/LibraryIndex.h/cpp` | Library scan: worker thread over the root folders, metadata cache (`AppDataLocation/library-index.json`), `QFileSystemWatcher` rescans; `LibraryEntry` carries a pre-built `summary`/`details` per file |
| `LibraryPanel` | `src/ui/LibraryPanel.h/cpp` | Local library tab: Models/IRs/Presets sections, search, favorites, recents, sort, two-line list; emits `activated(kind, path)`, owner reports back via `set_loaded_path` |
| `TempoBar` | `src/ui/TempoBar.h/cpp` | Tempo + metronome strip: BPM `NumberField`, Tap, play (Space), `BeatLeds` accent editor, meter/subdivision/sound/count-in menus, level/pan; emits `bpm_changed`; `poll()` from the meter timer |
| `NumberField` | `src/ui/NumberField.h/cpp` | Drag / scroll / click-to-type numeric readout with a unit (tempo BPM, tuner A4 reference) |
| `TempoSync.h` | `src/ui/` | Note-value table (index order is a preset contract — append only) + ms/Hz conversions; `MainWindow::add_tempo_sync`/`push_synced` wire the Sync switches |
| `Metronome` | `src/dsp/Metronome.h/cpp` | RT click generator mixed after the output gain and meter, before the limiter (not an effect, not metered); samples synthesized in `set_sample_rate`; settings are atomics |
| `MidiInput` | `src/midi/MidiInput.h/cpp` + `AlsaMidiInput.cpp` (Linux) / `WinMidiInput.cpp` (Windows) | Platform MIDI input; backend thread → queued `message_received(MidiMessage)`; devices by name, hot-plug, enable set in `midi/disabledDevices` |
| `MidiMapper` | `src/midi/MidiMapper.h/cpp` | Mappings (CC/note/program × absolute/relative/toggle/momentary, range) → registry knobs/toggles (registered automatically by `knob_param`/`toggle_param`) and actions; Learn; `.nammidi` files; clock → tempo. Tested by `tests/MidiMapperTest` |
| `MidiMappingsDialog` | `src/ui/MidiMappingsDialog.h/cpp` | MIDI → Mappings… window (devices, table, per-row learn, load/save/defaults) |
| `VUMeter` | `src/ui/VUMeter.h/cpp` | Vertical LED bar-graph peak meter (glowing segments, green/yellow/red zones, peak-hold LED; drawn with `LedPaint.h`, shared with `TunerDisplay`); one or two columns (the Output meter is stereo, L/R from `take_output_peak(channel)`; the Input is mono — one port); `set_peak(linear)` / `set_peaks(l, r)` on a 66 dB scale, time-based ballistics, repaints only on movement (fed at ~60 fps, paused while minimized) |
| `GainReductionMeter` | `src/ui/GainReductionMeter.h/cpp` | Compressor/Gate reduction readout in the rows' stereo slot; fed from `take_gain_reduction_db()` |
| `Log.h` | `src/dsp/` | `nam.audio` logging category — use `qCWarning(lc_audio, …)`, never `std::cerr` (invisible on Windows) |
| `KnobWidget` | `src/ui/KnobWidget.h/cpp` | `QDial` wrapper: float range, scroll step, label; `set_snap` makes it discrete, and with ≤ 32 positions it draws position dots (`Theme::detent`) instead of the sweep arc |
| `ToggleSwitch` | `src/ui/ToggleSwitch.h/cpp` | Custom-painted on/off switch |
| `Theme` | `src/ui/Theme.h/cpp` | Fusion + app stylesheet; per-effect accents (`tag_card(card, fx_id)`) + chrome color accessors — **all colors live here**. Modes (`ui/theme`): Follow the system (default, live), Dark / Light (= KDE Breeze Dark / Light palettes, user 2026-10-09), Custom (a theme file, `ui/customTheme`, reloaded on save). Every mode is one palette (window, text, base, button, accent, shadow, disabled text) → `from_palette` derives all tones; ours in every mode: LED colours, warning red, switch edges, effect accents (a theme file may override those). `AA_UseStyleSheetPropagationInWidgetStyles` is on, so the window's palette reaches widgets created later (Settings, MIDI Mappings) — without it they took the OS palette. Text follows the system font (no forced size; 2026-10-18): stylesheet sizes above the body are `@pt:N@` (N as drawn at 9 pt), and the rack's fixed columns and rows keep their 9 pt sizes, widened by `MainWindow::fit_to_font` (on every stylesheet/font change) to their widest real content — card names, switch columns, every compact knob's name and widest value (`KnobWidget::text_width_needed`, from its range/formatter) — plus the status bar's height. Measure widgets, never sample strings; never scale by the font ratio (that put scroll bars on a 1080p screen at 10 pt). KDE look (2026-10-19): the menu bar is KDE's Header colour (read from kdeglobals while following the system — Qt's palette has no Header role — only when its window colour matches the palette; Breeze's for Dark/Light; a theme file's `header`; else the window), the window colour while inactive (`QMenuBar[inactive]`, set in `MainWindow::changeEvent`); frames are the background 20 % toward the text; menus are rounded (an app event filter gives every QMenu `WA_TranslucentBackground` at Polish); button hover = accent border, fill unchanged |
| `ThemeFile` | `src/ui/ThemeFile.h/cpp` | Theme file reader (no widgets): our JSON or a KDE `.colors`, from `<config>/themes`; strict — an unknown key/effect ID or bad colour is an error with a message (`tests/ThemeFileTest`) |
| `AppImageIntegration` | `src/ui/AppImageIntegration.h/cpp` | AppImage only (`$APPIMAGE`): add/remove the launcher, icon and MIME types under `~/.local/share` (marker key `X-NAMX-AppImage-Integration`), first-run offer from `MainWindow::offer_desktop_integration` (skipped in `--self-test-and-exit`), Settings menu entry |
| `EffectShortcuts` | `src/ui/EffectShortcuts.h/cpp` | Keyboard toggles for effects (user has Latin American layout) |
| `Constants.h` | `src/dsp/` | DSP parameter ranges and defaults for all effects |

### Naming

Code follows `CPP_GUIDELINES.md` (Phase 20, 2026-10-01): `snake_case` functions,
variables and members with no prefixes, `UPPER_CASE` constants, `PascalCase` types; no
parameter or local reuses a member's name (`new_<x>` / `current_<x>` / `<x>_per_channel`).
Settings keys, preset JSON keys and effect IDs are data and keep their spelling
(`effectOrder`, `session/presetPath`, …). `.clang-tidy` encodes the naming rules.

### Parameter registry

`MainWindow::register_parameters()` is the **single source of truth for every user-facing
parameter**: one `knob_param`/`toggle_param` line per control builds `params`
(`ParamDesc { group, key, widget, defaults }`) and wires the widget to the
engine via a lambda connect. Preset save/load are single loops over this table; Phase 3
session persistence and future MIDI mapping consume it too. **When adding an effect
parameter, register it here — never add hand-written connects, slots, or preset keys.**
`group`+`key` mirror the preset JSON schema (empty group = top-level key).
Every rack knob and switch is in presets, the Tuner's On and Mute included (user,
2026-10-04); `--self-test-and-exit` fails if a rack control (outside the tempo bar) is
missing from the registry.

### Loading files

Every way a model/IR/preset gets loaded — library click, ‹ › arrows, `Library → Import…`
dialogs, drag-and-drop onto the window, session restore, preset references — goes through
`MainWindow::load_model_file` / `load_ir_file` / `load_preset_file`. They skip a reload of the
already-loaded path, persist `files/last*`, bump the library recents and mark the loaded
entry, and refresh the Amplifier/Cabinet hero rows (`update_hero_cards`). Don't call
`AudioEngine::load_model`/`load_ir` from the UI directly.

### Persistence

- **QSettings** (org `Insapiable` — `pichulero.cl` until 2026-10-04, migrated once by `main.cpp`; app name set in `main.cpp` — drives the config path): last model/IR/preset paths, audio device IDs, JACK port connections, window geometry + dock state (`window/state`), the library (`library/{model,ir,preset}Roots`, `favorites`, `recents`, `section`, and `modes`/`sorts` — one All/★/Recent mode and one sort per section; the old global `mode`/`sort` only seed them), and the full registry state plus `session/effectOrder` under `session/` (saved on exit + debounced 1 s after any widget change; restored on launch through the widgets). First run auto-connects `system:capture_1`/`playback_{1,2}`.
- **Presets** (8 starters in `resources/presets/`, seeded once into `<presets>/Starter`) are `.nampreset` JSON files via `QJsonDocument`, schema v2: all registry parameters plus `version`, `namEnabled`, `effectOrder` (stable-ID list), embedded `namPath`/`irPath` (applied on load when the file exists; otherwise a warning, current model/IR kept); older presets may carry a `tone3000` object from the removed integration, which is ignored. v1 files (no `version`) still load. Loading applies values through the widgets, so the UI and engine can't drift.

## NAM core library

`external/NeuralAmpModelerCore/NAM/dsp.h` defines `nam::DSP`, the base class for all models. The key interface used by `NamProcessor`:

```cpp
model->Reset(sampleRate, maxBufferSize);
model->prewarm();  // run inference on silence to settle state
model->process(NAM_SAMPLE** inputs, NAM_SAMPLE** outputs, int numFrames);
```

`NAM_SAMPLE` is `float` when compiled with `-DNAM_SAMPLE_FLOAT` (set in `CMakeLists.txt`, along with `-DNAM_ENABLE_A2_FAST` for the fast-path WaveNet). Models are loaded via `nam::get_dsp(std::filesystem::path)` from `NAM/get_dsp.h`. A model `Reset` to buffer size N must never be fed more than N frames — the core only asserts, and Release strips asserts. `NamProcessor` guards this: `prepare_buffer` re-`Reset()`s live models when the max grows, and `process()` bypasses oversized blocks.

## CMake notes

- `CMAKE_AUTOMOC ON` — MOC runs automatically; add new Qt classes to `GUI_SOURCES` in `CMakeLists.txt`.
- Shared code is compiled once (2026-10-04): `nam_core` (the NAM core, warnings off) and `namx_dsp` (every `src/dsp/*.cpp` in `GUI_SOURCES` except AudioEngine and the backends) are OBJECT libraries; the app, `NamProcessorTest`, `DspGoldenTest`, `namx-bench` and `namx-quality` link **both** (object files only reach direct linkers). Optimization/SIMD/NAM defines live on the `namx_build_options` interface target, so tests and benches run the code exactly as the app compiles it. A new `src/dsp` .cpp goes into `GUI_SOURCES` and lands in `namx_dsp` automatically.
- Backend selection is in `CMakeLists.txt`: `WasapiBackend.cpp` on WIN32, `JackBackend.cpp` otherwise. On Windows the target is `WIN32_EXECUTABLE` (no console — `std::cerr` output is invisible there).
- The clang language server shows false-positive errors for Qt types because it doesn't see the CMake include paths. Ignore these; the actual compiler succeeds.
- `stdc++fs` is linked explicitly on Linux for `std::filesystem`.
