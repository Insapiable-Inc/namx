# Boss DD-8 Digital Delay — Standard mode

Captured 2026-10-26, session [`2026-10-26-standard/`](2026-10-26-standard/): mono (Input A
→ Output A, nothing in the B jacks), MODE on Standard, a 9 V power supply, the same rig
and playback level as the TR-2 ([`tools/capture/README.md`](../../../tools/capture/README.md)).
Measurements: `tools/capture/analyze_dd8.py captures/boss/dd-8/2026-10-26-standard` →
[`fit/dd-8-standard.json`](2026-10-26-standard/fit/dd-8-standard.json) and the figure below.

The question was whether Standard mode does anything besides repeating the signal. It
does one thing: **the repeats hard-clip about 3 dB below a hot guitar's peaks**, while
the dry signal stays clean. Everything else is a textbook digital delay.

![DD-8 Standard: repeats and the wet path's clip](2026-10-26-standard/fit/dd-8-standard.svg)
![DD-8 Standard: the knobs](2026-10-26-standard/fit/dd-8-standard-knobs.svg)
![DD-8 Standard: tone, clip, Feedback 5:00, no modulation](2026-10-26-standard/fit/dd-8-standard-details.svg)

## The model

```
y = x_dry + Level · LP10k( delay_T( clip(x) + F · repeats ) )
```

- **Dry path:** analog, untouched. It arrives at the same sample as bypass, at unity gain
  (within 0.2 dB), and stays clean at +3 dB over the guitar's peak. At Level 7:00 the
  output is the dry path alone.
- **Clip:** before the delay and before the Level knob (the converter's input, most
  likely): a hard ceiling about **3 dB below the reference** (the hardest bridge-humbucker
  peak, `rig.json`). Below it the wet path is linear (3rd harmonic ≈ −80 dB); at the
  reference the repeat loses 2.5 dB and has a 3rd harmonic at −18 dB, at +3 dB −15 dB. So
  how hard you play, not where Level sits, decides whether the repeats distort. No
  compressor: the wet gain is the same at the start, middle and end of a 0.3 s burst and
  follows a level step within 2 ms.
- **Delay time** (Time knob, Feedback at minimum = one repeat):

  | Time | 7:00 | 9:00 | 12:00 | 1:00* | 2:00* | 3:00 | 5:00 |
  |---|---|---|---|---|---|---|---|
  | fraction of the rotation | 0 | 0.2 | 0.5 | 0.6 | 0.7 | 0.8 | 1 |
  | delay (ms) | 19.65 | 169.62 | 385.62 | 469.62 | 525.60 | 582.60 | 799.60 |

  \* set approximately. Close to linear up to 3:00 (56–84 ms per 10 % of rotation), then
  the last stretch to 5:00 covers twice that (108 ms per 10 %): use the points, linear in
  between, not a formula. 3:00 set a second time gave 582.60 ms again, to the sample, so
  the bend is the knob's, not the placement's. Repeats land exactly on multiples of the
  delay (to the sample over 7 repeats).
- **Wet level** (Level knob), the first repeat against Level 5:00, which is **+4.46 dB
  over the dry** (bursts):

  | Level | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
  |---|---|---|---|---|---|
  | dB re 5:00 | no repeat | −33.6 | −13.4 | −2.6 | 0 |

  A steep taper; the bursts at 12:00 agree (−14.9 dB re 5:00).
- **Feedback**, each repeat against the one before, at 1 kHz (from −12 dB sweeps):

  | Feedback | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
  |---|---|---|---|---|---|
  | dB per repeat | one repeat only | −12.2 | −7.07 | ≈ −0.6 | **≈ 0: the repeats never fade** |
  | extra loss at 4 / 8 kHz | | −0.3 / −1.4 | −0.4 / −1.4 | −0.8 / −0.9 | |

  The first repeat's level doesn't depend on Feedback. Over 9:00–3:00 the loss per repeat
  is close to linear in dB (5–6.5 dB per 30 % of rotation); at 7:00 the loop is off. At
  **5:00 the loop gain is unity**: a −12 dB test click's repeats stayed level for 15
  repeats (+0.02 dB per repeat) — a hold, not a runaway. (The take also shows tall pulses
  at the clip's ceiling: the start marker's echoes, which played at the reference level,
  clipped going in, and then circulated without fading. First read as self-oscillation;
  corrected the same day.)
- **Knob curves, in short:** Time near-linear with a steeper top end; Level an audio
  (logarithmic) taper — −33.6 / −13.4 / −2.6 dB at 9:00 / 12:00 / 3:00 where a linear
  pot would give −14 / −6 / −1.9 dB (amplitude ≈ position^2.2–2.4 in the lower half);
  Feedback steady in dB across the middle, levelling off at unity at 5:00. The end values
  are exact (the knob's stops); the in-between fractions assume stops at 7:00 and 5:00
  (300°). How far a by-eye setting can be off: "Time 12:00" set twice gave 385.62 and
  392.96 ms, about 5° apart; 3:00 (pointer horizontal) repeated to the sample.
- **Wet tone:** the first repeat against the dry (−12 dB take, re 1 kHz): flat to 4 kHz
  (±0.6 dB, +1.5 dB at 63 Hz), −2.1 dB at 8 kHz, −29.5 dB at 16 kHz: a low-pass around
  10 kHz. Each pass through the loop adds the
  small high-frequency loss above.
- **No modulation:** a held 1 kHz tone through Feedback 12:00 comes out as one spectral
  line; the only sidebands (±3.25 Hz) are 103 dB down.
- **Noise:** −101 dBFS RMS with Level 5:00 and Feedback 12:00, no signal.

## How it was measured

- **Sweeps with long silent tails, deconvolved** into the impulse response from what was
  played to what came out: the dry impulse at time 0 and every repeat after it, with a
  far better signal-to-noise ratio than a click. 18 takes of
  [`plans/dd-8-standard.json`](../../../tools/capture/plans/dd-8-standard.json): bypass,
  Level 7:00 (dry alone), Time at five positions, Level at three, Feedback at three (8 s
  tails), a −12 dB repeat for linearity, clicks, a held sine, silence; then two more
  Feedback takes at −12 dB, and four click takes: Time at 3:00 set again, 2:00 and 1:00
  (delay read straight off the clicks), and Feedback 5:00 (one −12 dB click, 6 s).
- **The full-level sweeps clip in the wet path**, which first looked like a compressor
  (repeats 6.4 dB lower, relative to the dry, than at −12 dB) and made a spurious "echo"
  at 81 ms in the deconvolution (gone at −12 dB). Their delay times and the relative
  Level map are unaffected (the clip is before both); the absolute wet gain comes from
  the bursts, and the Feedback map from −12 dB sweeps.
- **Bursts and steps** ([`plans/dd-8-dynamics.json`](../../../tools/capture/plans/dd-8-dynamics.json)):
  0.3 s 1 kHz tones at −30…+3 dB with 0.9 s gaps, so each repeat lands in silence: the
  static curve, at Level 5:00 and 12:00. A −24 → 0 → −24 dB step (3 s each), with the
  dry path subtracted using its bypass take (residual −62 dB): no attack or release.
- Knob positions 7:00, 9:00, 12:00, 3:00 and 5:00 (0, 20, 50, 80, 100 % of the
  rotation): 9:00 and 3:00 (pointer horizontal) can be set reliably, 9:30/2:30 can't.
- PipeWire's round trip moved during the session (449 → 583 → 631 → 455 samples); every
  take measures its own from the loopback.

## Implementing it

NAM-X's Delay already has a delay line, feedback, Low/High Cut and saturation. A DD-8
Standard voicing would add: the analog dry path (no change), a hard clip on the wet
input at the reference level − 3 dB, the measured Time range (20–800 ms), the Level
taper with +4.5 dB at maximum, the Feedback map, a ~10 kHz low-pass in the wet path
and a gentle extra high cut in the loop. Not done yet.

## Not measured yet

The stereo outputs (B jacks), what the Time knob does to the repeats while it moves, and
the other modes (Analog, Tape, Mod, Shimmer…).

# Analog mode (preliminary)

Session [`2026-10-26-analog/`](2026-10-26-analog/), plan
[`plans/dd-8-analog.json`](../../../tools/capture/plans/dd-8-analog.json): 14 takes, the
same rig, MODE on ANALOG. Analysed by hand (scratch scripts) on 2026-10-26; no analysis
script yet — the numbers below are from that pass.

| | Standard | Analog |
|---|---|---|
| Delay range (Time 7:00 → 5:00) | 19.65 – 799.60 ms | the same: 19.85 – 799.81 ms (9:00 169.83 ms) |
| First repeat's tone, re 1 kHz (−12 dB) | flat to 4 kHz; −2.1 dB at 8 kHz | **flat to 2 kHz (+0.4); −19.0 dB at 4 kHz, −50 dB at 8 kHz** |
| Repeat level against input level (bursts, Level 5:00) | flat (+4.46 dB) up to the clip | **rises ~2.3 dB per 6 dB**: −6.9 / −4.5 / −2.5 / +0.1 / +1.9 dB at −30 / −24 / −18 / −12 / −6 dB, then +0.4 / −1.2 dB at 0 / +3 dB |
| Distortion of the repeat | H3 ≈ −80 dB, hard clip from ~0 dB | **softer, earlier:** H3 −67 dB at −12, −44 dB at −6, −18 dB at 0; H2 −64 dB from −6 dB; the same ceiling (~−12 dBFS peak) |
| Feedback per repeat, 9:00 / 12:00 / 3:00 (−12 dB) | −12.2 / −7.07 / −0.6 dB | −12.0 / −7.9 / ≈ −1.5 dB (noisier at 3:00) |
| Modulation (held 1 kHz tone) | none (−103 dB) | **none** (nothing above −116 dB) |
| Noise | −101.1 dBFS | −101.2 dBFS |

So Analog is Standard's delay with a much darker repeat (a steep low-pass just above
2 kHz), a level-dependent repeat gain and a softer, earlier saturation — the sound of a
bucket-brigade delay, without its clock wobble. The level-dependent gain is the same
from 5 ms to 280 ms into each burst (it settles in under 5 ms, or is static) and stays
clean at low levels (H3 −77 dB at −30 dB), so it is neither a slow compander nor a plain
waveshaper; possibly a very fast expander. **To do:** a level-step take (as for
Standard) to find its time constants, whether the low-pass sits in the loop (the 4 kHz
level of later repeats), an analysis script, and the knob positions set by hand again —
"Time 12:00" measured 392.83 ms in one take and 385.83 ms in others.
