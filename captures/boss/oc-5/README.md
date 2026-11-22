# Boss OC-5 Octave

**Removed from NAM-X on 2026-11-10** (the user: a failed experiment): the Octave effect built
from this study (`OctaveProcessor`, effect ID `octave`) is gone from the app; an old preset's or
session's `octave` entries are ignored. The study and its data stay here as a record; where the
text below says what NAM-X does, it describes the removed effect.

Measured 2026-10-29 (mono, Input → Output, 9 V supply, GUITAR/BASS on GUITAR). Analysis:
[`tools/capture/analyze_oc5.py`](../../../tools/capture/analyze_oc5.py) →
[`2026-10-29/fit/oc-5.json`](2026-10-29/fit/oc-5.json) and the figure below. Levels are in
dB re the guitar's peak at the pedal input (the rig's reference). Controls: Direct Level,
+1 OCT Level, −1 OCT Level, −2 OCT/Range (the −2 octave's level in Vintage, Range in Poly),
the VINTAGE/POLY switch and the GUITAR/BASS switch.

| Session | Plan | What |
|---|---|---|
| [`2026-10-29/`](2026-10-29/) | [`oc-5.json`](../../../tools/capture/plans/oc-5.json) | 34 takes: the dry path, the knob laws, each octave alone with a held sine, re-picked notes from 73 Hz to 1.3 kHz, two dying notes, low-under-high note pairs, Range at three positions |
| [`2026-10-29-followup/`](2026-10-29-followup/) | [`oc-5-followup.json`](../../../tools/capture/plans/oc-5-followup.json) | 4 takes: the dry path's clip against the level (a 220 Hz staircase, 110 Hz plucks) |
| — | [`oc-5-reamp.json`](../../../tools/capture/plans/oc-5-reamp.json) | the guitar DI at six blends, for the by-ear A/B (to record, after the DI) |

Purpose (user, 2026-10-29): a NAM-X octave only if the pedal sounds good — so this pass
measures what a model would need, and the reamp takes decide.

## At a glance

- **Digital.** The dry path is a converter delay: **1.00 ms** late (48 samples), **+0.3 dB**
  at Direct 12:00, flat within ±0.2 dB from 63 Hz to 18 kHz.
- **The dry path hard-clips at −8.1 dB re the guitar's peak.** Clean (3rd harmonic under
  −77 dB) up to −9 dB, then a flat ceiling; a pluck at the guitar's peak loses 7.8 dB of
  its attack. The clip is before the Direct knob (the same square at 12:00 and 5:00,
  ceilings −8.0 and −1.8 dB). The octaves don't see it (they follow a dying note 1:1 from
  −3 dB): the dry has its own stage.
- **Two engines.** Vintage is an analog-style divider: square-wave subs (−1 locks on in
  3–10 ms, −2 in 9–52), monophonic — two notes make it follow the lower one, or wobble.
  Poly is a polyphonic shifter (both notes of a pair shifted, 7–84 ms to lock on, longer
  for low notes). +1 has the same sawtooth-like wave in both modes; Vintage's locks on in
  ~2 periods, Poly's about three times slower.
- **Range** (Poly): 7:00 is a different engine — only the **lowest note** of a chord is
  shifted, a smooth near-sine sub, fast (5–11 ms), 6–8 dB quieter; above 7:00 it is the
  polyphonic shifter with a **ceiling**: notes above ~500 Hz (12:00) or ~1 kHz (5:00) aren't
  shifted.
- **Knob laws:** Direct and −1 OCT share one audio taper: −9.7 / 0 / +4.6 / +6.1 dB at
  9:00 / 12:00 / 3:00 / 5:00; Direct at 7:00 mutes (−76 dB).
- **Noise:** switched on, the output idles at ~−72 dBFS peak (−97 bypassed) — why the plan
  waits for −60 dBFS between takes.

![OC-5](2026-10-29/fit/oc-5.svg)

## The dry path

