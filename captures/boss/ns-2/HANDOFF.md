# NS-2 control law — handoff (2026-11-08)

For Astra: the next attempt at NAM-X's Boss NS-2 model. Everything a fresh start needs is here or
linked; the study (`README.md` beside this file) has the measurements in full.

**The goal:** a control law that sounds closer to the pedal *on a guitar* than NAM-X's current
model does — by the numbers below — without getting worse on the older measurements, delivered as
an A/B candidate for the user to judge by ear. Not a replacement: the user liked the current model
by ear (2026-11-03), so it stays the default until they choose otherwise.

## 0. Latest session: documented and stopped (2026-11-08)

**Read this section before the original handoff below.** The user asked to document the state and
stop this session. Do not continue fitting or integrate the candidate without a new request.
The recommendation discussed was to stop adding complexity and judge the existing candidate by
ear. No approval to switch branches, replace the default, or add a UI selector was received.

**Checkout:** still `captures/reamps`; no branch was created or switched, no commit or merge was
made, and `src/` is unchanged. A tested C++ integration patch is prepared but unapplied. The
shipping model remains the default. Preserve unrelated untracked files; do not blanket-add or
clean the checkout.

### Result to retain

An offline candidate adds a **20 ms mean-absolute detector** alongside the existing 50 ms peak
follower. Above a 6 dB peak/mean ratio, twice the excess is subtracted from the fast level **for
the closing decision only**. Strong peaks retain immediate opening. The binary latch is replaced
by continuous closing control: `G` moves toward −7.56 dB only while the closing condition holds,
otherwise retains its value, and resets to zero on opening. The old fast/slow followers, slow
expander term, Threshold/Decay maps, open gain and output shelf are retained. The study's
“Offline control candidate: rectified average” section gives the equation and qualifications.

| RMS error, dB | current | candidate |
|---|---:|---:|
| older median | 1.8151 | 1.7669 |
| history median / mean | 0.4005 / 2.5594 | 0.4005 / 2.5594 |
| reamp median | 0.7498 | 0.7132 |
| 5:00 notes | 1.9341 | 1.4453 |
| 5:00 dynamics | 1.8922 | 1.5612 |
| 12:00 stabs | 4.3034 | 3.8199 |
| 3:00, Decay 7:00 funk | 3.3608 | 3.3291 |

No older take or history group worsens; the largest reamp take increase is 0.002 dB. The audit
passes all numerical scoring gates, with both 5:00 takes required below 1.8 dB. These are selection
scores on the available recordings, not an independent holdout. The history creep is unresolved
and the fast-Decay funk barely improves. No further complexity is justified by these results
alone; a remaining audible mismatch would be the reason to revisit it.

**C++ agreement:** maximum per-take RMS **score** differences are 0.045 dB on reamp, 0.037 on
older, and 0.032 on the two complete history takes. Reamp maximum median absolute window
difference is 0.0013 dB, but maximum RMS window difference is 0.096 dB. Do not describe this as
every window agreeing within 0.05 dB. The candidate's state/reset, block partitioning, stereo
independence and finite output checks pass at 44.1/48/96 kHz; its proposed default path is exactly
the original in those checks. The unchanged application passes 87 golden tests. A scratch
`namx-bench` comparison at 48 kHz / 128 frames gives 1.32 microseconds/block for the candidate,
1.14 for the current gate. These checks do not constitute testing an applied integration patch.

### Files and commands for resuming

Repository changes, all uncommitted:

- `tools/capture/ns2/refit.py`: moved CLI dispatch after `Setup` so `sweep` runs; added the
  candidate law and `score candidate`. The original `score` and `sweep` tables were reproduced.
- `tools/capture/ns2/rectified.py`: causal peak/mean feature calculation from played audio;
  local cache `build-ns2/rectified-mean-20ms.npz`, invalidated by source file size/mtime.
- `tools/capture/ns2/verify_candidate.py`: per-take acceptance audit, including every history
  group; writes `build-ns2/candidate-validation.json`.
- This handoff, the study `README.md`, and the listening manifest under the folder below.

```sh
python3 tools/capture/ns2/refit.py score
python3 tools/capture/ns2/refit.py score candidate
python3 tools/capture/ns2/verify_candidate.py
```

Local artifacts are in git-ignored `build-ns2/`; **retain this folder**, which currently contains
the only saved C++ candidate and proposed integration:

