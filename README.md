# NAM-X

A standalone Qt6 desktop application that loads [Neural Amp Modeler](https://www.neuralampmodeler.com)
`.nam` profiles and `.wav` impulse responses, processes live audio in real time, and
exposes a full pedal-style effects chain plus a 4-band EQ with low and high cuts.

It is built on [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore),
the C++ DSP library behind NAM, which this repository pulls in as a git submodule.

| Platform | Audio backend                       | Status                                          |
|----------|-------------------------------------|-------------------------------------------------|
| Linux    | JACK (jackd2 or PipeWire-JACK)      | Stable                                          |
| Windows  | WASAPI shared mode (native, MSVC)   | Functional first cut — float32 mix format only  |

## Features

- Loads `.nam` neural amp models (WaveNet, LSTM, ConvNet, Linear; versions 0.5–0.7)
- FIR cab simulation from any `.wav` impulse response
- Effects chain, freely reorderable by drag-and-drop: Tuner, Transpose, Noise Gate,
  Compressor, Dynamic Wah, Flanger, Phaser, Drive (Tube Screamer), NAM model, Volume
  pedal, 4-band EQ, Chorus, Tremolo, Delay (ping-pong), Reverb, IR cab sim —
  stereo-capable from the first stereo effect onwards
- Library panel: every model, IR and preset under your folders, with search, favorites,
  recents and ‹ › browsing from the Amplifier and Cabinet rows; drag-and-drop loading
- Presets (`.nampreset` JSON) that embed the model and IR paths; full session state
  (knobs, order, devices, connections, window) restored on relaunch
- Sample-accurate gain smoothing, lock-free parameter updates, no allocations on the audio thread

## Install

Linux: build from source (below) or use the AUR-style `PKGBUILD` in `packaging/`;
the app uses the Qt 6 and JACK (jackd2 or PipeWire-JACK) already on your system.
The [releases page](https://github.com/Insapiable-Inc/namx/releases)
carries a source tarball and a Windows zip. Official builds need an x86-64 CPU with AVX2
(Intel 2013+, AMD 2015+); older CPUs can build from source but are unsupported.

## Quick build (Linux)

```bash
git clone --recursive https://github.com/Insapiable-Inc/namx.git
cd namx
./build.sh
./build-release/namx
```

**Arch:** `sudo pacman -S git cmake ninja qt6-base jack2 qjackctl`
**Ubuntu 24.04+:** `sudo apt install git cmake ninja-build g++-14 qt6-base-dev libjack-jackd2-dev libasound2-dev`, then `export CXX=g++-14` (C++23)

On PipeWire systems (most modern distros) the app connects through the JACK
compatibility layer automatically — no `jackd` daemon required.

For Windows builds, detailed prerequisites, port wiring, controls reference, signal
chain, file formats, and troubleshooting, see [`BUILD.md`](BUILD.md).

## Repository layout

```
namx/
├── build.sh / build.ps1   # One-shot builds (Linux / Windows)
├── BUILD.md               # Detailed build & usage guide
├── CMakeLists.txt
├── src/
│   ├── dsp/               # AudioEngine, effect processors, NAM/IR loaders, backends
│   ├── midi/              # MIDI input and mapping
│   └── ui/                # MainWindow, knobs, meters, library, presets, Theme, …
├── tests/ · bench/        # Headless tests; DSP benchmarks
├── resources/ · packaging/
└── external/              # Git submodules: NeuralAmpModelerCore (with Eigen, nlohmann/json,
                           # AudioDSPTools), signalsmith-linear, signalsmith-stretch, chowdsp_wdf
```

Qt 6, JACK/PipeWire and ALSA come from your system.

## Project status

See [`ROADMAP.md`](ROADMAP.md) for the release plan: packaging, an in-app model/IR/preset
library, presets as a live unit, metronome, MIDI, and the rest.

NAM-X is an independent project and is **not affiliated with the official Neural Amp
Modeler plugin**; it is built on Steven Atkinson's
[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore).

## Credits

- Upstream DSP core: [sdatkinson/NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
- Development of version 0.4.0 of the core library was generously supported by [TONE3000](https://tone3000.com).

## License

NAM-X is free software under the **GNU GPL v3 or later**; see [`LICENSE`](LICENSE).
The NeuralAmpModelerCore library stays MIT; see [`LICENSE-MIT`](LICENSE-MIT). Releases
before 2026-10-01 were MIT-licensed. The other submodules keep their own licenses.
