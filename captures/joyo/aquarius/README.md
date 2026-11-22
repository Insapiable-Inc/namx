# JOYO R-07 Aquarius — Low Bit delay

A digital multi-delay and looper (eight delay modes, a 5-minute looper, tap tempo; one input,
one output, 9 V centre negative). Measured: the **Low Bit** mode only, on 2026-11-02 — one
session, 25 takes, `plans/aquarius-low-bit.json` (Level, Feedback, Time at 7:00 / 9:00 /
12:00 / 3:00 / 5:00; Mode on Low Bit, looper off, no tap tempo). Everything below is
re-measured by `tools/capture/analyze_aquarius.py` (40 s), which writes
`2026-11-02-low-bit/fit/aquarius-low-bit.json`.

## In one line

**"Low Bit" is a low sample rate, not a low bit depth.** The repeats are held at 5512.5 Hz
(the pedal's 44.1 kHz ÷ 8) with no anti-alias filter in front, inside a pre-/de-emphasis pair
that makes the folded-down aliases ~15 dB louder than the passband would; the bit depth stays
above ~13 bits. The grit is aliasing.

```
in ─▶ pre-emphasis ─▶ ADC 44.1 kHz ─┬──────────────────────────────────────────────┬─▶ DAC ─▶ de-emphasis ─▶ out (inverted)
     (zero ~1.2 kHz)               │                                              │        (pole ~1.2 kHz)
                                   └─▶ keep every 8th sample, hold ─▶ delay ─┬──▶ × Level ─┘
                                        (no filter: aliasing)          ▲     │
                                                                       └ × Feedback (flat, digital)
```

## The numbers

| | |
|---|---|
| **Dry** | digital: 45 samples (0.94 ms) late, −0.1 dB, flat to −0.3 dB at 10 kHz, −1.0 dB at 16 kHz |
| **Polarity** | the whole pedal inverts: dry and repeats alike (they stay in phase with each other) |
| **Hold** | 5512.68 Hz measured = 44.1 kHz ÷ 8 with the pedal's clock 32 ppm fast against the interface; a held 1031 Hz comes back with its mirror image at 4481.7 Hz |
| **No anti-alias filter** | single clicks: repeats 18 dB apart from click to click (the dry 9 dB): each click lands somewhere on the 8-sample grid, and a sample either catches it or not |
| **The repeat's tone** | a pure zero-order hold: sinc(f / 5512.5) to within ~1 dB up to 10 kHz (re bypass: −1.4 dB at 100 Hz, −2.8 at 1.6 kHz, −7.2 at 3.2 k, −20 at 5 k, a lobe back up to −15 at 8 k); an input above 2756 Hz comes out mirrored |
| **Emphasis** | first-order, zero ≈ 1.2 kHz before the converter, its inverse after. Linear signals cancel through it (hence the plain sinc); aliases don't: input at f is boosted, its alias at a low frequency isn't cut. Seen in the hold's images (−41.3 dB re a 211 Hz repeat, where the hold alone gives −28) and the sweep's aliases (+10.4 dB at 4.6 kHz → 0.9 kHz; a 1.2 kHz zero says +10.0). Probably analog around the converters: the dry clips at high frequencies when the input is hot (a sweep at the guitar's peak lost 2 dB at 1.6 kHz and 14 dB at 16 kHz in the dry) |
| **Bit depth** | nothing audible. Beside a 211 Hz repeat sits a constant −37.8 dB from −36 to +3 dB input: the hold's mirror images, linear. Harmonics −72 to −100 dB. At Level 12:00 the same (the repeat 5.9 dB lower) |
| **Time** (first repeat, incl. the 0.94 ms) | 7:00 46.3 ms · 9:00 324.4 · 12:00 1038.6 · 3:00 1740.0 · 5:00 1996.2 ms — linear from 9:00 to 3:00 (2.36 s per full turn), flatter at both ends. The LED blinks faster counter-clockwise (user). A customer review on a retailer's page puts the longest delay at ~3.5 s; Low Bit stops at 2.0 s |
| **Feedback** (per pass, flat in frequency) | 7:00 none · 9:00 −16.4 dB · 12:00 −5.9 · 3:00 −1.38 · 5:00 −0.01 (holds for ever, never grows) |
| **Level** (repeat at 100–200 Hz re bypass) | 7:00 off · 9:00 −18.1 dB · 12:00 −7.3 · 3:00 −2.6 · 5:00 −1.4 |
| **One taper** | Level (re its 5:00) and Feedback as gains: 9:00 0.146 / 0.151, 12:00 0.506 / 0.507, 3:00 0.861 / 0.853, 5:00 1 / 0.999 — the same law |
| **The loop** | digital: each pass a flat gain — the hold isn't inside the loop (a hold per pass would take another 3 dB at 2.5 kHz each time; the loop isn't a whole number of holds), so it acts once, before or after it (drawn before); the loop time = first repeat − 0.94 ms. At Feedback 5:00 a third harmonic 64 dB under the repeat grows ~1.5 dB per pass (requantization, inaudible) |
| **Noise** | −80.4 dB rms re the guitar's peak with the pedal on (Level 5:00, Feedback 12:00), mostly 250 Hz–2 kHz; −95.7 bypassed |

## The model, checked

The structure above with the hold at 5512.68 Hz (sampled at fractional times, no filter),
pre-emphasis zero at 1.2 kHz, first-order low-passes at 11 kHz before the hold and 13 kHz on
the repeat (fitted on the sweep), the measured loop gain and delays:

- **The Time 5:00 sweep** (its repeat alone, after the 2 s sweep), third octaves per 0.1 s
  within 30 dB of the moment's strongest band: median 0.12 dB off while the sweep is under
  half the hold rate, **0.63 dB in the aliasing** above it (rms 4.1 / 2.2: a few cells where
  the sweep enters a band mid-chunk).
- **The low-E DI** (`reamp-lowe`, Level 5:00, Feedback 12:00, Time 12:00), dry taken from the
  measured dry response: the model's repeats correlate 0.998 with the pedal's, the residual
  24.5 dB under them; per octave within 0.1 dB to 1.6 kHz, −0.5 / −0.7 / −1.0 dB at 3.2 /
  6.4 / 12.8 kHz; **the whole output per third octave every 0.5 s: 0.26 dB rms.** The hold
  grid's phase hardly matters on a guitar (most of its energy sits where the hold is
  transparent).

