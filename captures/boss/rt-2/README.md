# Boss RT-2 Rotary Ensemble

Measured 2026-11-07 on a 9 V supply, mono in (Input A), both outputs recorded (Output A on
input 2, Output B on input 1), factory settings (stereo output mode, noise suppressor off,
pedal switch Type 1):

| Session | Takes | What it gives |
|---|---|---|
| [`2026-11-07/`](2026-11-07/) | 62 (`plans/rt-2.json`) | speeds per knob position, the Rise/Fall ramps, Level and Balance, Drive per Mode with the rotors turning (441 Hz steps), the three Modes at both speeds (chord, noise, clicks), the low-E DI |
| [`2026-11-07-followup/`](2026-11-07-followup/) | 34 (`plans/rt-2-followup.json`) | each rotor alone per Mode — **ran at Drive 5:00 from its second take** (see *The follow-up's Drive*), superseded; the mono output; stop ↔ 10 Hz |
| [`2026-11-07-isolated/`](2026-11-07-isolated/) | 10 (`plans/rt-2-isolated.json`) | **each rotor alone per Mode, clean** (Balance 5:00 = the horn, 7:00 = the drum, 12:00 both; 45–60 s of white noise at Slow 9:00), every take checked as it landed: the model's data |
| [`2026-11-07-reamp3/`](2026-11-07-reamp3/) | 19 (`plans/rt-2-reamp.json`) | the guitar DI dataset through the pedal (`tools/capture/reamp.py`): the A/B against the model (*Reamps*; two earlier runs went wrong) |
| [`2026-11-21-focused/`](2026-11-21-focused/) | 15 (`plans/rt-2-focused.json`) | **Mode III's Drive with the rotors parked** (Slow 7:00): a quiet sweep, 110 and 441 Hz ladders, a noise ladder and ten seconds of funk per take; the drum alone, the horn alone and both at Drive 7:00 and 9:00, both at 12:00, 3:00, 5:00, a lead at 12:00 kept out of every fit (*Mode III's Drive*). Output B's input ran 6 dB hotter than Output A's (the bypass take gives the ratio). A first run on 2026-11-18 recorded silence with the effect on: Level at 7:00 mutes |
| [`2026-11-21-focused-m12/`](2026-11-21-focused-m12/) | 29 (`plans/rt-2-focused-m12.json`) | **Modes I and II's Drive with the rotors parked**: the same takes, a block per mode after one bypass take (*Modes I and II's Drive*). Both inputs padded: Outputs A and B at the same level |

The controls: two concentric knobs — Level (inner) and Drive/Balance (outer: a toggle on top
picks which stored value the ring sets; both act all the time), Fast (inner) and Slow (outer)
— and on top the Mode (I/II/III), Rise/Fall Time (SLOW/FAST) and Drive Knob toggles. Holding
the pedal switches the rotors between Slow and Fast (LED green/red). **The Drive Knob toggle
is easy to leave on the wrong side, and nothing on the pedal shows it**: three runs went wrong
that way. A ring move with the toggle on DRIVE stores Drive and leaves Balance alone, and the
reverse.

