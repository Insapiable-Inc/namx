# NAMPlayer — Build & Usage Guide

NAMPlayer is a standalone Linux desktop application that loads `.nam` neural amp models
and `.wav` impulse responses, processes live audio through JACK, and exposes a 3-band
EQ plus input/output gain controls via a Qt6 GUI.

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

### Minimum versions

| Dependency | Minimum |
|------------|---------|
| CMake      | 3.16    |
| Qt         | 6.2     |
| JACK       | 1.9.17 (jackd2) or PipeWire-JACK |
| GCC / Clang| GCC 11 / Clang 13 (C++20 required) |

---

## 2. Get the source & submodules

```bash
git clone https://github.com/YOUR_FORK/NeuralAmpModelerCore.git
cd NeuralAmpModelerCore

# The GUI depends on two git submodules (Eigen and AudioDSPTools)
git submodule update --init --recursive
```

If you already cloned without `--recursive`, the `build.sh` script calls
`git submodule update --init --recursive` automatically.

---

## 3. Build

### Quick build (recommended)

```bash
cd gui
chmod +x build.sh
./build.sh          # defaults to Release
# or
./build.sh Debug
```

The binary is placed at:

```
gui/build-release/NAMPlayer   # Release
gui/build-debug/NAMPlayer     # Debug
```

### Manual CMake build

```bash
cd gui
cmake -S . -B build-release \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel $(nproc)
```

#### Optional CMake flags

| Flag | Default | Description |
|------|---------|-------------|
| `-DCMAKE_BUILD_TYPE=Release` | Release | `Debug` for symbols, `Release` for `-O3` |

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

NAMPlayer registers two JACK ports:

| Port | Direction |
|------|-----------|
| `NAMPlayer:input`  | Audio input  |
| `NAMPlayer:output` | Audio output |

### Command-line connection

```bash
# Guitar interface → NAMPlayer → speakers
jack_connect system:capture_1    NAMPlayer:input
jack_connect NAMPlayer:output    system:playback_1
jack_connect NAMPlayer:output    system:playback_2   # stereo out
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