- `dsp-candidate.patch`: adds `src/dsp/Ns2ControlCandidate.h` and an opt-in
  `set_control_candidate(bool)` path to `NoiseGateProcessor`; default is false, no UI changes.
  `git apply --check build-ns2/dsp-candidate.patch` passed. The proposed source is in `proposed/`.
- `Ns2ControlCandidate.h`, `Ns2GateTrialProcessor.{h,cpp}`: the compiled scratch implementation;
  the trial processor has a distinct name so it can coexist with the shipping one.
- `render_ns2_candidate.cpp` / `render_ns2_candidate`: full audio rendering, same threshold and
  decay arguments as `render_ns2`, candidate enabled. `render_candidate_control.cpp` / `render_duty`:
  gain-only rendering using that same candidate control header, arguments T in dB re peak and
  Decay in ms. The `render_duty` name is historical; it now renders the final continuous law.
- `check_duty.py`: compares the C++ control with the final 1 ms law on all three sets; uses
  `crest_trial.py` and `control_trial.py`. Results: `conduction-check.{json,log}`.
- `listen_candidate.py`: regenerates the three listening triples using the full C++ candidate
  renderer and the baseline `build-ns2/render_ns2`.
- `candidate_smoke.cpp` / `candidate_smoke`, `candidate-smoke.log`; `NamxBenchCandidate.cpp`,
  `namx-bench-candidate`, `candidate-bench.log`; `golden-test.log`; `sweep-reproduced.log`.
- `conduction-grid.{json,log}`: coarse sensitivity screen; final setting is 20 ms / weight 2 /
  knee 6 dB. Earlier `vca`, `diode`, `series`, `crest` and latched `duty` trials are rejected
  experiments, not alternative accepted coefficients.

**Listening:** `captures/boss/ns-2/2026-11-08-reamp/ab/control-candidate/` contains pedal / NAM-X /
candidate WAV triples for `threshold-0500-notes`, `threshold-1200-stabs`, and
`threshold-0300-decay-0700-funk`. One common gain per triple; one causal 60 Hz high-pass on every
side; both model outputs through the measured bypass FIR, retaining the old output shelf and
+0.6 dB open gain. `manifest.json` records processing, gains and WAV hashes. The audio is local.

**The schematic is now available** (supersedes the missing-schematic suggestion in §6):
[user-supplied PDF](https://schematicsonline.com/wp-content/uploads/2022/11/Boss-NS-2-Noise-Supressor.pdf),
saved as `build-ns2/Boss-NS-2-Noise-Supressor.pdf`. The
[Mitsubishi databook](https://bitsavers.trailing-edge.com/components/mitsubishi/_dataBooks/1995_Mitsubishi_Sound_Processor_ICs.pdf)
is `build-ns2/Mitsubishi-Sound-Processor-ICs.pdf`; M5207L01 printed pages 4-204–4-213 describe the
linear-control VCA. The candidate uses the rectifier insight but is **not a circuit simulation**.
The separate attenuation-state and reduced diode/capacitor fits failed regressions; do not
restart those searches as though they were untried.

If the user later requests integration, confirm the existing branch permission state, create a
DSP branch from `pedals/all` as §7 requires, and apply/review the prepared patch while preserving
the analysis changes. Then wire reproducible renderer/benchmark selection and rerun checks on
the actual integrated processor. `refit.py check` currently checks only the shipping baseline.
The current render/benchmark candidate selection is scratch-only; app/UI exposure is undecided.

## 1. The current model (what NAM-X ships)

`src/dsp/Ns2Gate.h` (constants, knob tables) and `src/dsp/NoiseGateProcessor.cpp` (per sample, per
channel). Levels in dB re the guitar's peak at the pedal's input (NAM-X puts that peak at −6 dBFS,
`ns2::REFERENCE_DBFS`).

```
L       the input's level: a peak follower (instant rise, 50 ms exponential fall), in dB
E       fast level: rises with L at once, falls toward it exponentially in dB, τ = Decay
V       slow level: the same, τ = 7.94 × Decay
switch  opens once E > T + 1.29 dB; closes once E < T + 1.29 − 10.47 dB
G       0 dB while open; closed, sinks toward −7.56 dB with τ = 4.42 × Decay
gain    = +0.6 dB + G − 0.889 · max(0, T − V)
then    a −3.7 dB high shelf (8.2 kHz): the open path's top end as the −12 dB sweeps measured it
```

| knob | 7:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|---|
| Threshold T, dB re peak | −69.40 | −69.02 | −66.72 | −49.40 | −27.33 |
| Decay τ, ms | 24.6 | 63.5 | 93.0 | 145.1 | 460.3 |

The UI's Threshold knob is "opens at", in dBFS (T + 1.29 − 6); Release is Decay in ms. Mode Mute,
Send/Return and the pedal's distortion near full level aren't modelled.

## 2. The data

Three sets, all on this machine (the WAVs are git-ignored: they exist only in this checkout).
`tools/capture/ns2/dataset.py` reads them into one form: per take, the detector's level every 1 ms
(NAM-X's peak follower, dB re the guitar's peak, averaged per ms) and the pedal's gain.

| set | what | sessions | scored on |
|---|---|---|---|
| older (48 takes) | staircases (1 kHz, 4 dB steps of 1.5 s, up and down), −84/−12/−84 dB jumps, 220 Hz plucks decaying 35 dB/s, phrases, soft picking; every Threshold and Decay position | `2026-10-27/`, `2026-10-27-dynamics/` (its "bypass-*" takes ran engaged at Threshold 5:00, Decay 12:00) | the gain at the test tone every 1 ms, with the 10-30 research's validity mask |
| history (12 probe groups) | at Threshold 3:00, both Decay extremes: −56/−54/−44 dB tones for 8 s, after 24 s of quiet and after 2 s at −12 dB | `2026-10-30-history/` | the gain every 1 ms over 0.1–7.9 s of each probe |
| reamp (18 playing takes) | the user's guitar DI dataset (notes, dynamics, chords, chugs, funk, stabs, lead, neck pickup) at Threshold/Decay 12:00/12:00 (all), 5:00/12:00, 3:00/12:00, 3:00/7:00, 3:00/3:00 | `2026-11-08-reamp/` | the output re the open path in 20 ms windows (below) |

How the reamp is scored (`tools/capture/ns2/refit.py`, `reamp_windows`), and why — each choice
fixed a trap that cost a wrong report on 2026-11-08:

- **One 60 Hz high-pass, forward only, on every side.** Each time the pedal switches it steps its DC
  level; the interface's input coupling turns that into a ~1 Hz swing (−36 to −48 dB re the take's
  peak), and the step's edge is a click up to −68 dBFS (~30 dB over the input when it switches on a
  tiny sound). A two-way filter carries the step *back* before the switch.