Fit: `tools/capture/fit_rt2.py` (paths, filters, tone, `--write` → `src/dsp/Rt2RotaryData.h`;
its results in [`2026-11-07-isolated/fit/`](2026-11-07-isolated/fit/); the follow-up's
`fit/` is the first model's). The C++ engine (`src/dsp/Rt2Rotary.h`) rendered offline:
`tools/capture/render_rt2.cpp`. The analyses behind the numbers below: `tools/capture/rt2/` (the rates:
`rotor_rates.py`; Level and Balance: `balance_modes.py`; Drive: `drive_harmonics.py`, `drive_fit.py`; the engine:
`check_engine.py`; the reamps and the first session: `reamp_ab.py`, `session1_modes.py`).

## The model

```
x  → hard clip at +2.25 dB re the guitar's peak → Drive (a curve with a moving bias; a mode sets its gains and filters)
   → crossover: high-pass (4th-order Butterworth, 470–530 Hz) to the horn, low-pass (2.5–2.9 kHz) to the drum
   → horn:  the microphone's direct path      d(θh) = a V over each turn (a table)
            ~28 reflections off the room     d(θh) = T − S·cos 2π(θh − φ)
   → drum:  one path                          d(θd) = a V
   each path read at its delay, split into angle components 1, cos, sin … of θ − φ, each
   through its own measured filter; plus a fixed filter per rotor
   → the horn's high shelf (per output) → Balance → Level
```

θ is each rotor's angle in turns. The pedal is digital (48 kHz per the manual) and the model
is linear in the signal after Drive.

## The rotors

The speed knobs set the horn; the drum turns a fixed ratio slower (horn ÷ drum = **1.0870**
fast, **1.0270** slow, the same within 0.04 % at every position and in every Mode). Every
turn repeats exactly (a held tone's demodulated pattern correlates 1.000 after 8 turns): no
speed wander.

| Knob | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|---|
| Slow (horn, Hz) | **stop** | 0.2445 | 0.80–0.82 | 1.959 | 2.500 |
| Fast (horn, Hz) | 2.500 | 4.099 | 6.81–6.88 | 9.024 | 10.000 |

(12:00 read twice, the knob set again in between; Slow 9:00 set by hand again in the
isolated session gave 0.2567 Hz in every Mode.) The ends are round numbers: probably the
design's 2.5 and 10 Hz.

**Rise/Fall.** Tracked by matching each moment of a held tone to its pattern over one turn
(Viterbi over 512 angle states):
- FAST: the horn changes speed within ~1 s, the drum in ~2 s (up to 5.4 Hz at once, then
  creeping to 6.3 over ~5 s);
- SLOW: the horn in ~3 s each way (~2 Hz/s); the drum **coasts down** — its speed decays
  towards 0 (≈9 s time constant, the speed's ratio per second constant) until it meets the slow
  speed and is caught there — and spins up exponentially (τ ≈ 13.6 s; the take ended at 90 %).

NAM-X: horn FAST exponential τ 0.25 s, SLOW slew 2 Hz/s; drum FAST τ 1 s each way (coasting
down), SLOW coast τ 9 s, spin-up τ 13.6 s. Not yet fitted: the stop ↔ 10 Hz take.

## The horn and the drum

Per angle bin (white noise, cross-correlation), the responses show the structure directly
(figures in the analysis scripts' output):

- **The horn's direct path** per microphone: its delay is a **V** over the turn — 0.77 ms at
  the nearest point, rising nearly linearly to ~1.9 ms half a turn later (Mode I; II
  0.72–1.83, III 0.83–1.90) — the distance from a point on a ~22 cm circle to a microphone
  almost touching it; fitted as that shape (a free Fourier series rang around the tip, where
  the path is strongest). Its level swings 20–22 dB (Modes I, II), 10–11 dB (Mode III).
  **Outputs A and B are two microphones 0.17 turn (~60°) apart** (0.168, 0.166, 0.180 by
  Mode): that's the stereo. Mode III has a second arrival of about the same strength crossing
  the direct one; the V runs between them.
- **A fixed path** at ~0.85 ms on both outputs: part of the horn's sound doesn't move.
- **Reflections**: ridges at 3–15 ms, each a cosine curve of its own (T, S ≈ 0.5–0.7 ms, φ);
  most are nearest the horn at ~0.45–0.56 turn (behind it) and they sit at the same delays
  and phases on both outputs — image sources in the room (28, 28, 25 found by Mode).
- **The drum**: one moving path, a V (the scoop passing the microphone, 0.6–0.8 ms of swing),
  then a fixed tail; **identical on both outputs** (correlation 1.000).
- **The crossover**: alone, the pedal's horn has almost nothing under 300 Hz (−57 dB at 125 Hz,
  −35 at 250 re 1 kHz) and its drum almost nothing over 3 kHz (−50 dB at 4 kHz). Fitted without
  one, the horn's short filters leaked −15 to −20 dB of low end and the drum's a little treble;
  as filters after the model they fixed the spectrum but turned the phase in the band (the
  horn's waveform error went from −5 to 0 dB). Ahead of the rotors, in the fit and in the
  engine, the filters absorb its phase.
- **Mono** (Output B unplugged): Output A carries one microphone — the same as stereo A, level
  within 0.35 dB. NAM-X's mono is Output A.

**Fit quality** (held-out NMSE on each isolated take, all bands; least squares over the first
20 s, the rest scores): the horn −5.2/−4.6 dB (Mode I A/B), −4.4/−4.3 (II), −5.0/−4.9 (III);
the drum −14.6 (I), −14.9 (II), −10.9 (III) — the clean takes lifted the drum by 3 dB (−11.9,
−11.8, −7.4 before), the horn little. The horn loses most above 6 kHz (−1 to −2 dB there): least
squares shrinks what it can't place, ~2–3 dB of its top in Modes I and II, 5–8 dB in Mode III. A
high shelf per output puts it back (+1.9/+2.0 dB from ~650 Hz in Mode I, +3.0 from ~1.2 kHz in
II, +6.3/+7.0 from ~3.2 kHz in III; `fit_rt2.py tone`), which trades ~0.7 dB of waveform error
(−4.5/−3.9 in Mode I) for the pedal's spectrum. The horn alone then follows the pedal within
±1.5 dB from 630 Hz to 10 kHz; its low-end leak is down to +12–15 dB over the pedal's own at
100 Hz in Mode I (II and III within 4 dB) and +8–22 at 160–315 Hz (the pedal's is 35–57 dB under
its 1 kHz there). The drum alone is within 0.6 dB up to 630 Hz, −1.4 at 1 kHz, −4 at 1.6 kHz, and
+7 to +12 dB over 3 kHz, where the pedal's is 50 dB down. The C++ engine
matches the fitted model to −64 dB (horn) and −76 dB (drum) at 48 kHz.

## Level and Balance

From the first session's chord tones (fast, Mode I), dB re 12:00:

| Knob | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|---|
| Level | mute | −10.5 | 0 | +5.25 | +6.74 |
| Balance: horn | off (−40…−49) | −10.3 | 0 | +4.3 | +5.9 |
| Balance: drum | +7.3 | +5.8 | 0 | −8.1 | off (−46…−59) |

Level is flat within 0.1 dB across the tones. Balance is a plain gain per rotor. In the
isolated session the both-rotor take is the power sum of the horn alone −6.2 dB and the drum
alone −5.8 dB, within 0.3 dB in every octave and Mode: that session's 12:00 (the ring set by
hand) had 1.5 dB more drum and 0.3 dB less horn than the first session's. NAM-X takes the
isolated session's 12:00 (the model's), the first session's law around it: horn −10.0, 0,
+4.6, +6.2; drum +5.8, +4.3, 0, −9.6.

## Drive

**The input clips** (hard) between 0 and +3 dB re the guitar's peak: clean (harmonics −68
dB) up to 0 dB, ~3 % THD at +3 dB; NAM-X: +2.25 dB.

**Drive**, from the first session's steps (441 Hz, rotors turning, each step over exactly two
turns, re the same Mode at Drive 7:00): a level-compensated clipper — soft notes keep about
their level, loud ones saturate into a near-square wave.

