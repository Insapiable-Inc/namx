# Benchmarks

Measured, dated numbers for NAM-X (Phase 27). Every figure says how it was taken so
it can be re-run after a change; deterministic inputs only (fixed signals, real models).

**Machine (2026-10-02):** Intel Core i9-12900K, Linux 7.2.7 (Arch), GCC 16.2, Release
build, `powersave` governor, the benchmark pinned to one performance core (`taskset -c 2`).
The bench runs flat out, so clocks are up regardless of the governor; live DSP % in the
status bar reads higher under `powersave` (see BUILD.md → High CPU usage).

## Amplifier CPU

Mean time per block of the Amp stage (`namx-bench --only Amp`) and its share of the
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

Reproduce: `bench/bench_matrix.py --bench build-bench/namx-bench --cpu 2
--model "Label=path.nam" …`

**Release builds are as fast:** the portable build (`-DNAM_NATIVE_OPT=OFF`, AVX2 + FMA, what
CI ships) is within ~1 µs of `-march=native` on every model at 64 frames.

## Against the upstream NAM core

Same models through upstream's own `tools/benchmodel` (Release, `double` samples, default
flags — the core as it ships upstream), 64-frame blocks at 48 kHz, per block:

| Model | Upstream core | NAM-X | Speed-up |
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
- **Fast tanh** (the core's approximation, enabled by `AudioEngine` since Phase 14 —
  2026-10-01, for CPU: 166 → 67 µs; accuracy wasn't measured then) is what limits
  accuracy: the error is 46–65 dB below the signal. **The official NAM plugin enables
  it unconditionally too** (`NeuralAmpModeler.cpp`, constructor), so this matches the
  official plugin's accuracy. A2/slimmable models use their own activation and are exact.
- Decision (2026-10-02): **fast tanh stays the default**; a fast *and* accurate tanh is a
  Phase 22 item. Exact tanh costs 1.7–2.5× the amp CPU (128 frames, 48 kHz):

| Model | Fast tanh | Exact tanh |
|---|---|---|
| Standard (16+8 ch) | 67 µs · 2.5 % | 167 µs · 6.3 % |
| Lite (12+6 ch) | 79 µs · 3.0 % | 145 µs · 5.4 % |
| 5×10 ch | 184 µs · 6.9 % | 308 µs · 11.5 % |
| 4×8 ch (XR) | 87 µs · 3.3 % | 163 µs · 6.1 % |
| 32+8 ch | 307 µs · 11.5 % | 635 µs · 23.8 % |
| Slimmable (A2) | 60 µs · 2.2 % | 60 µs · 2.2 % |

## Every effect, every routing

Each effect alone (`--only`), 128 frames at 48 kHz, standard WaveNet (16+8 ch) and a
V30 IR, in three routings: mono in with its Stereo switch off; mono in with the switch on
(the six effects that have one); and a stereo signal in (every effect per channel — what
happens after an earlier stereo effect). Settings: defaults, except the worst case the UI
can reach for effects that skip idle work — all six EQ bands engaged, the delay's
modulation and saturation on, Transpose at −2 st.

| Effect | Mono | Stereo switch on | Stereo input |
|---|---|---|---|
| Tuner | 0.1 µs · 0.00 % | — | 0.1 µs · 0.00 % |
| Transpose | 26.8 µs · 1.01 % | — | 54.1 µs · 2.03 % |
| Gate | 0.4 µs · 0.01 % | — | 0.8 µs · 0.03 % |
| Compressor | 1.4 µs · 0.05 % | — | 2.7 µs · 0.10 % |
| Wah | 0.6 µs · 0.02 % | — | 1.2 µs · 0.04 % |
| Flanger | 1.0 µs · 0.04 % | 1.9 µs · 0.07 % | 1.9 µs · 0.07 % |
| Phaser | 1.3 µs · 0.05 % | 1.3 µs · 0.05 % | 1.3 µs · 0.05 % |
| Drive | 0.3 µs · 0.01 % | — | 0.6 µs · 0.02 % |
| Amp | 66.7 µs · 2.50 % | — | 67.3 µs · 2.52 % (summed) · 145.5 µs · 5.46 % (Stereo on) |
| Volume | 0.3 µs · 0.01 % | — | 0.3 µs · 0.01 % |
| EQ | 1.2 µs · 0.04 % | — | 2.3 µs · 0.09 % |
| Chorus | 0.9 µs · 0.03 % | 1.8 µs · 0.07 % | 1.7 µs · 0.06 % |
| Tremolo | 0.4 µs · 0.01 % | 0.7 µs · 0.03 % | 0.4 µs · 0.01 % |
| Delay | 1.2 µs · 0.05 % | 2.1 µs · 0.08 % | 2.0 µs · 0.07 % |
| Reverb | 7.8 µs · 0.29 % (Plate) | — | 8.2 µs · 0.31 % (Plate) |
| Cab | 5.5 µs · 0.21 % | — | 10.8 µs · 0.40 % |
| Limiter | 2.4 µs · 0.09 % | — | 2.4 µs · 0.09 % |

- Everything except the amp and the cab is noise: each under 0.1 % of the budget, stereo
  included. The stereo cost is about twice the mono one, as expected.
- The **cab** doubles with stereo input (two convolutions). Re-measured 2026-10-03 after
  the partitioned FFT convolution (Phase 22) with a 200 ms V30 IR (1960-V30, ~9,600 taps
  at 48 kHz): 5.5 / 10.8 µs. A full 1 s IR (the new cap) costs 19.5 / 39.5 µs — what
  4,096 taps (85 ms, the old cap) cost before: 17.3 / 37.8 µs. Accuracy: −133…−138 dB
  against an exact double-precision convolution, odd host block sizes and mono → stereo
  switches included; 0 heap allocations on the audio thread.
- *(2026-10-03: the amp no longer sums — a stereo input always runs it per channel, the
  "Stereo on" figure; the switch was removed.)* The **amp** with stereo input is summed to mono by default (same cost); its Stereo switch
  runs two models (2.2×).

**Whole chain** — every stage on at once, same settings and model:

| Routing | 128 frames | 32 frames |
|---|---|---|
| app default | 122 µs · 4.6 % (p99 135 µs) | 33 µs · 5.0 % (p99 37 µs) |
| all stereo switches off | 100 µs · 3.8 % (p99 114 µs) | 28 µs · 4.2 % (p99 31 µs) |
| all stereo switches on | 126 µs · 4.7 % (p99 144 µs) | 34 µs · 5.2 % (p99 51 µs) |
| stereo input, amp summed | 129 µs · 4.8 % (p99 156 µs) | 35 µs · 5.2 % (p99 38 µs) |
| stereo input, amp per channel | 207 µs · 7.8 % (p99 232 µs) | 54 µs · 8.2 % (p99 63 µs) |

Reproduce: `bench/bench_effects.py --bench build-bench/namx-bench --cpu 2
--model amp.nam --ir cab.wav`; totals with `namx-bench --model … --ir … [routing flags]`.

## Resampling

A 48 kHz model at another engine rate (NamProcessor, the official plugin's Lanczos
resampler): harmonics 1–5 of a 220 Hz sine at 44.1 and 96 kHz match the 48 kHz render to
0.01 dB; 0 heap allocations on the audio thread over 2000 blocks; ~+25 % amp CPU at
44.1 kHz (table above).

## Effect quality

`namx-quality` (built with `-DNAM_BUILD_BENCH=ON`; `--model amp.nam` adds the amp
column) drives each processor with fixed test signals and measures the output. The amp
column uses the standard (16+8 ch) model with fast tanh, as in the app. These are the
numbers Phase 22 has to beat.

### Aliasing

A sine at −6 dBFS, 2 s; energy *off* the harmonic series of the input (aliases folded back from above Nyquist, as inharmonic tones) relative to the whole output. 48 kHz, no oversampling. Lower is cleaner.

| Input | Drive 100 % | Drive 30 % (default) | Amp (model) |
|---|---|---|---|
| 1000 Hz | -30.8 dB | -137.3 dB | -95.4 dB |
| 3000 Hz | -21.1 dB | -89.7 dB | -59.0 dB |
| 5000 Hz | -14.3 dB | -49.7 dB | -30.6 dB |
| 7000 Hz | -9.0 dB | -43.1 dB | -13.1 dB |

### Transpose

A 220 Hz sine at −10 dBFS, mix 100 %. Pitch error of the spectral peak, artifact level (energy off the target note's harmonics), and latency (input start → the output's 5 ms RMS envelope reaching half its steady level; range over eight onset times).

| Shift | Pitch error | Artifacts | Latency |
|---|---|---|---|
| -12 st | -0.1 cents | -74.0 dB | 28–32 ms |
| -5 st | +0.1 cents | -70.9 dB | 22–23 ms |
| -2 st | -0.0 cents | -67.6 dB | 18–20 ms |
| +2 st | +0.1 cents | -72.5 dB | 16–16 ms |
| +5 st | +0.1 cents | -73.4 dB | 13–14 ms |
| +12 st | +0.0 cents | -75.7 dB | 10–10 ms |

Re-measured 2026-10-03 with the filter-bank shifter (Phase 22), which replaced the WSOLA
one; the Transpose CPU row above too. The old shifter measured −66…−85 dB artifacts for
downward shifts, −34 / −23 dB at +5 / +12 st, and 14–62 ms latency depending on where
the note started.

### Reverb decay

Impulse response at mix 100 %, damping 50 %, width 100 %, band-passed to 500 Hz – 2 kHz (where decay is quoted and heard); RT60 from Schroeder backward integration (T20, extrapolated to 60 dB), against the time the Decay knob shows.

| Decay | Shown | Plate | Hall | Room |
|---|---|---|---|---|
| 0 % | 0.20 s | 0.58 s | 0.35 s | 0.21 s |
| 25 % | 0.53 s | 0.67 s | 0.55 s | 0.50 s |
| 50 % | 1.41 s | 1.55 s | 1.37 s | 1.38 s |
| 75 % | 3.76 s | 3.83 s | 3.64 s | 3.55 s |
| 100 % | 10.00 s | 9.71 s | 9.64 s | 8.97 s |

Re-measured 2026-10-03 for the Phase 22 reverbs (the CPU row above too: the Plate,
the default; Hall ~0.4 % and Room ~0.8 % of a core per channel in the scratch harness).
Freeverb measured 0.19 / 0.25 / 0.39 / 0.83 / 9.14 s at these knob positions.

### Delay modulation

A 1 kHz sine through the delay (250 ms, feedback 0, mix 100 %). The modulation's wanted sidebands sit within ±50 Hz of the note; the energy further away (zipper noise, interpolation artifacts) relative to the total:

| Modulation | Off-note energy |
|---|---|
| 0 % | -124.4 dB |
| 30 % | -63.9 dB |
| 100 % | -63.9 dB |

### EQ response

All six bands engaged (Bass +6 dB @ 250 Hz, Middle −6 @ 1 k, High-Mid +3 @ 3 k, Treble −3 @ 4 k, Low Cut 80 Hz, High Cut 8 kHz), 40 sine probes 30 Hz–16 kHz against an independent Audio EQ Cookbook evaluation: **largest deviation 0.026 dB** (at 16000 Hz).

### Limiter

Ceiling −0.3 dBFS. Worst overshoot over four brutal inputs (sine bursts at +6 and +20 dBFS, +12 dBFS single-sample spikes, a +12 dBFS square): **+0.000 dB** relative to the ceiling (sine burst at +6 dBFS). Sample peaks; inter-sample peaks aren't measured.

**What they say:**
- **Drive aliases hard at full drive** (no oversampling): a 3 kHz sine leaves aliases
  21 dB below the output, 7 kHz only 9 dB. At the default 30 % it's clean below ~1 kHz
  (−137 dB) and modest above. Phase 22's anti-aliased WDF Tube Screamer is the fix.
- **The amp model aliases too** (−59 dB at 3 kHz, −13 dB at 7 kHz): a NAM capture runs at
  48 kHz without oversampling — the same in the official plugin.
- **Transpose is in tune** (±0.1 cents) and clean in both directions (−68…−76 dB) with
  the filter-bank shifter (Phase 22, 2026-10-03), with a steady latency set by the note's
  pitch: ~20 ms at 220 Hz for −2 st, shorter going up, longer for big drops (the bass
  bands are the slow ones). Was: strong artifacts on upward shifts and 14–62 ms latency
  depending on where the note started (WSOLA).
- **Reverb Decay** now shows seconds and means them: within ~10 % in the mids from
  0.5 s up for all three types at the default Damping (Plate can't go below ~0.6 s, Hall
  ~0.3 s; at 100 % Damping short Hall/Room tails come out shorter — the Damping strength
  was kept as approved by ear rather than eased). Was:
  Freeverb's 0–75 % covered only 0.19–0.83 s and the last quarter jumped to 9 s.
- **Delay modulation** is clean (off-note energy −64 dB, the linear interpolation's own
  images); without modulation, −124 dB.
- **EQ** matches the Audio EQ Cookbook to 0.03 dB; the **limiter** never exceeds its
  ceiling (0.000 dB worst overshoot, sample peaks).

## Whole app

Offscreen window (`QT_QPA_PLATFORM=offscreen`), isolated config, audio running through
PipeWire-JACK at 48 kHz / 128 frames, no input signal, `powersave` governor.

| What | Measured |
|---|---|
| Cold start (`--self-test-and-exit`, window shown) | 94–96 ms (3 runs) |
| Peak memory, nothing loaded | 96–105 MB |
| Memory with a standard model + IR loaded | 101 MB |
| Model switch (parse + build + warm both channels) | 13.5 ms (was 26.7 before review item 21) |
| Idle CPU, nothing loaded (process, all threads) | 1.2 % of one core |
| Idle CPU, standard model + IR loaded, silence in | 22.6 % of one core |

- The amp and cab keep running on silence (NAM models carry state), so a loaded rig costs
  CPU even when you're not playing — as in the official plugin.
- That 22.6 % is ~7× the bench's 3.2 % for amp + cab: with `powersave` the cores clock down
  between the short bursts of work (and the audio thread may land on an E-core). The
  `performance` governor should bring it close to the bench figure (not measured: needs
  root). Ideas if it matters: skip the amp/cab after a long silence (careful with tails).
- Not covered: a real compositor's painting cost (offscreen renders without one).

## CPU regression check (CI)

`bench/instructions.py` counts the CPU instructions one 128-frame block costs in each
stage, under valgrind's callgrind: nearly deterministic for a given binary, unlike wall
time on shared CI runners. Each stage runs for 50 and 150 blocks and the difference is
taken, so startup (model load) cancels out. The amp uses the repo's
`example_models/wavenet_a1_standard.nam`; the cab a small IR the script writes into its own
temporary directory. Baselines are per compiler (`bench/instructions-baseline.json`);
CI (`linux.yml`) fails when a stage grows more than 3 %, and only reports until its
compiler (GCC 14 on Ubuntu 24.04) has an entry — record one with `--update` on that
toolchain. Repeat runs agree within ±0.3 %; a deliberate +10 % fails. The tuner is left
out: its pitch detection runs on a worker thread woken by wall time.

Each figure includes ~9,000 instructions of the bench's own per-block work (test signal,
timing). GCC 16.2 baseline (2026-10-02): amp 1.73 M instructions/block, cab 108 k, every
other effect 9–67 k, whole chain 2.20 M.

## Still to measure (Phase 27)

Measured round-trip latency with a loopback cable
(needs `jack_iodelay`, package `jack-example-tools`).
