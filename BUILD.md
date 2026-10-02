# NAM Player — Build & Usage Guide

NAM Player (binary `NAMPlayer`) is a standalone Qt6 desktop application that loads `.nam` neural amp models
and `.wav` impulse responses, processes live audio, and exposes a full pedal-style
effects chain plus a 3-band EQ.

| Platform | Audio backend | Status |
|----------|---------------|--------|
| Linux    | JACK (jackd2 or PipeWire-JACK) | Stable |
| Windows  | WASAPI shared mode (native, MSVC) | Functional first cut — float32 mix format only |

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Get the source & submodules](#2-get-the-source--submodules)
3. [Build](#3-build)
4. [Run](#4-run)
5. [Connect audio ports](#5-connect-audio-ports)
6. [Controls reference](#6-controls-reference)
7. [Signal chain](#7-signal-chain)
8. [File formats](#8-file-formats)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Prerequisites

### Ubuntu 24.04+ / Debian 13+

```bash
sudo apt update
sudo apt install \
    git cmake ninja-build g++-14 \
    qt6-base-dev \
    libjack-jackd2-dev libasound2-dev jackd2 qjackctl
```

The code is C++23 and needs GCC 14. Ubuntu 24.04 defaults to GCC 13, so point CMake
at the newer compiler: `export CXX=g++-14` before `bash build.sh` (Debian 13 and
Ubuntu 24.10+ already default to GCC 14). Ubuntu 22.04 is no longer supported.

> **PipeWire users:** PipeWire ships a JACK compatibility layer.
> Install `pipewire-jack` instead of `jackd2` and start the session service —
> no `jackd` daemon required.
>
> ```bash
> sudo apt install pipewire-jack
> # JACK apps will connect automatically once PipeWire is running (it usually is by default)
> ```

### Arch / Manjaro

```bash
sudo pacman -S git cmake ninja qt6-base jack2 alsa-lib qjackctl
```

### Fedora / RHEL

```bash
sudo dnf install git cmake ninja-build qt6-qtbase-devel jack-audio-connection-kit-devel alsa-lib-devel qjackctl
```

### Windows (MSVC)

Three things to install:

1. **Visual Studio 2022 Build Tools** with the *"Desktop development with C++"*
   workload — provides `cl.exe`, MSBuild, and the Windows 10/11 SDK. Get them
   from <https://visualstudio.microsoft.com/downloads/>. The free Community
   edition or the standalone *Build Tools* installer both work.

2. **CMake** ≥ 3.16. Either install from <https://cmake.org/download/> or use the
   one bundled with VS (`Developer PowerShell for VS 2022` already has it on
   `PATH`).

3. **Qt 6.5+ for MSVC**:
   - **Official online installer** (recommended first build):
     <https://www.qt.io/download-qt-installer> — pick *Qt 6.x → MSVC 2019 64-bit*.
     Default install path is `C:\Qt\6.x.x\msvc2019_64`.
   - **aqtinstall** (scriptable, no account needed):
     ```powershell
     pip install aqtinstall
     aqt install-qt windows desktop 6.7.0 win64_msvc2019_64 -O C:\Qt
     ```

WASAPI links against the system DLLs (`ole32`, `mmdevapi`, `avrt`), so there's
no separate audio dependency — the Windows SDK already ships them.

> **Audio caveat:** the current WASAPI backend only handles **float32 mix format**
> (the modern Windows default). If your default capture or render device is
> configured for 16-bit PCM, the app will log "mix format not float32" and bail
> out of `start()`. Workaround: in *Sound Settings → device properties → Advanced*,
> pick a 24-bit or 32-bit format. PCM-to-float conversion is on the roadmap.

### Minimum versions

| Dependency | Minimum |
|------------|---------|
| CMake      | 3.20    |
| Qt         | 6.2 (Linux) / 6.5 (Windows MSVC) |
| JACK       | 1.9.17 (jackd2) or PipeWire-JACK — Linux only |
| GCC / Clang| GCC 14 / Clang 18 (C++23 required) |
| MSVC       | Visual Studio 2022 (17.10+) |

---

## 2. Get the source & submodules

```bash
git clone --recursive https://github.com/silvercobraa/NeuralAmpModelerGUI.git nam-player
cd nam-player

# The GUI depends on two git submodules (Eigen and AudioDSPTools)
git submodule update --init --recursive
```

If you already cloned without `--recursive`, the `build.sh` script calls
`git submodule update --init --recursive` automatically.

---

## 3. Build

### Linux — quick build

```bash
cd gui
chmod +x build.sh
./build.sh          # defaults to Release
./build.sh Debug    # Debug
```

Binary lands at `gui/build-release/NAMPlayer` (or `build-debug/`).

### Linux — manual CMake

```bash
cd gui
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel $(nproc)
```

Optional CMake variables:

| Variable | Purpose |
|----------|---------|
| `-DNAM_NATIVE_OPT=OFF` | Portable AVX2/FMA instead of `-march=native` (required for release builds) |
| `-DNAM_TONE3000_CLIENT_ID=t3k_pub_…` | Bake your TONE3000 publishable key into the build (see §6, *TONE3000*) |
| `-DNAM_BUILD_TESTS=ON` | Also build `Tone3000ClientTest` (needs `python3`; run it directly or via `ctest`) |

### Linux — install / packages

```bash
cd gui
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DNAM_NATIVE_OPT=OFF
cmake --build build-release --parallel
sudo cmake --install build-release --prefix /usr/local
```

This installs the binary, the launcher (`cl.pichulero.NAMPlayer.desktop`, with `.nam`
and `.nampreset` file associations), the AppStream metadata and the icon theme files.
Run `update-desktop-database` / `update-mime-database` afterwards if your distro
doesn't hook them.

An AUR-style `PKGBUILD` lives in [`packaging/`](packaging/README.md). There is no
AppImage or Flatpak on purpose: the app expects the system's Qt 6 and JACK
(jackd2 or PipeWire-JACK).

Command-line flags: `NAMPlayer [file…]` opens `.nam` / `.wav` / `.nampreset` files;
`--version`; `--self-test-and-exit` (headless CI check: window up, library scanned,
exit 0 after 1.5 s).

### Windows — quick build (PowerShell)

From a "Developer PowerShell for VS 2022" prompt:

```powershell
cd gui
.\build.ps1                                          # Release
.\build.ps1 -BuildType Debug                         # Debug
.\build.ps1 -QtDir 'C:\Qt\6.7.0\msvc2019_64'         # custom Qt
.\build.ps1 -Generator Ninja                         # Ninja for faster builds
```

Binary lands at `gui\build-release\Release\NAMPlayer.exe`. `windeployqt`
runs automatically at the end of the build to copy Qt DLLs next to the .exe.

> If you see *"Could not find a package configuration file provided by Qt6"*,
> pass `-QtDir 'C:\path\to\Qt\6.x.x\msvc2019_64'`. The script forwards it to
> CMake as `-DCMAKE_PREFIX_PATH`.

### Windows — manual CMake

```powershell
cd gui
cmake -S . -B build-release `
      -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64"
cmake --build build-release --config Release --parallel
& "C:\Qt\6.7.0\msvc2019_64\bin\windeployqt.exe" .\build-release\Release\NAMPlayer.exe
```

With Ninja (faster):

```powershell
cmake -S . -B build-release -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64"
cmake --build build-release --parallel
```

### Optional CMake flags

| Flag | Default | Description |
|------|---------|-------------|
| `-DCMAKE_BUILD_TYPE=Release` | Release | `Debug` for symbols, `Release` for optimization |
| `-DCMAKE_PREFIX_PATH=...` | none | Qt install root on Windows |
| `-DNAM_NATIVE_OPT=OFF` | ON | Portable AVX2/FMA instead of `-march=native` (release/CI builds) |
| `-DNAM_BUILD_TESTS=ON` | OFF | `Tone3000ClientTest` |
| `-DNAM_BUILD_BENCH=ON` | OFF | `NAMPlayerBench`, the chain benchmark (see Troubleshooting → High CPU usage) |

---

## 4. Run

### With jackd (classic JACK)

```bash
# Start JACK daemon (44.1 kHz, 256-frame buffer, ALSA backend)
jackd -d alsa -r 44100 -p 256 &

# Launch NAMPlayer
./build-release/NAMPlayer
```

Common `jackd` driver options:

| Flag | Meaning |
|------|---------|
| `-d alsa` | Use ALSA (most common on Linux) |
| `-r 44100` | Sample rate (44100 or 48000) |
| `-p 256`   | Buffer size in frames (lower = less latency, more CPU) |
| `-n 2`     | Number of periods (2 or 3) |

### With PipeWire-JACK (modern distros)

Just launch the app — no daemon needed:

```bash
./build-release/NAMPlayer
```

PipeWire exposes a JACK-compatible socket automatically.
Use **qpwgraph** or **Carla** to patch ports visually.

### With QjackCtl

1. Open **QjackCtl**, configure your interface, click **Start**.
2. Launch `NAMPlayer`.
3. Use the **Graph** or **Connect** panel to wire ports.

---

## 5. Connect audio ports

NAMPlayer registers three JACK ports (mono in, stereo out):

| Port | Direction |
|------|-----------|
| `NAMPlayer:input`    | Audio input (mono) |
| `NAMPlayer:output_L` | Audio output, left |
| `NAMPlayer:output_R` | Audio output, right |

On first run the app connects `system:capture_1` → `input` and `output_L/R` →
`system:playback_1/2` itself; connections you change afterwards are remembered.

### Command-line connection

```bash
# Guitar interface → NAMPlayer → speakers
jack_connect system:capture_1    NAMPlayer:input
jack_connect NAMPlayer:output_L  system:playback_1
jack_connect NAMPlayer:output_R  system:playback_2
```

### GUI connection tools

- **QjackCtl** → Connect / Graph tab
- **Carla** patch bay
- **qpwgraph** (PipeWire)

---

## 6. Controls reference

### Knob interaction

| Action | Effect |
|--------|--------|
| Click + drag up | Increase value |
| Click + drag down | Decrease value |
| Scroll wheel | Fine adjustment |
| Double-click | Reset to 0 dB |

### Parameters

| Knob | Range | Center freq / type |
|------|-------|--------------------|
| **Input** | ±20 dB | Input gain before model |
| **Bass** | ±12 dB | Low shelf, 40–500 Hz (default 250) |
| **Middle** | ±12 dB | Peak, 200 Hz–2.5 kHz (default 1 kHz, Q 0.7) |
| **High-Mid** | ±12 dB | Peak, 800 Hz–8 kHz (default 3 kHz, Q 1) |
| **Treble** | ±12 dB | High shelf, 1.5–12 kHz (default 4 kHz) |
| **Output** | ±20 dB | Output gain after IR |

Gain changes are sample-accurate smoothed (no clicks or zipper noise).

### Presets

The bar above the rack holds the current preset. A preset recalls every effect
setting, the effect order, the input/output gains, and the model and IR (by path, plus
the TONE3000 ids when they came from there).

| Control | Action |
|---------|--------|
| Name (click) | Pick a preset from the library; folders show as submenus |
| **‹ ›** | Previous / next preset in the library's Presets order |
| `*` after the name | The sound differs from the saved file. It goes away when you save, or when you turn everything back |
| **Save** (`Ctrl+S`) / **Save As…** (`Ctrl+Shift+S`) | Overwrite the current file / write a new one (an untitled state always asks) |
| **Slots 1–8** (`Ctrl+1…8`) | Load the preset stored in a slot. Clicking an empty slot stores the current preset there; right-click to store or clear |
| **A / B**, **A→B** | Two working states of the current preset: switch between them to compare, or copy the active one over the other. Loading a preset clears both |
| **⋯** (same as the **Preset** menu) | Open…, Import… (copies files into the presets folder), Export… (writes a copy elsewhere), Show in Folder, Delete… (to the trash), Reset All to Defaults… |

**Not part of a preset:** audio devices and port connections, the tuner (on/off,
reference pitch), the tempo and the metronome, MIDI mappings and devices, the library and TONE3000 state,
window layout, and the quick-slot assignments. These
belong to the machine or the session, so loading a preset never changes them.

**Starter presets.** On first run eight starter presets are copied to
`<Documents>/NAM Player/Presets/Starter` (Clean Chorus, Tight Rhythm, Lead Delay,
Ambient Swell, Surf Tremolo, Funk Wah, Jet Flanger, Vibe Phaser). They contain no
model or IR, so they apply over whatever amp and cab are loaded. If you delete them
they stay deleted.

### Tempo & metronome

The second bar holds the global tempo and the metronome.

| Control | Action |
|---------|--------|
| **BPM** | Drag up/down or scroll (Shift = fine), click to type. 40–240, quarter notes |
| **Tap** | Tap the beat; the last four intervals are averaged, a 2 s pause starts over |
| **▶ / ■** (`Space`) | Start / stop the metronome. It keeps running when you load presets |
| Beat lights | One column per beat; the current beat lights up (amber during the count-in). Click a beat to cycle its accent: strong → normal → soft → mute |
| **4/4** | Time signature: common meters, or **Custom…** (1–16 beats over 2, 4, 8 or 16). In x/8 the click is on eighths, so 6/8 at 120 BPM clicks eighths at 240 |
| **♩ / ♪♪ / 3 / ♬** | Subdivision: beats only, eighths, triplets, sixteenths |
| **Click / Wood / Beep / Cowbell** | Sound set |
| **Count-in** | 0–2 bars counted in when you press play; **Silent after count-in** keeps only the lights running, to check your own timing |
| **Level / Pan** | The click's own volume and position. It is mixed in after the Output knob and isn't shown on the output meter |

**Tempo sync.** Delay **Time** and the Tremolo / Chorus / Flanger / Phaser **Rate**
each have a **Sync** switch: on, the knob picks a note value (4 bars … 1/32, dotted and
triplets) instead of ms / Hz, and follows the tempo. Delay times are capped at 1000 ms.

The tempo and every metronome setting belong to the session, not to presets: a preset
stores *note values*, so it fits whatever tempo you're playing at.

### MIDI

Any class-compliant MIDI device works: USB controllers, DIN through an interface, and
anything PipeWire/JACK expose on the ALSA sequencer (Linux) or WinMM (Windows). Devices
connect automatically, including ones plugged in later. The status bar's **MIDI** light
blinks on every incoming message.

| How | What |
|-----|------|
| Right-click a knob or switch → **MIDI Learn** | The control glows. Move a knob, press a footswitch or send a program change on your device to bind it. `Esc` cancels. **Forget MIDI** removes the binding |
| Right-click a slot, ‹ ›, A/B, ▶ (metronome) or Tap → **MIDI Learn** | The same for those actions |
| **MIDI → Mappings…** | Every mapping in a table: on/off, source (CC / note / program), channel (or any), number, target, mode, range, per-row **Learn**, delete. **Save… / Load…** store whole setups as `.nammidi` files (one per controller, say). **Defaults** resets to program 0–7 → slots 1–8 |
| **MIDI → Devices** | Enable or disable each input device |
| **MIDI → Follow MIDI Clock** | Incoming clock sets the tempo (averaged over four beats) |

Modes: **Absolute** (the CC sets the value; *Min/Max %* limit the sweep, e.g. an
expression pedal over part of a knob), **Relative** (endless encoders: 1–63 up,
65–127 down), **Toggle** (each press flips a switch or fires an action; the default for
footswitches), **Momentary** (on while held). Learn picks Absolute for a knob moved by a
CC and Toggle for everything else.

Out of the box, program changes 0–7 recall quick slots 1–8. The mappings are
machine settings (QSettings `midi/*`), not part of presets.

### Settings

**Settings → Settings…** (`Ctrl+,`) collects the machine settings in one window; every
change applies immediately.

| Page | What's there |
|---|---|
| Audio | Input / output device (same as the status bar's In: / Out:), the server's sample rate, buffer and round-trip latency (read-only: PipeWire/JACK own them), xruns + reset |
| MIDI | Input devices on/off, Follow MIDI clock, **Mappings…** |
| Library | Model / IR / preset folders, the TONE3000 download folder, rescan |
| Tuner | Reference pitch, show the detected frequency (also on the tuner card) |
| Shortcuts | Every keyboard shortcut, read from the window (**Settings → Keyboard Shortcuts…**) |

### Library

The **Library** dock (left of the rack; `Ctrl+L` shows/hides it) lists every `.nam`
model, `.wav` IR and `.nampreset` preset found under the configured folders
(**Library → Folders…**, which opens the Library page of **Settings**; defaults: `~/NAM` for models and IRs, `~/Music/IRs`, and
`~/Documents/NAM Player/Presets`). Folders are scanned recursively on a background
thread, metadata is cached, and the list follows changes on disk.

| Control | Action |
|---------|--------|
| **Models / IRs / Presets** | Switch section |
| Search field (`Ctrl+F`) | Filter as you type (name, folder, metadata) |
| **All / ★ / Recent** | Everything, favorites only, or recently used |
| **Name / Newest** | Sort order |
| Click, arrows, PgUp/PgDn, Return | Load the entry under the cursor |
| ★ on a row | Toggle favorite |
| **Folders…** / ↻ | Configure the scanned folders / rescan now |

The **Amplifier** and **Cabinet** rows show the loaded file with its metadata
(architecture, sample rate, gear; rate, channels, length). **‹ ›** step through the
library in its current visible order; clicking the file name focuses the matching section. Files can
also be dropped onto the window, imported through **Library → Import…**, or unloaded
from the row's right-click menu. The **On/Off** toggle bypasses the stage without
unloading it.

### TONE3000

The Library dock's second tab browses [TONE3000](https://www.tone3000.com), the
community catalog of NAM captures and IRs. It uses your own TONE3000 account: sign-in
happens in the browser (TONE3000 e-mails a code), and the app only ever holds the
OAuth tokens it is issued (stored in the app's settings; **Sign out** clears them).

| What | How |
|------|-----|
| First use | **Continue** on the splash opens TONE3000 in the browser. Sign in, pick a tone, and you are sent back to the app with that tone open. |
| Browse | **Trending**, **Latest**, **Favorites**, **Downloads** and **Mine** are lists from your account. **Browse TONE3000…** opens the full catalog (search, filters) in the browser and brings the picked tone back. |
| Tone page | Image, gear type, format, license, creator, description, the model list. Click a model to download it into the library; click again (**Load**) to put it in the rig. **Download all** fetches the rest. **★** favorites the tone on TONE3000. |
| Where files go | `~/Documents/NAM Player/TONE3000/<creator>/<tone>/` (change it from the account menu). The folder is always part of the library, so downloads show up in the local tab too, tagged *TONE3000 · @creator*. Each file gets a `.t3k.json` sidecar with the tone/model ids, creator and license. |
| Origin mark | The Amplifier / Cabinet row shows the T3K mark when the loaded file came from TONE3000; click it to open that tone. |
| Presets | A preset saved with a TONE3000 model remembers its model id. Loading it on a machine where the file is missing finds the model elsewhere in the library, or offers to download it. |
| Offline | The tab says so; the local library keeps working. |

**App key.** TONE3000 identifies integrations by a *publishable key* (`t3k_pub_…`, not
a secret). Packaged builds carry one via `-DNAM_TONE3000_CLIENT_ID=…`. Building from
source without it, the tab shows a setup page: create a key on
[tone3000.com → Settings → API Keys](https://www.tone3000.com/settings), register the
redirect URI `http://localhost:51731/callback` (ports 51732–51734 are tried if that
one is busy), and paste it in. The key can be changed later from the account menu.

NAM Player uses TONE3000's free non-commercial API tier: the bounded lists above and
the browser-based catalog picker. Catalog search is deliberately not done in-app.

Development: `NAM_TONE3000_API_BASE=http://127.0.0.1:8765/api/v1` points the client at
`tests/mock_tone3000.py`, and `NAM_TONE3000_NO_BROWSER=1` logs the sign-in URL instead
of opening it.

---

## 7. Signal chain

Default order (drag the ≡ grips to reorder any effect). The Volume Pedal sits first in
the effects loop, after the amp, so it swells the level without changing the amp's gain:

```
input ─▶ Input Gain ─▶ Tuner ─▶ Transpose ─▶ Noise Gate ─▶ Compressor ─▶ Wah ─▶ Flanger ─▶ Phaser ─▶ Drive
      ─▶ Amplifier (NAM) ─▶ Volume Pedal ─▶ Equalizer ─▶ Chorus ─▶ Tremolo ─▶ Delay ─▶ Reverb
      ─▶ Cabinet (IR A + IR B) ─▶ Output Gain ─▶ Limiter ─▶ (+ metronome) ─▶ output L/R
```

The chain starts mono. Effects in stereo mode (Chorus, Tremolo, Delay, Reverb by
default; Flanger/Phaser when switched; a panned Cabinet) make it stereo from
there on. A stereo signal reaching the Amplifier is summed to mono unless its **Stereo**
switch is on, which runs the model once per channel at twice the CPU.

| Stage | Main controls | ▾ More |
|-------|---------------|--------|
| Transpose | Semitones (±12), Mix | — |
| Drive | Drive, Tone, Level (TS-style: clipping on the mids/highs, clean lows kept) | — |
| Dynamic Wah | Mix, Sensitivity, Resonance, Mode (Auto / Pedal), Position | — |
| Flanger / Phaser / Chorus | Mix, Rate (Sync), Depth, Feedback (not Chorus); Flanger **Range**; Phaser **Vibe** switch (off = classic) | — |
| Equalizer | Bass, Middle, High-Mid, Treble (±12 dB) | the four band frequencies, Low Cut, High Cut |
| Delay | Mix, Time (Sync), Feedback (ping-pong in stereo) | R Time, Low/High Cut, Saturation |
| Reverb | Mix, Decay, Damping, Width | Pre-delay, Low/High Cut |
| Cabinet | IR A (library, ‹ ›) | Level/Pan/Ø A, Blend, Low/High Cut · IR B (‹ ›, Choose…, ×), Level/Pan/Ø B |
| Volume Pedal | Volume, Mid (level at half travel) | — |
| Output | Gain, **Limit** (look-ahead limiter at −0.3 dBFS) | — |

Times, rates and frequencies use logarithmic knobs. Transpose uses the built-in pitch
shifter, which adds about 30 ms of latency while it's active.

---

## 8. File formats

| Format | Details |
|--------|---------|
| `.nam`  | Neural Amp Modeler model file. Supported architectures: WaveNet, LSTM, ConvNet, Linear. Version 0.5–0.7 supported. |
| `.wav`  | Impulse response / cab simulation. Mono or stereo (first channel used). Any sample rate. Truncated to 4096 taps (~93 ms at 44.1 kHz). |
| `.nampreset` | Preset, JSON (schema v2): `version`, every effect group (`noiseGate`, `compressor`, …) with its parameters, `effectOrder` (stable effect IDs), `namPath` / `irPath`, optional `tone3000` linkage. A group missing from the file leaves that effect untouched; a missing key inside a group takes its default. |
| `.t3k.json` | Sidecar written next to a TONE3000 download: tone/model ids, title, creator, license, URL. Read by the library; safe to delete (the file just loses its origin). |

NAM models are available from the [NAM community model library](https://www.neuralampmodeler.com).
IR files are compatible with any standard cab sim WAV (e.g., from OwnHammer, Celestion, Rosen Digital, etc.).

---

## 9. Troubleshooting

### JACK: not connected

- Make sure `jackd` is running: `jack_lsp` should list ports.
- On PipeWire, confirm `pipewire-jack` is installed and the PipeWire service is active:
  ```bash
  systemctl --user status pipewire
  ```
- Check that no other application has exclusive ALSA access (e.g., PulseAudio without PipeWire).

### Failed to load NAM model

- Confirm the file is a valid `.nam` file (JSON with weights).
- Check that the file version is between 0.5.0 and 0.7.0.
- Run from a terminal to see the error printed to stderr.

### Failed to load IR

- Only `.wav` files are supported (not `.flac`, `.mp3`, etc.).
- The file must contain at least one audio channel with non-zero samples.

### No audio output / very quiet output

- Check JACK port connections (`jack_lsp -c`).
- Verify input and output gain knobs are not zeroed (double-click to reset).
- Ensure the IR "Enable" checkbox is ticked if an IR is loaded.

### High CPU usage

- The status bar's **DSP %** is the audio thread's time per buffer as a share of
  the buffer's duration (highest in the last half second); **xruns** counts dropouts.
  Click either to reset.
- Increase the JACK buffer size (`-p 512` or `-p 1024`).
- Use a Release build (`./build.sh Release`), not Debug.
- Large NAM models (e.g., standard WaveNet) are CPU-heavy by design, and a stereo
  effect placed *before* the Amplifier runs the model twice (one per channel).
- With the `powersave` CPU governor, cores idle at low clock between audio buffers,
  so DSP % reads several times higher than the benchmark below. `performance`
  (or `cpupower frequency-set -g performance`) trades power for headroom.

**Measuring.** `NAMPlayerBench` times every effect in chain order, block by block,
against the real-time budget:

```bash
cmake -S gui -B gui/build-bench -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_BENCH=ON
cmake --build gui/build-bench --target NAMPlayerBench
gui/build-bench/NAMPlayerBench --model amp.nam --ir cab.wav   # --frames 128 --rate 48000 by default
```

`NAMPlayer --self-test-and-exit` prints the cold-start time and peak memory.

**Budget** (checked with the tools above): the default chain with a standard
WaveNet and an IR stays under **10 %** of a 128-frame block on one core; the idle UI
does no periodic repaints (meters repaint only when they move and pause while the
window is minimized); the window is up in **< 1 s**. Reference numbers (i9-12900K,
128 frames @ 48 kHz, standard WaveNet + 2048-tap IR, every effect on): **4.1 %**
(amp 2.5 %, cab 1.1 %, all other effects together 0.4 %); cold start 135 ms; peak
RSS 170 MB.

### Submodule build errors (missing eigen / wav.h)

```bash
git submodule update --init --recursive
```
