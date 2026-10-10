# NAM-X — Build & Usage Guide

NAM-X (binary `namx`) is a standalone Qt6 desktop application that loads `.nam` neural amp models
and `.wav` impulse responses, processes live audio, and exposes a full pedal-style
effects chain plus a 4-band EQ with low and high cuts.

| Platform | Audio backend | Status |
|----------|---------------|--------|
| Linux    | JACK (jackd2 or PipeWire-JACK) | Stable |
| Windows  | WASAPI shared or exclusive mode (native, MSVC) | Shared: float32 mix format only; exclusive: new, not yet tested on hardware |

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
    qt6-base-dev qt6-xdgdesktopportal-platformtheme \
    libjack-jackd2-dev libasound2-dev jackd2 qjackctl
```

`qt6-xdgdesktopportal-platformtheme` gives NAM-X your desktop's own file dialogs
(through the XDG portal); without it, it falls back to Qt's plainer ones. On Arch it
is part of `qt6-base`.

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
sudo dnf install git gcc-c++ cmake ninja-build qt6-qtbase-devel jack-audio-connection-kit-devel alsa-lib-devel qjackctl
```

### Windows (MSVC)

Three things to install:

1. **Visual Studio 2022 Build Tools** with the *"Desktop development with C++"*
   workload — provides `cl.exe`, MSBuild, and the Windows 10/11 SDK. Get them
   from <https://visualstudio.microsoft.com/downloads/>. The free Community
   edition or the standalone *Build Tools* installer both work.

2. **CMake** ≥ 3.20. Either install from <https://cmake.org/download/> or use the
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

> **Exclusive mode** (*Settings → Audio → Exclusive mode*): NAM-X takes the
> input and output devices over — the lowest latency WASAPI offers (the devices'
> minimum period, typically 3 ms), at the cost that no other app can use them while
> it runs. It picks a format both devices take natively (float32, int32, 24-in-32,
> packed 24-bit or 16-bit) at the output device's rate (or 48 / 44.1 / 96 / 88.2 kHz)
> and converts. If a device refuses (busy, or no usable format) it falls back to
> shared mode and says so in the status bar.
>
> **Shared-mode caveat:** shared mode only handles the **float32 mix format** (the
> modern Windows default). If a device is configured for 16-bit PCM, the app logs
> "mix format not float32" and won't start; pick a 24-bit or 32-bit format in *Sound
> Settings → device properties → Advanced*, or use exclusive mode.

### Minimum versions

| Dependency | Minimum |
|------------|---------|
| CMake      | 3.20    |
| Qt         | 6.2 (Linux) / 6.5 (Windows MSVC) |
| JACK       | 1.9.17 (jackd2) or PipeWire-JACK — Linux only |
| GCC / Clang| GCC 14 / Clang 18 (C++23 required) |
| MSVC       | Visual Studio 2022 (17.10+) |
| CPU        | x86-64 with **AVX2 + FMA** for the official binaries (Intel Haswell 2013+, AMD Excavator 2015+ / any Ryzen; many Pentium/Celeron/Atom models lack it). Older CPUs: a source build with the default `NAM_NATIVE_OPT=ON` targets the building machine and works, slower — **unsupported** |

---

## 2. Get the source & submodules

```bash
git clone --recursive https://github.com/Insapiable-Inc/namx.git
cd namx

# Code dependencies are submodules under external/: the NAM core (with Eigen,
# nlohmann/json, AudioDSPTools), signalsmith-linear, signalsmith-stretch, chowdsp_wdf.
git submodule update --init --recursive
```

If you already cloned without `--recursive`, the `build.sh` script calls
`git submodule update --init --recursive` automatically.

---

## 3. Build

### Linux — quick build

```bash
chmod +x build.sh
./build.sh          # defaults to Release
./build.sh Debug    # Debug
```

Binary lands at `build-release/namx` (or `build-debug/`).

