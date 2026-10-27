# Pedal captures

Measurements of real pedals, one folder per pedal (`<maker>/<pedal>/`), one session per
pedal, mode and day (`<date>[-<mode or note>]/`). How it's done — the rig, the tools, the
workflow, the lessons — is in [`tools/capture/README.md`](../tools/capture/README.md).
The WAVs stay on the capturing machine (git-ignored, ~240 MB per session; back them up);
the sidecars, rigs, fits, figures and each pedal's study are committed.

## Status

The collection: 17 Boss pedals, on a Focusrite Scarlett 8i6 (3rd gen).

| Pedal | What | Status | Study |
|---|---|---|---|
| **TR-2** Tremolo | modulation (amplitude) | **Done.** Grey-box model (clamped offset triangle, fast-fall/slow-rise follower, Depth-dependent top level), fitted to a battery and a 9 V session; in NAM-X as the Tremolo's TR-2 type, frozen (branch `dsp/tremolo-tr2`) | [`boss/tr-2/README.md`](boss/tr-2/README.md) |
| **DD-8** Digital Delay | delay | **Done:** Standard, Analog, Tape, Warm, Mod, Reverse; Shimmer minimal (an octave-up wash beside plain echoes, not pursued). Not done: GLT (low priority). Excluded by choice: Warp, Looper, +RV. Not in NAM-X yet: the four "voicings" (Standard/Analog/Tape/Warm) are one engine with a parameter set each — the study's side-by-side table | [`boss/dd-8/README.md`](boss/dd-8/README.md) |
| **CE-5** Chorus Ensemble | modulation | **Next.** See below | |
| BF-3 Flanger | modulation | after the CE-5 (same tools) | |
| CS-3 Compressor | dynamics | quick: bursts and level steps were built for this | |
| SD-1, DS-1, JB-2, HM-2W | drives | NAM captures (a different workflow: NAM's trainer); a NAM "pedal" before the amp needs ROADMAP Phase 24. The SD-1 also cross-checks NAM-X's Tube Screamer model | |
| NS-2 Noise Suppressor, AW-3 Dynamic Wah | dynamics | bursts, steps | |
| PH-3 Phase Shifter | modulation | low priority: NAM-X's Phaser (Phase 90) is approved by ear | |
| GE-7 Equalizer | linear | low priority | |
| RV-6 Reverb | reverb | a project of its own | |
| OC-5 Octave | pitch | reference recordings only | |
| AC-3 Acoustic Simulator | filters | per-mode responses; no NAM-X counterpart | |
| TU-3 Tuner | | nothing to capture | |

Every pedal on a 9 V supply, not a battery: the TR-2's tired battery (8.37 V) held the top
of its cycle ~0.5 dB high at full Depth, and a drained battery won't boot the DD-8.

## Next: the CE-5

A chorus is a short modulated delay, mixed with the dry signal; the DD-8's Mod mode
(Standard + a 1.51 Hz sine LFO of ±0.62 ms, a fixed depth) is the comparison. What to
measure, with the existing tools:

- the **LFO**: held tones (`hold-sine…`, one take per Rate position and per Depth
  position): `analyze_dd8_mode.wow_flutter` gives the rate, the delay swing in ms and the
  folded waveform (it subtracts the dry tone fitted before the first repeat arrives — with
  a chorus's few-ms delay that window is too short: fit the dry from the bypass take
  instead);
- the **base delay** (a few ms, not 20–800): too short for the click method; read it from
  a deconvolved sweep (`analyze_dd8.impulse_response`) at minimum Depth, or from the phase
  of a held tone;
- the **wet tone** (a bucket-brigade chorus is dark) and the **dry/wet balance**, from
  sweeps at minimum Depth;
- **level and saturation** (bursts and the `-12 dB` rule), **noise** (BBD hiss, the
  compander's breathing: a level step);
- the CE-5's knobs (Level, Rate, Depth, Filter): check the panel before writing the plan;
  positions 7:00 / 9:00 / 12:00 / 3:00 / 5:00. Mono first (stereo outputs later).

A new analysis script (`analyze_ce5.py`, or a generic chorus one) will be needed; the
plan can start from `plans/dd-8-mod.json`.

## Starting a session

The interface is in its normal state between sessions. Before capturing (commands in
[`tools/capture/README.md`](../tools/capture/README.md#the-reference-rig-focusrite-scarlett-8i6-3rd-gen-linux)):
NAM-X closed; card profile Pro Audio; analogue outputs 3/4 routed to PCM 3/4; input 2's
pad on (INST, gain at minimum); `capture.py init <session> --from <a DD-8 session>` (same
rig and calibration). Afterwards, all three back, and NAM-X's connections checked.
