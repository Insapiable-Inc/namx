# RV-6 Reverb handoff

## 2026-11-18 — Shimmer in the rack

The user accepted the prototype match and asked for the port. On `dsp/reverb-rv6`,
`eaa7d7a` records the capture study and `32ca65b` implements it in **Reverb → Shimmer**
(preset type 8). One 1.345816 Hz clock drives the triangular octave reads and the moving
delay taps. The paths sum to mono before the measured stereo filters. The rack embeds the
twelve fitted settings. At 48 kHz the wet path adds 64 samples (1.33 ms) and the dry passes
immediately. [DSP](../../../src/dsp/Rv6Shimmer.cpp)

On the short-Time guitar release, the old tank left 52.7% of wet energy outside the stereo
relationship measured on the pedal sweep. The compiled rack measured **5.3%** (median of
four clock starts, range 3.1–8.2%). The recorded pedal measured **5.5%**. Across 20 guitar
excerpt/setting pairs, median seven-band spectral error fell from **2.35 to 1.14 dB**.
[Per-excerpt rack scores](2026-11-15-focused/fit/shimmer-rack.json)

The Release build passed all six test suites and the app self-test. The DSP suite passed
82 checks, including reset and irregular callbacks at 44.1, 48 and 96 kHz. The local
comparison WAVs play the pedal first and the rack second at matched playback gain.
[Short-Time stabs](../../../build-rv6/shimmer-model/rack/time-0700-pedal-rack.wav) ·
[Noon stabs](../../../build-rv6/shimmer-model/rack/noon-guitar-pedal-rack.wav) ·
[Build and reproduce the comparisons](SHIMMER-PROTOTYPE.md#rack-implementation)

Two residuals from the accepted prototype are worth revisiting if Shimmer is reopened.
The separate noon guitar release was 3.7 dB quiet, and the unshifted fundamental of the
held noon E4 was about 10 dB quiet. [Guitar and held-note measurements](SHIMMER-PROTOTYPE.md#guitar-results)

## 2026-11-17 — Earlier tank model

For Astra: the next attempt at NAM-X's Boss RV-6 model. This is everything the session of
2026-11-17 tried on the Reverb, in order, with what each thing scored, what was kept, what was
dropped and why. The study beside this file ([`README.md`](README.md)) has the measurements in
full; this file is the lab notes behind its last three sections, including the dead ends that
only got a line there.

**The goal.** A Reverb that sounds closer to the pedal *on a guitar*. The part that matters most
is Shimmer: the user listened to the first refit and said "shimmer still sounds very different,
brighter and or louder than the pedal", and that in `time-0700-shimmer.pedal.wav` they seem to
hear some modulation in the tail. A second round answered the first half by measurement (§4.6)
and could not model the second (§4.7). The user has not given a verdict on the second round:
they asked for this handoff instead. The other five modes got three fixes (§4.1–4.4) that
nobody has heard yet.

## 0. Where it stands

- **Branch `captures/reamps`**, four commits: `96f272f` (E.Level, Tone, Time), `986e753`
  (Shimmer, first refit), `dc7153b` (cost notes), `e1fe4c9` (Shimmer after listening). Merged
  into `pedals/all` (`7d4a896`, no difference under `src/`); `build-release/namx` is built from
  it. Nothing was pushed when this was written.
- **Tests:** `DspGoldenTest` 81 passed, with the six `reverb-rv6-*` entries re-recorded (only
  those lines); the other five test programs exit 0; the format check exits 0; the app's
  `--self-test-and-exit` exits 0.
- **Heard by the user:** Shimmer after the first refit (verdict above). Not heard: Shimmer as it
  is now, and every change to Modulate, Spring, Plate, Hall and Room.
- **Listening files** (local, git-ignored): `2026-11-15-focused/ab/<take>.pedal.wav`,
  `.was.wav` (the build before this session) and `.now.wav`, for eighteen takes; plus
  `noon-guitar-shimmer.one-lfo.wav` and `time-0700-shimmer.one-lfo.wav` (§4.7).
- **The budget the user set** for the RV-6 (after the PH-3's Rise and Fall ran away): fixes are
  tables and constants; no engine rebuild without asking; a tail whose texture constants can't
  fix is written down as a known miss and the user is asked. This session stretched that twice,
  both times with a measurement behind it: one more tank (§4.3) and Hall's tank moved (§4.4).

Words used below:

- **wet**: the pedal's output less its dry path (`rv6/takes.py` takes the dry out).
- **7:00, 9:00, 12:00 (noon), 3:00, 5:00**: the knob positions measured; in the code 0, 0.2,
  0.5, 0.8, 1 (`KNOB_AT`).
- **burst**: 50 ms of noise at −12 dB. **stabs**: the guitar excerpt of short chords at full
  level, with the tail after the last one.
- **the scorer**: `rv6/focused.py`, the per-take report of 2026-11-15.
- **as heard**: `rv6/heard.py`, added in this session (§3). It is the measure to trust on guitar.

## 1. The model now

`src/dsp/Rv6Reverb.h` (the engine and its constants), `Rv6Early.h` and `Rv6EarlyData.h` (the
measured starts), `ReverbProcessor.cpp` (the effect around it). Per output:

```
out = dry + level · Tone(wet of the mode) + low reverb
```

- **Level** (`level_gain`): the knob's square from 12:00 up, `(knob / 12:00)^1.696` below.
- **Tone** (`TONE`, `design_tone`): three filter sections per measured position, the same in
  every mode; flat at 12:00.
- **The low reverb** (`LOW`, `LOW_TANK`): only in Plate, Hall and Room and only with Tone below
  12:00. One 16-line tank of its own, the same signal on both outputs.
- **The modes' wets:** a 16-line feedback delay network per mode (`TANKS`, `Tank`), for Room,
  Hall, Plate and Spring behind the pedal's own measured start (a convolution; the engine
  subtracts the tank's own response over that window), a second slower tank under Hall and Room
  (`DECAYS`), Spring an engine of its own. Tables per mode over Time: `VOICINGS` (RT60 at 1 kHz,
  the wet's energy correction, gain, colour EQ).
- **Hall's tank is fed 240 ms in**, after the measured swell (§4.4).

**Shimmer's signal path**, per sample (`Engine::process`, `prepare_shimmer`):

```
in      = band(x)                        2nd-order floor (band_hp, band_q), low-pass (band_lp)
A, B    = tank(in, shifted)              its own recirculation scaled by 1 − share_loss · share
back    = climb_band(0.5 · (A + B))      floor shift_hp, 4th-order top shift_lp
A, B   += long_gain · long_tank(in, shifted)      lines × 1.13, never fed back
forward = forward_band(x)                4th-order floor forward_hp, top forward_lp
shifted = octave_up(forward[Tone] · forward + share[Tone] · back_gain · back_at[Time] · back)
```

`octave_up` is `GrainShifter`: two taps reading 299 ms grains at twice the speed. Three names
from here on: the **forward octave** is the note itself sent up once; the **climb** is the
loop that sends a share of the tank's output up again on every pass, so octaves stack; the
**long tank** is the part of the tail that the share doesn't shorten.

The constants now (`SHIMMER`, and Shimmer's row of `VOICINGS`):

| | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---:|---:|---:|---:|---:|
| `forward`, over Tone | 0 | 0.162 | 1.742 | 2.07 | 2.288 |
| `share`, over Tone | 0 | 0.0054 | 0.417 | 0.385 | 0.364 |
| `back_at`, over Time | 0.35 | 0.6 | 1 | 1.411 | 1.544 |
| RT60 at 1 kHz, s, over Time | 4.51 | 5.87 | 11.73 | 19.70 | 22.37 |
| energy, dB, over Time | −4.29 | −4.75 | 0 | +2.75 | +3.24 |

`back_gain` 2.376, `share_loss` 0.8285, climb band 56.8 Hz – 3.2 kHz, forward band 150 Hz –
1074 Hz, tank input band 226 Hz (Q 0.617) – 1206 Hz, `long_gain` 0.0857 (−21.3 dB), long tank
RT × 0.799 (its highs × 1.46, its lows × 0.783), both tanks' highs × 0.425 of the tank
kind's, modulation depth × 2.569 with a rate per line (`mod_spread` 0.6), gain −0.93 dB. The
fit's own parameter names for all of these are in `build-rv6/shimmer/final-params.json`.

## 2. The recordings

All under `captures/boss/rv-6/`; the WAVs are git-ignored and exist on the user's machine only.

| session | what it has | used for |
|---|---|---|
| `2026-10-31`, `2026-10-31-followup` | sweeps, bursts and held tones per mode and knob | October's fits; Tone 9:00 in Hall (`tone_law` in the follow-up's JSON) |
| `2026-11-01-shimmer` | Shimmer on held sines (82 Hz – 2.6 kHz), a chord, a fifth, plucks, one note at Time and Tone's ends | the Shimmer fit's first target set: the **held-notes residual** below |
| `2026-11-15-focused` | 123 takes, both outputs: per mode a sweep, a burst and the stabs at every Time, Tone and E.Level position, Tone's ends at Time's ends, two check settings | everything in this session |

In the focused session every take is `<block>-<mode>`: `noon`, `again` (noon two hours later),
`noon-guitar` (stabs, chords, lead, funk), `time-0700/0900/0300/0500`,
`tone-0700/0900/0300/0500`, `tone-0700-time-0700`, `tone-0700-time-0500`,
`tone-0500-time-0700`, `tone-0500-time-0500`, `level-0900/0300/0500`, `check-a` (E.Level 3:00,
Tone 9:00, Time 3:00), `check-b` (E.Level 9:00, Tone 3:00, Time 9:00). `level-0700-hall` is the
dry alone. Modulate's and Shimmer's takes also hold a note (1 kHz, 330 Hz).

Three things to know before trusting a take:

- **The six `tone-0900-*` takes were recorded with Tone at 12:00.** Their rows in any report
  compare the model at 9:00 with the pedal at 12:00: ignore them. Tone 9:00 exists only in
  `check-a`, and in October's Hall.
- **`check-a-shimmer` is no longer a check.** Tone 9:00 in Shimmer was set from it (§4.6).
  `check-b` is the only Shimmer take in no fit; `check-a` and `check-b` are still out of every
  fit in the other five modes.
- **Unread data:** the held 330 Hz note in every Shimmer take of the focused session (the
  octaves' levels and decays at every knob position), the lead and funk excerpts, and Output B
  beyond its level and its likeness to A.

## 3. The tools, and how a number is made

Run from the repository root. `build-rv6/` is git-ignored: the renderer and all scratch live
there.

```sh
# the engine offline; the fit tools call it (override any constant as name=value)
g++ -std=c++23 -O2 -Isrc/dsp -Iexternal/signalsmith-stretch/include \
    -Iexternal/signalsmith-linear/include tools/capture/render_rv6.cpp -o build-rv6/render_rv6

python3 tools/capture/rv6/focused.py measure > build-rv6/report.txt   # the scorer, all takes, 3.5 min
python3 tools/capture/rv6/heard.py [--was BINARY] [TAKE ...]          # as heard; default: six Shimmer takes
python3 tools/capture/rv6/ab.py [--was BINARY] [TAKE ...]             # listening files into the session's ab/
python3 tools/capture/rv6/tone.py [--write | --check]                 # Tone's filter and the low reverb
python3 tools/capture/rv6/time.py [--write]                           # Time off noon (Spring held and is left)
python3 -I tools/capture/fit_rv6_shimmer.py --render build-rv6/render_rv6 \
    --audio captures/boss/rv-6/2026-11-01-shimmer --focused --evaluate    # Shimmer on every target, 2 min
```

`RENDER_RV6=path` picks another renderer for the `rv6/` tools. After a `--write`:
`clang-format -i` the header, then rebuild the renderer.

Renderers kept in `build-rv6/` (local): `render_rv6` (the code as committed), `render_rv6_base`
(before this session: the `--was` of every comparison), `render_rv6_shimmerfit` (the first
Shimmer refit's constants), `render_rv6_quarter` and `render_rv6_inphase` (§4.7),
`render_rv6_hall240` (§4.4's first trial). Scratch scripts: `build-rv6/fix/` (the three fixes)
and `build-rv6/shimmer/` (both Shimmer rounds, the two fits' logs and reports). They are named
below where they produced a number; none is committed.

**How the takes are made comparable** (`rv6/takes.py`): the recording's one-sample slips undone,
the dry removed with a 4096-tap filter measured on the E.Level-off take, and NAM-X rendered from
the same played signal through the same filter. A tail can't be matched sample for sample (the
tanks aren't the pedal's algorithm), so everything compares levels per band over time.

**Three measures, and they disagree:**

1. **The scorer** (`focused.py`): per take the level, tone per third octave, RT60 per octave
   from the burst, and on guitar the bands' means *over the steps where the pedal sounds*, as a
   mean of decibels.
2. **The Shimmer fit's residuals** (`fit_rv6_shimmer.py`), no unit, lower is better. The
   *held-notes residual* is the 2026-11-01 session (lines' levels, decays, spectra). The
   *bursts-and-stabs residual* is the focused session: the wet per octave band, 125 Hz – 8 kHz,
   100 ms steps, after the burst at twelve settings and over the stabs at five.
3. **As heard** (`heard.py`): the wet's *energy* per octave band summed over the stabs and their
   tail, every step counted, the pedal's noise floor added under NAM-X; NAM-X less the pedal.

On the same take the first and the third can differ by 9 dB. Before this session the scorer
read Shimmer's stabs at E.Level 9:00 as 9.3 dB quiet; by energy that take was 0.1 dB quiet
overall, with a short tail and 4–6 dB too much bass. A mean of decibels over time is dominated by
the many quiet steps of a tail, and a measure limited to where the pedal sounds cannot see the
model ringing on. Read §4.6 before using the first two on guitar.

## 4. What was tried, in order

### 4.1 E.Level at 9:00 (kept)

The scorer had the wet 1.3–3.4 dB quiet at E.Level 9:00 in all five modes, on bursts and on
guitar, and again in `check-b`. The pedal's 9:00 is 13.5 dB under 12:00; the model's square law
gave 15.9. Now a power of 1.696 below 12:00 (0.4^1.696 is −13.5 dB) and the square above, where
3:00 and 5:00 already held within 0.9 dB. Result at 9:00, model less pedal, burst / guitar:
Modulate −0.2 / −0.3 dB, Spring −0.3 / +0.5, Plate +0.6 / +0.1, Hall −0.9 / +0.3, Room +1.1 /
+0.3; `check-b` within ±0.9 dB on bursts. Nothing is measured between 7:00 and 9:00.

### 4.2 Tone is one filter (kept)

October's Tone was fitted in Hall only, as an energy ratio. Reading it per mode
(`build-rv6/fix/tone_look.py`) showed a bass lift at 7:00 that depends on the mode: none in
Modulate and Spring, about +20 dB at 80 Hz in Plate (19–25 by the measure). So the energy ratio
mixes two things. What separated them:

- **A coherent ratio.** Room, Hall, Plate and Spring repeat to the sample, so the sweep response
  of a Tone take over the noon take's, as `sum(H_tone · conj(H_noon)) / sum(|H_noon|²)` per
  sixth octave, is the filter alone: anything Tone *adds* is another signal and averages out
  (`tone_fine.py`, now `tone.py`). The four modes then agree within 0.1 dB at 3:00 and 5:00, and
  at 7:00 wherever the coherence holds.
- **What it showed:** below noon the filter does nothing under 500 Hz; its notch at 4 kHz is
  20 dB deep, not the 13–14 the energy ratio showed; above noon there is a low cut of 5.5 dB at
  50 Hz that October's fit had at a quarter of that.

Steps that went wrong on the way:

- The first fit masked bands with coherence under 0.9, which removed the notch itself (the wet
  is 20 dB down there): the notch came out 9.1 dB deep. With the mask at 0.6, and under 700 Hz
  at 7:00 only Spring counted (the other modes carry the added reverb), it is −12.3 dB at Q 5
  plus a broad −2.7 dB dip, 0.07 dB rms from the measured curve.
- The generated C++ had literals like `1413f`. `tone.py`'s `number()` now always writes a
  decimal point.

Tone 9:00 has no take (§2): it is October's Hall curve with the added reverb's share removed as
7:00 shows it. `check-a`'s bursts, at Tone 9:00 in every mode and in no fit, then read 0.5–1.3 dB
rms (from 1.5–2.3).

Result, the curve re noon over the third octaves from 50 Hz to 5 kHz: 0.03–0.3 dB rms at 3:00 and
5:00 in all five modes (from 0.6–1.2), 0.1–0.2 dB at 7:00 in Modulate and Spring (from 2.0–4.2).

### 4.3 The low reverb under Tone 12:00 (kept, second version)

The bass that Plate, Hall and Room gain at Tone 7:00 is not a filter. The evidence, in the
order it was found (scripts in `build-rv6/fix/`):

1. `bass_look.py`: at 60–180 Hz the two outputs' correlation goes from about 0 at noon to
   +0.9…+1.0 at Tone 7:00.
2. `mono_look.py`: the Tone 7:00 response less the Tone 12:00 response at the same Time (the
   tank's own wet cancels) leaves one signal that is the same on both outputs: correlation
   +0.97…+0.99, the outputs' difference 19–21 dB under their sum.
3. `mono_tf.py`, `mono_spec.py`: it starts about 40 ms after the note in all three modes, is
   even from 50 to 400 Hz and falls about 3.5 dB an octave above, alike in the three modes.
4. `mono_abs.py`: it does not follow E.Level. At 60–120 Hz in Plate its energy after the burst
   reads 21.5 dB (on the script's own scale) with E.Level 12:00 and 18.8 dB in `check-a`
   (E.Level 3:00, Tone 9:00), where the mode's own wet is 8 dB up. Measured at those two E.Level
   settings only.

**First version, dropped:** a third output of each mode's own tank (its lines summed with the
input's signs), through a low shelf and a low-pass, with a level per mode and per Time. It
matched the sweeps' spectra (Plate's Tone 7:00 curve from 4.4 to 1.1 dB rms). On guitar it was
wrong over time (`low_guitar.py`, model less pedal per 200 ms at 125 and 250 Hz): in Plate and
Hall the bass ended each stab 5–14 dB loud (it rang as long as the mode's tank), in Room
5–30 dB short, and once Hall's tank was moved to 240 ms (§4.4) Hall's bass came 250 ms late. A
spectrum over the whole response cannot tell a right level with a wrong decay from a wrong
level with a right decay.

**Second version, kept:** a tank of its own (`LOW_TANK`, sixteen lines of 19–66 ms, fed 20 ms
in) with an RT60 per mode and Time. Two details cost a rerun each:

- **Isolating it.** The half sum less the half difference of the outputs still held some of
  the mode's own wet: Hall's low reverb at Time 7:00 read 1.08 s that way. The take less its
  Tone-noon twin (`tone.py: pedal_low`) reads 0.67 s.
- **Reading a short decay.** A line through the level per 100 ms reads a short tank long (a
  tank set to 0.56 s read 0.74). So the tank's RT60 is set to whatever makes NAM-X *read* as
  the pedal reads, with one calibration pass: set 0.47 s to read 0.61 against the pedal's 0.60.

RT60 as read, at Time 7:00 / 12:00 / 5:00: Plate 0.60 / 2.19 / 5.72 s, Hall 0.67 / 3.39 / 7.12,
Room 0.64 / 1.53 / 4.35 (NAM-X reads 0.61 / 2.23 / 5.85, 0.67 / 3.44 / 7.28, 0.65 / 1.59 /
4.45). Result at Tone 7:00, the curve re noon at Time 12:00 / 7:00 / 5:00: Plate 0.5 / 1.3 /
1.0 dB rms (from 4.4 / 4.9 / 4.1), Hall 0.9 / 2.9 / 0.7, Room 0.8 / 0.9 / 1.1; the outputs'
likeness, pedal / model: Plate 0.25 / 0.28, Hall 0.34 / 0.36, Room 0.18 / 0.19 (the model's was
0). After it the guitar's bass shows no drift (within about ±5 dB per 200 ms step; Plate's
125 Hz still has one stretch 10 dB loud).

Guessed, not measured: under E.Level 12:00 the low reverb follows the knob (so that 7:00 is
silence) and above it stays put; at Tone 9:00 it is 1.2 dB under 7:00 (October's Hall). Cost:
about half a percent of one core while Tone is below 12:00 in those modes, nothing otherwise.

### 4.4 Time off noon (kept; Hall needed more than tables)

`rv6/time.py` follows the wet after the burst per octave band (250 Hz – 8 kHz, 20 ms steps) and
fits, per mode and position, the tank's RT60, the wet's level, the measured start's level and
the slower tank's level.

- **The first run scored the wrong thing:** Plate read 13–15 dB rms. It counted the burst's own
  steps (leftovers of the dry) and compared decibels down to the noise against a model with no
  noise. Now the first steps are skipped, the pedal's floor is under the model, and cells more
  than 30 dB under a band's top count 0.4. Plate then read 1.03 dB rms, which is the honest
  starting point.
- **Tables alone, Modulate, Plate, Room:** Modulate 9:00 RT60 1.08 to 1.01 s (levels 3.08 to
  2.28 dB rms; 7:00 had nothing to gain, 2.42 to 2.40, and was left); Plate 7:00 and 9:00 1.03
  to 0.95 and 0.92; Room 2.4 to 1.6 and 2.5 to 1.5, by turning its slower tank down at short
  Time (`slow_at` 0.45 and 0.43). Room's price: 1 kHz now reads 16–17 % short.
- **Tables alone, Hall: no.** 4.02 to 3.61 dB rms, with the RT60 moving *up* (0.72 to 0.82 s)
  on a decay that already read long. A fit that improves by moving a parameter the wrong way
  is fitting the wrong structure.
- **What Hall does** (`hall_start.py`: each Time's sweep response laid over noon's, per 20 ms):
  the pedal's first 260 ms are noon's at every Time, only 2 dB louder at 7:00 and 9:00, 2 dB
  quieter at 3:00, 3 dB quieter at 5:00, and the decay that Time sets begins after them. The
  model's tank was fed 26 ms in and decayed from the first moment, so at short Time its own
  start came through 7–8 dB loud at 60–100 ms.
- **The change:** Hall's tank fed 240 ms in. Noon was then retuned to where it had been
  (`noon_tail.py`: the tail 0.5–3 s per third octave within 0.3 dB of the old model, mean −0.3
  re the pedal; echo density 1.02–1.09 against the pedal's 1.04–1.08): tail gain −11.77 to
  −15.35 dB, the 1 kHz low shelf +2.55 to +4.55 dB. A trap there: `BiquadChain::design` scales
  a chain to unity at 1 kHz, so raising that shelf lowers everything above it and the gain has
  to move with it; the first try left the lows where they were and dropped the highs.
- **An override that did nothing.** A scan of where the measured start begins its extra decay
  returned the same number at 200, 225, 250, 275 and 300 ms: the renderer re-prepared the
  kernel after the override. Identical results across a scan mean the parameter isn't reaching
  the engine. Fixed (`Engine::early_from`); the scan then gave 2.50 / 2.41 / 2.55 / 2.92 /
  3.46 dB rms at Time 7:00. October's 200 ms stays (225 is better by less than the takes
  repeat).

Hall's result: levels over time after the burst 4.0 to 2.5 dB rms at 7:00 and 3.5 to 2.3 at
9:00; the decay at 2–8 kHz from 23–27 % long to 4–9 %; the start's waveform at 3:00 and 5:00
−17 to −22 dB and −15 to −20. Still off: at 500 Hz and 1 kHz the decay reads 15–18 % long, the
pedal's start is darker at short Time (the model keeps noon's colour), and the model dips 2–5 dB
at 220–280 ms.

### 4.5 Shimmer, the first refit (superseded by §4.6)

The 2026-11-15 scoring had Shimmer far off on bursts and stabs: fitted on held notes and
plucks only, its tail after a burst was half the pedal's length. The structure was kept and
its thirty constants refitted with the focused session added: the burst at twelve settings and
the stabs at five (`FOCUS_BURSTS`, `FOCUS_STABS`), `check-a` and `check-b` out. Least squares
by simulation, 275 evaluations, 47 minutes with seven workers (`FIT_WORKERS=7`).

| | before | after |
|---|---:|---:|
| held-notes residual | 1.760 | 1.816 |
| bursts-and-stabs residual, as that fit counted it | 0.821 | 0.514 |
| levels over time per take, dB rms | 4.2–12.1 | 2.4–6.1 |

What moved: the plain tank about half again as long (RT60 at noon 7.4 to 11.5 s) with 5 dB less
gain, the forward octave from ×1.02 to ×1.53 at noon, the tanks' input band wider (253–942 Hz
from 357–469), the long tank three times as loud. Stable at every knob's end.

This is the state the user heard and judged brighter and or louder than the pedal.

### 4.6 Shimmer after the user listened

**Finding it** (`build-rv6/shimmer/`): `bands.py` split the stabs into "while playing" and "the
tail" per octave band: the model was 3–5 dB over the pedal at 4–8 kHz while playing, and at
Tone 9:00 (`check-a`) 10–14 dB over. `look.py` plotted the wet per band in 20 ms steps for
`time-0700-shimmer` (`look-time-0700.png`): to 2 kHz the two agree; at 8 kHz the model holds
for most of a second after each stab, 20–30 dB over a pedal that dies with the stab.

**Why the fit had not seen it.** `focus_residual` compared only the steps where the pedal stood
6 dB over its floor. Where the pedal had gone quiet and NAM-X had not, nothing was counted.
That was an error in the comparison, not in the data. It now counts every step of every band,
with the pedal's floor under NAM-X. On the Time 7:00 stabs the 8 kHz band then reads 17.0 dB
rms and 4 kHz 8.3, with NAM-X alone over the pedal's floor in 51 steps. Under the corrected
comparison the first refit's residual is 0.425, not 0.514.

**Tried by hand on the first refit's constants** (`variants.py`: five takes, the corrected
comparison; residual 0.486 as fitted):

| change | residual | what it did |
|---|---:|---|
| climb's top at 2.5 kHz (`shift_lp`) | 0.437 | Time 7:00 stabs' 8 kHz 17.0 to 10.5, but Time 3:00 stabs' 8 kHz 2.7 to 5.9 and the noon burst's 5.7 to 9.1 |
| climb's top at 1.8 kHz | 0.447 | Time 7:00 to 6.0; Time 3:00 to 10.3, noon burst to 10.8 |
| `share` × 0.6 | 0.566 | worse everywhere: more steps over silence (51 to 78) |
| both tanks' highs shorter (`high_scale` 0.25) | 0.525 | 2–4 kHz goes with the top: the noon stabs' 2 kHz band 5.4 to 9.5 dB rms |
| climb × 0.5 at Time 7:00 only | 0.402 | Time 7:00 stabs' 8 kHz 17.0 to 7.3, 4 kHz 8.3 to 3.9; nothing else moves |
| climb × 0.25 at Time 7:00 only | 0.399 | 8 kHz 5.2, 4 kHz 3.4, six steps over silence; the burst there 3.05 to 4.26 dB rms |

Limiting the climb's band cures short Time and spoils long Time, where the pedal has that
treble. So the pedal's octave regeneration depends on Time, which the 2026-11-01 session had
already said on one held note. In the model the tank's loss per pass hardly moves the octave
loop, because the lines are short against the RT60 at any Time. Hence a table for the climb's
gain over Time: `ShimmerShape::back_at`.

**A second fit** with the corrected comparison and `back_at` free (278 evaluations, 43 minutes)
barely moved: the combined residual 0.877 to 0.868, held notes 1.830, bursts and stabs 0.386,
`back_at` 0.67 / 0.80 / 1 / 1.41 / 1.54. It is a middle between two sets of takes that want
opposite things at short Time: the noise bursts want the octaves, the guitar doesn't. As heard
it still left 8 kHz +4.6 dB at noon and raised Time 3:00's 8 kHz from +1.2 to +4.2.

**Three choices made by hand for the guitar**, each scanned, each against the held notes:

1. *The climb at Time 7:00 and 9:00: 0.35 and 0.6* (`guitar_first.py`). On the Time 7:00 stabs
   against the two bursts there:

   | `back_at` at 7:00 | stabs' residual | bursts' residual | stabs' 8 kHz, dB rms | steps over silence |
   |---:|---:|---:|---:|---:|
   | 0.25 | 0.388 | 0.395 | 5.1 | 13 |
   | 0.35 | 0.387 | 0.375 | 5.1 | 17 |
   | 0.45 | 0.411 | 0.357 | 6.4 | 23 |
   | 0.67 (the fit) | 0.516 | 0.331 | 10.8 | 35 |

   At 9:00 there are no stabs at the other knobs' noon; the burst alone prefers 0.8 (residual
   0.243 against 0.249 at 0.65 and 0.269 at 0.5). 0.6 is my interpolation toward the guitar's
   choice at 7:00, not a measurement. `check-b` suggests it is too low (§5).
2. *Tone 9:00 from `check-a`.* The model's 9:00 was a guess: 0.4 of noon's forward octave and
   of noon's share. Fitted on that take's burst and stabs, they are 0.093 and 0.013 (residual
   1.372 to 0.323): the pedal has next to no octave at Tone 9:00. The burst's levels 17 to
   3.6 dB rms.
3. *The climb's top at 3.2 kHz, from 5.06* (`scan_lp.py`, `eval_variant.py`). On the stabs the
   8 kHz band goes 5.4 to 3.3 dB rms at noon and 3.7 to 2.2 at Time 3:00 (Tone 5:00 4.4 to
   5.2). The price is on the bursts: their 8 kHz band 1–1.7 dB rms worse (noon 6.3 to 7.9,
   Time 5:00 12.2 to 13.6).

| state | held notes | bursts and stabs (corrected) |
|---|---:|---:|
| the first refit | 1.816 | 0.425 |
| the second fit | 1.830 | 0.386 |
| + choices 1 and 2 | 1.934 | 0.376 |
| + choice 3: as committed | 1.969 | 0.383 |

So the committed Shimmer is the second fit plus three hand choices that favour the guitar's
stabs over the noise bursts and the held notes. That is a judgement, not an optimum. If the
user's ears disagree with it, the second fit's constants are in `fit-2.json`.

**Tried and left: the bands the other way round.** The 2026-11-01 session read the forward
octave as wide (it passes 2.6 kHz at full level) and the climb as weak above that. The fits
have it the other way: forward to 1.07 kHz, climb to 3.2–5 kHz. Set by hand with nothing else
refitted (`scan_bands.py`), every variant is worse:

| forward top / climb top | residual | 8 kHz: stabs, bursts | 4 kHz: stabs, bursts |
|---|---:|---|---|
| 1075 Hz / 5063 Hz (as fitted then) | 0.376 | 4.1, 6.9 | 3.2, 3.7 |
| 3000 / 1800 | 0.425 | 6.8, 7.6 | 5.1, 5.3 |
| 3000 / 2500 | 0.419 | 6.0, 7.5 | 5.3, 5.3 |
| 4500 / 1800 | 0.444 | 7.2, 8.6 | 5.3, 5.6 |
| 4500 / 1300 | 0.450 | 7.9, 8.6 | 5.2, 5.5 |
| 6000 / 1800 | 0.452 | 7.3, 9.1 | 5.3, 5.7 |

This is not a fair test of that structure: the other twenty-eight constants were fitted around
the narrow forward band. A whole refit starting from the swapped bands was not run.

**Stability**, on the committed constants: an impulse at every knob's end decays steadily
(−324 dB after 59 s at Time and Tone 5:00); a full-scale six-note chord held 30 s and
full-scale noise held 20 s stay bounded (the wet's peak 4.6 and 2.7 times full scale: the wet
is linear, the app's output limiter is after it).

### 4.7 The modulation in the pedal's tail (found, not modelled)

The first two measures are on `time-0700-shimmer`, the take the user named. The third is on
three takes with longer tails: at Time 7:00 the tail is at the noise within two seconds, too
short for it.

- **Level ripple** on each stab's tail (`ripple.py`: the band's envelope with its trend
  removed): 2–3 dB rms in the pedal, in the old model and in the new one, with no steady rate
  in any of them. No difference.
- **Pitch wander** of the tail's partials (`pitch.py`): about 8 cents rms in all three; the
  pedal's first stab up to 23 cents, near 1 Hz. No clear difference.
- **Periodicity** (`periodic.py`): over the 4 s after the last stab, the share of the
  1–4 kHz envelope's movement (0.7–20 Hz) that sits within 0.12 Hz of a multiple of
  1.349 Hz, the pedal's LFO rate as October measured it in Modulate and in Shimmer. A movement
  with no pattern puts 18 % there.

  | take | the pedal | NAM-X (a rate per line) | NAM-X, one rate |
  |---|---:|---:|---:|
  | `noon-guitar-shimmer` | 41 % | 18 % | 23 % |
  | `tone-0500-shimmer` | 36 % | 12 % | 18 % |
  | `time-0300-shimmer` | 35 % | 12 % | 21 % |

  Two trial builds with one rate and the lines' phases changed: spread over a quarter cycle
  14–15 %, all together 15–23 % (`render_rv6_quarter`, `render_rv6_inphase`; two takes).

So the pedal's tail moves in a figure that repeats every 0.74 s, and no way of swinging the
tank's lines tried here reproduces it. I stopped there.

How strong this evidence is: three takes, one 4 s window each, Output A only, one band. The
2026-11-01 session saw the same thing another way (the octaves' envelopes ripple at 1.37 Hz and
its harmonics: 31 % on A and 58 % on B for 330 Hz's octave). It is enough to say the pattern is
there; it is not enough to say what makes it.

One constraint: the rate per line (`mod_spread` 0.6) was **chosen by ear on 2026-11-01**. With
one shared rate a chord's notes took turns swelling. Don't change it for a number; the two
`.one-lfo.wav` files exist so the user can say. (While the second fit ran, the user was also
offered two trial files of `time-0700-shimmer`, with the climb at 0.3 and the same with one
rate. No answer came on those; the final files replaced them.)

## 5. The numbers now

**Shimmer as heard** (`heard.py`; NAM-X less the pedal, dB; before this session / now):

| take | 125 Hz | 250 | 500 | 1 k | 2 k | 4 k | 8 k | all |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| noon | +7.0 / +3.7 | +5.2 / +1.3 | +3.2 / +0.9 | +0.3 / 0.0 | −0.2 / +0.5 | +2.1 / +2.4 | +4.5 / +2.2 | +1.4 / +0.5 |
| Time 7:00 | +10.3 / +4.8 | +8.4 / +2.3 | +3.7 / −0.3 | +3.3 / +0.2 | +2.9 / +0.5 | +4.0 / +0.3 | +5.2 / −3.0 | +3.6 / +0.2 |
| Time 3:00 | +9.8 / +5.1 | +9.0 / +3.8 | +5.6 / +3.3 | +2.8 / +1.2 | 0.0 / −0.6 | +1.7 / +2.0 | +1.7 / +1.4 | +3.1 / +1.3 |
| Tone 7:00 | +1.4 / −0.7 | +5.6 / +2.4 | +2.2 / −0.1 | −0.5 / +1.5 | −5.6 / +0.9 | −5.8 / +1.6 | −4.6 / +0.5 | +1.8 / +0.8 |
| Tone 5:00 | +6.1 / +2.6 | +5.1 / +0.8 | +5.3 / +2.3 | +5.2 / +2.7 | +4.4 / +1.6 | +5.0 / +1.9 | +4.0 / −1.5 | +5.0 / +2.2 |
| Tone 9:00 (`check-a`, now fitted) | −1.8 / +0.9 | +2.8 / +4.3 | +0.3 / +2.7 | −2.6 / +1.9 | +0.2 / −0.3 | +8.9 / +1.3 | +14.1 / +0.3 | −0.4 / +2.4 |
| E.Level 9:00 | +5.7 / +4.9 | +4.3 / +2.7 | +1.2 / +0.9 | −1.1 / +0.9 | −1.5 / +1.2 | +0.8 / +3.8 | +2.7 / +3.0 | −0.1 / +1.1 |
| E.Level 3:00 | +7.9 / +4.7 | +5.9 / +1.9 | +2.4 / −0.3 | +1.1 / +0.6 | +0.4 / +0.8 | +2.8 / +3.3 | +4.7 / +2.6 | +1.7 / +0.5 |
| **`check-b`, in no fit** | +6.5 / +1.5 | +5.1 / −0.4 | +3.3 / −1.4 | +3.7 / +0.1 | +3.7 / −0.1 | +3.4 / −0.5 | +0.6 / −6.0 | +3.6 / −0.4 |

Read with care:

- **`check-b`** (E.Level 9:00, Tone 3:00, Time 9:00) is the only out-of-fit Shimmer take. As
  heard it improved in every band but the top: 8 kHz is now 6 dB *under* the pedal. That is
  choice 1 at Time 9:00 (0.6, where the burst wanted 0.8) and probably choice 3. The scorer
  disagrees about this take: its guitar bands read 2.3 dB rms before and 4.8 now. Start here.
- **E.Level 9:00** got worse at 4 kHz (+0.8 to +3.8), and **`check-a`** is cured in colour but
  2.4 dB loud overall.
- **The 125 Hz octave** is 4–5 dB loud at noon, at Time 7:00 and 3:00 and at both E.Level
  settings; **Tone 5:00** is about 2 dB loud in most bands.

**Shimmer by the scorer** (the burst's decay per octave, model against pedal; full rows in
`build-rv6/report.txt`, before in `report-before.txt`): at noon within 16 % from 250 Hz to 2 kHz
and 30 % short at 4 kHz; at Time 3:00 4–30 % long; with Tone at 3:00 or 5:00 about 38 % long at
500 Hz; Tone 7:00 at Time 7:00 7–49 % long; Tone 5:00 at Time 5:00 37–44 % long to 1 kHz; at
Time 7:00 25–34 % short (the price of choice 1). Before the session these were 40–65 % short
at most settings.

**The five other modes as heard** (the same measure, run for this handoff on seventeen takes;
before this session / now):

| take | 125 Hz | 250 | 500 | 1 k | 2 k | 4 k | 8 k | all |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Plate, noon | −0.1 / −0.1 | −0.4 / −0.4 | +0.7 / +0.7 | −0.2 / −0.2 | +0.3 / +0.3 | +0.5 / +0.5 | −0.2 / −0.2 | +0.1 / +0.1 |
| Hall, noon | −0.4 / −0.8 | +0.2 / +0.2 | 0.0 / 0.0 | +0.1 / +0.5 | +0.3 / +0.5 | +0.1 / +0.4 | 0.0 / +0.3 | +0.1 / +0.2 |
| Room, noon | +0.1 / +0.1 | +0.2 / +0.2 | −0.2 / −0.2 | 0.0 / 0.0 | 0.0 / 0.0 | −0.1 / −0.1 | 0.0 / 0.0 | 0.0 / 0.0 |
| Plate, E.Level 9:00 | −2.3 / +0.1 | −2.6 / −0.1 | −1.3 / +1.1 | −2.2 / +0.2 | −1.7 / +0.7 | −1.5 / +0.9 | −1.0 / 0.0 | −1.9 / +0.5 |
| Modulate, Time 7:00 | −1.9 / −1.9 | −2.5 / −2.5 | −0.2 / −0.2 | −0.1 / −0.1 | −0.8 / −0.8 | +1.0 / +1.0 | +1.3 / +1.3 | −0.4 / −0.4 |
| Plate, Time 7:00 | −0.4 / −0.4 | −1.0 / −1.0 | 0.0 / −0.4 | −0.2 / −0.4 | −0.2 / −0.5 | +0.4 / 0.0 | +0.3 / 0.0 | −0.2 / −0.5 |
| Room, Time 7:00 | −0.8 / −1.0 | +0.2 / −0.2 | −0.5 / −0.8 | −0.7 / −0.5 | −0.6 / −0.5 | −0.5 / −0.7 | 0.0 / −0.3 | −0.4 / −0.6 |
| Hall, Time 7:00 | −5.7 / −4.2 | −3.4 / −2.3 | −3.5 / −2.6 | −2.0 / −1.8 | −1.3 / −1.2 | +0.3 / 0.0 | +1.5 / +1.1 | −2.8 / −2.2 |
| Hall, Time 9:00 | −5.0 / −3.9 | −2.9 / −2.1 | −3.0 / −2.3 | −1.7 / −1.5 | −1.1 / −1.1 | −0.4 / −0.7 | −3.0 / −3.6 | −2.4 / −1.9 |
| Plate, Tone 7:00 | −2.7 / +2.5 | −1.1 / +1.0 | −0.6 / +0.2 | +0.2 / 0.0 | +1.8 / +0.2 | +1.7 / +0.9 | 0.0 / −0.3 | −0.3 / +0.3 |
| Hall, Tone 7:00 | +2.5 / +0.9 | −0.4 / −1.4 | −0.1 / −0.4 | −0.3 / −0.1 | +1.4 / +0.5 | +0.3 / +0.3 | 0.0 / +0.1 | −0.1 / −0.6 |
| Room, Tone 7:00 | +3.0 / +0.1 | +0.8 / +0.2 | +0.5 / +0.1 | +0.5 / +0.5 | +1.7 / −0.2 | +1.2 / −0.3 | +0.3 / −0.1 | +0.7 / +0.2 |
| Hall, Tone 7:00, Time 7:00 | −3.5 / −0.8 | −5.6 / −0.8 | −4.1 / −2.4 | −2.5 / −1.9 | −0.6 / −1.1 | −1.2 / −0.4 | 0.0 / +0.1 | −3.9 / −1.7 |
| Plate, `check-a` | +2.2 / +1.5 | +1.3 / −0.7 | +1.5 / +1.1 | −0.6 / −0.3 | −0.4 / +0.6 | −0.1 / +0.9 | −0.6 / −0.6 | +0.3 / +0.2 |
| Hall, `check-a` | +3.7 / 0.0 | +2.4 / +0.3 | +0.4 / −0.1 | −0.3 / +0.5 | −0.6 / +0.7 | −1.1 / +0.2 | −0.2 / +0.2 | +0.7 / +0.2 |
| Hall, `check-b` | −6.9 / −3.5 | −5.3 / −1.7 | −5.1 / −1.8 | −4.1 / −1.0 | −2.8 / −0.7 | −1.1 / +0.2 | −0.1 / +0.6 | −4.5 / −1.4 |
| Room, `check-b` | −2.9 / −0.7 | −2.2 / +0.2 | −2.5 / −0.4 | −2.8 / +0.2 | −2.0 / +0.1 | −1.9 / −0.3 | −1.9 / +0.1 | −2.5 / 0.0 |

- **Nothing rings on.** No band stands more than 1.5 dB over the pedal except Plate's 125 Hz
  at Tone 7:00, so the blind spot of §4.6 hides nothing in these modes.
- **Hall at short Time is still quiet on guitar:** about 2 dB overall and 4 dB at 125 Hz at
  Time 7:00 and 9:00, and 1.4 and 3.5 dB in `check-b`. The scorer has the same 2 dB on guitar
  (−2.1 and −2.2) while its burst reads within 0.5 dB. The tables fitted on the bursts (§4.4)
  moved the guitar's energy by only half a decibel, and Hall's 250 Hz decay now reads 11–14 %
  short where it read 3–7 % long. My reading, not tested: the miss is the bass, 125–500 Hz,
  where a guitar has its energy and a white burst weighs little; `time.py` fits no band under
  250 Hz and weighs its six octave bands alike. This is the largest error left in the five
  modes.
- **Plate's bass at Tone 7:00** went from 2.7 dB short to 2.5 dB over at 125 Hz.
- Noon is untouched and within 0.8 dB in every band; E.Level 9:00 and both check settings came
  in (Room's `check-b` from −2.5 dB to 0.0, Hall's `check-a` bass from +3.7 to 0.0).

**The five other modes by the scorer**, the median over each mode's guitar takes of the bands'
means, before to now: Modulate 2.00 to 1.74 dB rms, Spring 1.61 to 1.57, Plate 1.57 to 1.56,
Hall 1.20 to 0.98, Room 2.26 to 2.05; the worst level error 2.2 dB (from 5.3).

## 6. What is still wrong, and where I would look

My reading; question it.

1. **Ask what the user hears now.** Everything in §4.6 is numbers. The second round's files
   have no verdict. If Shimmer is still "very different" after the as-heard table reads within
   about 2 dB overall, the difference is not in octave-band energy, and items 2 and 3 are the
   candidates.
2. **The repeating 1.35 Hz pattern (§4.7).** Untested ideas, none with evidence yet:
   - a single modulated delay or all-pass that the whole signal passes (before the tank, after
     it, or once per pass of the loop), instead of sixteen lines swung apart: one path would
     give every partial the same phase of movement;
   - the shifter. Twice the grain rate of a 299 ms grain is 6.7 Hz, which is the fifth multiple
     of 1.35 Hz, so `periodic.py` counts the grain ripple of both the pedal and the model as
     "on the LFO". Whether the pedal's pattern is its shifter's own long period rather than
     the tank's LFO has not been separated. The focused session's held 330 Hz notes (unread)
     are the cleanest signal for that: one line per octave, at every knob position;
   - Output B. Everything in §4.7 is Output A.
3. **The top octave comes late.** Each step up waits half a grain and a pass through the tank:
   at noon the 8 kHz band peaks 0.2–0.4 s after the pedal's. The fits answer by holding the
   forward octave's band narrow and letting the climb carry the top, which is the opposite of
   what the held notes read (§4.6, "the bands the other way round"). A refit *from* the wide
   forward band, with the corrected comparison, is the experiment that was not run. It costs
   about 45 minutes of machine time.
4. **The swell at long Time.** At Time 3:00 and 5:00 the pedal starts 13–16 dB quieter and
   peaks at 0.4–0.7 s after a burst; the model starts at once. Hall's lesson may apply (§4.4):
   lay the Time takes' responses over noon's before fitting anything. Not done for Shimmer.
5. **Bursts against guitar.** At short Time the two sets pull the octave regeneration in
   opposite directions (§4.6, choice 1). A linear, time-invariant loop should serve both. That
   it doesn't says the structure is missing something that depends on the signal's length or
   level, or that noise through a pitch shifter isn't a fair probe. The study had already
   noted that the held notes' tails have their knee 20–40 dB down and the bursts' 15–20.
6. **Tone 9:00.** One take (`check-a`), at Time 3:00 and E.Level 3:00, now sets it in Shimmer;
   October's Hall sets it in the other modes. Redoing the six `tone-0900` takes is a short
   capture; ask the user.
7. **The other modes**, smaller: Hall's bass at short Time (2–4 dB low on guitar from 125 to
   500 Hz: §5), Hall's and Room's 500 Hz – 1 kHz decay at Time 7:00–9:00 (15–18 % long; Room's
   1 kHz 16–17 % short), Plate's 250 Hz at short Time, Plate's 125 Hz at Tone 7:00 (2.5 dB
   over), Spring's 250 Hz (12–29 % long) and its 4 kHz octave at long Time (22 % short),
   Modulate's 2–8 kHz at 3:00 and 5:00 (to 18 % short), the low reverb under E.Level 12:00
   (unmeasured).
8. **Unanswered questions to the user:** reverb knobs in real units (Time in seconds, Level in
   dB), as the other pedals got; whether to redo the Tone 9:00 block.

**What done would mean** (the study's own bar, plus this session's): at the measured positions
the decay within 10 % per octave band and the level within 1 dB per third octave; on guitar the
as-heard table within about 2 dB per band with nothing ringing on over the pedal's silence;
`check-b` no worse; the held-notes residual not far over 1.8; stable at every knob's end. Then
listening files for the user, who decides.

## 7. Traps this session fell into

1. **A comparison limited to where the reference sounds is blind to the model ringing on.**
   It cost a wrong refit and a wrong report to the user (§4.6). `rv6/time.py` still fits only
   down to 6 dB over the pedal's floor, and the scorer's guitar numbers are means of decibels
   where the pedal sounds; the as-heard table in §5 is the check on both.
2. **A spectrum over a whole response cannot tell a filter from an added signal** (§4.2), nor a
   right level with a wrong decay from the reverse (§4.3). Check a spectral fix on guitar over
   time before keeping it.
3. **Lay the responses at every knob position over noon's before fitting tables** (§4.4).
   Hall's whole problem was visible in one printout.
4. **Test every new override once with a value that must change the output.** A scan that
   returns the same number five times is a parameter that isn't connected (§4.4).
5. **A fit that barely moves is information.** The second Shimmer fit converged at once
   between two sets of takes that disagree. Look at which takes pull which way before running
   it again with other weights.
6. **`BiquadChain::design` scales every chain to unity at 1 kHz** (or at the frequency given):
   a shelf near there moves the other side.
7. **A short decay read from 100 ms steps reads long.** Calibrate the reading on the model
   itself (§4.3).
8. **Machine time:** a Shimmer fit is 45 minutes and an evaluation of all targets 2; the scorer
   3.5; a take through `heard.py` well under a minute. Run fits in the background against a
   private copy of the renderer, since a fit that finds the binary rebuilt under it mixes two
   models.

## 8. Working in this repository

- `CLAUDE.md` at the root is the project guide; `tools/capture/README.md` the capture manual
  (its "Where we stopped, 2026-11-17" has this session in a paragraph).
- **Branches:** this work is on `captures/reamps`, merged into `pedals/all`, the build with
  every pedal model; never commit on `pedals/all` itself. Ask the user before switching the
  checkout's branch. No git worktrees. Scratch and builds go in git-ignored `build-*/` folders
  under the repository, not `/tmp`. The user pushes.
- **Sound:** Shimmer was closed by ear on 2026-11-01 and reopened on 2026-11-17 by the user's
  "do both". A change to how any Reverb mode sounds needs listening files and the user's
  verdict; the rate per line is an earlier by-ear choice (§4.7). The EQ, Tremolo, Delay, Chorus
  and Flanger are frozen.
- **`DspGoldenTest`** must stay green; when the Reverb is meant to change, replace only the
  `reverb-rv6-*` lines of `tests/dsp-golden.txt` (`NAM_GOLDEN_PRINT=1 build-test/DspGoldenTest`
  prints them), never the whole file.
- **Before committing:** the clang-format check CI runs on `src/`, `tests/`, `bench/`
  (`--dry-run --Werror`); `git diff --cached --stat`; no personal paths, usernames or machine
  details anywhere: the repository is public.
- **Running NAM-X** (self-tests included): with `PIPEWIRE_REMOTE=none JACK_DEFAULT_SERVER=none
  JACK_NO_START_SERVER=1` and HOME and the XDG folders pointed into a `build-*/` folder.
- **A new capture** is the user's to record. Plans use the positions 7:00, 9:00, 12:00, 3:00,
  5:00 only, ordered so each step turns as few knobs as possible, with the take count and the
  minutes stated before handing the plan over.
- A missing tool or Python package: ask the user to install it; don't work around it.