- **The rig's floor through the same filter**, from the 0.1–0.3 s before the marker: −110.6 dBFS. A
  floor read after a two-way filter came out 37 dB high and made the model look loud wherever the
  pedal cut deep.
- **The open path** = the played signal through the bypass take's chain (a 512-tap FIR from the
  unfiltered bypass recording); the pedal's open gain re it on this dataset is +0.45 dB
  (`REAMP_GAIN0`; +0.55 at 80 Hz … +0.31 at 2 kHz).
- **The prediction** = Σ open-path power × the law's gain, per 1 ms, averaged over 20 ms, **plus the
  floor**; the pedal's power over the same window; d = model − pedal in dB.
- **Scored windows:** open path ≥ 12 dB over the floor (10 ms windows of hiss 6 dB over the floor
  scatter by several dB), not next to a level jump of 6 dB (a one-window slip there is tens of dB),
  not where the pedal is 3 dB louder than the open path (a click, not a gain).
- The noise-only takes are left out: the guitar's hum reaches the pedal ~11 dB over the floor at
  best and sits under every Threshold.

Older and history are pure tones, so a linear filter in front of the detector is a constant offset
for them (`weighting.offset_db`); the reamp's levels per detector variant are precomputed.

## 3. Tools

```sh
# NAM-X's own processor, offline (the reference the 1 ms laws are checked against)
g++ -std=c++23 -O2 -Isrc/dsp tools/capture/render_ns2.cpp src/dsp/NoiseGateProcessor.cpp -o build-ns2/render_ns2

python3 tools/research/validate_ns2_control_candidates.py   # older set → tools/research/ns2_control_regression_cache.npz (12 MB, untracked on purpose; may already exist)
python3 tools/capture/ns2/dataset.py                         # reamp set → build-ns2/reamp-dataset.npz
python3 tools/capture/ns2/weighting.py                       # detector variants for the reamp → build-ns2/reamp-levels-*.npy
python3 tools/capture/ns2/refit.py check                     # the 1 ms baseline vs render_ns2 on the reamp (median |Δ| 0.03 dB)
python3 tools/capture/ns2/refit.py score                     # NAM-X's model, per set and per take
python3 tools/capture/ns2/refit.py sweep                     # the 2026-11-08 search (a few minutes)
python3 tools/capture/ns2/reamp_ab.py                        # the reamp table, listening pairs in 2026-11-08-reamp/ab/
```

