# Benchmarks

Measured, dated numbers for NAM Player (Phase 27). Every figure says how it was taken so
it can be re-run after a change; deterministic inputs only (fixed signals, real models).

**Machine (2026-10-02):** Intel Core i9-12900K, Linux 7.2.7 (Arch), GCC 16.2, Release
build, `powersave` governor, the benchmark pinned to one performance core (`taskset -c 2`).
The bench runs flat out, so clocks are up regardless of the governor; live DSP % in the
status bar reads higher under `powersave` (see BUILD.md → High CPU usage).

## Amplifier CPU

Mean time per block of the Amp stage (`NAMPlayerBench --only Amp`) and its share of the
real-time budget, fast tanh on (the default). One model per architecture from a real
library. ¹ Not 48 kHz: the model runs at its 48 kHz training rate behind the resampler.

| Model | Rate | 32 fr | 64 fr | 128 fr | 256 fr |
|---|---|---|---|---|---|
| Standard (16+8 ch) | 44.1 kHz ¹ | 23 µs · 3.2 % | 43 µs · 2.9 % | 85 µs · 2.9 % | 173 µs · 3.0 % |
| Standard (16+8 ch) | 48 kHz | 19 µs · 2.8 % | 34 µs · 2.6 % | 67 µs · 2.5 % | 140 µs · 2.6 % |
| Standard (16+8 ch) | 96 kHz ¹ | 13 µs · 3.9 % | 22 µs · 3.2 % | 39 µs · 3.0 % | 78 µs · 2.9 % |
| Lite (12+6 ch) | 44.1 kHz ¹ | 30 µs · 4.1 % | 51 µs · 3.5 % | 97 µs · 3.3 % | 190 µs · 3.3 % |
| Lite (12+6 ch) | 48 kHz | 24 µs · 3.6 % | 42 µs · 3.1 % | 78 µs · 2.9 % | 154 µs · 2.9 % |
| Lite (12+6 ch) | 96 kHz ¹ | 17 µs · 5.0 % | 27 µs · 4.0 % | 47 µs · 3.5 % | 88 µs · 3.3 % |
| 5×10 ch | 44.1 kHz ¹ | 67 µs · 9.2 % | 115 µs · 7.9 % | 216 µs · 7.4 % | 414 µs · 7.1 % |
| 5×10 ch | 48 kHz | 55 µs · 8.2 % | 98 µs · 7.3 % | 183 µs · 6.9 % | 358 µs · 6.7 % |
| 5×10 ch | 96 kHz ¹ | 35 µs · 10.5 % | 57 µs · 8.6 % | 102 µs · 7.7 % | 193 µs · 7.2 % |
| 4×8 ch (XR) | 44.1 kHz ¹ | 32 µs · 4.5 % | 56 µs · 3.8 % | 104 µs · 3.6 % | 204 µs · 3.5 % |
| 4×8 ch (XR) | 48 kHz | 27 µs · 4.0 % | 46 µs · 3.5 % | 85 µs · 3.2 % | 162 µs · 3.0 % |
| 4×8 ch (XR) | 96 kHz ¹ | 19 µs · 5.7 % | 29 µs · 4.4 % | 52 µs · 3.9 % | 92 µs · 3.4 % |
| 32+8 ch | 44.1 kHz ¹ | 96 µs · 13.2 % | 178 µs · 12.3 % | 344 µs · 11.8 % | 682 µs · 11.8 % |
| 32+8 ch | 48 kHz | 81 µs · 12.1 % | 159 µs · 12.0 % | 311 µs · 11.7 % | 621 µs · 11.7 % |
| 32+8 ch | 96 kHz ¹ | 46 µs · 13.7 % | 84 µs · 12.5 % | 165 µs · 12.4 % | 321 µs · 12.0 % |
| Slimmable (A2) | 44.1 kHz ¹ | 22 µs · 3.0 % | 42 µs · 2.9 % | 76 µs · 2.6 % | 141 µs · 2.4 % |
| Slimmable (A2) | 48 kHz | 19 µs · 2.9 % | 34 µs · 2.6 % | 63 µs · 2.4 % | 122 µs · 2.3 % |
| Slimmable (A2) | 96 kHz ¹ | 13 µs · 4.0 % | 22 µs · 3.2 % | 38 µs · 2.8 % | 69 µs · 2.6 % |