| Mode I | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|
| gain into the clipper | ~+9 dB | +16.7 | +24 | +26.4 |
| soft notes | −1.1 dB | −1.6 | +2.8 | +4.5 |

Mode III is hotter: already ~Mode I's 9:00 at 7:00, saturating from 9:00 (+30–33 dB into the
clipper) with a 2nd harmonic (−14 to −30 dB) Mode I lacks at loud levels. Mode II was measured
at 7:00 only. **The Drive was measured again with the rotors parked and is a different model
since 2026-11-21: *Mode III's Drive*, *Modes I and II's Drive*.** This section is the first
session's reading, kept as the record; nothing in NAM-X uses it any more.

**The clipper's harmonics** (the steps' harmonics with the rotors' own response divided out,
from the same session's noise takes): saturated, the pedal puts out nearly a square wave — the
3rd, 5th, 7th, 9th at −9…−12, −13…−17, −17…−22, −20…−28 dB re the fundamental (a square: −9.5,
−14, −17, −19) — and it gets there early: Mode III at 7:00, −18 to −12 dB in, has its 5th–9th
16–45 dB under the fundamental where tanh gives 46–140. Under −18 dB the pedal's Drive does
something no static clipper does: a strong 2nd harmonic (−11 to −18 dB) and the fundamental held
back 1–4 dB, gone a step later. A refit of the clipper's gain and level (tanh, or v/(1+|v|^p)^(1/p))
scores 40–50 % better on the steps but trades the soft notes' level against the harmonics; not
adopted. That was the model's Mode III on guitar: 7–17 dB short at 3–5 kHz (*Reamps*), though
within 1.5 dB on noise. (The parked-rotor session explains both: the curve has a dead zone
whose place depends on the level played.)