`refit.py`: laws are numba functions at a 1 ms control rate, `law(levels, t_switch, t_slope,
decay_s, gain0, p) → gain dB per ms` (the baseline takes one threshold). `Setup(order, corner,
maps, attack)` holds the three sets with a detector variant and fits a law's parameters plus
per-knob threshold corrections by `least_squares`, each take alike within its set, each set alike
(`SET_WEIGHT`). `Setup.report` prints median/mean/max per set and the three 5:00 reamp takes. To try
a new law: write it like `extended`, give it names/start/bounds, and point `Setup.gain` at it (or
generalise `Setup` to take the law). The control-rate surrogate is within 0.04 dB of the
per-sample processor on the reamp; check any new law the same way once it is in C++.

## 4. What the pedal does (measured; the evidence for a new law)

**Over the threshold the model is the pedal** in all 18 playing takes: median +0.0 to +0.2 dB, 95 %
of windows within 0.3 dB. Everything below is about what happens under it.

**Under the threshold, in playing (d = model − pedal; n windows, median, rms; by detector level − T):**

| take | −10…−5 | −15…−10 | −20…−15 | −30…−20 |
|---|---|---|---|---|
| 12:00 notes | 117, +3.5, 3.8 | – | – | – |
| 12:00 stabs (the hiss after each stab) | 113, +7.0, 6.9 | – | – | – |
| 5:00 notes | 698, +2.2, 3.2 | 388, +0.4, 3.0 | 209, −0.8, 3.4 | 134, −0.4, 2.1 |
| 5:00 dynamics | 279, +0.1, 3.0 | 252, −0.2, 2.6 | 100, −0.5, 2.7 | 123, −0.0, 1.9 |
| 3:00 notes | 55, +0.1, 0.9 | 26, +0.0, 2.9 | 73, +2.3, 4.0 | 180, +0.4, 0.6 |
| 3:00 Decay 3:00 notes | 55, +0.1, 0.1 | 26, −0.9, 2.7 | 73, −5.2, 5.1 | 182, −1.2, 3.5 |
| 3:00 Decay 7:00 stabs | 10, +15.5 | 9, +15.1 | 11, +11.8 | 156, +0.6, 4.2 |
| 3:00 Decay 7:00 funk (all under-threshold windows) | median +6.8, p95 13 dB | | | |

Just under the threshold the pedal cuts more than the model (12:00 tails and hiss, 5:00 notes, the
3:00 Decay 7:00 scratches); deeper down the sign varies with Decay (3:00 Decay 3:00: the model cuts
5 dB more).

**On a guitar, where the pedal starts closing depends on the note's pitch** (5:00, Decay 12:00: the
first 20 ms window after a pluck where the pedal's gain is under −1 dB; level re T on NAM-X's peak
detector):

| pitch | 98–164 Hz | 195–296 Hz | 327–369 Hz | 393–495 Hz | 658 Hz | 1 kHz staircase |
|---|---|---|---|---|---|---|
| level − T at close, dB | −3.1 … −6.9 | −3.1 … −7.6 | −3.1 … −5.7 | −6.8 … −12.0 | −7.4 … −11.1 | holds to −8.7 (1.25 s) |

Noisy note to note, but up to ~370 Hz the pedal closes 3–8 dB under the threshold and from ~390 Hz
7–12 dB under. (Two exceptions, unexplained: the dynamics take's two hardest low picks, 15–18 dB
over T at onset, closed only at −17.5 dB.) So the detector hears bass less than treble — but a first-order 150 Hz high-pass in front of NAM-X's
detector, the best of the filters tried, gains only 0.15–0.2 dB.

**On a steady tone the pedal holds fully open inside the hysteresis band** (older
`stairs-threshold-0500`, way down, 1 kHz, Decay 12:00; gain in dB):

