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
├── LICENSE            # GPL-3.0-or-later (the app); LICENSE-MIT = the core's MIT text (+ pre-2026-10-01 app code)
├── presets/           # Personal .nampreset files — untracked on purpose, do not add to git
├── src/
│   ├── main.cpp       # QApplication setup (org/app name → QSettings path), Theme::apply
│   ├── ui/            # Qt widgets
│   ├── midi/          # MidiInput (ALSA / WinMM), MidiMapper (no widgets beyond the target pointers)
│   └── dsp/           # AudioEngine, backends, effect processors (no Qt dependencies)
├── resources/         # resources.qrc, app icon (icons/), starter presets (presets/)
├── packaging/         # desktop file, AppStream, mime, Windows .rc (AppImage built by release.yml — the only Linux release, user 2026-10-04; no Flatpak, Snap or AUR package)
├── tests/             # MidiMapperTest, DspGoldenTest, WavReaderTest, NamProcessorTest, SignalGraphTest, LibraryIndexTest, ThemeFileTest (-DNAM_BUILD_TESTS=ON)
├── bench/             # namx-bench, instructions.py (CPU regression) (-DNAM_BUILD_BENCH=ON)
└── external/          # Git submodules (CMake: EXTERNAL_DIR, NAM_ROOT, NAM_DEPS_PATH)
    ├── NeuralAmpModelerCore/  # NAM core — our fork (Insapiable-Inc), branch `namx` = upstream 3cde95c + our weight-check fix; brings Eigen, nlohmann/json, AudioDSPTools in its Dependencies/
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

### Signal graph (branch `ui/node-graph`, 2026-10-17)