## Mode III's Drive (2026-11-21)

**Method.** With Slow at 7:00 the rotors stand still, and a parked rotor is a fixed filter:
recording = H · Drive(played). H comes from each take's quiet sweep (deconvolved; its harmonic
products cut), and a recording divided by it is the Drive's own output — the drum-alone take
under ~1.1 kHz, the horn-alone one above (`tools/capture/rt2/parked.py`). Two checks that
this holds: the both-rotor takes are those two responses mixed at Balance 12:00's known gains
(horn −6.3 dB, drum −5.8 re each alone) through the same Drive output to −27 dB (Drive 9:00) and
−38 dB (7:00), so **one Drive sits ahead of the crossover and both rotors**; and a take played
twice repeats to 0.01 dB in every harmonic.

**What it is** (`src/dsp/Rt2Drive.h`; fitted by `drive3_curve.py`, `drive3_fit.py`):

```
gain in (the knob) → one-pole low-pass, 5 kHz → y = F(x − c) → that low-pass's inverse → gain out (the knob)
F, odd: 8.93·v² up to −23.3 dB (a square-law dead zone), then the slope falling linearly to
        zero at −7.9 dB, flat after: a ceiling at −12.2 dB (levels re the guitar's peak, at Drive 7:00)
c, the bias: dc/dt = (C_FAR − c)/0.43 s + 63/s · G(x − c), G odd: the excursion past ±(−24.1 dB)
```

- **One curve.** Every settled step of the 441 Hz ladders at Drive 7:00 and 9:00 lies on the
  same two parabolas, each step at its own bias, within 1.3 % of its level (a 40-point free
  curve did no better: 1.6 %).
- **The bias moves.** At rest it sits at −23.7 dB, just where the signal starts to pull: a quiet
  note rides the curve's side and comes out nearly clean (a second harmonic, the level held
  back up to 2 dB by −36 dB, 4 dB by −24). A louder one pulls the dead zone under itself —
  crossover distortion, then the ceiling: from −18 dB in 40 ms, from −12 dB in 10 ms, at the
  guitar's peak in 5 ms — and it drifts back out over a few hundred ms once the playing stops.
  That is the "something no static clipper does" of the first session.
- **The knob only moves the gains.** Into the curve 0, +18.6, +27.7, +32.0, +33.3 dB at 7:00,
  9:00, 12:00, 3:00, 5:00; after it 0, −11.5, −14.9, −15.4, −15.5 dB. Already at 7:00 anything
  over −12 dB re the guitar's peak meets the ceiling.
- **The filter round the curve**: a −36 dB sine is turned down 2.0 dB at 500 Hz re a far
  quieter signal, 0.9 dB at 4 kHz, 0.2 dB at 8 kHz, and without the filter the model's guitar is
  2.5–7 dB short at 6–10 kHz. At 5 kHz it is within 1 dB there (4 kHz: 1.5 dB over at 3–6 kHz; 6 kHz:
  2 dB short at 10 kHz).
- The pedal's Drive is oversampled (aliases of a square wave's harmonics 20–40 dB under their
  neighbours, more near 22 kHz): NAM-X runs it at 4×.

**Against the pedal, at its outputs** (`drive3_check.py`: each Drive through the parked rotors'
measured responses; the guitar, third octaves, model − pedal):

| | 100 Hz – 2.5 kHz | 3.15–5 kHz | 6.3–8 kHz | 10 kHz | waveform residual |
|---|---|---|---|---|---|
| the Drive until now, 7:00 | 0 to −1.9 dB | −5 to −8 | −13 to −18 | −19 | −13 to −14 dB |
| the Drive until now, 12:00–5:00 | −1 to −2.8 | −2.5 to −6.5 | −6.5 to −13 | −13 to −16 | −9 to −13 |
| the Drive until now, the lead at 12:00 | −1 to −4.2 | −5.5 to −9.3 | −11 to −17 | −20 | −11 |
| **now**, every position | −0.2 to +0.8 | +0.2 to +0.7 | −0.4 to +0.6 | −1.0 to −1.2 | −15 to −23 |
| **now**, the lead at 12:00 (in no fit) | 0 to +0.6 | +0.2 to +1.1 | −0.5 | −1.7 | −15 |

