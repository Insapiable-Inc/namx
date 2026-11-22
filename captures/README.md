# Pedal captures

Measurements of real pedals, one folder per pedal (`<maker>/<pedal>/`), one session per
pedal, mode and day (`<date>[-<mode or note>]/`). How it's done — the rig, the tools, the
workflow, the lessons — is in [`tools/capture/README.md`](../tools/capture/README.md).
The WAVs stay on the capturing machine (git-ignored, ~240 MB per session; back them up);
the sidecars, rigs, fits, figures and each pedal's study are committed.

## Status

The collection: 17 Boss pedals and a JOYO R-07 Aquarius, on a Focusrite Scarlett 8i6 (3rd gen).

| Pedal | What | Status | Study |
|---|---|---|---|
| **TR-2** Tremolo | modulation (amplitude) | **Done.** Grey-box model (clamped offset triangle, fast-fall/slow-rise follower, Depth-dependent top level), fitted to a battery and a 9 V session; in NAM-X as the Tremolo's TR-2 type, frozen (branch `dsp/tremolo-tr2`) | [`boss/tr-2/README.md`](boss/tr-2/README.md) |
| **DD-8** Digital Delay | delay | **Done:** Standard, Analog, Tape, Warm, Mod, Reverse; Shimmer minimal (an octave-up wash beside plain echoes, not pursued). Not done: GLT (low priority). Excluded by choice: Warp, Looper, +RV. In NAM-X: the Delay's DD-8 types (Standard, Analog, Tape, Warm, Mod); Analog's and Tape's loops refitted 2026-11-10 (on guitar 5.8 → 1.5 and 6.9 → 1.9 dB rms), not heard yet. **Validated 2026-11-11** on a blind test and the same knobs in all five modes, in no fit: each mode's first repeat on guitar within about 0.5 dB in tone; done (the user) | [`boss/dd-8/README.md`](boss/dd-8/README.md) |
| **CE-5** Chorus Ensemble | modulation | **Measured** 2026-10-27, wet-only (a plug in Output B) and mono sessions: a digital chorus — dry 0.98 ms late at +0.35 dB, wet 5.4 ms after it with a 60 Hz low cut and a 10 kHz linear-phase wall; bowed-triangle LFO 0.25–5.9 Hz, Depth raising mainly the top (5.7–8.1 ms); Low = resonant high-pass shelf, High = first-order low-pass; two hard clips (the input at ~+2 dB over the guitar's peak, dry and wet; the wet's at 0 dB with E.Level up). **Done** (with a short extra session for four open questions). In NAM-X: the Chorus's CE-5 type (branch `dsp/chorus-ce5`), to A/B | [`boss/ce-5/README.md`](boss/ce-5/README.md) |
| **BF-3** Flanger | modulation | **Standard measured** 2026-10-27 (mono): digital (0.55 ms dry at −0.1 dB; wet +1.6 dB over it, low cut ~190 Hz, roll-off from ~4 kHz, the same filter round the feedback loop); Manual 14.7–0.45 ms; Res −16.7 to −1.4 dB per pass, never self-oscillates; sweep = floor + swing · triangle^2.5 through a 1.53 Hz smoothing, Depth blending Manual toward 0.33 ms; Rate 0.055–10 Hz; one hard clip on the output at +0.55 dB re the guitar's peak. Checked against a second session; a plug in Output B changes nothing. Ultra measured: three taps at d/2, d, 3d/2 (its Res loop not pinned down yet). Gate/Pan, Momentary and stereo left out. In NAM-X as the Flanger's BF-3 Standard / Ultra types (branch `dsp/flanger-bf3`), not approved by ear yet. A blind test (2026-11-11) read mode, delay, feedback and rate right; Depth between 7:00 and 9:00 is unmeasured | [`boss/bf-3/README.md`](boss/bf-3/README.md) |
| **CS-3** Compression Sustainer | dynamics | **Measured** 2026-10-27: analog (a VCA, a peak detector); a fixed output ceiling (~85:1) with Sustain as the gain before it (−15 to +17.6 dB); Attack mostly sets the release (458 → 44 ms to 63 %) and the clamp-down a little (14 → 6 ms); Tone a tilt around 1 kHz; Level an audio taper (7:00 mutes). In NAM-X as the Compressor's CS-3 type (branch `dsp/compressor-cs3`), not approved by ear yet | [`boss/cs-3/README.md`](boss/cs-3/README.md) |
| **NS-2** Noise Suppressor | dynamics | **Measured** 2026-10-27 (Reduction mode, Input → Output; two sessions): not a hard gate — it opens at once (≤ 1 ms) once the input passes the threshold (−74 to −32 dBFS for a −6 dBFS guitar peak; little change below 12:00) and closes by turning down, ~8 dB at first, then deeper (slope ~0.9) as a slow level sinks under the threshold; Decay sets how long a dying note holds and fades (25–460 ms fall of the fast level); open +0.6 dB, −3.1 dB at 20 kHz; adds no noise; Mute mutes. Grey-box model within a median 1.5 dB rms over 39 takes; open: a ~1 s creep near the threshold the model misses. **2026-11-21: the gain read directly** (the signal into INPUT, a probe into RETURN) and the control circuit modelled from the schematic: on guitar in no fit the gain within 0.8 dB rms (the first model: 4.6); in NAM-X since 2026-11-21, approved by ear by the user that day | [`boss/ns-2/README.md`](boss/ns-2/README.md) |
| **GE-7** Equalizer | linear | **Measured** 2026-10-28 (four sessions: the first's Level takes are the 6.4 kHz slider; a retake for two takes that missed the fit; every printed mark on four sliders): one circuit fits all 60 sweeps to 0.23 dB rms — the classic one-op-amp graphic EQ, six LC bands (91, 196, 370, 728, 1528, 3275 Hz, Q ~3) and a 6.4 kHz shelf (an RC, corner 3.8 kHz), interacting through the shared op-amp; the printed marks aren't the dB they say (800 Hz: +5 → +5.9, −5 → −4.0, ±10 → +10.7/−8.5), one law for every band slider; Level flat ±15 dB (−3 dB at 13 kHz at +15); flat = bypass within 0.3 dB, no added noise; one ceiling at +1.8 dB re the guitar's peak whatever the sliders and Level (a rounded clip) | [`boss/ge-7/README.md`](boss/ge-7/README.md) |
| SD-1, DS-1, JB-2, HM-2W | drives | **Skipped** (user, 2026-10-27): NAM-X's Tube Screamer covers them | |
| **AW-3** Dynamic Wah | envelope filter | **Up measured** 2026-10-31, 41 takes: digital (~0.56 ms), a +14.2 dB resonator with Q falling from 14 to 3; Manual raises the start (194–525 Hz), the upper limit stays ~2.23 kHz; Sens has a detector-limited range, Decay a shared short-release floor and a slower return at high settings. Tied grey-box model and pluck validation; long-Decay timing and saturation details remain open. **Down and Sharp measured** too (28 takes): Down = Up's filter parked high (840 Hz–4 kHz by Manual) and pulled down to a common ~400 Hz floor; Sharp = Up's control with a taller (+17 dB), lopsided, Manual-independent filter (structure not identified); Decay the same in all three; Humanizer and Tempo left out (user). **In NAM-X** (branch `dsp/wah-aw3`): Wah Type AW-3 Up / Down / Sharp. **Focused reamp 2026-11-11** (24 takes, the filter's place read over time on five quiet tones and on the guitar): the model moved 0.6–0.8 octave too little on a guitar (its detector, fitted on noise, weighted the treble too much); refitted with the noise staircases, 0.12–0.3 octave on every take; then the control capped just over the top (a jumps session: the return does not lengthen with Sens) and validated on a blind session in no fit (the return's track 0.09 octave, funk while playing 0.22); merged into `pedals/all`; the user: sounds really good (2026-11-21). **A check session at settings in no fit** (2026-11-21, 13 takes): with Sens at 3:00 and 5:00 the filter follows the playing within 0.02–0.4 octave in all three modes; at Sens 9:00 it moved 0.6–0.8 octave too little on a guitar — **fixed with an onset path below Sens 12:00** (0.23–0.32 in all three modes, two of them in no fit; nothing changes from 12:00 up), the user: sounds good; with Sens high it comes back too slowly after a hit (0.2–0.6 octave between stabs): not fixed | [`boss/aw-3/README.md`](boss/aw-3/README.md) |
| **PH-3** Phase Shifter | modulation | **Measured** 2026-10-28 (five sessions, mono): digital (0.54 ms, 44.1 kHz); every mode = dry + wet through second-order all-pass sections (4/8/10 Stage: 2/4/5 sections, one scale swept by a triangle² law; **12 Stage = a 6 Stage phaser run twice**, its notches double zeros, the second pass on a delayed LFO; Rise/Fall a barber pole, one notch spacing per LFO period, notches 1.88 octaves apart; Step the 8 Stage sweep sampled and held, 350 → 71 ms from Rate 8:00 to 1:30); Rate 0.047–21.5 Hz, Depth = the swing, the mix fixed; **Res = a feedback loop around the whole engine** (dry inside, one sample and a DC blocker in the loop, k 0.25–0.70 from 9:00 to 5:00; 8 Stage's loop passes a parked fifth section at 8.3 kHz). Open: 4 Stage's Res (its top notch fills and a sag follows it — no simple structure found; use the 8/10 Stage loop), Step's hold above Rate 3:00, the 12 Stage pass lag. In NAM-X 2026-10-29 as Phaser types beside the Phase 90 (branch `dsp/phaser-ph3`): by ear "sounds real good in all modes" (user, 2026-10-29); Rate now reaches Stop | [`boss/ph-3/README.md`](boss/ph-3/README.md) **2026-11-10/11, focused sessions:** the Stage modes are chains of identical first-order stages (in NAM-X), 4 Stage Res unsolved and left, Depth in Rise and Fall measured and modelled (the study). **The Res session, 2026-11-11** (118 takes, grouped by knob setting): Rise and Fall miss the pedal by 8–10 dB over the cycle even with Res at its minimum (8 Stage: 1.2); the pedal runs two passes in series with a loop each (gains 0.25 to 0.93 by Res), the dry outside the loops; no model of it yet. |
| **RV-6** Reverb | reverb | **Measured** 2026-10-31, two sessions (39 + 31 takes), stereo (Output B on input 1): Modulate, Spring, Plate, Hall, Room, Shimmer (Dynamic, +Delay left out). The dry is the analog bypass (unity, no delay), the wet digital and added; Level a wet gain ≈ knob² (7:00 off, noon −12 dB re 5:00); Tone one tilt filter after the tank (bass up/treble down below noon, a +9–10 dB treble shelf above); Time the decay by a five-point law (7:00→9:00 barely moves, 9:00→3:00 most) with the wet's energy held constant (the first ~100 ms only rescaled). **Room, Hall, Plate, Spring are time-invariant** (repeat across sessions); at noon RT60 at 1 kHz 1.3 / 3.7 / 3.0 / 2.7 s, onsets 58 / 26 (a slow swell to 245 ms) / 57 / 75 ms, decorrelated A/B (Spring half); Spring a dispersive multi-spring model (group-delay peak ~3.7 kHz, band edges decaying in ~1 s at any Time); Modulate one 1.35 Hz LFO, ±0.45 ms; Shimmer the same LFO plus octave-up feedback (octave at the fundamental's level). **In NAM-X** (branch `dsp/reverb-rv6`): six Reverb types, RV-6 Modulate … Shimmer (FDNs, a dispersive spring, an octave-up shifter; Mix/Decay/Damping = Level/Time/Tone), fitted with `fit_rv6.py` — decay at all five Time positions, energy, level and colour; not approved by ear yet. A blind test (2026-11-11, Room): mode read right; Tone's curve in Room and the tail's length to check in the focused session. **Focused session recorded and scored 2026-11-15 (`2026-11-15-focused`, 123 takes), fitted 2026-11-17:** E.Level's lower half (9:00 was 2.4 dB quiet); Tone is one filter in every mode (three sections per measured position, within 0.1 dB), and below noon the pedal adds **a low reverb, the same on both outputs, in Plate, Hall and Room** (a tank of its own in NAM-X; Plate's Tone 7:00 curve 4.4 → 0.5 dB rms); Time off noon by tables, and Hall's tank fed after its 250 ms swell, which the pedal keeps at every Time; Shimmer's constants refitted with bursts and stabs among the targets, then again after the user's listening ("brighter and or louder"; the first comparison didn't count the model sounding on after the pedal had gone quiet): the octaves' regeneration follows Time, as the pedal's does, the climb's top is lower, and Tone 9:00 comes from the one take that has it (next to no octave there). Left: Tone 9:00 has no block of its own (recorded at 12:00: six takes to redo), Shimmer's top octave blooms late and its swell at long Time and the repeating 1.35 Hz pattern of the pedal's tail aren't modelled, Hall's and Room's mids at Time 7:00–9:00. Heard once, between the two Shimmer fits (the study's last three sections) | [`boss/rv-6/README.md`](boss/rv-6/README.md) |
| **OC-5** Octave | pitch | **Removed from NAM-X 2026-11-10** (the user: a failed experiment; the study stays as a record). **Measured** 2026-10-29 (mono, GUITAR): digital (dry 1.00 ms, +0.3 dB at Direct 12:00, flat; a hard clip at −8.1 dB re the guitar's peak, before Direct — a hard pick's attack loses ~8 dB); **Vintage** = a divider: square subs (−1 locks in 3–10 ms, −2 in 9–52), monophonic, the OC-2's warble on chords, tracking to 659 Hz; **Poly** = a polyphonic shifter (both notes, 7–84 ms to lock); **Range** 7:00 = lowest note only (a clean near-sine sub), above it a ceiling (~500 Hz at 12:00, ~1 kHz at 5:00); one audio taper for the knobs. In NAM-X 2026-10-29 as the Octave effect (branch `dsp/octave-oc5`): **by ear "still not there"**, parked by the user. A reamp of the user's low-E DI (`2026-10-29-reamp-lowe`) puts levels within ~2 dB of the pedal and its octaves much cleaner than the model's; the open items (Vintage's structure, a dark Poly −1, a long DI) are in the study | [`boss/oc-5/README.md`](boss/oc-5/README.md) |
| **AC-3** Acoustic Simulator | filters | **First look** 2026-10-31, 24 takes: modelable — per-mode fixed filters (a 200 Hz body peak, a 1.5–3 kHz scoop, +25–36 dB treble), Body/Top as two path levels, an output ceiling near the guitar's peak, a short room reverb (RT60 0.7–1.2 s). A modelling session needs −48 dB signals (the plan's −24 dB ones hit the ceiling in the treble); no NAM-X counterpart | [`boss/ac-3/README.md`](boss/ac-3/README.md) |
| **RT-2** Rotary Ensemble | modulation (rotary speaker) | **Measured** 2026-11-07 (first session 62 takes; the isolated rotors, clean, 10; the follow-up's 34 ran at Drive 5:00 from its 2nd take), stereo: digital; a crossover (horn above ~500 Hz, drum below ~2.7 kHz), the horn's direct path a V over each turn (A and B two microphones ~60° apart), ~28 room reflections as moving taps, the drum one V-shaped path (mono); speeds on the knobs' curves (Slow 7:00 stops; Fast 2.5–10 Hz; the drum 1.087/1.027 slower), Rise/Fall inertia (the drum coasts down), Level and Balance laws, the input clip (+2.25 dB), Drive a level-compensated clipper in Modes I and II. **Mode III's Drive measured with the rotors parked 2026-11-21** (15 takes): one curve ahead of both rotors — a square-law dead zone into a ceiling 12 dB under the guitar's peak — on a bias the playing pulls under itself in 5–40 ms, the knob only its gains; on guitar at the pedal's outputs within ±0.7 dB per third octave from 100 Hz to 8 kHz at every Drive position (the clipper before it: 5–9 dB short at 3–5 kHz). **Modes I and II's the same day** (29 takes): the same curve and bias with about 19 dB less gain into it, out of the path at 7:00, a 5.5 kHz low-pass after it; within ±0.7 dB from 200 Hz to 8 kHz at every position. Held-out NMSE horn −4 to −5 dB, drum −11 to −15 dB; against the clean guitar reamp Mode I within ±1.5 dB 80 Hz–3 kHz, Mode II ±2 dB to 5 kHz, Mode III within ±0.8 dB at 2–5 kHz with the new Drive (was 7–17 dB short). **In NAM-X**: the Rotary's Mode I/II/III; not heard yet | [`boss/rt-2/README.md`](boss/rt-2/README.md) |
| TU-3 Tuner | | nothing to capture | |
| **JOYO R-07 Aquarius** (multi delay + looper) | delay | **Low Bit measured** 2026-11-02 (25 takes, mono): "Low Bit" is a low sample rate, not a low bit depth — the repeats held at 5512.5 Hz (44.1 kHz ÷ 8) with no anti-alias filter, inside a first-order pre-/de-emphasis (~1.2 kHz) that makes the aliases ~15 dB louder; no audible quantization. Dry digital (0.94 ms, flat), the whole pedal inverts; Time 46 ms–2.0 s; Feedback flat per pass, 5:00 = unity hold; Level and Feedback one taper. The model reproduces the low-E DI's repeats to −24.5 dB (whole output 0.26 dB rms per third octave). Other modes not measured; no NAM-X implementation | [`joyo/aquarius/README.md`](joyo/aquarius/README.md) |

Every pedal on a 9 V supply, not a battery: the TR-2's tired battery (8.37 V) held the top
of its cycle ~0.5 dB high at full Depth, and a drained battery won't boot the DD-8.

## Next

**2026-11-18.** All 123 focused RV-6 takes are on disk. The accepted Shimmer port is on
`dsp/reverb-rv6`. The six Tone 9:00 takes were recorded at noon and can be redone if that
setting is revisited. [RV-6 handoff](boss/rv-6/HANDOFF.md)

**2026-11-21.** The RT-2's Mode III Drive is measured (rotors parked, 15 takes) and in NAM-X:
[the study](boss/rt-2/README.md#mode-iiis-drive-2026-11-21). To hear: `boss/rt-2/2026-11-07-reamp3/ab/m3-*`
(rotors turning) and `boss/rt-2/2026-11-21-focused/ab/` (parked), pedal / before / after. Modes I
and II's followed the same day (29 takes): Mode III's own curve and bias with other gains,
[the study](boss/rt-2/README.md#modes-i-and-iis-drive-2026-11-21); to hear,
`boss/rt-2/2026-11-21-focused-m12/ab/` (parked) and `boss/rt-2/2026-11-07-reamp3/ab/m1-slow-drive-0300-*`
(rotors turning). The NS-2
already has pedal, current model and candidate recordings ready for listening.
[NS-2 listening notes](boss/ns-2/HANDOFF.md)

**Earlier capture order (2026-11-09).** Focused reamps go pedal by pedal, easiest to hardest. The method, the
scorecard, the knob-curve rule, the order and each pedal's state are in
[`tools/capture/README.md` → Focused reamps](../tools/capture/README.md#focused-reamps-2026-11-09-on).
Done on 2026-11-09:

- **GE-7** (two sessions, a battery one by mistake and the adapter's): the linear model is at the
  pedal's own floor on guitar. In NAM-X the Equalizer's knobs are now dB with ±15 on every band
  and a frequency per band, and its clip is gone.
- **TR-2** (the focused reamp with a probe tone, then a tone-only session on the square
  settings): the gain doesn't depend on what is played; with Wave up and Depth down the pedal's
  dip has a floor the model lacked, and its squareness falls with the rate. Both are in NAM-X's
  Tremolo now, with Depth and Wave as plain curves over the pedal's range.

Next: the DD-8's focused plan, then CE-5, BF-3, PH-3, CS-3, AW-3, RV-6, and the two known gaps
(NS-2 under the threshold, RT-2 Mode III). The OC-5 is dropped, and its Octave effect was
removed from NAM-X (2026-11-10).
The list below is older and stays for its plans.

**The missing items (plans written 2026-10-29), one capture day with the OC-5 (`plans/oc-5.json`) and the AW-3:**

1. **Reamps with the guitar DI dataset**: the dataset is recorded (2026-11-07, `captures/di/`: noise, notes,
   dynamics, chords, chugs, funk, stabs, lead, neck; the WAVs off git — keep a backup) and the RT-2 is done
   (`boss/rt-2/2026-11-07-reamp3`). Next: `tools/capture/reamp.py PEDAL` for the other eleven pedals with a
   NAM-X model (plans `*-reamp.json`, 11–19 min of audio a pedal), over several sittings. It replaces the
   single `phrases.wav` the older plans waited for.
2. **CS-3 follow-up** (`plans/cs-3-followup.json`, 4.8 min): pairs of re-picked notes 50 /
   100 / 200 / 500 ms apart at Sustain 9:00 / 12:00 / 3:00 × Attack 7:00 / 5:00 — the
   second note's attack and the gain it has recovered, which the staircase and the
   isolated plucks can't show; then the reamp.
3. **BF-3 follow-up** (`plans/bf-3-followup.json`): Rate 5:00 with clicks every 21.3 ms
   (the study's 25 ms clicks saw its ~10 Hz sweep at four phases only), and the reamp.
4. **Reamps** for every pedal with a NAM-X type: item 1 (the plans were regenerated for the DI dataset;
   `reamp.py` makes each pedal's session).

Then: each NAM-X type run over the same played files at the same settings, against the
pedal (by ear, and over the existing takes' played signals for the numbers).

**Which pedal next** (discussed with the user, 2026-10-29):

- **AW-3 first** (recommended): an envelope follower and a swept filter, with the CS-3 / NS-2
  / GE-7 methods already proven; it would become a Type on NAM-X's Dynamic Wah.
- **SD-1:** quickest as a circuit variant of NAM-X's Tube Screamer WDF (asymmetric clipping, a
  different tone stage), checked against a short capture. NAM snapshots are the other route
  (one model per knob setting; NAM-X can't put one before the amp yet, ROADMAP Phase 24). The
  core's WaveNet takes extra input channels with FiLM conditioning, so a knob-conditioned NAM
  model is possible but needs custom training, NAM-X support and a Drive × Tone grid (~25
  settings, ~75 min of reamp).
- **RV-6:** the largest. Not NAM (linear, seconds of memory). Measure each mode's IRs at ~5
  Time × ~3 Tone positions (decay per band, pre-delay, early reflections, density), the tail
  modulation (repeated impulses), stereo (two outputs: a second pedal input on the rig),
  Dynamic by level steps, Shimmer and +Delay like the DD-8's; then RV-6 types on the Reverb,
  measured early reflections joined to a fitted algorithmic tail. Spring is a project of its
  own (dispersive all-pass chains). About 50 min of capture.
- **Pedals the user doesn't own**, worth borrowing: a CE-2 (an analog BBD chorus, a contrast to
  the digital CE-5; the CE-5's methods plus BBD noise, filters and saturation), a real MXR
  Phase 90 (to check NAM-X's circuit model against the pedal it imitates), a BBD delay
  (DM-2/DM-3), a Uni-Vibe, a POG (the reference for a clean polyphonic octave). Fuzzes need a
  reamp box that imitates a guitar's pickup.

The CS-3, NS-2, GE-7 and PH-3 are measured. Next by the list: AW-3 Dynamic Wah (level-driven, the
CS-3's staircase and jump signals; for anything dynamic, notes that decay like a real string
— the NS-2's first plucks never reached its threshold). Order every plan by setting, not by signal,
so the knobs are turned as few times as possible.

## Starting a session

The interface is in its normal state between sessions. Before capturing (commands in
[`tools/capture/README.md`](../tools/capture/README.md#the-reference-rig-focusrite-scarlett-8i6-3rd-gen-linux)):
NAM-X closed; card profile Pro Audio; analogue outputs 3/4 routed to PCM 3/4; input 2's
pad on (INST, gain at minimum); `capture.py init <session> --from <a DD-8 session>` (same
rig and calibration). Afterwards, all three back, and NAM-X's connections checked.