Reproduce: `gui/bench/bench_matrix.py --bench gui/build-bench/NAMPlayerBench --cpu 2
--model "Label=path.nam" …`

**Release builds are as fast:** the portable build (`-DNAM_NATIVE_OPT=OFF`, AVX2 + FMA, what
CI ships) is within ~1 µs of `-march=native` on every model at 64 frames.

## Against the upstream NAM core

Same models through upstream's own `tools/benchmodel` (Release, `double` samples, default
flags — the core as it ships upstream), 64-frame blocks at 48 kHz, per block:

| Model | Upstream core | NAM Player | Speed-up |
|---|---|---|---|
| Standard (16+8 ch) | 73 µs | 34 µs | 2.1× |
| Lite (12+6 ch) | 53 µs | 42 µs | 1.3× |
| 5×10 ch | 156 µs | 98 µs | 1.6× |
| 4×8 ch (XR) | 82 µs | 46 µs | 1.8× |
| 32+8 ch | 376 µs | 159 µs | 2.4× |
| Slimmable (A2) | 87 µs | 34 µs | 2.6× |

Note: upstream, Lite is cheaper than Standard (53 vs 73 µs); here it is the other way
round (42 vs 34 µs) — some architectures gain far more from our build than others. A lead
for Phase 22.

## Amplifier accuracy

Null test against upstream's reference renderer (`tools/render`, exact `double` math): the
same 4 s input (a 40 Hz–10 kHz sweep at −10 dBFS, then a re-plucked E chord) through
both, the residual's level relative to the reference output (first 50 ms skipped: the
load fade-in).

| Model | Exact tanh | Fast tanh (default) |
|---|---|---|
| Standard (16+8 ch) | −139.0 dB | −53.2 dB |
| Lite (12+6 ch) | −139.2 dB | −65.4 dB |
| 5×10 ch | −136.8 dB | −46.3 dB |
| 4×8 ch (XR) | −120.3 dB | −49.2 dB |
| 32+8 ch | −117.8 dB | −51.5 dB |
| Slimmable (A2) | −135.4 dB | −135.4 dB |

- With exact tanh the host adds nothing: the residual is float rounding (below 24-bit).
- **Fast tanh** (the core's approximation, enabled by `AudioEngine`) is what limits
  accuracy: the error is 46–65 dB below the signal. **The official NAM plugin enables
  it unconditionally too** (`NeuralAmpModeler.cpp`, constructor), so this matches the
  official plugin's accuracy. A2/slimmable models use their own activation and are exact.
- Exact tanh costs 1.7–2.5× the amp CPU (128 frames, 48 kHz):

| Model | Fast tanh | Exact tanh |
|---|---|---|
| Standard (16+8 ch) | 67 µs · 2.5 % | 167 µs · 6.3 % |
| Lite (12+6 ch) | 79 µs · 3.0 % | 145 µs · 5.4 % |
| 5×10 ch | 184 µs · 6.9 % | 308 µs · 11.5 % |
| 4×8 ch (XR) | 87 µs · 3.3 % | 163 µs · 6.1 % |
| 32+8 ch | 307 µs · 11.5 % | 635 µs · 23.8 % |
| Slimmable (A2) | 60 µs · 2.2 % | 60 µs · 2.2 % |

## Resampling

A 48 kHz model at another engine rate (NamProcessor, the official plugin's Lanczos
resampler): harmonics 1–5 of a 220 Hz sine at 44.1 and 96 kHz match the 48 kHz render to
0.01 dB; 0 heap allocations on the audio thread over 2000 blocks; ~+25 % amp CPU at
44.1 kHz (table above).

## Still to measure (Phase 27)

Effect quality (Drive aliasing, Transpose pitch error / latency, reverb decay, delay
modulation, EQ curves, limiter overshoot), whole-app numbers (cold start, RSS, model-load
time, idle CPU with the window open), measured round-trip latency with a loopback cable
(needs `jack_iodelay`, package `jack-example-tools`), and the CI regression check.