`dry-sweep` (Direct 12:00, every octave down, −12 dB) against `bypass-sweep`: 1.006 ms late,
the same at every frequency (a converter's latency, like the CE-5 and BF-3), response re
bypass −1.3 dB at 31.5 Hz, 0.0 at 63, then +0.2 to +0.4 dB from 125 Hz to 18 kHz.

**The clip** (follow-up, Direct 12:00, octaves down, a 220 Hz staircase against its bypass):

| Input (dB re the guitar's peak) | −30 … −9 | −6 | −3 | 0 | +3 |
|---|---|---|---|---|---|
| Gain at 220 Hz (dB) | +0.07 | −1.14 | −3.59 | −6.35 | −9.22 |
| Output peak (dB) | input + 0.1 | −8.0 | −8.0 | −8.0 | −8.1 |
| 3rd harmonic re fundamental (dB) | −77 to −90 | −20.2 | −13.7 | −11.5 | −10.5 |

A hard clip: clean to −9 dB, then a flat ceiling at −8.0 dB — the clip point is **−8.1 dB re
the guitar's peak**. The held sines of the first session (`direct-*`) show the same square at
every Direct position, scaled by the knob (ceilings −8.0 at 12:00, −1.8 at 5:00), so the
clip comes before Direct. Plucks at the guitar's peak (110 Hz, 0.6 s decay): the first
~100 ms come out 7.8 dB under the bypass's peak, then the note passes unchanged (0.0 dB at
1 s). So the OC-5 squares off the attack of every hard-picked note by up to 8 dB; at a
lower input (a quieter guitar or a pad) it wouldn't.

## The knobs

220 Hz held sine, the output's level at the frequency of interest (dB re the guitar's peak):

| | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|---|
| Direct (the 220 Hz dry; clipped) | −76.3 | −15.7 | −6.1 | −1.6 | −0.1 |
| −1 OCT, Vintage (110 Hz) | — | −15.5 | −5.7 | −0.9 | +0.5 |

Both re 12:00: −9.6/−9.8 at 9:00, +4.5/+4.8 at 3:00, +6.0/+6.2 at 5:00 — one audio taper
(the dry's numbers are of a clipped wave, but the clip is before the knob, so the ratios
hold). The other octave knobs are assumed to follow it (measured at 12:00 only).

## Each octave alone (a held 220 Hz sine)

| Octave | Fundamental (dB) | Harmonics 2–6 re fundamental (dB) | Waveform |
|---|---|---|---|
| Vintage −1 (110 Hz) | −5.7 | −12.3, −18.5, −23.5, −23.9, −25.6 | a square with rounded edges and sagging tops (high-pass ~15 Hz), not 50 % duty (even harmonics) |
| Vintage −2 (55 Hz) | −9.1 | −13.3, −22.0, −19.9, −23.7, −31.4 | the same kind, softer |
| Vintage +1 (440 Hz) | −7.5 | −4.9, −14.9, −35.1, −31.4, −31.1 | a sawtooth-like wave: fast rise, slow fall |
| Poly +1 (440 Hz, Range 5:00) | −7.6 | −8.6, −17.6, −30.3, −33.7, −32.5 | the same shape, smoother |
| Poly −1, Range 7:00 (110 Hz) | −13.1 | −41.8, −29.6, −45.9, −23.9, −55.1 | a smooth, near-sine sub |