12:00, 3:00 and 5:00 gave only their two gains each to the fit; the curve and the bias come from
7:00 and 9:00. On the noise ladder (in no fit) the pedal's gain re its quietest step is −0.7,
−1.6, −3.1, −5.1, −3.9 dB at −24, −18, −12, −6, 0 dB; the model's −0.5, −1.4, −3.7, −4.8, −3.3.
The C++ against the fitted model: −50 dB at Drive 7:00, −34 to −39 dB above (their oversampling
filters differ); about 0.4 % of a core.

**The rotors' level.** Their filters were fitted on −12 dB white noise through the pedal at
Drive 7:00, which this Drive turns down 1.22 dB: they hold that gain, and the engine gives it
back (`ROTORS_DB`).

**Not modelled.** Driven hard for long, the bias goes on past the centre, to about −0.08 of the
signal's size over ~200 ms (an even harmonic at −25 dB under a near-square wave). Over 10 kHz the
model is 1–4 dB short.

## Modes I and II's Drive (2026-11-21)

`2026-11-21-focused-m12`: Mode III's session again, a block of takes per mode
(`tools/capture/rt2/drive12.py`, `RT2_MODE=1` or `2`).

**At 7:00 the Drive is out of the path.** Both ladders come out clean to the guitar's peak
(harmonics under −70 dB; Mode III distorts at 7:00). So the both-rotor 7:00 take's quiet sweep is
the parked rotors' exact response, and any other take divided by it is the Drive's whole output:
the rotors one at a time aren't needed (they agree: Balance 12:00 is the horn at −6.4 dB and the
drum at −5.7 re each alone, to −28 dB, as in Mode III).

**With Drive on it is Mode III's stage.** The ladders show the same signs: a quiet tone carries a
2nd harmonic and loses level (5 dB by −24 dB at 9:00); louder, the harmonics turn odd and the
level comes back, then the ceiling squares it. With Mode III's curve and bias untouched and only
two gains per position free, the fit is 8–10 % off each 25 ms's level; with the curve and the
bias free too, 8.40 % becomes 8.34 %. **The pedal has one Drive stage, and a mode sets its
gains** (dB, at 9:00, 12:00, 3:00, 5:00):

| | into the curve | after it |
|---|---|---|
| Mode I | −1.5, +8.4, +13.1, +14.5 | +0.3, −7.6, −10.1, −10.7 |
| Mode II | −1.9, +8.3, +13.0, +14.5 | +0.5, −7.6, −10.2, −10.8 |
| Mode III | +18.6, +27.7, +32.0, +33.3 | −11.5, −14.9, −15.4, −15.5 |

Modes I and II are one law (NAM-X uses their mean); Mode III puts 19–20 dB more into the curve
at every position, and its 7:00 (0 dB) is about their 9:00.

**A low-pass after the curve.** A quiet sweep with Drive on, over the 7:00 one, is the same at
all four positions: −0.4 dB at 2 kHz, −1.0 at 3.15, −2.4 at 5, −5.1 at 8, −7.0 at 10, −8.9 at 12
re 1 kHz — a bilinear one-pole at 5.52 kHz (at 48 kHz) within 0.03 dB (Mode II: 5.50 kHz, 0.1 dB).
It follows the curve: ahead of it the model's guitar is 2–4 dB over at 5–8 kHz (the curve's
products unfiltered), after it within 0.4 dB. Mode III's low-pass *ahead* of the curve is another
filter, which these modes don't have (their quiet response is this one pole, once); whatever
follows Mode III's curve is in its rotors' filters, fitted through its Drive, so Mode III stays
as it was.

**Against the pedal, at its outputs** (`drive12.py check`: the Drive through the parked rotors'
response; the guitar, third octaves, model − pedal, both outputs):

| | 200 Hz – 2.5 kHz | 3.15–5 kHz | 6.3–8 kHz | 10 kHz | level |
|---|---|---|---|---|---|
| the clipper until now, 9:00 | −0.6 to +1.9 dB | −2.3 to −3.9 | −5.4 to −6.1 | −2 to −6 | +1.4 dB |
| the clipper until now, 12:00 | −0.5 to −2.2 | −2.7 to −3.6 | −5.6 to −9.2 | −10 | −0.7 |
| the clipper until now, the lead at 12:00 | −0.1 to −2.3 | −5.4 to −9.4 | −14 to −19 | −14 | −0.9 |
| the clipper until now, 3:00 and 5:00 | 0 to +1.2 | +0.6 to +2.3 | +0.9 to +3.5 | 0 to +3.5 | +0.6 to +0.9 |
| **now**, Mode I, every position and the lead | −0.3 to +0.3 | −0.4 to +0.2 | −0.7 to 0 | −0.9 to −2.7 | ±0.1 |
| **now**, Mode II, every position and the lead | −0.3 to +0.2 | −0.6 to +0.1 | −0.6 to +0.5 | −0.7 to +1.0 | −0.1 |

