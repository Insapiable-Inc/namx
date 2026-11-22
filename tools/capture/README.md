# Pedal captures

Tools for measuring real pedals through an audio interface and fitting models of them,
so NAM-X's own effects can be voiced after the real thing. Started 2026-10-26 with a
collection of Boss pedals. Done so far: the TR-2 tremolo, the DD-8 delay, the CE-5 chorus, the BF-3 flanger and the CS-3 compressor, the NS-2, GE-7, PH-3, OC-5, the RV-6 reverb and the JOYO Aquarius's Low Bit delay — see
[`captures/README.md`](../../captures/README.md) for every pedal's status and what's next.

| File | What it does |
|---|---|
| `capture.py` | Guided recording through JACK: ports, metering, level calibration, plan-driven takes, the guitar DI for reamp takes (`di`) |
| `signals.py` | Deterministic test signals (sine, exponential sweep, noise, clicks, silence, steps — a sine or white noise through levels —, plucks, pluck-train — a frequency per note or one for all, optionally re-picked, each note ending the last —, a recorded file), the take layout, and each signal's silent stretches |
| `analysis.py` | Reading sessions back: aligned takes, envelopes, LFO frequency and cycle folding, band levels, harmonics |
| `fit_tr2.py` | The TR-2 model fit: parameters, fit errors, tone, and the figures |
| `analyze_dd8.py` | The DD-8's Standard mode: deconvolved sweeps (dry path, delay times, Level/Feedback maps, tone), bursts (the clip), and figures |
| `analyze_dd8_mode.py` | Any other DD-8 mode recorded with the Analog/Tape/Warm/Mod plans: bursts, steps, tone, feedback (sweeps and bursts), Feedback 5:00, wow/flutter and LFO (rate, delay swing, shape), Time turned by hand, delays, noise |
| `analyze_ce5.py` | The CE-5 (wet-only + mono sessions): dry and wet split by time in the deconvolved sweeps (delay, gain, tone, group delay), E.Level, the filters (corner, slope, floor), the LFO from held tones' phase (rate, absolute delay, bowed-triangle shape), bursts, note-off, noise |
| `analyze_bf3.py` | The BF-3 (Standard, mono): dry and wet split in time at Manual 7:00 (delay, gain, tone), Manual's delay from the comb, Res from the clean echo and the comb's ripple depth, the output clip, the sweep from click trains (delay per click, LFO rate, smoothed-triangle^power shape, Depth's floor blend and swing) |
| `analyze_cs3.py` | The CS-3: the static curve from a 1 kHz staircase per Sustain position, attack and release from a −30/0/−30 dB jump (the gain's 1 ms envelope against the bypass) per Attack position, picked notes, Tone sweeps, Level, noise |
| `analyze_ge7.py` | The GE-7 (four sessions): every sweep against its bypass per 1/12 octave and one graphic-EQ circuit fitted to all of them (LC bands, the RC top shelf, Level's stage, one wiper per printed mark), the staircases' ceiling and harmonics, noise |
| `analyze_ph3.py` | The PH-3 (five sessions): every static sweep against its bypass, the notches and their order (simple or double zeros), the Res factor against the all-pass chain's phase, Step's hold time per Rate, the LFO rate per Rate, the level per Depth — and the fitted models (all-pass sections per mode, the 12 Stage twin passes, the Res loop, the sweep laws) drawn over them. The fits themselves were done with exploratory scripts (not kept); their results are the script's `FINDINGS` table and the study explains how they were obtained |
| `analyze_oc5.py` | The OC-5: the dry path (delay, gain, its clip from a held sine), the Direct and −1 OCT laws, each octave's waveform and harmonics, tracking per note (pitch, level, lock-on time), two-note chords, dying notes |
| `analyze_rv6.py` | The RV-6 (stereo: Output A, loopback, Output B): bursts deconvolved with the marker in (it plays through the reverb), sweeps from the signal on (two excitations leave ghosts), the pedal's ~3 kHz whine notched before decay fits, the dry path and the wet over it, time-invariance (bursts predicted from sweeps), RT60 per octave per Time, onsets and the Time-invariant start, stereo coherence, echo density, spectra, Level and Tone, Spring's dispersion, Shimmer's octaves; with `--shimmer`, the Shimmer session (each line k·f0 followed by complex demodulation, re the dry note: onsets, steady levels, decays, line listings); with `--namx-render`, NAM-X's Plate/Hall/Room at matched settings; with `--followup`, the second session: Time/Tone/Level laws, the modulation from held tones (line spectra), Shimmer's octaves, the first session's bursts predicted from its sweeps |
| `analyze_aquarius.py` | The JOYO Aquarius in Low Bit mode: deconvolved sweeps (dry path, delay per Time, Level, the repeat's tone, polarity), the hold rate from a held sine's mirror image, the ladder (repeat level and what's beside it, per input level, at two Level positions), click repeats, feedback per pass, noise, and the sample-and-hold model rendered over the Time 5:00 sweep and the low-E DI |
| `render_reverb.cpp` | NAM-X's Reverb offline (wet only, any type/Decay/Damping/pre-delay/Shimmer), raw float32 in and out, for `analyze_rv6.py`; build command in its header |
| `fit_voicings.py` | Refits NAM-X's pedal filter chains (DD-8 voicings, BF-3 wet tone, CS-3 Tone, NS-2 top end) to the studies' measurements, band-averaged as measured, and prints them as C++ constants |
| `fit_rv6.py` | Fits the RV-6 Reverb types (`src/dsp/Rv6Reverb.h`, its FITTED block) to the RV-6 study's JSON: per mode the Time law's RT60 table, the wet's energy against Time, the colour EQ and the level (Shimmer apart: `fit_rv6_shimmer.py`; the Tone filter is `rv6/tone.py`'s since 2026-11-17). Renders through `render_reverb` and measures like `analyze_rv6.py`; `--write` rewrites the block, `--build` rebuilds between rounds, `--modes` fits only those (the others as they are); with `--audio` (the first session's WAVs) a mode with a measured start has its colour and level fitted on its tail, which is all its EQ and gain move; `--keep-rt` leaves the RT60 tables to `fit_rv6_decay.py` |
| `rv6_early.py` | Measures the RV-6's Room, Hall, Plate and Spring early responses for `src/dsp/Rv6EarlyData.h`: the pedal's wet at noon over its first 350–450 ms from the sweeps, and per mode where Time's extra decay starts on it and its level at the five Time positions, fitted on the follow-up's sweeps (the engine subtracts its own tank over the window); prints the envelope error against the pedal at every Time position with and without it; `--write` |
| `rv6/takes.py`, `rv6/focused.py`, `rv6/plots.py` | The RV-6 focused session against NAM-X's Reverb on the wet alone. `takes.py`: both outputs aligned with the recording's one-sample slips undone (read against the median of all takes: `scorecard.slips` misreads a sweep), the dry taken out (the E.Level-off take's sweep deconvolved: a 4096-tap FIR), the model rendered through the same chain with the marker silenced. `focused.py measure / floor / report`: level, tone per third octave, RT60 per octave from the burst's tail, the start's waveform, the outputs' likeness, the guitar's bands; `floor` = the pedal against itself. `plots.py`: a page per mode |
| `rv6/tone.py`, `rv6/time.py`, `rv6/ab.py` | The RV-6's fits on the focused session. `tone.py`: Tone's filter (a coherent ratio per sixth octave against the noon take, the time-invariant modes: three sections per measured position) and the low reverb Tone adds below noon in Plate, Hall and Room (the take less its Tone-noon twin: its decay per mode and Time, its colour and level; NAM-X's rendered alone through `render_rv6`'s `low.` overrides); `--write` rewrites `Rv6Reverb.h`'s FITTED TONE block, `--check` compares NAM-X as built. `time.py`: Time off noon, per mode and position the tank's RT60, the wet's level, the measured start's level and the slower tank's level fitted to the burst's levels over time (octave bands, 20 ms steps); `--write` rewrites the three tables it touches. `ab.py`: listening files per take, the pedal beside NAM-X now and a build from before (`--was`). `heard.py`: the wet's energy per octave band over a take's stabs and tail, NAM-X less the pedal, every step counted (what rings on after the pedal has gone quiet included) |
| `fit_rv6_decay.py` | Fits the RV-6 tails' finer shape (`Rv6Reverb.h`'s DECAYS block: a second, slower tank under the first, the tank's own low and high RT ratios and a broad band of its own) for Room, Hall and Modulate by simulation (Plate keeps one tank): the octave bands' Schroeder curves (125 Hz–4 kHz, the pedal's noise taken off) after the measured start, at every Time position the sweeps have, against NAM-X through `render_rv6` (`decay.*=` overrides); `--held` adds the held notes' decay (from the IRs) to the targets; the plucks' notes reported; `--write`, `--evaluate`, `--report` |
| `render_rv6.cpp` | Renders the Reverb rack from a mono DI at the requested Time, Tone and Level. The tank fitting overrides use `rv6::Engine` directly. Build with `cmake --build build-release --target render_rv6`. |
| `fit_rv6_shimmer.py` | Historical fitter for the tank implementation at `e1fe4c9`. Fits that Shimmer (the SHIMMER block and its VOICINGS entry) by simulation: renders the Shimmer session's played files through `render_rv6`, measures them with `analyze_rv6.py`'s own line code against the pedal's (steady levels, decays, the 82 Hz pluck, the chord's and the fifth's lines, third-octave spectra held and in the tail, the octaves' spread), one fit per shifter grain (or block) and forward delay; with `--focused` also the focused session's bursts and stabs (the wet's level per octave band in 100 ms steps, at every Time and Tone position measured); `--write`, `--report`, `--figure`, `--evaluate` |
| `rv6/shimmer_prototype.py`, `rv6/export_shimmer.py`, `rv6/shimmer_rack_check.py` | Fit the clocked mono octave paths and twelve stereo responses, export the embedded filter bank, and compare the compiled rack with the pedal on guitar. [Measurements and commands](../../captures/boss/rv-6/SHIMMER-PROTOTYPE.md#rack-implementation) |
| `fit_rt2.py` | The RT-2 Rotary Ensemble: from the isolated session's takes (each rotor alone, clean), the paths (each output's direct path tracked per angle bin, the room's reflections by a ridge search, the drum's V), their filters (multi-input least squares over angle components, ridge, low rank) and `--write` → `src/dsp/Rt2RotaryData.h`; `check MODE` scores the exported model |
| `render_rt2.cpp` | The RT-2 engine (`rt2::Engine`) offline from a capture's played signal, the rotors started where the capture's were; build command in its header |
| `rt2/` | The RT-2 study's analyses (run from the repository root; most render with `build-rt2/bin/render_rt2`): `reamp_ab.py` the reamps against the model (tone per output, level, stereo; WAV pairs in the session's `ab/`), `session1_modes.py` the first session's takes against it, `check_engine.py` the C++ engine against the Python model and the pedal, `balance_modes.py` each rotor alone against both (the Balance offsets), `rotor_rates.py` the noise takes' rotor rates, `drive_harmonics.py` + `drive_fit.py` the first session's reading of the Drive (a clipper, from the 441 Hz steps), `parked.py` + `drive3_curve.py` + `drive3_fit.py` + `drive3_check.py` Mode III's Drive from the parked-rotor session (the rotors' responses divided out, the curve, the bias's law, the checks; `render_rt2_drive.cpp` renders the C++ stage alone), `drive12.py` Modes I and II's from theirs |
| `ns2/` | The NS-2 study's analyses (run from the repository root): `make_focused_plan.py` writes the plan that reads the gain directly (a probe into RETURN); `focused.py` reads the gain from each of its takes; `circuit.py` the control circuit as a model (`fit`, `score`, `engine`: NAM-X's C++ through `build-ns2/render_ns2_gain`, `plot`); `focused_ab.py` and `reamp_ab.py` NAM-X before and after against the pedal's own output (listening files in the sessions' `ab/`); `dataset.py`, `refit.py`, `rectified.py`, `weighting.py`, `verify_candidate.py` the 2026-11-08 search on the first model |
| `scorecard.py` | The one comparison of a model with its pedal, the same for every pedal (see Focused reamps): third-octave bands in 50 ms windows over the floor, the waveform's residual, the pedal against itself as the floor |
| `ab.py` | Listening pairs for the focused reamps (GE-7, TR-2, DD-8): per guitar take, the pedal's recording and NAM-X's model rendered from the same played signal, as `<take>.pedal.wav` / `<take>.model.wav` in the session's `ab/` (git-ignored); one gain per pair; the TR-2's LFO placed on the pedal's, its probe tone notched out |
| `render_dd8.cpp`, `dd8/` | The Delay (`DelayProcessor`, mono) offline at a take's mode, measured delay and the pedal's Level and Feedback positions; `dd8/focused.py` the marker's first repeat, the decay per repeat and the scorecard per take, `dd8/repeats.py` each stab's first three repeats per band, pedal against model, `dd8/stereo.py` the pedal's two outputs against NAM-X's Stereo (the renderer's fifth argument: R Time) |
| `render_tr2.cpp`, `tr2/` | The Tremolo (`TremoloProcessor`) offline; `tr2/focused.py` reads the pedal's gain cycle from the probe, compares it with itself under guitar and with the model's, and scores the guitar takes with the model's LFO placed on the pedal's; `tr2/knobs.py` fits Depth and Wave per setting |
| `render_ge7.cpp`, `ge7/` | The Equalizer (`EQProcessor`) offline at a capture's printed marks; `ge7/focused.py` scores a GE-7 focused session with the scorecard, `ge7/sliders.py` fits the slider positions per setting (how much of an error is the hand on the sliders) |
| `plans/*.json` | One plan per pedal: the takes, their knob settings and signals |

Needs Python with numpy, scipy, soundfile and matplotlib, plus the JACK client for Python
(Arch: `python-jack-client`). With PipeWire, `pipewire-jack` provides the JACK server.

## Method

Not every pedal suits the same kind of model. NAM learns a static input→output map with
a short memory (~100 ms), which is exactly what a drive is. It can't capture an effect
driven by its own clock (an LFO runs whatever the input does) or one with long memory
(compressor release, delay repeats). So the pedals split in two:

- **Drives** (SD-1, DS-1, JB-2, HM-2W): NAM captures at a few knob settings. Not started.
- **Everything else: grey-box system identification.** Write the effect's structure
  as a small DSP chain with a few parameters; send test signals that each expose one
  part of it, so most parameters can be read off directly; refine with least squares;
  measure each knob at ~5 positions to get its map from knob position to parameter. The
  result is a handful of numbers that drop into NAM-X's own processors, not a black box.

The plan for the other pedal types: delays → time, feedback gain, loop filter from a click; chorus/flanger → the
modulated delay from a short-window cross-correlation of noise; phaser → the all-pass
break frequency over time; compressor → static curve from stepped sines, attack/release
from tone bursts; reverb → decay per band; EQ → filter fits from sweeps.

## The rig

```
interface out A ──────────▶ pedal INPUT
pedal OUTPUT ─────────────▶ interface input (instrument, gain fixed)
interface out B ──────────▶ interface line input            ← the loopback
```

Every take plays the same signal on both outputs and records both inputs together:

- **The loopback gives each take's round-trip latency** from a chirp marker at its start
  (PipeWire/USB latency can change between sessions; delays, choruses and NAM training
  need the alignment to the sample). A delay or chorus smears the marker, so it can't
  be found in the pedal channel.
- It shows the level that actually left the interface, and lets the interface's own
  converters be divided out of a frequency response.

**No reamp box needed for Boss pedals:** every Boss compact has a buffered input, so it
reacts the same to a line output as to a guitar. A pedal with a true-bypass, unbuffered
input (most fuzzes) would need one.

### Level calibration

Pedals behave differently depending on how hard they're hit, so the test signals play at
the level the player's own guitar reaches. Once per rig (it carries over to every pedal as
long as the input gain and cables stay put):

1. `meter`: guitar → pedal (bypassed) → input; play as hard as you ever do; note the
   **hold** peak. Check open strums and hard palm mutes and take the higher.
2. `calibrate --target-dbfs <hold>`: the interface plays a 1 kHz sine through the
   bypassed pedal and adjusts its level until the input shows the same peak. The result
   (`ref_dbfs`) is saved in `rig.json`; plans give levels relative to it (`level_db`).

### The reference rig (Focusrite Scarlett 8i6, 3rd gen, Linux)

What it took, 2026-10-26:

- **Card profile "Pro Audio"** (KDE Sound settings or pavucontrol). The default "Analog
  Surround 2.1" hides the extra channels. Under Pro Audio the ports are
  `Scarlett 8i6 USB Pro:playback_AUX0…5` and `capture_AUX0…9`, numbered from 0.
- **Internal routing.** The factory routing feeds all four analogue outputs from the
  internal mixer, which only carries the computer's channels 1–2 — so anything played on
  channels 3–4 never reached the jacks. Analogue outputs 3/4 now play the computer's
  channels 3/4 directly (the kernel's Scarlett Gen 3 mixer driver, `alsa-utils`):

  ```
  amixer -c USB cset numid=26 19     # Analogue Output 03 ← PCM 3
  amixer -c USB cset numid=27 20     # Analogue Output 04 ← PCM 4
  # undo: amixer -c USB cset numid=26 11; amixer -c USB cset numid=27 12   (Mix C / Mix D)
  ```

  This also takes over headphone output 2, which shares those channels. The card name
  (`USB`, the Scarlett's ALSA id — its number moves between boots) and the numids are this
  machine's; `amixer -c USB controls` lists them.
- **Ports used:** pedal feed `playback_AUX2` (out 3), loopback `playback_AUX3` (out 4)
  → line input 5 = `capture_AUX4`, pedal into front input 2 = `capture_AUX1` (input 1 is
  taken by a microphone).
- **Input 2:** INST on, AIR off, **pad on** (`amixer -c USB sset "Line In 2 Pad" cap`;
  without it a bridge humbucker peaked at −6.1 dBFS at minimum gain, leaving too little
  headroom for a drive with its Level up), **gain fully counter-clockwise**: the one knob
  position that can always be found again.
- **Stereo pedals (Output B):** input 1, set like input 2 (INST, AIR off, pad on —
  `amixer -c USB sset "Line In 1 Pad" cap`, undo `nocap` —, gain fully counter-clockwise);
  `init … --pedal-in-b "Scarlett 8i6 USB Pro:capture_AUX0"` records it as the WAV's third
  channel (pedal A, loopback, pedal B). Input 1 has no calibration of its own: compare it
  with input 2 on the bypass takes (the RV-6's Output B carries the dry signal bypassed).
  Phantom power is shared by inputs 1–2: keep it off.
- **Reference level:** −13.5 dBFS on input 2 (bridge humbucker, hard palm mutes; hard
  open strums −13.9). The interface plays it at −4.67 dBFS.
- **Noise:** with the bypassed pedal the input sits at −93.7 dBFS RMS, 80 dB under the
  reference; mains hum at 50/150 Hz is at −129/−132 dBFS, nothing from the computer's
  PSU, drives or monitor shows above the measurement floor.
- **After a session, put everything back**: the Pro Audio profile renames the card's
  ports, so every saved connection breaks — NAM-X's (it restores them by port name at
  launch, and saves whatever it is connected to when it quits) and any virtual device
  fed from the card (here a "Voz (Scarlett In 1)" microphone, which fell back to another
  card's input). Restore the profile, the routing and the pad:

  ```
  pactl set-card-profile alsa_card.usb-Focusrite_Scarlett_8i6_USB_SERIAL-00 \
      output:analog-surround-21+input:multichannel-input
  amixer -c USB cset numid=26 11; amixer -c USB cset numid=27 12
  amixer -c USB sset "Line In 2 Pad" nocap
  ```

  then restart NAM-X (or reconnect it from the status bar) before closing it, so it
  doesn't save the broken connections.
- **PipeWire quantum 128** gave no xruns. If takes report xruns, raise it for the session
  (`pw-metadata -n settings 0 clock.force-quantum 1024`, back with `0`).

## Workflow

```
tools/capture/capture.py ports                                      # pick the ports
tools/capture/capture.py init captures/boss/<pedal>/<date> --pedal-out … --loop-out … \
    --pedal-in … --loop-in … --note "input gain, pad, power, cables…"
tools/capture/capture.py init captures/boss/<pedal>/<date> --from <earlier session>
tools/capture/capture.py meter <session>                             # guitar peak, noise
tools/capture/capture.py calibrate <session> --target-dbfs <hold>    # pedal bypassed
tools/capture/capture.py plan tools/capture/plans/<pedal>.json       # list the takes
tools/capture/capture.py run <session> tools/capture/plans/<pedal>.json
```

`run` is interactive (run it in a terminal). After you press Enter it listens to the pedal
first: a delay with high Feedback keeps repeating after a take, and you can't hear it (its
output only goes to the interface). If the pedal is already quiet it records at once; if
it had to wait, it says so and asks for Enter again (so a take never starts while you are
still setting knobs); if it still sounds after 30 s it suggests turning Feedback down or
switching the pedal off and on (which clears the repeats). It names the knobs to set,
marking the ones
that changed since the last take, records, prints the take's peaks, latency and xruns,
and flags problems (clipping, a silent channel or loopback, an unclear marker, xruns)
with a redo option; a bypass take with sound where nothing was played (the pedal left on)
is flagged too. It skips takes already recorded, so stopping and running it again
resumes. Knob positions are clock positions: 7:00 fully counter-clockwise, 5:00 fully
clockwise. Positions between those are set by eye; the fits treat them as approximate.

Power the pedal from its adapter where possible: a draining battery changes a pedal's
headroom (and an analog LFO's speed) between takes. The TR-2 session ran on a battery.

### Reamp takes (guitar DI)

For a by-ear A/B between a pedal and NAM-X's model of it, and for checking a model against
real playing, every pedal gets the same guitar: **the DI dataset**, nine short files in
`captures/di/`, recorded once (2026-11-07 on) and played into each pedal exactly as hard as
they were played. `capture.py di SESSION captures/di/NAME.wav --seconds N --note "..."`
records the guitar through a bypassed pedal (as it reaches a pedal's input), scaled so 1.0
is the rig's reference. The WAVs are git-ignored like every capture — **back them up**: every
A/B needs these very files; their `.json` are committed.

| File | Seconds | What to play (bridge humbucker unless said, as in the calibration) |
|---|---|---|
| `noise` | 10 | guitar volume up, hands off the strings: its hum and hiss (the gate) |
| `notes` | 90 | each string, low to high: open, 5th, 7th, 12th fret, picked medium, each left to ring ~3 s |
| `dynamics` | 40 | one note in three registers (low E 3rd fret, D 7th, high E 12th), picked very soft to very hard in six steps, ~2 s apart |
| `chords` | 60 | open chords (E, A, D, G, C, Em) strummed and let ring ~4 s each, then the same arpeggiated |
| `chugs` | 25 | an E5 power chord: palm-muted 8th-note chugs for two bars, one open hit, silence; repeat |
| `funk` | 25 | 16th-note strumming on one chord, mostly muted scratches |
| `stabs` | 25 | one short chord stab on each bar's first click, muted at once; silence for the rest |
| `lead` | 60 | a lead line across the neck; end on a held note |
| `neck` | 40 | the neck pickup: the six open chords strummed, one every 4 clicks |

`tools/capture/reamp.py di` walks through them in this order, saying what to play before each (with a
click in the headphones); after each recording, and for one already recorded, Enter keeps it and r
records it again. Every recording is saved as it ends: a run stopped with Ctrl+C resumes. Each recording starts on Enter; start playing after a second of
silence.

`tools/capture/reamp.py PEDAL` then plays the dataset into a pedal: it creates the day's
session (`captures/boss/PEDAL/DATE-reamp`, ports and calibration from the rig, Output B
recorded for the stereo pedals: CE-5, RV-6, RT-2) and runs `plans/PEDAL-reamp.json`: a bypass
reference, the main setting with every segment, then 2–5 extra settings with the segments
that show them (11–19 min of audio a pedal). Plans for the twelve pedals with a NAM-X model,
generated together so they share one layout. The main setting's first take is music (the noise
comes last), so a check on it can catch a wrong setting before the rest of the run: the RT-2's
plan sets `outputs_differ`, and `run` flags a take with the effect on whose Outputs A and B come
out the same.

### Focused reamps (2026-11-09 on)

The plain reamps say *that* a model is off, not *which part*: every model here was fitted and
checked on test signals (sweeps, steps, plucks), and the first guitar through the NS-2, the RT-2
and the RV-6's Shimmer showed errors those fits had hidden. A focused reamp is a short session of
guitar excerpts arranged so each take shows one thing, scored the same way for every pedal. The
user's rule (2026-11-09): which pedal needs work is decided by these numbers, not by ear.

`tools/capture/reamp.py PEDAL --plan focused` runs `plans/PEDAL-focused.json` into the session
`DATE-focused` (`--session NAME` for another folder, e.g. a redo). What a plan is made of:

- **Excerpts** of the DI dataset, 10–25 s (`"start"`, `"seconds"` on a file signal): chords 0–25 s
  (strummed: the widest spectrum, sustained), funk 0–12 s (the dataset's hottest peaks), chugs
  0–10 s (the low strings), lead 2–22 s.
- **A level ladder** (`"level_db"`: −24 / −12 / 0): what scales with the level is linear, what
  doesn't is a clip, a threshold or a detector.
- **The same take twice**: the pedal against itself, the floor under which a model's error can't
  be told from the pedal's own.
- **Held-out takes** (the lead excerpts): never used in a fit, only for the final number.
- **A probe** for pedals whose gain or sweep runs free or follows the playing (`"probe": {"freq":
  9000, "level_db": -30}` on a file signal: a steady sine under the guitar, at that level re the
  reference whatever the file's own level): the pedal's gain is read from it at every moment,
  guitar or not, also where a note has died into the noise. Each setting gets the probe alone
  too. Where a pedal has a detector, one probe-only take must show it ignores the probe.

**The scorecard** (`scorecard.py`): pedal and model through one causal third-octave bank
(80 Hz–10 kHz), the level per band in 50 ms windows compared wherever either side is 12 dB over
the rig's floor in that band; `d` = model − pedal in dB. Reported per take: `rms` (the headline),
`p95`, `level` (one gain off), `tone` per band (a filter off), `residual` (the waveform's error
after sub-sample alignment; time-invariant pedals only). `scorecard.load` undoes the one-sample
slips PipeWire makes in the middle of about one take in six (found on the loopback channel; the
sidecar it returns lists them under `slips`): a slip in the bypass take made the chain's estimate
1.6 dB low at 9 kHz before that. The model's render goes through the bypass take's chain (a
512-tap FIR) first. Each pedal needs an offline render of NAM-X's own processor (`render_*.cpp`)
and a small script that feeds the scorecard (`ge7/focused.py` is the pattern).

**Before a session**: the pedal on its **adapter** (the GE-7's first focused session ran on a
battery by mistake: its ceiling sat 1.7 dB lower); the interface on the capture profile; the take
count said out loud. **After**: the interface restored, the takes checked (xruns, slips), the
scores, the sidecars committed on `captures/reamps`, then merged into `pedals/all`.

**The order** (the user: easiest to hardest) and where each stands, 2026-11-09:

| Pedal | Focused session | Result |
|---|---|---|
| GE-7 | done: `2026-11-09-focused` (battery, 8.45 V), `2026-11-09-focused-psu` (adapter, 9.33 V) | the linear model at the pedal's own floor on guitar (0.1–0.3 dB rms with the slider positions fitted, the pedal twice 0.14–0.17; hand-set sliders alone cost 0.3–1.1 dB). The Equalizer's controls then became ours (dB, ±15 on every band, movable frequencies) and its clip was removed (on guitar the pedal's ceiling sits at +2.2 to +2.6 / −3.0 dB re the guitar's peak, 1.7 dB lower on the battery); frozen (the GE-7 study, The controls, has the formulas) |
| TR-2 | done: `2026-11-09-focused` (adapter; the probe) | the gain doesn't depend on what is played (each guitar take's gain cycle within 0.0006 of full gain of the probe-alone one), so the test-tone fit carries over whole; the model's cycle 0.007–0.031 rms of full gain from the pedal's (the study's own fit: median 0.013), worst on the square settings' edges; the knobs within 1 % of the plan; guitar bands 0.16–0.38 dB rms but for the square settings (0.8–2.6). Frozen, nothing changed (the TR-2 study, On guitar). A tone-only session on the square settings (`2026-11-09-edges`) then found the model's largest error — Wave up with Depth down: it dips to silence where the pedal stops at 0.34 — and a Wave top that falls with the rate; the refit fixes both without touching Wave 12:00 and below (the study, The square settings). In NAM-X since the same day, with Depth and Wave as plain curves over the pedal's range; frozen in that state |
| DD-8 | done: `2026-11-09-focused` (16 takes, the adapter, Output A, the CARRYOVER switch on; no xruns, no slips), analysed 2026-11-10 (`render_dd8.cpp`, `dd8/focused.py`, `dd8/repeats.py`) | Standard, Warm and Mod: the repeats' tone right within 0.5 dB, their level 1.1–1.9 dB hot (mostly the hand on the steep Level knob), the decay within 0.35 dB per repeat, the clip's effect on the tone reproduced within 0.8 dB. **Analog**: the pedal's loop loses bass every pass and has a hump at 1.6 kHz the model lacks — at Feedback 3:00 the pedal rings on in the mids (−0.25 dB per repeat at 1.6 kHz) where the model fades evenly (−1.4). **Tape**: its band is inside the loop and narrower than the model's (+5 dB of bass and +4 dB of treble too much on the first repeat, more on each one). Nothing changed: a refit is the user's call (the DD-8 study, On guitar). The two Feedback 3:00 takes were still ringing when their 10 s tails ended (Standard 45 dB over the floor, Analog 57): the decay per repeat is all there, the very end isn't. The marker chirp went through the pedal and its repeats land on every take (the model's render gets them too: the scores stand): **delay and reverb plans need `marker_to_pedal: false`**. Stereo, a first look 2026-11-10 (`2026-11-10-stereo`, `-stereo-input-b`): fed through INPUT A it is a ping-pong in every mode (odd repeats on A, even on B, equal in pairs, the Feedback once per pair: one loop 2T long); fed through INPUT B alone, a wet/dry split (the DD-8 study, Stereo) |
| CE-5 | done: `2026-11-10-focused` (both outputs: the chorus alone on A, the dry on B; 28 takes) and `2026-11-10-focused-mono` (the same takes, Output A alone); `render_ce5.cpp`, `ce5/focused.py`, `ce5/tone.py` | the probe (8 kHz) reads the sweep to under a microsecond: it doesn't depend on the playing, and its shape has a corner at the bottom too (the model rounded it: 50–93 µs rms off; a new law: 4–7). The wet clip sits after the Low and High filters; the chorus-alone output is flat to 8 kHz (the model 1–5 dB dark from 5 kHz). A candidate with all of it: the chorus alone cancels against the pedal's to −28 to −31 dB on moving takes (was −7 to −13), bands within 0.1–0.3 dB rms (were 0.7–1.5). In NAM-X since the same day (the user approved it on the plots; the CE-5 study, On guitar) |
| BF-3 | recorded and analysed 2026-11-10: `2026-11-10-focused` (33 takes, both outputs); `render_bf3.cpp`, `bf3/focused.py` | Output B is the same flanger an eighth of a cycle ahead (the pedal's stereo). With the delay standing still Standard (refit) cancels against the pedal's whole output to −32 to −35 dB where Standard reaches −8 to −14 (its bass 3–6 dB off): the refit is the pedal. The sweep's law holds with new numbers; moving takes −16 to −31 dB (were −6 to −13), limited by 30–75 µs of sweep shape. Ultra is −11 to −13 dB: open. In NAM-X since the same day: the refit is Standard, the sweep's numbers, a Stereo switch (the pedal's), the knobs in real units (preset version 7). Ultra turned out to be two of Standard's flangers in series (d/2, then d, a Res loop in each): −23 to −26 dB standing, about −20 moving, where the three-tap model reached −11 to −13 (the BF-3 study, On guitar) |
| PH-3 | recorded and half analysed 2026-11-10: `2026-11-10-focused` (38 takes, every mode; tone takes: nine sines together, the phaser's response at each at every moment); `render_ph3.cpp`, `ph3/focused.py`, `ph3/stages.py` | The Stage modes are chains of identical first-order all-pass stages on one sweeping corner (4: four; 8: six and two parked at 4.35 kHz; 10: six and four parked at 7.65 kHz; 12: six, run twice); Depth only shrinks the LFO's travel. A candidate on that structure follows the pedal's tones over the whole cycle within 0.4–1.3 dB with Res off (was 2–8.4). Res on 4 and 8 Stage, Step, Rise/Fall, the guitar takes: open (the PH-3 study) **2026-11-11:** two more sessions. `2026-11-10-res4` (17 takes: the sweep stopped at five corners, Res 7:00, 12:00, 5:00 at each; `ph3/res4.py`): what Res adds in 4 Stage follows the corner, the loop is the plain one (k 0.78 at 5:00), plus two more sweeping orders that no single network fits: 4 Stage Res is left as it is (the user: closed). `2026-11-11-rise` (9 takes): Depth is not dead in Rise and Fall: it sets the mix (deep notches at bypass level fully up, shallow and 5.6 dB louder at 7:00); now in the Phaser, 100 % unchanged. Step's tone takes don't fold on the cycle: not scored. |
| CS-3 | done 2026-11-11: `2026-11-11-focused` (27 takes: guitar over a quiet 9 kHz probe at Sustain and Attack 7:00, 12:00, 5:00) and `2026-11-11-attack` (10 takes: eight Attack positions); `render_cs3.cpp`, `cs3/focused.py` | after a chord the gain climbs in dB toward one fixed level whatever Sustain is; Sustain 5:00 has 2.9 dB more gain than October read. With both, the gain on guitar is within 0.2–0.8 dB rms of the pedal's and the recoveries within 0.1–0.45 dB at every setting. The refit became the Compressor that day; not heard yet |
| AW-3 | done 2026-11-11: `2026-11-11-focused` (24 takes: five quiet tones under the guitar read the filter's frequency over time), `2026-11-11-jumps` (9), and two blind sessions in no fit; `aw3/focused.py`, `aw3/fit_detector.py` | October's fit, on noise alone, moved the filter 0.6–0.8 octave too little on a guitar; the detector and control law refitted, within 0.14–0.33 octave on every take, 0.09–0.22 on the blind session. The user: sounds really good (2026-11-21). A check session that day (`2026-11-21-check`, 13 takes at settings in no fit) found it 0.6–0.8 octave short at Sens 9:00: an onset path below Sens 12:00 (`aw3/fit_onset.py`) brings that to 0.23–0.32; the user: sounds good |
| RV-6 | done: `2026-11-15-focused` (123 takes, the adapter, both outputs; no xruns, no clipping; one-sample slips in 30 takes, undone), scored 2026-11-15 (`rv6/takes.py`, `rv6/focused.py`, `rv6/plots.py`); the `tone-0900` block was recorded at Tone 12:00 | Modulate, Spring, Plate, Hall, Room: level within 0.9 dB at E.Level 12:00–5:00, the wet linear, Tone 12:00–5:00 within 1.2 dB rms, decay from Time 12:00 up within 9 % (Plate, Hall, Room), on guitar the bands within 1.2–2.3 dB rms. Off: E.Level 9:00 2.4 dB quiet; Tone 7:00 under 250 Hz (the pedal's bass lift depends on the mode: Plate 14 dB short at 80 Hz); Time 7:00–9:00 (Hall 11–31 % long); **Shimmer** (after a burst its tail half as long as the pedal's below Time 3:00, 7–9 dB quiet on stabs). **Fitted 2026-11-17** (`rv6/tone.py`, `rv6/time.py`, `fit_rv6_shimmer.py --focused`): E.Level 9:00 within about 1 dB; Tone one filter per measured position (0.1 dB) plus the low reverb the pedal adds below noon in Plate, Hall and Room (Plate's Tone 7:00 4.4 → 0.5 dB rms); Hall's tank after its swell (short Time 4.0 → 2.5 dB rms on the burst's levels); Shimmer 4–12 → 2.4–6 dB rms on bursts and stabs, then by the user's ears still brighter and louder than the pedal: the comparison hadn't counted the model sounding on over the pedal's silence; corrected, with the octaves' regeneration following Time, a lower top for the climb and Tone 9:00 from `check-a`. Left: Tone 9:00 (six takes to redo), Shimmer's late top octave, its swell at long Time and the pedal's repeating 1.35 Hz pattern, Hall's and Room's mids at short Time |
| NS-2 | done 2026-11-21: `2026-11-21-focused` (13 takes: the signal into INPUT, a three-tone probe into RETURN, so the output is the probe times the gate's gain; tones round the threshold and guitar at Threshold 12:00, 3:00, 5:00 and Decay 7:00, 12:00, 5:00) and the same takes recorded plain (`-focused-plain`); `ns2/focused.py`, `ns2/circuit.py`, `ns2/focused_ab.py`, `render_ns2_gain.cpp` | not a switch: the gain follows the level linearly in amplitude over 7 dB, a loud note holds it open (24–870 ms by Decay), a signal carrying on slows the closing. The control circuit modelled from the schematic and fitted: the gain on guitar in no fit within 0.8 dB rms (the law before: 4.6), on the 2026-11-08 reamp the worst windows under the threshold 1–3 dB (were 5–17). In NAM-X, approved by ear by the user (2026-11-21). Misses: the release's last stretch, the hold with a signal carrying on |
| RT-2 | done: `2026-11-21-focused` (15 takes, Mode III, rotors parked: a quiet sweep, 110 and 441 Hz ladders, a noise ladder and funk per take; the horn, the drum and both), analysed and fitted the same day (`rt2/parked.py`, `rt2/drive3_curve.py`, `rt2/drive3_fit.py`, `rt2/drive3_check.py`) | Mode III's Drive read directly (each parked rotor's response divided out): one curve with a moving bias ahead of both rotors. On guitar at the pedal's outputs within ±0.7 dB per third octave from 100 Hz to 8 kHz at every Drive position and on the lead kept out of the fit; the clipper before it was 5–9 dB short at 3–5 kHz. Then `2026-11-21-focused-m12` (29 takes, `plans/rt-2-focused-m12.json`: the same takes in Modes I and II; `rt2/drive12.py`): Mode III's curve and bias with other gains, out of the path at 7:00, a 5.5 kHz low-pass after the curve; within ±0.7 dB from 200 Hz to 8 kHz at every position, and on the Drive 3:00 reamps of 2026-11-07 (rotors turning) within ±1.3 dB to 5 kHz where the clipper was up to 6.7 dB short |
| OC-5 | dropped (the user, 2026-11-09); the Octave effect removed from NAM-X 2026-11-10 | |

**Knob curves** (the user, 2026-11-04 and again 2026-11-09 — the rule for every pedal): we are
learning the pedal's set of sounds, not its pots. A NAM-X knob is a plain linear or log curve
whose two ends are the pedal's; every sound the pedal makes in between must be reachable, and
where on the travel it sits doesn't matter (the pedal's sound at 9:00 may sit at 9:27 on ours).
A knob that sets one quantity gets the plain curve in that quantity (the Tremolo's Wave: log in
its squareness; the Equalizer's gains: dB); a knob that moves several together walks the pedal's
own path through them at an even pace (the Tremolo's Depth: linear in D, the top level along the
pedal's T-against-D). Consequences: a capture plan doesn't need exact angles between the stops
("somewhere between 3:00 and 5:00" is a whole take: it only has to be reproducible by some knob
value); the positions set by eye stop being errors; each model keeps a `…_from_pedal` conversion
for captures and for settings saved on the old scale; tooltips don't quote clock positions for
such knobs. `KNOBS.md` (branch `docs/knob-curves`) lists the knobs still on pedal positions.

**Where we stopped, 2026-11-11**: the GE-7, the TR-2 and the DD-8 are done and approved by the
user (their sound is frozen: no change without asking). Everything is on `captures/reamps`
(8e22eaf) and merged into `pedals/all` (2b85e69); `build-release/namx` is that build; nothing is
pushed. The Octave effect is out of NAM-X (the OC-5 study stays as a record). Presets are
version 4. The CE-5 followed on 2026-11-10 (see the table: recorded, analysed, the Chorus refitted, its knobs in plain units and its stereo our own; the user: done — its sound is frozen too). The BF-3 followed the same day (the table) and the user signed the Flanger off: frozen as well. **Next pedal: the PH-3** (no focused plan written yet; the probe from the start, both outputs recorded).

**Where we stopped, 2026-11-17 (the RV-6's fits):** the focused session's four findings are in
NAM-X's Reverb, on `captures/reamps` and merged into `pedals/all` (`build-release/namx` is that
build), Shimmer's in commits of their own so that they can be dropped alone; nothing is pushed.
Heard: Shimmer after its first refit only (the user: still brighter and or louder than the pedal;
corrected since, no verdict yet). Everything tried, with its numbers and its dead ends, is in
[`captures/boss/rv-6/HANDOFF.md`](../../captures/boss/rv-6/HANDOFF.md), written for the next
attempt. Listening files: `captures/boss/rv-6/2026-11-15-focused/ab/` (`rv6/ab.py`: pedal, was,
now).
Lessons: (1) a scorer's band levels can't tell a filter from an added signal: Tone's "bass lift"
was a second reverb, found by the two outputs' likeness and by subtracting the Tone-noon twin of a
take (time-invariant modes repeat to the sample: use it); (2) check a spectral fix on guitar over
time before calling it done (the low reverb borrowed from each mode's tank matched every sweep and
rang wrong on stabs); (3) lay a mode's responses at every Time over noon's before fitting tables:
Hall's showed its tank comes after its swell, which no table could express; (4) an override that
a later call silently resets looks like a parameter with no effect: test each new override once;
(5) a comparison limited to the moments the pedal sounds can't see the model ringing on after
them: the Shimmer's top octaves hung a second after each stab unseen until the user heard it —
count every step, the pedal's floor under the model, and sum energy per band as it is heard.
Open, for the user: redo the six `tone-0900` takes (Shimmer's Tone 9:00 is the largest error
left); the Reverb's knobs in real units; the D port has none of this.

**Where we stopped, 2026-11-12 (after the AW-3, the blind tests and the PH-3's Res session):**
- **AW-3:** the detector refitted on guitar, the control capped just over the top, validated on a blind
  session kept out of every fit; merged into `pedals/all`, `build-release/namx` is that build. Left: its
  knobs in real units (preset version 10), a listen; Down's cap is assumed.
- **Blind tests** (`captures/boss/mystery/README.md`; `plans/mystery-reamp.json`, `reamp.py mystery
  --session NAME`): the user sets a pedal and tells nothing; ten general takes; kept as validation.
  Pedal right 7 of 7, mode 6 of 7. They found: the DD-8 holds in all five modes on guitar (done), the
  BF-3's Depth unmeasured under 9:00, the RV-6's Tone in Room, the RT-2's Drive in Mode III, and the
  PH-3's Rise and Fall.
- **PH-3 Res session** (`captures/boss/ph-3/README.md` → Rise and Fall again): Rise and Fall are 8–10 dB
  from the pedal over the cycle even with Res down; the pedal's structure there is read (two passes, a
  loop each, the dry outside) but not modelled. **Next for it:** each pass's terms per tone, filters to
  match, then the engine. The Phaser is unchanged.
- **PH-3 Rise and Fall, 2026-11-13** (no takes; the study → Rise and Fall, read a third time): the wet is
  shifted in frequency (one-sided at every tone), in **three** stages in series, each shifter fed a band
  (the whole range; about 170 Hz – 3.3 kHz; a narrow one about 1 kHz) and the rest passing straight; the
  loops' phase is a straight line in log frequency (−190° an octave). Read from October's noise takes at
  every frequency (`ph3/noise.py`) and from the tones (`ph3/orders.py`). No model yet; every form that
  carried one loop gain across the Res ladder per tone failed (listed in the study). **Next for it:** the
  engine (one all-pass pair in quadrature, an oscillator, three stages with band filters and loops), or
  first four 60 s noise takes in Rise (Rate 9:00, Depth 5:00, Res 7:00, 12:00, 3:00, 5:00) that would
  give every stage's band and loop directly.
  **2026-11-14:** those takes were recorded (`2026-11-14-noise`); no single loop law carries the Res
  ladder (2–4 dB even with every term free per frequency). The user closed it: Rise and Fall stay as
  they are. Next is the Wah's knobs.
- **Then:** the Wah's knobs in real units, the RV-6's focused session. Chorus, Tremolo and EQ get no
  blind test (the user: nearly perfect). Nothing pushed; everything on `captures/reamps`.

Lessons of these two days:
- **Hold a session back.** A model fitted to everything recorded looks fixed; the AW-3's return was
  "fixed" on jumps at one Sens and failed at another. A blind session the fit never saw says so.
- **Check a refit against every earlier session**, not the ones at hand (the AW-3's first refit was a
  third too fast on October's jumps).
- **Group takes by knob setting** and move only the mode switch inside a group, in the switch's own
  order, back and forth: the same angles in every mode, and one calibrating mode in the group measures
  what an in-between angle really gave. Many short takes are less tiring than a few long ones; a
  hundred-odd is still a lot.
- **Test a structure before fitting it.** With one feedback loop 1/H is linear in its gain: three
  responses at three gains are linearly dependent (8 Stage: to 0.05 %), whatever is in the loop. A
  swept effect's gain at one tone is periodic in the sweep: its Fourier series over the cycle and the
  recurrence it obeys give the loops' number and gains directly (`ph3/cycle.py`). Whole-structure
  least-squares fits started from guesses found nothing in a dozen tries.
- **Keep the phase:** the pedal channel over the loopback channel, take by take, is free of latency and
  of the recording's slips.
- **Score the scorer** on a mode known to be right before believing a bad number (`ph3/baseline.py`:
  8 Stage 1.2 dB, Rise 8.9).
- **White noise reads a moving effect at every frequency at once:** short-time cross-spectra against the
  loopback, projected on the sweep's harmonics (`ph3/noise.py`). Nine tones could not place three stages'
  bands in two sessions; a 60 s noise take from October did in an hour. Look for the take that answers
  the question among those already recorded before fitting forms to too few points.
- **Count the orders before naming a structure** (`ph3/orders.py`): the series over the cycle fitted
  with ratios of polynomials of several degrees says how many stages and loops there are; "two passes"
  was one stage short and every form built on it failed.
- **Never read a delay from a click**, nor a reverb's start from a take's first click (the marker's
  tail). A stored setting nobody can see (the RT-2's Balance) can sit wrong for days: check it by sound.

**Where we stopped, 2026-11-11 (the PH-3):** the Stage modes are on the pedal's measured
structure, Res is closer on 8 Stage, 4 Stage Res is closed as it is (three attempts; the user:
other phaser types are there), Depth works in Rise and Fall. All of it is merged into
`pedals/all` and `build-release/namx` is that build; nothing is pushed. **Waiting for the user:**
a listen to the Phaser (every mode; Rise and Fall with Depth turned down; Step, which runs on
the refitted 8 Stage). **Done since:** the Phaser's knobs in real units (Depth 0–100 % = the share of the
travel, Feedback = the loop's gain per pass, 0–75 %, the same in every mode) with preset
version 8; the sound at a pedal position is unchanged (the golden test's entries didn't
move). **The AW-3, 2026-11-11** (`captures/boss/aw-3/README.md` → On guitar): the filter's place over time read on five quiet tones between hits and on the guitar's own spectrum while it plays; the model's detector, fitted in October on noise alone, moved the filter up to an octave too little on a guitar; refitted to both. Lesson: a detector fitted on noise says little about a guitar — fit it on what will be played, and keep the noise as a check. For a resonant filter the waveform residual is useless (a few percent of frequency turns the phase): the track is the measure. Knobs in real units not done yet. **The CS-3, 2026-11-11** (two sessions, `captures/boss/cs-3/README.md` → On guitar):
the gain over time read on the guitar and on a probe under the threshold; the recovery heads
for a fixed level whatever Sustain is, Sustain 5:00 has 2.9 dB more gain, the most gain rises
with a faster release; the refit with those is the Compressor (the first type and the Type knob
gone, Release = the recovery's time scale, preset version 9). Lessons: a probe under the
threshold reads a compressor's recovery to 0.01 dB; when the knob in NAM-X is in real units, a
take that measures its own value (here T) makes the pedal's knob angle irrelevant; say how
sure each claim is before proposing a fix (an offset that follows where the Level was matched
is not a model error). **Waiting for the user:** a listen to the Phaser and the Compressor
(the guitar's battery is dead). **Then:** the AW-3's focused plan. Lessons
of the day: a sweep take must be aligned by its loopback to a fraction of a sample before any
complex fit (whole-sample latencies cost 10-50 %); a tone's response over the cycle drawn in
the complex plane (a circle: centre = the steady part, radius = the moving part) reads a mix
law at a glance; say in the plan what differs between takes that look alike.

What the DD-8 ended as, in short (the study: `captures/boss/dd-8/README.md`): one hard clip for
all modes on each repeat after the mode's tone and gain; the line's own limit (Analog's a soft
saturation); the feedback path 16 samples late per pass; Tape's wow a staircase of pitch on a
0.186 s clock; Analog's and Tape's loops and Standard's and Warm's tone refitted through a wide
window; Feedback = gain per round at the loop's loudest frequency, 0–100 % (100 holds, nothing
grows), Level in dB (−30 Off … +6, default −12 at the centre), both the same in every mode; no
Low/High Cut.

**How to compare a model with its pedal** (what the DD-8 took two long days to arrive at: start
here on the next pedal):

1. **The effect alone**: subtract the dry (the played signal through the bypass take's chain,
   `scorecard.chain_filter` / `through`) from the pedal's output and from the model's, and
   compare those. Whole-output band levels hide the effect under the dry.
2. **Waveform against waveform, and plot it, from the first pass**: the residual (model − pedal re
   the pedal, dB) and a picture (envelopes, level per third octave and 20 ms, the difference
   map, the two waveforms overlaid at a few moments: `dd8/loop.py --plot`, `ab_plot.py`). A
   table of band levels matched within 1 dB while the timing was 17 samples a pass off.
3. **Timing to a fraction of a sample**: each arrival against the dry and against the one
   before it (cross-correlation, 8–16× oversampled). One sample is 8–10 dB of residual; slide
   the model by the fraction of a sample the pedal is off before reading a residual.
4. **Tone through a long window** (170 ms or more): a 21 ms window read every mode's lows 1 dB
   or more high.
5. **Anything nonlinear: a ladder** of short bursts from under to over the limit, at two
   frequencies (one whose harmonics pass the effect's filters), with what follows each burst
   in silence. It showed where the clip sits and that one clip serves every mode.
6. **Anything that moves: a probe tone** under the guitar (−30 dB, above the guitar's band); its
   phase on the effect's output is the delay moment by moment. It gave Mod's sweep to 17 µs and
   Tape's staircase.
7. **A knob that several takes share is set once and left**; a knob set by eye is matched to
   what the pedal did (level, fade) before comparing: "apples to apples" (the user). Listening
   pairs (`ab.py`, `dd8/loop.py --ab`) are rendered that way.
8. **Per-pass or per-cycle quantities from long tails**, band by band: 0.01 dB precision.
9. Plans for anything with a tail: `marker_to_pedal: false` on test signals (on guitar takes
   the marker's repeats turned out to be a useful probe). The slip finder in `scorecard.load`
   misreads takes with a steady tone (probe, sweep): load those as recorded (`dd8/loop.load`).
10. When the user hears a difference the numbers don't show: look at the files they heard, and
    plot them, before proposing a cause.

**Open, for whenever** (none asked for now): the Delay's Feedback up to 110 % (the pedal's
self-oscillation; the user: maybe later, not now), a headroom trim for its clip and a Dry
switch (offered, not requested), its stereo at high level (the pedal's second pair isn't louder
than Feedback allows; needs a two-output session), Analog's attack (5–8 dB short of the
pedal's harmonics above 4 kHz; the 300 Hz ladders are the data), the pedal clears its line on
a mode change (NAM-X keeps the repeats; the user hasn't chosen). Older: the NS-2 candidate's
listening triples, the BF-3 and CS-3 "(refit)" types, the Equalizer's gain labels following a
moved frequency, the TR-2's last details, the D port (none of November's changes), pushing.
Found on the way: the Debug build's `--self-test-and-exit` aborts on an assertion in
`KnobWidget::set_taper` (the Flanger's or Chorus's Rate knob); the Release build passes.

**Headroom clips**: the user's stance (2026-11-09) is that a pedal clipping a little over a
guitar's peak is a limit of its 9 V hardware, not its sound. The Equalizer's is gone. The
BF-3's output clip, the CE-5's input and wet clips, the DD-8's input clip and the OC-5's are to
be decided as each pedal's session comes up — ask then; the CS-3's ceiling and the RT-2's Drive
are the effect and stay.

## Sessions and file formats

A session is one pedal on one day: `captures/<maker>/<pedal>/<date>/`.

- `rig.json`: ports, reference level (`ref_dbfs`), calibration, free-text note.
- `<take>.wav`: float32, 2 channels: 1 = pedal output, 2 = loopback. **Git-ignored**
  (~10 MB per take, ~240 MB per pedal); back them up separately.
- `played/<take>.wav`: exactly what was played on the loopback (mono float32). Git-ignored too.
  With `marker_to_pedal: false` the pedal got the same with everything before `signal_start`
  silenced: zero that part before using the file as the pedal's input.
  With a plan's `loop_probe` (`{"freq": [...], "level_db": ...}`: a second feed for a pedal with
  two inputs, the NS-2's INPUT to its detector and RETURN to its gain) the loopback output played
  the marker and then that steady tone, saved as `played/<take>.loop.wav`, and its cable went to
  the pedal; `played/<take>.wav` is the pedal's own signal, and channel 2 of the recording is the
  pedal's output again (the latency is read from the marker in it).
- `<take>.json`: the take's sidecar: pedal, bypass flag, knob settings, signal spec,
  sample rate, reference level, `marker_start` and `signal_start` (samples, in the
  played file), `latency_samples` and its correlation quality, peaks, xruns, SHA-256 of
  what was played, time, the tool's git commit, and a copy of the rig.
- `run.log`: every `run`'s console transcript, appended (answers, redos, problems):
  read it instead of copying the terminal.
- `fit/`: a fit script's output (parameters JSON and figures).

A take as played: 0.3 s silence, a 50 ms Hann-windowed log chirp (200 Hz–10 kHz) at the
reference level, 0.5 s silence, the signal, the tail (default 1 s). In the recording,
the signal starts at `signal_start + latency_samples`. Note: the TR-2 sidecars' peaks
were recorded before e63d826, so they include the marker and all read about −13 dBFS.

## Analysis helpers (`analysis.py`)

- `Take(session, name)`: loads a take; `signal(extra_seconds)` returns the pedal channel
  over the signal, aligned.
- `envelope(x, rate)`: Hilbert magnitude, 300 Hz low-pass: the gain over time of a
  sine carrier.
- `lfo_frequency(g, rate)`: the modulation's fundamental to ~0.01 % (FFT peak, refined
  by maximizing the projection on a complex exponential) and its phase.
- `fold_cycle(g, rate, f, phase, bins)`: the median over all cycles, per phase bin, phase
  0 at the fundamental's peak.
- `third_octave_db`, `harmonics_db`, `rms_db`.

## Adding a pedal

1. Write `plans/<pedal>.json` (see `plans/tr-2.json`: `pedal`, `knobs`, and `takes` with
   `name`, `bypass` or `settings` for every knob, `signal`, optional `note`). Order the
   takes so each changes as few knobs as possible; include bypass references of the same
   signals and a noise floor.
2. `init` a session (or `--from` the last one), check the rig with a short `meter`, and
   `run` the plan.
3. Explore the data in a scratch script first; write `fit_<pedal>.py` when the model
   structure is settled, and document the study in `captures/<maker>/<pedal>/README.md`.

## Lessons

- **A take can slip mid-way without an xrun** (2026-10-31). PipeWire dropped ~1 ms
  (47–48 samples) in the middle of three takes out of 666 — both recorded channels shift
  together, the start marker can't see it. `run` now checks the loopback against what was
  played over the whole take (`find_slips`) and flags a slip for a redo. It can only read
  windows with one clear match — noise, a sweep's upper octaves — not tones, sine steps or
  plucks. Pedal-over-loopback deconvolution cancels a slip; dividing by a bypass twin
  doesn't. Found so far: `aw-3/2026-10-31-up/stairs-manual-0700`,
  `aw-3/2026-10-31-modes/down-rest-manual-0700`, `ge-7/2026-10-28-marks/6k4Hz-p15-sweep`
  (its 1/12-octave band energies move by under 0.1 dB).
- **Keep a deconvolved impulse response's negative times** (`analyze_dd8.head`, 2026-10-29).
  The pedal input's analog path is a fraction of a sample faster than the loopback, so a
  bypass (or analog) impulse peaks just before 0 and ~20 % of its energy wraps to the end of
  the circular deconvolution. Cutting `h[:n]` dropped it: every bypass read 1–2 dB low, every
  digital pedal's level re bypass that much high (CE-5 dry +2.3 → +0.35 dB, BF-3 −0.1 not
  +1.8), dry windows lost the same share (the DD-8's repeat tones, the CS-3's Tone curves,
  the NS-2's top end bent by up to 1.5 dB) and the PH-3's notches filled in. Found with the
  OC-5: a level staircase (no impulse response) disagreed with the sweep by 2.2 dB. The rolled
  bypass reads −8.8 dB in every session: the rig's loop gain, as the calibration says.

- **Look at the data before choosing a model.** The first guess (a triangle morphing
  into a square) was wrong; the clamp showed up in the first plot of averaged cycles.
- **Fit the knob structure, not each take.** Separate per-take fits always look good;
  tying parameters to knob positions (one value per Wave position, shared by every take
  that uses it) is what tells a right structure from a wrong one, and the cross takes
  (settings no other take combines) are the check.
- **Linear relations between per-take parameters reveal the structure.** Offset against
  amplitude fell on one straight line per knob, which gave the formula.
- **Look hard at the measured-vs-model plots:** the asymmetric smoothing (fast fall,
  slow rise) showed as the fast takes' edges not matching.

From the DD-8:

- **Check the references.** A "bypass" take recorded with the pedal still on made the
  Analog mode look like it had an expander. `run` now flags a bypass take with sound
  where nothing was played (`signals.silent_windows`: burst gaps, between clicks, tails).
- **The pedal can't be heard during captures** (its output only goes to the interface),
  so `run` checks for silence after each Enter: a delay with high Feedback keeps
  repeating, and switching the pedal off and on clears it.
- **Time-varying effects break deconvolution.** Tape's wow and Mod's LFO wander the
  delay, and a swept measurement of feedback or tone comes out scattered. Bursts, with
  each repeat's level read directly, are robust.
- **Design tests that can tell hypotheses apart.** Plucks 4.000 s apart came back with
  identical timing in Reverse — but 4 s was ~7 chunk lengths, so a fixed grid predicted
  the same. Irregular onsets settled it (triggered by the playing).
- **Cross-check every surprising result with a different kind of take** before writing
  it down: the "compressor" (a clip, seen through full-level sweeps), the "runaway" at
  Feedback 5:00 (a hold: the start marker's echoes at the clip ceiling), the "±2.4 dB
  wobble" (an analysis edge effect) — all caught that way.
- **Knob positions the player can set:** 7:00, 9:00 (pointer left), 12:00, 3:00
  (pointer right), 5:00 = 0, 20, 50, 80, 100 % of the rotation. By eye, "12:00" set
  twice differed by ~5°; 3:00 repeated to the sample.
- **Signal levels:** the DD-8's repeats clip ~3 dB under a hot guitar's peaks, so
  anything meant to be linear (sweeps for tone or feedback) goes at −12 dB.

From the CE-5:

- **A pedal's own hiss can be louder than the silence threshold.** `run` waits for a
  peak under −75 dBFS before each take; the CE-5 hisses at −65 to −73 even when idle. A
  plan can set its own `quiet_dbfs`, and so can a take whose setting raises the hiss (the
  GE-7 with every slider up hisses at about −36 dBFS).
- **Look for a wet-only output.** With a plug in Output B (other end loose), the CE-5's
  Output A carries only the effect sound: no dry to separate.
- **Split dry and wet in time, not by subtraction:** windowing the impulse response
  between the dry (1 ms) and the wet (6.4 ms) needs no alignment; subtracting another
  take's dry does, to a fraction of a sample (the loopback latency can be a sample out).
- **A wrong take can be read back:** the mis-set `dry-sweep` gave away all three wrong
  knobs (wet level, LFO rate, delay swing). `run.log` now keeps every session's transcript.
- **Flat group delay through a steep cut means digital:** an analog filter that steep
  delays the frequencies near its corner.

From the BF-3:

- **Click trains read a flanger's sweep:** a click every 25 ms, each echo at the current
  delay — no period ambiguity at 0.3–15 ms. Only an LFO near 10 Hz (4 clicks a cycle) is
  too fast; its swing comes from the fundamental of the four phases.
- **A digital pedal runs on its own clock:** its output lands up to a sample apart from
  take to take, which the loopback can't see. Fit the shift when one take's template is
  used on another, or measure inside one take.
- **Resonant settings need a quieter sweep:** with Res up the comb's peaks reached the
  output clip at −12 dB (the run log's peak against the clip tells); −24 dB is clear.
- **Loop gain from the comb's ripple depth** (max/min of the wet over its no-feedback
  twin), not from a complex fit that hinges on the loop's phase.

From the CS-3:

- **Order a plan by setting, not by signal:** takes that share a knob setting go back to
  back, and the settings are sequenced for the fewest knob turns (check the `plan`
  listing's "(changed)" marks).
- **A compressor needs time in the signal:** 1.5 s steps settle the static curve; a
  4 s jump each way shows attack and a release of up to ~0.8 s.

From the RT-2:

- **A knob with two functions keeps both values, and the toggle that picks one is easy to
  leave on the wrong side.** The reamp ran twice with the Drive Knob toggle on BALANCE, so
  every "Drive 7:00" take was Balance 7:00: the drum alone. Give a reset its own steps in the
  note, and have the data check it: a plan's `outputs_differ` (the text to show) makes `run`
  flag a take with the effect on whose Outputs A and B correlate over 0.99.
- **A take can say what it must sound like** (`expect`, see `check_expect`): within N dB per
  octave of an earlier clean take (`like`), one octave at least N dB under another (`below`:
  a rotor alone), the outputs the same (`same`), plus a `hint`. The follow-up's isolated-rotor
  takes ran at Drive 5:00 unnoticed — the ring moved Drive at its second take, so Mode II's
  "horn alone" was both rotors — and were recorded again with these checks
  (`plans/rt-2-isolated.json`).