The cycles barely change from one period to the next (spread ~0.02 of the guitar's peak):
the subs are synthesized waves at the tracked pitch, scaled by the input's envelope — not
the input's own waveform. Under +1 the input itself doesn't leak through (220 Hz at −72 dB in
Vintage, −91 in Poly; for the subs 220 Hz is their own 2nd or 4th harmonic).

## Tracking

Re-picked notes 3 s apart, 73 Hz (drop D) to 1.3 kHz; level 150–600 ms after the pick;
lock-on = the output's envelope reaching half its peak.

| | Pitch | Level | Lock-on |
|---|---|---|---|
| Vintage −1 | ×½ on every note | −6 to −8 dB to 659 Hz; −14 at 880, −28 at 1318 | **3–10 ms** |
| Vintage −2 | ×¼ on every note | −8 to −10 dB (−12 to −14 on the two lowest: 18–21 Hz, at the high-pass); −24 at 880, −54 at 1318 | 9–52 ms |
| Vintage +1 | ×2 on every note | −7 to −12 dB, all the way up | ~2 periods: 29–40 ms at 73–82 Hz → 3 ms at 1.3 kHz |
| Poly +1 (Range 5:00) | ×2 | −8 to −11 dB, all the way up | **84 ms at 73 Hz** → 7 ms at 1.3 kHz |
| Poly −1, Range 5:00 | ×½ | −1.3 to −5.6 dB (a note-to-note ripple), −15.5 at 1318 | 24 → 4 ms |
| Poly −1, Range 12:00 | ×½ | the same up to 494 Hz; **−25.5 at 659**, −49 at 880 | 34 → 5 ms |
| Poly −1, Range 7:00 | ×½ | **−9 dB flat** to 880, −13 at 1318 | 5–12 ms (16 at 1318) |

Every note came out at exactly its octave (the one odd reading, −2 on the low D at
20.1 Hz, was a sideband of the decaying note: 18.31 Hz on a longer window). Vintage's
fall above 659 Hz is the divider's band: its input filter keeps the highest notes from
triggering it (the OC-2 does the same). Poly's lock-on falls with pitch like a shifter
working in pitch periods; its −1 at Range 12:00 shows Range as a ceiling between 494 and
659 Hz.

## Two notes at once

A low note under a higher one, picked together (level of each note's shifted copy):

| | 82 + 196 Hz | 110 + 330 Hz | 147 + 494 Hz | 196 + 659 Hz |
|---|---|---|---|---|
| Vintage −1 | low −11.4, high −52 | **low −33, high −41**: a wobbling sub (52.7 / 57.1 Hz and sidebands) | low −9.8, high −49 | low −9.3, high −45 |
| Poly +1 (Range 5:00) | −14.6 / −16.3 | −15.9 / −16.0 | −15.6 / −16.3 | −16.0 / −16.2 |
| Poly −1, Range 5:00 | −8.3 / −9.6 | −14.4 / −16.3 | −9.3 / −14.7 | −8.2 / −15.0 |
| Poly −1, Range 12:00 | −15.1 / −14.9 | −7.5 / −15.1 | −15.7 / −13.0 | −8.5 / **−33.1** |
| Poly −1, Range 7:00 | −9.9 / −80 | −10.5 / −28 | −11.0 / −97 | −10.4 / −112 |

So: **Vintage** follows the lower note and drops the upper one, except where the two
fight — 110 + 330 Hz (a twelfth: the upper note is the lower one's 3rd harmonic) breaks
the divider into the classic OC-2 warble; it also makes intermodulation products (155 and
237 Hz under 82 + 196). **Poly** shifts both notes at Range 5:00 and up to its ceiling at
12:00 (the 659 Hz note dropped). **Range 7:00** shifts only the lowest note, cleanly.

## Dying notes

Two notes (82 and 196 Hz) decaying 3.5 dB/s over 10 s, Vintage −1, −2 and +1: every
octave follows the input's level **1:1 (slope 0.98–1.08 dB/dB) from −3 dB down to −33 dB**
(the end of the take), no dropout, no glitch, no gate. The top ~3 dB are slightly
compressed. Where the divider finally loses a note is below −33 dB — not reached.

## Open

- **By ear:** the reamp takes (`oc-5-reamp.json`, after the guitar DI) — the decision on a
  NAM-X octave rests on them.
- Not measured: the +1 / −2 knob laws (assumed the −1's), Range's ceiling between 7:00 and
  12:00 and above, Poly's −2 (none: the knob is Range there), BASS mode, where Vintage
  loses a note below −33 dB, the noise floor as a take (only the silence meter's −72 dBFS).

## In NAM-X

The Octave effect (2026-10-29, `src/dsp/OctaveProcessor.h/cpp`, branch `dsp/octave-oc5`),
built from the description below; the user is to judge it by ear while playing, ahead of the reamp.
Checked by rendering this session's played signals through it and measuring them the way
`analyze_oc5.py` measures the pedal:

- **Pitch:** every tracking note at exactly its octave in all seven settings.
- **Levels:** the held sine had put every octave 2.3–4 dB low, so the levels were
  recalibrated on the tracking takes. After that, note by note against the pedal: Vintage
  −1 within ±1.5 dB (to 1.3 kHz +2), −2 ±2.8 (1318 Hz +5), +1 ±2.7, Poly +1 ±1.9 (1318 Hz
  −2.8), Poly −1 Range 5:00 ±3.3 (the pedal's note-to-note ripple), Range 12:00 the same up
  to 494 Hz (659 Hz −24 against −25.7), Range 7:00 ±0.6.
- **Two notes:** Vintage keeps the lower note (82 + 196: −10.4 / −47 against −11.4 / −50);
  at 110 + 330 the model loses less of the low note than the pedal (−9.6 against −17).
  Poly shifts both notes, Range 12:00 drops the 659 Hz one, Range 7:00 only the lowest.
- **Dry clip:** the follow-up's staircase and plucks peak within 0.3 dB of the pedal's.
- **Lock-on:** faster than the pedal in Poly (a 20–50 ms shifter against its 24–84 ms).

### By ear, and what changed (2026-10-29, evening)

The test tones above were too clean to show the problems a real guitar does. In order:

1. **First version, by ear: "doesn't sound good".** Only Poly +1 was fine. The lower octaves
   jumped between octaves, and all of Vintage was bad. The cause was the tracker: zero
   crossings of the input through a fixed 250 Hz low-pass, so a string's strong 2nd and 3rd
   harmonics, hum and noise toggled the flip-flops. Poly −1 jumped too, because Range 0 %
   (the default) is the tracker's sine sub, not the shifter.
2. **The tracker became YIN-steered** (`YinPitch.h`, 2c36b29). YIN on the input decimated to
   ~12 kHz estimates the fundamental; the comparator sees a band-pass at it, and counts a
   crossing only 70 % of a period after the last. Judged on a 50 s DI of the user's low E
   string (chromatic, re-picked; scratch only, `pw-record` at a higher, unknown gain), by an
   offline YIN on the output, frames an octave off went: Vintage −1 3.1 → 0.8 %, −2 3.6 →
   0.8 %, +1 7.7 → 1.2 %, the sine sub 3.9 → 0.2 %. What's left is at the picks.
3. **The reamp** ([`2026-10-29-reamp-lowe/`](2026-10-29-reamp-lowe/),
   `plans/oc-5-reamp-lowe.json`, on branch `captures/ph-3`). That DI, its peak set to the
   reference, went through the real pedal at the six blends of `oc-5-reamp.json`; the model
   rendered the same file at the same settings. Per note (67 notes), pedal minus model:
   - Poly within ~2 dB at f/2, f and 2f in every take. The pedal's Poly −1 on a low E is
     about as weak as the model's: the string's fundamental is weak, and a shifter only
     moves what's there.
   - Vintage −1 was ~5 dB loud, −2 ~3 dB, +1 ~4 dB. The subs were scaled by everything under
     750 Hz, mostly the string's harmonics on a low E. Now they follow the band-passed
     fundamental, as the pedal's do (5c4f530): −1 within 0.3 dB, −2 2 dB, +1 1.2 dB. The
     test tones were unchanged.
4. **The pedal's octaves alone.** The user tests at Direct 0 %, while the reamp takes have the
   pedal's Direct at 12:00. Its dry path is a known one, so it was subtracted: the clipped
   bypass take at a fitted delay (0.92–1.01 ms) and gain (+0.1 to +0.9 dB; the study has
   1.00 ms and +0.3 dB). By ear, the user found **the pedal's octaves much cleaner than the
   model's**, with only some clipping on transients. Compared band by band:
   - Vintage with +1: the model had far more 4–16 kHz (−39/−42 dB against −47/−54). Two
     causes: the oscillator's phase snapped to every input cycle (a click per cycle), and a
     cycle that missed the pitch estimate gated the +1 off for a cycle. Now a phase-locked
     loop spreads each correction over the next cycle, and the waves fade in and out over
     ~5 ms (cb95f9d): −51/−61 dB. The sine sub's junk above 8 kHz is gone too.
   - Vintage −1 and −2: close to the pedal in every band.
   - **Poly −1: the model is far darker above 500 Hz** (−34/−66 dB at 500 Hz/1 kHz against
     −21/−26), and still −29/−47 with the Range ceiling removed. The pedal's extra content
     isn't its input passed through (coherence with the input under 0.25 at every
     frequency): its shifter keeps more of the shifted upper partials than
     `FilterBankShifter` at −12 does.
5. **By ear after that: "still not there."** Stopped for now (user).

**What the pedal's Vintage seems to be.** With the pedal's own flip-flop (the sign of its f/2
component), "a square times the level" explains 49 % of its −1 on the low-E notes,
"|fundamental| × square" 57 %, the plain input times the square 33 %. With the model's
flip-flop, the same square explains only 14.5 %, so the flips happen at different moments.
They don't glitch more, though: π jumps of the sub's phase, 1.7/s pedal against 0.8/s model,
re-picks included. The pedal's −1 has strong 1.5f and 2.5f on the low E (only 3–11 dB under
f/2; the model's 15–22) and is partly coherent with the input (0.70 at 700 Hz, 0.86 at
1.5 kHz). Both point to the guitar signal being multiplied by the flip-flop, the OC-2's way,
maybe with an offset or uneven duty (which would also explain the even harmonics of the
held-sine takes), rather than a synthesized square. Not built yet.

### Open, for next time

- **Vintage:** try the OC-2 structure (the band-limited input times the flip-flop, with an
  offset or duty), fitted to the isolated pedal octaves, and find why the flips land at
  different moments than the pedal's.
- **Poly −1:** a brighter octave-down shifter. Retune `FilterBankShifter` for −12, or a
  different shifter for that voice. The Range ceiling probably acts on notes, not on the
  spectrum (a note above it isn't shifted; the harmonics of a lower note are).
- **Better data:** the long guitar DI (`capture.py di`, 15–30 min: every string, chords,
  dynamics, dying notes), then each voice captured alone with Direct at 7:00: Vintage −1, −2,
  +1, Poly +1, Poly −1 at 2–3 Range positions. That's the ground truth for either a grey-box
  fit or the learned parts of a hybrid model. Discussed with the user: keep the tracker,
  flip-flops and shifter as code, learn only the timbre. A plain NAM capture can't count
  cycles (Vintage) and is unlikely to learn the shifter (Poly).
- **The comparison scripts** (the dry-path removal, per-note levels, band energies, the
  structure and phase-jump tests) were scratch, not kept. The method is all above. Rendering
  the model takes a small probe that runs `OctaveProcessor` on a raw float stream.

## For NAM-X (if it sounds good)

What a model would need, from the above: a dry path with its hard clip at −8.1 dB re
the guitar's peak (before the Direct knob) and the audio taper; **Vintage** as a divider: a lowest-note pitch tracker fast enough to lock in
a few ms (or a zero-crossing flip-flop on a filtered input, as the OC-2), driving a
square (−1) and a half-rate square (−2) through a ~15 Hz high-pass and a low-pass, scaled
by the input's envelope — the chord glitches come for free with a flip-flop; **+1** as a
rectifier-like doubler (sawtooth-ish, ~2 periods to lock); **Poly** as a polyphonic shifter
— NAM-X's Transpose (`FilterBankShifter`, ~20 ms at the low E) is a candidate, with a Range
ceiling as a low-pass on what it shifts; **Range 7:00** as a clean monophonic sub (a sine
at the lowest note's half). The tracking table is the acceptance test: pitch, level per
note, lock-on time.

## Lessons

- **Roll a deconvolved impulse response before cutting it.** The bypass's peak lands a
  fraction of a sample before the loopback alignment, so a fifth of its energy wraps to
  the end of the circular deconvolution; cutting the first 0.5 s read the bypass ~2 dB low
  (1 dB in the DD-8 sessions) and a digital pedal's gain re bypass ~2 dB high. Caught here
  because the staircase's gain (+0.07 dB) disagreed with the sweep's (+2.3 dB).

- **A long window around a decaying note shows sidebands.** The −2 octave's low D read
  20.1 Hz on a 1 s window: the note's 0.8 s decay put sidebands ±1.6 Hz around the true
  18.3 Hz, and with the high-pass cutting the fundamental, a sideband won. Read pitch on
  several windows before calling a tracking error.
- **A digital pedal's own hiss can exceed the silence threshold.** The run waited forever
  before the first pedal-on take (−72 dBFS idle against −75); the plan sets `quiet_dbfs`.
- **Check every take that says PROBLEM before going on:** take 22 was kept silent once;
  redoing it meant deleting its three files.