### Linux — manual CMake

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel $(nproc)
```

Optional CMake variables:

| Variable | Purpose |
|----------|---------|
| `-DNAM_NATIVE_OPT=OFF` | Portable AVX2/FMA instead of `-march=native` (required for release builds) |
| `-DNAM_BUILD_TESTS=ON` | Also build the tests (`MidiMapperTest`, `DspGoldenTest`, `WavReaderTest`, `NamProcessorTest`, `LibraryIndexTest`, `ThemeFileTest`; run them directly or via `ctest`) |

### Linux — install / packages

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DNAM_NATIVE_OPT=OFF
cmake --build build-release --parallel
sudo cmake --install build-release --prefix /usr/local
```

This installs the binary, the launcher (`io.github.Insapiable_Inc.namx.desktop`, with `.nam`
and `.nampreset` file associations), the AppStream metadata and the icon theme files.
Run `update-desktop-database` / `update-mime-database` afterwards if your distro
doesn't hook them.

Prebuilt Linux build: an AppImage on the releases page (glibc 2.39+; it offers to add
itself to the applications menu on first start; Settings → Remove from Applications
Menu undoes it; no `libjack` inside). There is no distribution package;
Arch users build from source as above.

Command-line flags: `namx [file…]` opens `.nam` / `.wav` / `.nampreset` files;
`--version` / `-v`; `--help` / `-h`; `--self-test-and-exit` (headless CI check: window
up, library scanned, exit 0 after 1.5 s). An option the app doesn't know is an error.

### Windows — quick build (PowerShell)

From a "Developer PowerShell for VS 2022" prompt:

```powershell
.\build.ps1                                          # Release
.\build.ps1 -BuildType Debug                         # Debug
.\build.ps1 -QtDir 'C:\Qt\6.7.0\msvc2019_64'         # custom Qt
.\build.ps1 -Generator Ninja                         # Ninja for faster builds
```

Binary lands at `build-release\Release\namx.exe`. `windeployqt`
runs automatically at the end of the build to copy Qt DLLs next to the .exe, plus
`vc_redist.x64.exe` (the Visual C++ runtime). A machine without that runtime
fails to start `namx.exe` silently ("VCRUNTIME140_1.dll was not found" at best): run
the redistributable once. The release zip carries it too.

> If you see *"Could not find a package configuration file provided by Qt6"*,
> pass `-QtDir 'C:\path\to\Qt\6.x.x\msvc2019_64'`. The script forwards it to
> CMake as `-DCMAKE_PREFIX_PATH`.

### Windows — manual CMake

```powershell
cmake -S . -B build-release `
      -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64"
cmake --build build-release --config Release --parallel
& "C:\Qt\6.7.0\msvc2019_64\bin\windeployqt.exe" --compiler-runtime .\build-release\Release\namx.exe
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
| `-DNAM_BUILD_TESTS=ON` | OFF | `MidiMapperTest`, `DspGoldenTest`, `WavReaderTest`, `NamProcessorTest`, `LibraryIndexTest`, `ThemeFileTest` |
| `-DNAM_BUILD_BENCH=ON` | OFF | `namx-bench`, the chain benchmark (see Troubleshooting → High CPU usage) |
| `-DNAM_WERROR=ON` | OFF | Compiler warnings are errors (CI and `build.sh` turn it on) |

---

## 4. Run

### With jackd (classic JACK)

```bash
# Start JACK daemon (44.1 kHz, 256-frame buffer, ALSA backend)
jackd -d alsa -r 44100 -p 256 &

# Launch namx
./build-release/namx
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
./build-release/namx
```

PipeWire exposes a JACK-compatible socket automatically.
Use **qpwgraph** or **Carla** to patch ports visually.

### With QjackCtl

1. Open **QjackCtl**, configure your interface, click **Start**.
2. Launch `namx`.
3. Use the **Graph** or **Connect** panel to wire ports.

---

## 5. Connect audio ports

namx registers three JACK ports (mono in, stereo out):

| Port | Direction |
|------|-----------|
| `NAM-X:input`    | Audio input (mono) |
| `NAM-X:output_L` | Audio output, left |
| `NAM-X:output_R` | Audio output, right |

On first run the app connects `system:capture_1` → `input` and `output_L/R` →
`system:playback_1/2` itself; connections you change afterwards are remembered.

### Command-line connection