| level − T | 1.5 s step: pedal at +0.1 s / +0.7 s / end | NAM-X at the same |
|---|---|---|
| −0.7 | 0.6 / 0.6 / 0.6 | 0.6 / 0.6 / 0.6 |
| −4.7 | 0.6 / 0.6 / 0.6 | −0.2 / −2.4 / −3.0 |
| −8.7 | 0.6 / 0.6 / −0.1 (slides from 1.25 s) | −3.8 / −5.9 / −6.5 |
| −12.7 | −1.8 / −5.9 / −7.2 | −9.4 / −16.1 / −17.4 |
| −16.7 | −8.8 / −12.6 / −13.8 | −18.3 / −20.6 / −21.2 |
| −20.7 | −15.2 / −18.7 / −19.8 | −22.0 / −24.1 / −24.7 |
| −24.7 | −21.2 / −24.4 / −25.4 | −25.5 / −27.7 / −28.3 |
| −28.7 | −26.7 / −28.7 / −30.0 | −29.1 / −31.3 / −31.9 |
| −32.7 | −30.7 / −32.4 / −33.4 | −32.7 / −34.8 / −35.4 |
| −36.7 | −33.2 / −34.4 / −34.9 | −36.2 / −38.4 / −38.7 |

Once it closes it slides with τ ≈ 0.4–0.5 s toward roughly (level − T), and the cut saturates near
35 dB. NAM-X's slope term acts while the switch is still open, so it cuts 3–6 dB at −4.7 and −8.7
where the pedal doesn't — yet on guitar notes the pedal closes *earlier* than NAM-X. The same law
can't be both: the detector (frequency, possibly crest) and the law must change together.

**Way up** (same take), from far below: the gain creeps up ~1.3 dB/s while the input steps up 4 dB
every 1.5 s, then snaps open at about T + 1.3. **After quiet** (history, 3:00): a −56 dB tone
(T − 6.6) starts at −15.5 dB and creeps to −6 over 8 s at both Decay extremes; after loud, minimum
Decay sinks from +0.65 to −5.4, maximum Decay stays open. The 10-30 report
(`tools/research/NS2_HISTORY_RESULTS.html`, `NS2_CONTROL_MODEL_SELECTION.html`) has the rest.

**Other facts:** the cut is the same in every band within ~1 dB (a plain VCA gain, no closing
low-pass); the open path is flat in playing (+0.3 to +0.6 dB re bypass from 80 Hz to 12.5 kHz),
while NAM-X's −3.7 dB shelf makes it 1.5 dB darker at 8 kHz (the shelf came from −12 dB sweeps —
likely slew limiting at a sweep's level, not a filter; a separate A/B offered to the user); a fast
averaging detector breaks the palm-muted chugs at 5:00 (they keep the pedal open the way a peak
reader does; attack ≥ 5 ms makes the model close between hits — seen on the earlier scoring).

## 5. What was tried (2026-11-08) and how it scored

Median rms per take (history: median, mean), on the final scoring:

| attempt | older | history | reamp | reamp 5:00 notes / dynamics |
|---|---|---|---|---|
| NAM-X | 1.82 | 0.40, 2.56 | 0.75 | 1.93 / 1.89 |
| the six constants refitted (all sets; on the earlier scoring) | 1.67 | 1.19, 1.54 | ≈ unchanged | ≈ unchanged |
| `extended` + switch/slope threshold corrections per knob, refitted | 1.69 | 0.33, 1.02 | 0.70 | 1.98 / 2.03 |
| same, detector HP 1st order 150 Hz | 1.70 | 0.34, 1.03 | 0.73 | 2.02 / 1.89 |
| same, detector attack 1 ms | 1.69 | 0.31, 1.07 | 0.68 | 2.17 / 2.04 |
| NAM-X's law, detector HP 150 Hz, nothing refitted | 1.79 | 0.40, 2.56 | 0.61 | 1.72 / 1.68 |
| NAM-X's law, detector attack 1 ms, nothing refitted | 1.82 | 0.40, 2.56 | 0.66 | 1.65 / 1.88 |
| `extended` fitted to the reamp **alone** (no filter / HP 150 / attack 1 ms) | 2.6–3.4 | 5–7 | 0.64–0.94 | 1.84–1.99 / 1.92–2.30 |

`extended` = the baseline plus: the slope term's share while the switch is open (0 = fully open,
as on the staircase), a smooth ceiling on the cut, and separate thresholds for the switch and the
slope term. Also screened on the earlier scoring and not worth repeating: detector high-passes
100–600 Hz (1st and 2nd order; above ~300 Hz everything gets worse), averaging detectors with
attack 0.25–50 ms and RMS detectors.

**The reading:** the refits mend the history probes and leave the playing as it was. The last row
says more: even with the reamp as its only target, this family can't take the 5:00 notes under
~1.85 dB — it cannot make the pedal's closing on a dying guitar note. The 10-30 research found the
mirror image (fits to the history probes broke the older note tails). A new structure is needed,
not new constants.