At 100 Hz the model is 0.1–1.1 dB under. Mode I's parked horn has little over 10 kHz (the
recording's own hiss by 12.5 kHz); Mode II's has more, and the model is within 2.3 dB there. On the
noise ladder (in no fit) the model's gain is within 1 dB of the pedal's on most steps, 1.7 dB too
low at worst (−12 dB at 9:00, −24 dB at 12:00). The lead is in no fit, and the positions gave
only their two gains each.

**Rotors turning, another day** (the reamps of 2026-11-07, Mode I at Drive 3:00, in no fit;
`drive12.py turning`): level +0.2 to +0.4 dB (the clipper: +0.8 to +1.1), tone within ±1.3 dB
from 80 Hz to 5 kHz (the clipper: −1.3 to −6.7 at 3.15–5 kHz), −1.6 to −3.4 at 8 kHz (−9.6 to
−10.5).

**In NAM-X** (`rt2::Drive`, every mode): the C++ against the fitted model −38 to −49 dB with
Drive on, −67 dB at 7:00. The Drive's delay (22.5 samples, its oversampling) is now there in
every mode and at 7:00 too, so the latency doesn't move with the knob. **Between 7:00 and 9:00
nothing was measured**: the gain into the curve comes up from −60 dB, linear in dB, the gain
after it holding a quiet signal at 0 dB, and the low-pass fades in.

**Seen, not modelled.** With the drum alone (Balance 7:00) and Drive at 7:00, a 110 Hz tone at
the guitar's peak clips (a 3rd harmonic at −13 dB, the level 1.9 dB down) in both modes; with
both rotors it doesn't: a ceiling after the Balance, nothing of the Drive's. And a measuring
note: the takes' measured latencies jitter by a sample, so a take's Drive output can sit one
sample off the played signal (`drive12.py` delays them one, for a causal low-pass).

## The follow-up's Drive

The follow-up's isolated takes are what the first model was fitted on. Its second take
(`m2-horn-noise`, meant to be Balance 5:00) came out as both rotors — the ring moved with the
toggle on DRIVE, storing Drive 5:00 — so Mode II's "horn alone" was both rotors and every
later take ran at Drive 5:00 (the toggle then stayed on BALANCE, so the Balance changes
worked). Against the isolated session: the same at 125 Hz–2 kHz, its Mode I 4.4 dB short at 8
kHz, its Mode III 4.1 dB hot throughout. That made the first model's Mode II horn 6–9 dB quiet
(its stereo nearly mono: A↔B correlation 0.9 where the pedal has 0.6–0.7) and dulled Modes I
and III. Its stopped-rotor Drive block is unusable too.

## Reamps

The guitar DI dataset (`captures/di/`, `tools/capture/README.md`) through the pedal, the
model rendered from each take's played signal at its settings (`tools/capture/rt2/reamp_ab.py`):