```bash
# Guitar interface → namx → speakers
jack_connect system:capture_1    NAM-X:input
jack_connect NAM-X:output_L  system:playback_1
jack_connect NAM-X:output_R  system:playback_2
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
| Scroll wheel | One step per notch: the knob's own step size, or 1 % of its range |
| Double-click | Reset to the knob's default |

### Parameters

| Knob | Range | Center freq / type |
|------|-------|--------------------|
| **Input** (column) | ±20 dB | Gain into the chain |
| Amplifier **Input** | ±20 dB | Level into the model: more or less breakup |
| Amplifier **Output** | ±20 dB | Trim on top of the loudness normalization (models that state their loudness are brought to −18 dB) |
| **Bass** | ±12 dB | Low shelf, 40–500 Hz (default 250) |
| **Middle** | ±12 dB | Peak, 200 Hz–2.5 kHz (default 1 kHz, Q 0.7) |
| **High-Mid** | ±12 dB | Peak, 800 Hz–8 kHz (default 3 kHz, Q 1) |
| **Treble** | ±12 dB | High shelf, 1.5–12 kHz (default 4 kHz) |
| Cabinet **Level** | ±20 dB | The cabinet's level |
| **Output** (column) | ±20 dB | Gain after the chain, before the limiter |

Gain changes are sample-accurate smoothed (no clicks or zipper noise).

### Presets

The bar above the rack holds the current preset. A preset recalls every effect
setting, the effect order, the input/output gains, and the model and IR (by path).

| Control | Action |
|---------|--------|
| Name (click) | Pick a preset from the library; folders show as submenus |
| **‹ ›** | Previous / next preset in the library's Presets order |
| `*` after the name | The sound differs from the saved file. It goes away when you save, or when you turn everything back |
| **Save** (`Ctrl+S`) / **Save As…** (`Ctrl+Shift+S`) | Overwrite the current file / write a new one (an untitled state always asks) |
| **Slots 1–8** (`Ctrl+1…8`) | Load the preset stored in a slot. Clicking an empty slot stores the current preset there; right-click to store or clear |
| **A / B**, **A→B** | Two working states of the current preset: switch between them to compare, or copy the active one over the other. Loading a preset clears both |
| **⋯** (same as the **Preset** menu) | Open…, Import… (copies files into the presets folder), Export… (writes a copy elsewhere), Show in Folder, Delete… (to the trash), Reset All to Defaults… (every effect, the gains, the chain order, the tempo and metronome, the tuner reference; the model and IR stay) |

**Not part of a preset:** audio devices and port connections, the tuner's reference
pitch (its On and Mute switches are saved, like every rack control), the tempo and the
metronome, MIDI mappings and devices, the library state, window layout, and the
quick-slot assignments. These belong to the machine or the session, so loading a preset
never changes them.

**Starter presets.** On first run eight starter presets are copied to
`<Documents>/NAM-X/Presets/Starter` (Clean Chorus, Tight Rhythm, Lead Delay,
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
| **Click / Wood / Beep** | Sound set |
| **Count-in** | 0–2 bars counted in when you press play; **Silent after count-in** keeps only the lights running, to check your own timing |
| **Level / Pan** | The click's own volume and position. It is mixed in after the Output knob and isn't shown on the output meter |

**Tempo sync.** Delay **Time**, the Tremolo / Chorus / Flanger / Phaser **Rate** and
the Reverb **Pre-delay** each have a **Sync** switch: on, the knob picks a note value (4 bars … 1/32, dotted and
triplets) instead of ms / Hz, and follows the tempo. Delay times are capped at 3000 ms.

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

Modes: **Absolute** (the CC sets the value; *Min/Max* — in the knob's own unit (st, %, dB, Hz) — limit the sweep, e.g. an
expression pedal over part of a knob), **Relative** (endless encoders: 1–63 up,
65–127 down), **Toggle** (each press flips a switch or fires an action; the default for
footswitches), **Momentary** (on while held). Learn picks Absolute for a knob moved by a
CC and Toggle for everything else.

**One control, many targets.** Learning a control replaces only *that control's*
previous binding, so the same knob, pedal or footswitch can be learned on several
controls and drives them all at once. A footswitch learned on two effects flips both;
if one starts off and the other on, each press alternates between them. On an
expression pedal, setting **Min** above **Max** in Mappings… reverses that row's
sweep: volume 0→100 % and wah 100→0 % on one pedal crossfade the two.

**Reverse** on the Transpose, Dynamic Wah and Volume Pedal cards flips the pedal for
every controller mapped to Semitones / Position / Volume (heel and toe swap), on top of
each mapping's Min/Max. It's saved in presets; the knob always shows the real value.

Out of the box, program changes 0–7 recall quick slots 1–8. The mappings are
machine settings (QSettings `midi/*`), not part of presets.

### Settings

**Settings → Settings…** (`Ctrl+,`) collects the machine settings in one window; every
change applies immediately.

| Page | What's there |
|---|---|
| Audio | Input / output device (same as the status bar's In: / Out:), the server's sample rate, buffer and round-trip latency (read-only: PipeWire/JACK own them), xruns + reset, **Normalize models** (loudness normalization, on by default), **Exclusive mode** (Windows) |
| MIDI | Input devices on/off, Follow MIDI clock, **Mappings…** |
| Library | Model / IR / preset folders, rescan |
| Tuner | Reference pitch, show the detected frequency (also on the tuner card) |
| Appearance | Theme: **Follow the system** (light or dark and the accent colour, live), **Dark** / **Light** (KDE's Breeze Dark / Breeze Light), or a theme file from the themes folder; **Import…** copies a file there, **Open Folder** shows it (see [File formats](#8-file-formats)) |
| Shortcuts | Every keyboard shortcut, read from the window (**Settings → Keyboard Shortcuts…**) |

### Library

The **Library** dock (left of the rack; `Ctrl+L` shows/hides it) lists every `.nam`
model, `.wav` IR and `.nampreset` preset found under the configured folders
(**Library → Folders…**, which opens the Library page of **Settings**; defaults: `~/NAM` for models and IRs, `~/Music/IRs`, and
`~/Documents/NAM-X/Presets`). Folders are scanned recursively on a background
thread, metadata is cached, and the list follows changes on disk.

| Control | Action |
|---------|--------|
| **Models / IRs / Presets** | Switch section |
| Search field (`Ctrl+F`) | Filter as you type (name, folder, metadata) |
| **All / ★ / Recent** | Everything, favorites only, or recently used — each section (Models, IRs, Presets) remembers its own |
| **Name / Newest** | Sort order, per section too |
| Click, arrows, PgUp/PgDn, Return | Load the entry under the cursor |
| ★ on a row | Toggle favorite |
| **Folders…** / ↻ | Configure the scanned folders / rescan now |

The **Amplifier** and **Cabinet** rows show the loaded file with its metadata
(architecture, sample rate, gear; rate, channels, length). **‹ ›** step through the
library in its current visible order; clicking the file name focuses the matching section. Files can
also be dropped onto the window, imported through **Library → Import…**, or unloaded
from the row's right-click menu. The **On/Off** toggle bypasses the stage without
unloading it.

---

## 7. Signal chain

Default order (drag the ≡ grips to reorder any effect). The Volume Pedal sits first in
the effects loop, after the amp, so it swells the level without changing the amp's gain:

```
input ─▶ Input Gain ─▶ Tuner ─▶ Transpose ─▶ Noise Gate ─▶ Compressor ─▶ Wah ─▶ Flanger ─▶ Phaser ─▶ Overdrive
      ─▶ Amplifier (NAM) ─▶ Volume Pedal ─▶ Equalizer ─▶ Chorus ─▶ Tremolo ─▶ Delay ─▶ Reverb
      ─▶ Cabinet (IR) ─▶ Output Gain ─▶ (+ metronome) ─▶ Limiter ─▶ output L/R
```

The chain starts mono. Effects in stereo mode (Delay and Reverb by default; Chorus,
Tremolo, Flanger and Phaser when their **Stereo** switch is on) make it stereo from
there on. A stereo signal reaching the Amplifier runs the model once per channel, at
twice the CPU — it is never summed to mono.

| Stage | Main controls | ▾ More |
|-------|---------------|--------|
| Transpose | Semitones (±12), Mix; **Reverse** (pedal heel ↔ toe), **Continuous** switch (off = Discrete: whole semitones; on = smooth whammy-style glides, the shifter then stays active at 0 st) | — |
| Overdrive | Drive, Tone, Level — a Tube Screamer circuit model (the op-amp + diode clipper as a wave digital filter, oversampled) | — |
| Amplifier | On/Off, ‹ › through the library, Input, Output (see Parameters); the row shows the model and its metadata | — |
| Dynamic Wah | **Manual** switch (off = Auto: your playing sweeps it), **Reverse** (pedal heel ↔ toe), Mix, Sensitivity, Resonance, Position (the sweep in Manual mode — map an expression pedal) | — |
| Flanger / Phaser / Chorus | Mix, Rate (Sync), Depth, Feedback (not Chorus; Phaser up to 90 %); Flanger **Range**. The Phaser is an MXR Phase 90 model: Depth 50 % and Mix 50 % are the pedal, Feedback 0 % the original "script" circuit | — |
| Equalizer | Bass, Middle, High-Mid, Treble (±12 dB) | the four band frequencies, Low Cut, High Cut |
| Delay | Mix, Time (Sync), Feedback (ping-pong in stereo) | R Time, Low/High Cut, Saturation, Shimmer (octave-up repeats) |
| Reverb | Type (Plate / Hall / Room), Mix, Decay (0.2–10 s), Damping, Width | Pre-delay (0–250 ms; with the main row's **Sync** on, a note value at the tempo, up to 2 s), Low/High Cut, Shimmer (octave-up feedback into the tail) |
| Cabinet | On/Off, ‹ › through the library, Level; the row shows the IR and its metadata | — |
| Volume Pedal | Volume, Mid (level at half travel), **Reverse** (pedal heel ↔ toe) | — |
| Output | Gain, **Limit** (look-ahead limiter at −0.3 dBFS) | — |

Times, rates and frequencies use logarithmic knobs. Transpose uses a filter-bank pitch
shifter: while it's active it adds about 20 ms of latency in the bass (low E string) and
about 4 ms higher up, where the pick attack is.

---

## 8. File formats

| Format | Details |
|--------|---------|
| `.nam`  | Neural Amp Modeler model file. Supported architectures: WaveNet, LSTM, ConvNet, Linear. Version 0.5–0.7 supported. |
| `.wav`  | Impulse response / cab simulation. Mono; PCM 16/24/32-bit or float 32/64-bit. Any sample rate. Up to 1 s is used; a longer file is faded out over its last 10 ms there. |
| `.nampreset` | Preset, JSON (schema v2): `version`, every effect group (`noiseGate`, `compressor`, …) with its parameters, `effectOrder` (stable effect IDs), `namPath` / `irPath` (older files may carry a `tone3000` object; it is ignored). An effect group missing from the file loads at its defaults, as does a missing key inside a group; a missing `effectOrder` loads the default order. |

### Theme files

Themes live in the themes folder: `~/.config/Insapiable/namx/themes` on Linux,
`%LOCALAPPDATA%\Insapiable\namx\themes` on Windows (**Settings → Appearance → Open
Folder**). Two formats are read:

- **KDE colour schemes** (`.colors`), as they are — the ones in
  `/usr/share/color-schemes` or `~/.local/share/color-schemes`, or downloaded from the
  KDE Store. The window, view, button and selection colours are used.
- **JSON** (`.json`): only `window` and `text` are required.

```json
{
  "name": "Nord",
  "window": "#2e3440",
  "text": "#eceff4",
  "base": "#3b4252",
  "button": "#434c5e",
  "accent": "#88c0d0",
  "effects": { "nam": "#ebcb8b", "ir": "#bf616a" }
}
```

| Key | Used for | When missing |
|---|---|---|
| `window` | Window background | required |
| `text` | Text | required |
| `base` | Lists, fields | `window` |
| `button` | Effect cards, buttons | `window` |
| `accent` | Selections, the Input/Output knobs, the tempo bar | Breeze blue `#3daee9` |
| `shadow`, `disabledText` | Shadows, disabled controls | derived from `window` / `text` |
| `effects` | Per-effect colour, by effect ID: `tuner`, `transpose`, `gate` (Noise Gate), `comp` (Compressor), `wah`, `flanger`, `phaser`, `drive` (Overdrive), `nam` (Amplifier), `volume` (Volume Pedal), `eq`, `chorus`, `tremolo`, `delay`, `reverb`, `ir` (Cabinet) | NAM-X's own |

Colours are `#rrggbb` (or SVG names such as `"red"`). Every other tone (hover, borders,
knob bodies, LED panels) is derived from these, light or dark depending on `window`.
Effect colours that would be hard to read on the cards are lightened (dark themes) or
darkened (light themes) until they stand out. A file with an error (a misspelt key, an
unknown effect, a bad colour) isn't loaded; Settings lists it with the reason. The theme
in use reloads each time its file is saved.

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

- Confirm the file is a valid `.nam` file (JSON with weights); the dialog says what
  was wrong (a truncated weights list, an unsupported architecture, more than one
  channel, a stated sample rate outside 8–384 kHz, a file over 256 MB).
- Check that the file version is between 0.5.0 and 0.7.0.

### Failed to load IR

- Only `.wav` files are supported (not `.flac`, `.mp3`, etc.).
- The file must contain at least one audio channel with non-zero samples.

### No audio output / very quiet output

- Check JACK port connections (`jack_lsp -c`).
- Verify input and output gain knobs are not zeroed (double-click to reset).
- The Amplifier and Cabinet rows' **On** switches bypass them when off.

### High CPU usage

- The status bar's **DSP %** is the audio thread's time per buffer as a share of
  the buffer's duration (highest in the last half second); **xruns** counts dropouts.
  Click either to reset.
- Increase the JACK buffer size (`-p 512` or `-p 1024`).
- Use a Release build (`./build.sh Release`), not Debug.
- A model always runs at the rate it was trained at (usually 48 kHz). With the audio
  device at another rate (44.1 or 96 kHz) the Amplifier resamples around it, which
  costs roughly 30 % more amp CPU and ~1 ms; running the device at 48 kHz avoids both.
- Large NAM models (e.g., standard WaveNet) are CPU-heavy by design. A stereo signal
  reaching the Amplifier (a stereo effect before it) runs the model twice, once per
  channel — the amp never folds the two sides to mono.
- With the `powersave` CPU governor, cores idle at low clock between audio buffers,
  so DSP % reads several times higher than the benchmark below. `performance`
  (or `cpupower frequency-set -g performance`) trades power for headroom.
- **Laptops:** plug in and pick the performance power mode — battery-saver clocks are
  the usual cause of crackles, more than the CPU itself; thin laptops also throttle
  after a few minutes of load. If it still crackles, use 256 frames, or move stereo effects after the Amplifier.
  For scale: every effect on in stereo, the amp per channel, measured ~10 % of the
  128-frame budget on average on an i9-12900K (2026-10-03); a 2017-era laptop CPU is
  roughly twice that, the slowest AVX2 laptops (2014 low-voltage) about four times.

**Measuring.** `namx-bench` times every effect in chain order, block by block,
against the real-time budget:

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DNAM_BUILD_BENCH=ON
cmake --build build-bench --target namx-bench
build-bench/namx-bench --model amp.nam --ir cab.wav   # --frames 128 --rate 48000 by default
```

`namx --self-test-and-exit` prints the cold-start time and peak memory.

**Budget** (checked with the tools above): the default chain with a standard
WaveNet and an IR stays under **10 %** of a 128-frame block on one core; the idle UI
does no periodic repaints (meters repaint only when they move and pause while the
window is minimized); the window is up in **< 1 s**. Reference numbers (i9-12900K,
128 frames @ 48 kHz, standard WaveNet + 2048-tap IR, every effect on): **4.1 %**
(amp 2.5 %, cab 1.1 %, all other effects together 0.4 %); cold start 135 ms; peak
RSS 170 MB.

### Submodule build errors (missing eigen / NAM/dsp.h)

```bash
git submodule update --init --recursive
```