On this branch the fixed chain is gone: the rig is a **node graph** the user wires on a
canvas (look from `mockup.png`: top bar, node network left, parameters right; the
mockup's bottom-right library panel is deliberately left out). `src/dsp/SignalGraph.h`
holds the rules, `NodeCatalog` the node types:

- Every port carries one **mono** signal. Inputs are on a card's top edge, outputs on its
  bottom edge. **An output may feed any number of inputs** (that is the split — there is
  no split node); **an input takes at most one wire**, so two signals meet only in a
  **Merge** node (2 inputs, 1 output, a Mix A/B crossfade, Invert B for polarity,
  no EQ). No loops. Where two signals meet (Merge, an Output with both sides wired)
  the earlier one is delayed to line up with the later — path latencies summed from
  `latency_ms()` every block in `run_plan`, up to `GraphPlan::MAX_ALIGN` samples
  (user, 2026-10-18).
- Kinds: `INPUT` (1 out: the guitar after the input gain), `OUTPUT` (L, R in; one side
  wired = mono on both), `MONO` effects (1 in, 1 out), `STEREO` = **mono in, stereo out**
  (1 in, L and R out) — the **only** version of Flanger, Phaser, Chorus, Tremolo (L/R
  LFOs fixed half a cycle apart), Delay (Ping-Pong knob) and Reverb (user, 2026-10-18:
  one node per effect; for mono, wire one output), `MERGE` (A, B in). Each effect card has its own
  bypass button (power icon left of the ⋮); Input, Output and Merge have none (user,
  2026-10-18: no Merge bypass is obviously right). A Select (A | B) node was tried and
  dropped the same day: a Merge's Mix mapped to a footswitch is the A/B switch, its
  gains glide over 10 ms so the flip doesn't click.
- **Routing nodes** (user, 2026-10-18; Utility, Merge's blue, no bypass —
  `NodeType::bypass = false`): **Crossover** (STEREO kind, outputs labelled Low / High
  via `NodeType::output_labels`; Linkwitz-Riley 24 dB/oct, the bands sum to an
  all-pass), **Pan** (STEREO kind, constant power, −3 dB centre) — the inverse of Merge
  — and **Meter** (MONO pass-through; its card draws a level bar, the panel a
  `VUMeter`; `RigWindow`'s meter timer is the one reader of `MeterProcessor::take_peak`).
  All three in `src/dsp/RoutingProcessors.h`. Not wanted: a Gain node (not yet), an FX
  loop node.
  **Every effect takes one input** (user, 2026-10-18): no 2-in effects, a stereo rig
  runs two nodes, one per side — Amp and Cab included, which are mono only (briefly
  stereo-only; mono is the most intuitive).
- **The tuner is global, not a node** (user, 2026-10-18): the engine runs it on the input
  after the input gain, before the graph (Mute silences the graph's input); the top
  bar's Tuner button (Ctrl+T) shows its strip. Older graphs' Tuner nodes are dropped on
  load and the chain rejoined.
- **Cards move freely** (user, 2026-10-18; dragging moves every selected card), and
  positions are saved in presets and the session (`x`, `y`). **Auto-Organize** (canvas
  right-click, ⋮ menu, Ctrl+L) applies `GraphLayout` once — every card the same size,
  north to south: rows by longest path from the Input, the Output alone at the bottom,
  a lane for each wire that skips rows, barycenter ordering, unwired nodes parked in a
  column on the right. Files without positions (v2, earlier v3) are organized on load.
  The lanes last until a card moves or the structure changes (then plain curves); a rig
  loaded exactly as organized gets them back. A new node lands where the menu opened or
  the wire was dropped (from the ⋮ menu or search: right of the rig).
- Any number of instances of a type (two amps, two cabs…). Polarity is not a node yet
  (user: could be its own node later).
- `AudioEngine` compiles the graph (`compile_graph`: topological order, a buffer per
  output port) into a `GraphPlan` published through an `RtSlot`; the plan owns
  `shared_ptr<GraphNode>`s, so a removed node lives until the audio thread has left the
  last plan holding it. Only nodes on a path from the Input to the Output run
  (`live_node_ids`); dead ends and unfed branches are skipped and drawn faded with a
  dashed edge. A node whose input
  wiring changed gets `request_reset()`. Output gain, meter, metronome and limiter follow
  the graph as before. Latency = the slowest Input→Output path.
- `NodeCatalog` type IDs are the old effect IDs (never rename) and parameter keys are the
  v2 preset keys, so v2 presets and the chain-era session migrate by name
  (`GraphModel::from_v2`: enabled effects in `effectOrder`, mono effects after a stereo
  one doubled into L/R twins, as the chain ran them).
- **Tempo sync** is catalog data: a rate/time `ParamSpec` names its Sync switch and note
  parameter (`sync_switch`, `sync_note`, `Kind::NOTE`); `GraphModel::effective_value`
  resolves it at the tempo (`set_bpm`, fed by the tempo bar) and the panel shows the
  note knob in the free knob's place while synced. Keys as in v2 presets.
- **MIDI**: every node parameter is a widget-free `MidiMapper` target
  (`add_value`/`add_switch`), ID `param:<node name>/<key>` (`enabled` = power), so a
  mapping follows a node name across presets; registered again on every structure
  change (`RigWindow::register_midi_targets`; renames are structure changes, names are
  unique). Chain-era IDs (`param:delay/mix`, `param:namEnabled`, …) are aliases to the
  first node of the type, so existing mappings keep working — and `main` keeps reading
  the same `midi/mappings`. Panel controls carry a `midiTarget` property for Learn.
- Not ported yet: the gate/compressor reduction meters, preset quick slots and A/B, the
  library panel.

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
| `AudioEngine` | `src/dsp/AudioEngine.h/cpp` | Owns the backend, runs the compiled graph (`create_node`, `set_graph`, `load_model`/`load_ir` per node), metronome, limiter, VU peaks |
| `SignalGraph` | `src/dsp/SignalGraph.h/cpp` | Node kinds and ports, `GraphNode`, `GraphWire`, `compile_graph` / `can_add_wire` / `run_plan`; `IoProcessor` (Input/Output gain, limiter switch), `MergeProcessor` (`tests/SignalGraphTest`) |
| `NodeCatalog` | `src/dsp/NodeCatalog.h/cpp` | Every node type: name, category, mono/stereo variants, file kind, `ParamSpec`s; `create(id, type, stereo)` |
| `GraphModel` | `src/ui/GraphModel.h/cpp` | The rig as edited: nodes (name, position, values, file) + wires; every change pushed to the engine; JSON v3 + v2 migration |
| `NodeCanvas` | `src/ui/NodeCanvas.h/cpp` | The node network (QGraphicsView): cards (draggable; Auto-Organize = `GraphLayout` once), ports, bezier wires, drag-to-connect (onto a wired input replaces its wire; a loop is refused, drawn red), wire pick-up, per-card bypass button, context menus (Add / node / wire: every node type in one flat alphabetical list — no submenus — inserted on a wire from the wire menu; a wire dropped on empty canvas opens the same list and wires the new node to it), zoom tools; left-drag on the background = box selection (fully enclosed items; Ctrl adds), middle-drag pans (user, 2026-10-18); Ctrl+click multi-selects and Delete removes the selection (user, 2026-10-18: keep Ctrl+click for that — no Ctrl+click bypass); `NodeIcons` paints the category icons and the power symbol |
| `ParamPanel` | `src/ui/ParamPanel.h/cpp` | Right panel: the selected node's header (icon, name, I/O, power, ⋮) and its catalog parameters — switches in a row, choices as segments, knobs as `KnobWidget::Form::PANEL` (name with the unit, typeable value field with the bare number, large dial drawn like the canvas cards: flat body, accent arc, no glow — laid out as main's tiles: dial left, label top right, value bottom right) in two columns, synced knobs swapping to their note value; file row for Amp/Cab, Input/Output meters |
| `TopBar` | `src/ui/TopBar.h/cpp` | Logo, preset name menu + ✎ rename, metronome + BPM (full `TempoBar` in a popup), search over effects/presets/models/IRs, ★, ⋮ menu, ⚙ |
| `RigWindow` | `src/ui/RigWindow.h/cpp` | The main window: audio start/devices/connections, presets, session, search, Settings, notices (`set_notice`) |
| `IAudioBackend` | `src/dsp/IAudioBackend.h` | Backend interface: start/stop, device enumeration, port routing |
| `JackBackend` | `src/dsp/JackBackend.h/cpp` | Linux JACK/PipeWire-JACK backend |
| `WasapiBackend` | `src/dsp/WasapiBackend.h/cpp` | Windows WASAPI backend: shared mode (float32 mix format only) or opt-in exclusive mode (native format search + conversion, minimum period, falls back to shared; `audio/exclusive`). No Windows machine here: compile-checked with MinGW, not run |
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
| `SettingsDialog` | `src/ui/SettingsDialog.h/cpp` | Settings window (Ctrl+,): Audio / MIDI / Library / Tuner / Shortcuts pages, instant apply; drives the same objects as the rest of the UI (`SettingsTargets`); the Shortcuts page is built from the window's menus and `QShortcut`s (each needs a `whatsThis`) |
| `AudioStatusBar` | `src/ui/AudioStatusBar.h/cpp` | Bottom status bar: backend state, rate/buffer/latency (round trip = server I/O + `AudioEngine::processing_latency()`, from each effect's `latency_ms()` and the limiter; breakdown in the tooltip), device pickers; persists IDs, emits `device_selection_changed` |
| `LibraryIndex` | `src/ui/LibraryIndex.h/cpp` | Library scan: worker thread over the root folders, metadata cache (`AppDataLocation/library-index.json`), `QFileSystemWatcher` rescans; `LibraryEntry` carries a pre-built `summary`/`details` per file |
| `LibraryPanel` | `src/ui/LibraryPanel.h/cpp` | Local library tab: Models/IRs/Presets sections, search, favorites, recents, sort, two-line list; emits `activated(kind, path)`, owner reports back via `set_loaded_path` |
| `TempoBar` | `src/ui/TempoBar.h/cpp` | Tempo + metronome strip: BPM `NumberField`, Tap, play (Space), `BeatLeds` accent editor, meter/subdivision/sound/count-in menus, level/pan; emits `bpm_changed`; `poll()` from the meter timer |
| `NumberField` | `src/ui/NumberField.h/cpp` | Drag / scroll / click-to-type numeric readout with a unit (tempo BPM, tuner A4 reference) |
| `TempoSync.h` | `src/ui/` | Note-value table (index order is a preset contract — append only) + ms/Hz conversions (on `ui/node-graph` only the metronome uses it so far — tempo sync of node parameters is not ported) |
| `Metronome` | `src/dsp/Metronome.h/cpp` | RT click generator mixed after the output gain and meter, before the limiter (not an effect, not metered); samples synthesized in `set_sample_rate`; settings are atomics |
| `MidiInput` | `src/midi/MidiInput.h/cpp` + `AlsaMidiInput.cpp` (Linux) / `WinMidiInput.cpp` (Windows) | Platform MIDI input; backend thread → queued `message_received(MidiMessage)`; devices by name, hot-plug, enable set in `midi/disabledDevices` |
| `MidiMapper` | `src/midi/MidiMapper.h/cpp` | Mappings (CC/note/program × absolute/relative/toggle/momentary, range) → registry knobs/toggles (registered automatically by `knob_param`/`toggle_param`) and actions; Learn; `.nammidi` files; clock → tempo. Tested by `tests/MidiMapperTest` |
| `MidiMappingsDialog` | `src/ui/MidiMappingsDialog.h/cpp` | MIDI → Mappings… window (devices, table, per-row learn, load/save/defaults) |
| `VUMeter` | `src/ui/VUMeter.h/cpp` | Vertical LED bar-graph peak meter (glowing segments, green/yellow/red zones, peak-hold LED; drawn with `LedPaint.h`, shared with `TunerDisplay`); one or two columns (the Output meter is stereo, L/R from `take_output_peak(channel)`; the Input is mono — one port); `set_peak(linear)` / `set_peaks(l, r)` on a 66 dB scale, time-based ballistics, repaints only on movement (fed at ~60 fps, paused while minimized) |
| `GainReductionMeter` | `src/ui/GainReductionMeter.h/cpp` | Compressor/Gate reduction readout in the rows' stereo slot; fed from `take_gain_reduction_db()` |
| `Log.h` | `src/dsp/` | `nam.audio` logging category — use `qCWarning(lc_audio, …)`, never `std::cerr` (invisible on Windows) |
| `KnobWidget` | `src/ui/KnobWidget.h/cpp` | `QDial` wrapper: float range, scroll step, label; `set_snap` makes it discrete, and with ≤ 32 positions it draws position dots (`Theme::detent`) instead of the sweep arc |
| `ToggleSwitch` | `src/ui/ToggleSwitch.h/cpp` | Custom-painted on/off switch |
| `Theme` | `src/ui/Theme.h/cpp` | Fusion + app stylesheet; per-effect accents (`tag_card(card, fx_id)`) + chrome color accessors — **all colors live here**. Modes (`ui/theme`): Follow the system (default, live), Dark / Light (= KDE Breeze Dark / Light palettes, user 2026-10-09), Custom (a theme file, `ui/customTheme`, reloaded on save). Every mode is one palette (window, text, base, button, accent, shadow, disabled text) → `from_palette` derives all tones; ours in every mode: LED colours, warning red, switch edges, effect accents (a theme file may override those). `AA_UseStyleSheetPropagationInWidgetStyles` is on, so the window's palette reaches widgets created later (Settings, MIDI Mappings) — without it they took the OS palette |
| `ThemeFile` | `src/ui/ThemeFile.h/cpp` | Theme file reader (no widgets): our JSON or a KDE `.colors`, from `<config>/themes`; strict — an unknown key/effect ID or bad colour is an error with a message (`tests/ThemeFileTest`) |
| `AppImageIntegration` | `src/ui/AppImageIntegration.h/cpp` | AppImage only (`$APPIMAGE`): add/remove the launcher, icon and MIME types under `~/.local/share` (marker key `X-NAMX-AppImage-Integration`), first-run offer from `RigWindow::offer_desktop_integration` (skipped in `--self-test-and-exit`), Settings menu entry |
| `Constants.h` | `src/dsp/` | DSP parameter ranges and defaults for all effects |

### Naming

Code follows `CPP_GUIDELINES.md` (Phase 20, 2026-10-01): `snake_case` functions,
variables and members with no prefixes, `UPPER_CASE` constants, `PascalCase` types; no
parameter or local reuses a member's name (`new_<x>` / `current_<x>` / `<x>_per_channel`).
Settings keys, preset JSON keys and effect IDs are data and keep their spelling
(`effectOrder`, `session/presetPath`, …). `.clang-tidy` encodes the naming rules.

### Parameters (branch `ui/node-graph`)

`NodeCatalog` (`src/dsp/NodeCatalog.cpp`, no Qt) is the **single source of truth for every
node parameter**: one `ParamSpec` per control (key, label, range, default, unit, format,
log taper, stereo-only, setter). `ParamPanel` builds its controls from it, `GraphModel`
stores `key → value` per node and applies through the setter, presets and the session
save the same map. **A new parameter is one line in the catalog** — no hand-written
widgets, connects or preset keys.

### Loading files

A model/IR goes into a node through `GraphModel::load_file(id, path)` (the panel's
Browse… and ‹ ›, the search box, drag-and-drop, `namx file…`, presets). Don't call
`AudioEngine::load_model`/`load_ir` from the UI directly.

### Persistence

- **QSettings** (org `Insapiable`; app name set in `main.cpp`): audio device IDs, JACK
  port connections, the library roots/favorites, and on this branch `graph/session` (the
  graph JSON, debounced 1 s after any change and on exit), `graph/presetPath` +
  `graph/presetBaseline` (the `*`), `graph/window/{geometry,split}`. The chain-era
  `session/…` keys are only read (first launch migrates them) and never written, so
  `main` keeps its own session. First run auto-connects `system:capture_1`/`playback_{1,2}`.
- **Presets** are `.nampreset` JSON, format 3: `{"version": 3, "graph": {"nodes": [{id,
  type, stereo?, name, x, y, enabled, params, file?}], "wires": [{from, fromPort, to,
  toPort}]}}`. v2 files load through the migration; the 8 starters are still v2.

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