## 6. Where to go next (my reading — question it)

1. **The circuit.** On 2026-10-27 the user chose to model the control circuit from the schematic;
   no free copy was found (freestompboxes topic 11953 blocks bots — the user may fetch it in a
   browser; Ronsound sells one). Ask the user before anything else: a circuit-shaped law (detector
   network with its own frequency response, comparator with hysteresis, the VCA's control
   smoothing) is the likeliest to generalise across all three sets.
2. **A law built from §4 rather than on the baseline:** a switch with hysteresis on a
   frequency-weighted peak detector; while open, no cut; once closed, a control voltage that
   moves toward a static expander curve (slope ≈ 1 dB/dB below the knee, ceiling ≈ 35 dB) at
   Decay-dependent speed downward (τ ≈ 0.4–0.5 s at Decay 12:00) and a slow, mostly
   Decay-independent speed upward (the creep), snapping open above the threshold. Then check
   it reproduces the staircase table, the history probes and the pitch table *before* fitting it.
3. **The detector's frequency response** as a fitted filter (shelf or pre-emphasis, not only a
   high-pass) jointly with that law; the pitch table and the 1 kHz staircase pin it.
4. **Weighting:** per take alike undersells the under-threshold windows (most windows of most
   takes are open and already right). Consider scoring the reamp's under-threshold windows as
   their own group.
5. **If data is missing, propose a capture** (the user records; never touch the Scarlett or its
   routing yourself): decaying tones at 82/220/660/2000 Hz at Threshold 5:00 and 12:00 (the
   detector's frequency response), slow ramps down at 3 and 10 dB/s (hold vs slide), and noise
   bursts with tails (the hiss case). Rules for plans: knob positions 7:00/9:00/12:00/3:00/5:00
   only; takes ordered so each step turns as few knobs as possible; settings named as printed
   (Threshold, Decay, Mode REDUCTION; the pedal's LED for on/off); tell the user the take count
   and minutes before handing the plan over.

**Done means:** on `refit.py`'s scoring, the reamp median under 0.75 dB and the 5:00 notes and
dynamics clearly under 1.9 dB, no older take more than 0.5 dB worse and the older median not
worse, the history no worse than NAM-X's; the law in C++ matching its 1 ms twin within ~0.05 dB
(the `check` pattern); listening triples for the user (pedal / NAM-X / candidate, one gain per
triple) from the 5:00 notes, the 12:00 stabs and the 3:00 Decay 7:00 funk, under
`2026-11-08-reamp/ab/`. Then the user listens.

## 7. Working in this repository

- `CLAUDE.md` at the root is the project guide; `CPP_GUIDELINES.md` the C++ style.
- **Branches:** pedal work lives on branches merged (`--no-ff`) into `pedals/all`, the build with every
  pedal model; never commit on `pedals/all` itself. Capture and analysis work is on
  `captures/reamps`; a DSP candidate goes on its own branch from `pedals/all` (e.g.
  `dsp/gate-ns2-refit`). Ask the user before switching the checkout's branch. **No git worktrees**
  (stale ones blocked branch switches before); scratch files and builds go in git-ignored `build-*/`
  folders under the repository, not `/tmp`.
- **Sound:** the current NS-2 model is liked by ear — a new law is an A/B candidate, never a silent
  replacement. The gate's Type knob was removed on 2026-11-03 (`noiseGate/type` is ignored); how
  to expose a candidate is the user's call (a temporary Type entry, or a separate build).
- **DSP rules:** no allocations or locks on the audio thread; parameters as atomics; per-channel
  state; `DspGoldenTest` must stay green unless a sound change is intended (then re-record, as its
  header says); `namx-bench` after DSP changes.
- **Before committing:** the clang-format check CI runs on `src/`, `tests/`, `bench/`
  (`--dry-run --Werror`); `git diff --cached --stat`; no personal paths, usernames or machine
  details anywhere — the repository is public.
- **Running NAM-X** (self-tests included): with `PIPEWIRE_REMOTE=none JACK_DEFAULT_SERVER=none
  JACK_NO_START_SERVER=1` and HOME/XDG pointed into a `build-*/` folder, so it neither joins the
  live audio graph nor writes the user's settings.
- A missing tool or Python package: ask the user to install it from the distribution's
  repositories; don't work around it. numba, scipy and soundfile are installed.
