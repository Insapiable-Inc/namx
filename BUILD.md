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

### Ubuntu / Debian (22.04 or later)

```bash
sudo apt update
sudo apt install \
    git cmake ninja-build \
    qt6-base-dev \
    libjack-jackd2-dev jackd2 qjackctl
```

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
sudo pacman -S git cmake ninja qt6-base jack2 qjackctl
```

### Fedora / RHEL

```bash
sudo dnf install git cmake ninja-build qt6-qtbase-devel jack-audio-connection-kit-devel qjackctl
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
| CMake      | 3.16    |
| Qt         | 6.2 (Linux) / 6.5 (Windows MSVC) |
| JACK       | 1.9.17 (jackd2) or PipeWire-JACK — Linux only |
| GCC / Clang| GCC 11 / Clang 13 (C++20 required) |
| MSVC       | Visual Studio 2022 (17.6+) |

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
| **Low** | ±12 dB | Low shelf at 250 Hz |
| **Mid** | ±12 dB | Peaking EQ at 1 kHz (Q = 0.707) |
| **Treble** | ±12 dB | High shelf at 4 kHz |
| **Output** | ±20 dB | Output gain after IR |

Gain changes are sample-accurate smoothed (no clicks or zipper noise).

### File buttons

| Button | Action |
|--------|--------|
| **Load NAM…** | Open a `.nam` model file |
| **Unload** (model) | Remove the model (audio passes through dry) |
| **Load IR…** | Open a `.wav` impulse response / cab sim |
| **Unload** (IR) | Remove the IR |
| **Enable** checkbox | Bypass the IR without unloading it |

---

## 7. Signal chain

```
┌──────────┐    ┌────────────┐    ┌────────────────┐    ┌──────┐    ┌───────────┐    ┌───────────┐
│ JACK in  │───▶│ Input Gain │───▶│  NAM Model     │───▶│  EQ  │───▶│  IR (cab) │───▶│ Output    │
│          │    │  (±20 dB)  │    │  (WaveNet /    │    │ 3-band    │  FIR conv │    │  Gain     │
└──────────┘    └────────────┘    │  LSTM / etc.)  │    │           └───────────┘    │  (±20 dB) │
                                  └────────────────┘    └──────┘                     └─────┬─────┘
                                                                                           │
                                                                                     ┌─────▼─────┐
                                                                                     │ JACK out  │
                                                                                     └───────────┘
```

If no NAM model is loaded, audio passes through to the EQ and IR unchanged.
If the IR is disabled or not loaded, that stage is skipped.

---

## 8. File formats

| Format | Details |
|--------|---------|
| `.nam`  | Neural Amp Modeler model file. Supported architectures: WaveNet, LSTM, ConvNet, Linear. Version 0.5–0.7 supported. |
| `.wav`  | Impulse response / cab simulation. Mono or stereo (first channel used). Any sample rate. Truncated to 4096 taps (~93 ms at 44.1 kHz). |

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

- Increase the JACK buffer size (`-p 512` or `-p 1024`).
- Use a Release build (`./build.sh Release`), not Debug.
- Large NAM models (e.g., standard WaveNet) are CPU-heavy by design.

### Submodule build errors (missing eigen / wav.h)

```bash
git submodule update --init --recursive
```