A by-ear pair from it (pedal, model, one gain) was written to the session scratchpad
(`listen/aquarius-lowbit-{reamp,wet}-{1-pedal,2-model}.wav`); regenerate from the script's
model functions if gone.

## Method notes

- **`marker_to_pedal: false`** (the marker's repeat would land on the signal): the pedal
  got the played file with everything before `signal_start` silenced, but `played/` keeps the
  loopback's marker. Zero it before using the file as the pedal's input — a first pass that
  didn't put the marker's repeat into the model and an extra 25 dB at 1.6–3.2 kHz into
  the first 4 s.
- **Ladder with the repeat alone:** 0.25 s bursts every 3 s at Time 12:00 (the repeat at
  1.04 s lands in the gap); the dry and its own repeat never overlap.
- **A constant "SNR" isn't quantization:** the residual beside the repeat fell with the level,
  6 dB per 6 dB — a companded quantizer would look like that too, so the residual's lines told
  it apart: the hold's images at 5512.5 ± 211 Hz, not harmonics.

## Not measured / open

- The other seven delay modes, tap tempo, trails, the looper.
- The Time knob between the five positions (assumed smooth).
- The hot-input clip (pre-emphasis into the converter): only seen in the 0 dB sweep's dry;
  not modelled. A guitar's highs are far below a full-level sine sweep's.
- The emphasis corners are fitted from aliasing (±0.6 dB median); the 13 kHz output
  low-pass is on the repeats only (the dry is flat to 10 kHz).

## In NAM-X

**Low Bit**, a standalone effect (user, 2026-11-02: the sound without the delay), branch
`dsp/lowbit` (`src/dsp/LowBitProcessor.h/cpp`, effect ID `lowbit`, after the Chorus): the
pre-emphasis → 11 kHz low-pass → hold → de-emphasis → 13 kHz low-pass above, Sample Rate
(1–16 kHz, default 5512.5 Hz) and Mix. At the engine's rate the hold is fractional: the held
value is the signal interpolated (Catmull-Rom) at the hold's instant, each step band-limited by
a two-sample polyBLEP. Rendered over this session's takes: the low-E DI's repeats to −24.5 dB
(as the analysis model); the Time 5:00 sweep's cells a median 0.6 dB off below half the hold
rate and 1.7 dB in the aliasing, its aliases 1–1.5 dB louder than the analysis model's
(bilinear pre-emphasis near Nyquist; a two-sample BLEP where the model band-limits with a 4×
FIR) — 2× oversampling the hold would close that if it's heard. Not judged by ear yet.

A Delay type with the pedal's repeats (the loop, the Time map, the shared Level/Feedback taper)
is not done.