- [`2026-11-07-reamp/`](2026-11-07-reamp/): the Drive Knob toggle on BALANCE and Drive at 5:00
  (left by the follow-up's last take): its `DRIVE 7:00` takes are the drum alone (Balance 7:00),
  its `drive-0300` ones Balance 3:00, all driven.
- [`2026-11-07-reamp2/`](2026-11-07-reamp2/): the toggle on BALANCE again: the drum alone, this
  time with Drive at 7:00 by every sign (no compression at the peaks) — kept as a clean
  drum-alone reamp.
- [`2026-11-07-reamp3/`](2026-11-07-reamp3/): right (both rotors on every take; `run` now flags
  Outputs A and B coming out the same).

Against reamp3 (model − pedal, third octaves, both outputs):

- **Mode I, Slow**: level within ±0.6 dB; tone within ±1.5 dB from 80 Hz to 3.15 kHz on most
  bands, ±2.5 at worst (the stabs, the neck pickup), +1.8 to −4.7 dB at 5 kHz.
- **Mode I, Fast**: level +1.5 dB; +3.5 to +3.9 dB at 315 Hz — the pedal's drum has less
  250–400 Hz at Fast (its noise takes too: 2.6 dB under Slow's at 250 Hz); not modelled.
- **Mode II**: level within 0.1 dB, tone within ±2 dB from 80 Hz to 5 kHz (the first model:
  6–11 dB short from 800 Hz).
- **Mode III**: level +1.3 dB, tone within ±3.1 dB up to 1.25 kHz, then 7–17 dB short at 3–5
  kHz — its Drive (*Drive*). **With the Drive of 2026-11-21** (*Mode III's Drive*): level +0.7
  to +1.5 dB, within ±0.8 dB from 2 to 5 kHz, ±2 at 6–8 kHz (was 7–18 short), −2 to −4 at 10 kHz
  on chords (−10 on the chugs); the +2 to +2.6 dB at 200–315 Hz is the drum at Fast, below.
- **Stereo**: the model's A↔B correlation 0.65–0.92 where the pedal's is 0.49–0.91: a little
  narrower.
- Over 6 kHz the guitar takes are within 3–20 dB of the pedal's own hiss (some 90 dB under the
  music): no tone to compare; on noise (the first session) the model is within ±1 dB to 10 kHz.

WAV pairs (pedal, model) for listening: `2026-11-07-reamp3/ab/` (git-ignored; `reamp_ab.py` makes
them). For Mode III's Drive, pedal / before / after (`drive3_check.py listen`): the two Mode III
takes there (rotors turning, Drive 7:00) and `2026-11-21-focused/ab/` (rotors parked, every
Drive position and the lead). For Modes I and II's (`drive12.py listen`, `turning`):
`2026-11-21-focused-m12/ab/` (parked, both modes, every Drive position and the lead) and
`m1-slow-drive-0300-*` in the reamp's folder (rotors turning, Drive 3:00).

## Next

- The by-ear A/B (`2026-11-07-reamp3/ab/`, `2026-11-21-focused/ab/`, `2026-11-21-focused-m12/ab/`):
  the new Drive hasn't been heard in any mode.
- Drive between 7:00 and 9:00 in Modes I and II (how it comes in from clean): one take at 8:00.
- Mode III's bias driven hard for long, and the top octave (*Mode III's Drive*).
- The drum's low-mids at Fast.
- The horn: least squares leaves ~5 dB of its error to a per-angle model; its low-end leak
  (an 8th-order crossover, or the filters held at the crossover).
- The stop ↔ 10 Hz take for the inertia laws.

## A blind test (2026-11-11)

`../mystery/2026-11-11-mystery-4` (`../mystery/README.md`), in no fit: Mode III, Fast on, Fast 8:00, Slow
11:00, Rise/Fall SLOW, Level 1:00, the Drive Knob toggle on DRIVE with the ring at 10:00. Read: Mode III
(the drum's tone fits the model's Mode III to 0.5 dB rms, Modes I and II to 1.1–1.2; a second harmonic
at −40 dB on a 440 Hz tone), Fast on with Fast at 7:30 (the drum turns at 2.646 Hz, the horn then at
2.88), Level about 12:00, and **Balance fully to the drum**: the horn was 50 dB down at 4 kHz. With the
toggle on DRIVE the ring had left Balance where it was last stored, most likely at the isolated-rotor
session's drum-alone takes: the pedal had been without its horn since, and nothing on it shows that.

- **Drive in Mode III is too strong in the model on the drum:** at Drive 10:00 the pedal compresses
  loud noise by 1.3 / 3.2 / 4.8 / 3.8 dB at −18 / −12 / −6 / 0 dB; the model by about 3 / 7 / 11 / 17,
  and matches only with Drive nearly off (7:30–8:00, which is how it was read). Closed 2026-11-21:
  those four numbers are the pedal's at Drive **7:00** (1.6 / 3.1 / 5.1 / 3.9 on the parked session's
  noise ladder; at 9:00 it is far more), so the ring's 10:00 most likely wasn't stored; and the new
  Drive gives 1.4 / 3.7 / 4.8 / 3.3 there.
- The drum's pattern repeats once a turn, as the study has it (the speed rings were first reported the
  other way round: the inner knob is Fast).
