# Boss GE-7 Equalizer

Measured 2026-10-28 on a 9 V supply, mono (Input → Output):

| Session | What it gives |
|---|---|
| [`2026-10-28/`](2026-10-28/) | every band at +15 and −15, 800 Hz and 6.4 kHz also at their half-way marks, neighbours together, every band up and down; flat and bypass references, the clipping with every band up, noise |
| [`2026-10-28-level/`](2026-10-28-level/) | the Level slider at four positions, the clipping and noise with every slider up |

Same rig and playback level as the other pedals ([`tools/capture/README.md`](../../../tools/capture/README.md)).
Plans: `tools/capture/plans/ge-7.json` (33 takes) and `ge-7-level.json` (8 takes), every
slider at 0 unless a take says otherwise. Analysis:
`tools/capture/analyze_ge7.py captures/boss/ge-7/2026-10-28 captures/boss/ge-7/2026-10-28-level`
→ [`2026-10-28/fit/ge-7.json`](2026-10-28/fit/ge-7.json), the figure below and a per-sweep fit figure.

The sliders: 100, 200, 400, 800 Hz, 1.6, 3.2, 6.4 kHz and Level, each ±15 dB on the printed
scale. Positions are read off that scale: +15 = top, 0 = centre, −15 = bottom, ±7.5 =
half-way between the centre and the end.

**Read the first session's `level-*` takes as the 6.4 kHz slider.** Its sidecars say Level,
but the 6.4 kHz slider was the one moved: their curves match the 6.4 kHz takes to 0.2 dB and
are flat below 1 kHz. `analyze_ge7.py` swaps them; the second session has the real Level
takes. (The take names `16kHz`, `32kHz`, `64kHz` are 1.6, 3.2 and 6.4 kHz with the dot
dropped.)

![GE-7: bands, bands together, Level, clipping](2026-10-28/fit/ge-7.svg)

## One circuit fits every sweep

Each sweep against its own session's bypass, per 1/12 octave from 30 Hz to 16 kHz, and one
circuit fitted to all 30 at once — **0.27 dB rms** overall, ≤ 0.2 dB for most single-band
takes:

- **The EQ stage** is the classic graphic equaliser: one op-amp, the seven slider pots
  between its two inputs (the input side fed through R, the feedback side from the output
  through the same R), each wiper into a series resonator to ground. With the op-amp's
  inputs at the same voltage, the response is
  `H = (1 + R·Σ x/(Z + x(1−x))) / (1 + R·Σ (1−x)/(Z + x(1−x)))` (impedances relative to the
  pot, x = the wiper, 1 = the boost end). The bands interact through the shared op-amp, which
  is why they don't simply add: with every band up, 1 kHz rises +13 dB, where the seven
  single-band curves added give +16.
- **Six bands are LC resonators** (gyrators, in the pedal) with nearly equal parts:
  impedance 0.10–0.12, series resistance 0.035, Q 2.8–3.4 (R = 0.335):

  | Band | 100 Hz | 200 Hz | 400 Hz | 800 Hz | 1.6 kHz | 3.2 kHz |
  |---|---|---|---|---|---|---|
  | resonance | 90.9 | 195.8 | 369.5 | 728.4 | 1528 | 3275 Hz |

  Each lands within 10 % of its label. At +15 a band peaks at +13.5 to +15.8 dB (a little
  off the resonance, the shared op-amp pulling it), at −15 it dips by about the same.
- **The 6.4 kHz slider is a shelf, not a band**: a capacitor and resistor in place of the
  resonator (corner 3.8 kHz, resistance 0.085). At +15 it rises steadily from 1 kHz to
  +10.7 dB at 13 kHz (+8.2 dB at 6.4 kHz) and never comes back down. A low-Q resonator up at
  12 kHz fits as well; the RC is the simpler part.
- **The half-way marks are not half-way along the pot.** They give about two thirds of the
  end's dB on the bands (800 Hz: +7.6 and −6.4 dB; 6.4 kHz: +7.0 of its +8.2 at 6.4 kHz) and
  ±11.5 dB on Level. In this circuit a linear pot at mid-travel would give only ~4 dB; the
  fit puts the marks at wiper 0.96 (bands) and 0.99 (Level) — the sliders' law is not
  linear in travel, or the scale isn't printed by travel. Two marks don't settle it, and
  NAM-X doesn't need it: a slider set in dB solves the wiper from the circuit.
- **Level** is a flat ±15 dB (measured −14.9 / +14.7): the same circuit with a resistor
  for the resonator. Its top end rolls off with its gain (an op-amp's bandwidth, about
  87 kHz over the stage's noise gain): −3 dB at 13 kHz at +15, nothing at 0.
- **Flat** (every slider at 0) is within 0.3 dB of bypass, 30 Hz–16 kHz. The two sessions
  differ by 0.9 dB, the same at every frequency — Level's centre set by hand, not a band
  (an off-centre band would show at its own frequency). Each session's takes are fitted
  with their own offset (+0.08 and −0.24 dB).

Every sweep against the fit, one panel each with its error:
[`2026-10-28/fit/ge-7-fit.svg`](2026-10-28/fit/ge-7-fit.svg).

What's left: ~0.5 dB flat on the all-up and all-down takes (the sliders' own setting), a
1.7 dB miss on the all-up curve at 3.4 kHz, and up to 1–3 dB above 10 kHz on the takes that
boost the top (the 6.4 kHz shelf, Level +15).

## Clipping: one ceiling

A 1 kHz staircase (−36 to +3 dB re the guitar's peak, 1.5 s per step) flat, with every band
up, and with every band and Level up: the output never passes **−11.9 dBFS peak, +1.8 dB
over the guitar's peak** (which reads −13.7 dBFS in the bypass take), whatever the sliders
and Level. Level +15 doesn't raise the ceiling, it reaches it 15 dB sooner — so every stage
clips at the same rails, the last one after Level. Even flat, the +3 dB step clips (1 dB
short, H2 and H3 at −26 dB), and the bypass take starts to distort there too (H2 −38 dB):
the input buffer is near the same rails.

Clipped hard, the wave is a rounded square — H3 −14.5 dB, H5 −23 dB, a little H2 (−25 dB:
the positive side 0.1–0.3 dB lower). Its flat tops creep up to the ceiling over ~0.4 ms
and fall fast, so no static curve fits it (best: a soft clip, error −10 dB re the ceiling at
25 dB overdrive): the op-amp rails inside the reactive feedback network. A soft clip at the
ceiling gets the level and the harmonic balance, not the exact shape.

## Noise

Flat, the GE-7 adds nothing: −99.0 dBFS rms against −99.4 bypassed (85 dB under the
guitar's peak). Every slider up: −51.9 dBFS rms, peaks around −34 dBFS — +30 dB of gain on
the pedal's own hiss, and loud enough that `capture.py` needed a take-level quiet threshold
(−30 dBFS) to start those takes.

## Takes and lessons

- 33 + 8 takes; 800 Hz −7.5 redone once; the first session's Level takes moved the 6.4 kHz
  slider (above).
- **Stairs must fit the interface:** the reference plays at −4.67 dBFS, so no step can go
  over about +4.5 dB. The first plan went to +12 and stopped mid-run; `run` now builds every
  take before the first one.
- **A take can set its own `quiet_dbfs`** for a setting that raises the hiss.
- **Slider takes: name the slider in the note.** Level sits beside the 6.4 kHz slider; the
  follow-up plan says which one it is.

## For NAM-X

NAM-X's EQ is a 4-band semi-parametric. A GE-7 type would be: seven fixed bands (six
resonators at the fitted frequencies and Q, the 6.4 kHz shelf) through the one-op-amp
formula above, so they interact like the pedal's, each slider in dB at its band (the wiper
solved from it), Level ±15 dB, and a soft clip at +1.8 dB re the guitar's peak (an assumed
−6 dBFS post-amp peak, as for the other pedals). The response is a 13th-order rational
function of the eight positions — factored into biquads (and one first-order section)
when a slider moves. As a new type to A/B, not a change to the current EQ.
