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
| **CE-5** Chorus Ensemble | modulation | **Measured** 2026-10-27, wet-only (a plug in Output B) and mono sessions: a digital chorus — dry 0.98 ms late at +2.3 dB, wet 5.4 ms after it with a 60 Hz low cut and a 10 kHz linear-phase wall; bowed-triangle LFO 0.25–5.9 Hz, Depth raising mainly the top (5.7–8.1 ms); Low = resonant high-pass shelf, High = first-order low-pass; two hard clips (the input at ~+2 dB over the guitar's peak, dry and wet; the wet's at 0 dB with E.Level up). **Done** (with a short extra session for four open questions) | [`boss/ce-5/README.md`](boss/ce-5/README.md) |
| **BF-3** Flanger | modulation | **Standard measured** 2026-10-27 (mono): digital (0.55 ms dry at +1.8 dB; wet +1.6 dB over it, low cut ~190 Hz, roll-off from ~4 kHz, the same filter round the feedback loop); Manual 14.7–0.45 ms; Res −16.7 to −1.4 dB per pass, never self-oscillates; sweep = floor + swing · triangle^2.5 through a 1.53 Hz smoothing, Depth blending Manual toward 0.33 ms; Rate 0.055–10 Hz; one hard clip on the output at +0.55 dB re the guitar's peak. Checked against a second session; a plug in Output B changes nothing. Ultra measured: three taps at d/2, d, 3d/2 (its Res loop not pinned down yet). Gate/Pan, Momentary and stereo left out | [`boss/bf-3/README.md`](boss/bf-3/README.md) |
| **CS-3** Compression Sustainer | dynamics | **Measured** 2026-10-27: analog (a VCA, a log-domain peak detector); a fixed output ceiling (~66:1) with Sustain as the gain before it (−15 to +17.6 dB); Attack mostly sets the release (458 → 44 ms to 63 %) and the clamp-down a little (14 → 6 ms); Tone a tilt around 1 kHz; Level an audio taper (7:00 mutes) | [`boss/cs-3/README.md`](boss/cs-3/README.md) |
| **NS-2** Noise Suppressor | dynamics | **Measured** 2026-10-27 (Reduction mode, Input → Output; two sessions): not a hard gate — it opens at once (≤ 1 ms) once the input passes the threshold (−74 to −32 dBFS for a −6 dBFS guitar peak; little change below 12:00) and closes by turning down, ~8 dB at first, then deeper (slope ~0.9) as a slow level sinks under the threshold; Decay sets how long a dying note holds and fades (25–460 ms fall of the fast level); open +0.6 dB, −2.7 dB at 20 kHz; adds no noise; Mute mutes. Grey-box model within a median 1.5 dB rms over 39 takes; open: a ~1 s creep near the threshold the model misses — next, model the control circuit from the schematic (not found free yet) | [`boss/ns-2/README.md`](boss/ns-2/README.md) |
| **GE-7** Equalizer | linear | **Measured** 2026-10-28 (four sessions: the first's Level takes are the 6.4 kHz slider; a retake for two takes that missed the fit; every printed mark on four sliders): one circuit fits all 60 sweeps to 0.23 dB rms — the classic one-op-amp graphic EQ, six LC bands (91, 196, 370, 728, 1528, 3275 Hz, Q ~3) and a 6.4 kHz shelf (an RC, corner 3.8 kHz), interacting through the shared op-amp; the printed marks aren't the dB they say (800 Hz: +5 → +5.9, −5 → −4.0, ±10 → +10.7/−8.5), one law for every band slider; Level flat ±15 dB (−3 dB at 13 kHz at +15); flat = bypass within 0.3 dB, no added noise; one ceiling at +1.8 dB re the guitar's peak whatever the sliders and Level (a rounded clip) | [`boss/ge-7/README.md`](boss/ge-7/README.md) |
| SD-1, DS-1, JB-2, HM-2W | drives | **Skipped** (user, 2026-10-27): NAM-X's Tube Screamer covers them | |
| AW-3 Dynamic Wah | dynamics | bursts, steps | |
| **PH-3** Phase Shifter | modulation | **Measured** 2026-10-28 (five sessions, mono): digital (0.54 ms, 44.1 kHz); every mode = dry + wet through second-order all-pass sections (4/8/10 Stage: 2/4/5 sections, one scale swept by a triangle² law; **12 Stage = a 6 Stage phaser run twice**, its notches double zeros, the second pass on a delayed LFO; Rise/Fall a barber pole, one notch spacing per LFO period, notches 1.88 octaves apart; Step the 8 Stage sweep sampled and held, 350 → 71 ms from Rate 8:00 to 1:30); Rate 0.047–21.5 Hz, Depth = the swing, the mix fixed; **Res = a feedback loop around the whole engine** (dry inside, one sample and a DC blocker in the loop, k 0.25–0.70 from 9:00 to 5:00; 8 Stage's loop passes a parked fifth section at 8.3 kHz). Open: 4 Stage's Res (its top notch fills and a sag follows it — no simple structure found; use the 8/10 Stage loop), Step's hold above Rate 3:00, the 12 Stage pass lag. Low priority for NAM-X: its Phaser (Phase 90) is approved by ear | [`boss/ph-3/README.md`](boss/ph-3/README.md) |
| RV-6 Reverb | reverb | a project of its own | |
| OC-5 Octave | pitch | reference recordings only | |
| AC-3 Acoustic Simulator | filters | per-mode responses; no NAM-X counterpart | |
| TU-3 Tuner | | nothing to capture | |

Every pedal on a 9 V supply, not a battery: the TR-2's tired battery (8.37 V) held the top
of its cycle ~0.5 dB high at full Depth, and a drained battery won't boot the DD-8.

## Next

**The missing items (plans written 2026-10-29), one capture day with the AW-3:**

1. **A guitar DI**, recorded once: `capture.py di` (what to play: `tools/capture/README.md`,
   *Reamp takes*) → `captures/di/phrases.wav`.
2. **CS-3 follow-up** (`plans/cs-3-followup.json`, 4.8 min): pairs of re-picked notes 50 /
   100 / 200 / 500 ms apart at Sustain 9:00 / 12:00 / 3:00 × Attack 7:00 / 5:00 — the
   second note's attack and the gain it has recovered, which the staircase and the
   isolated plucks can't show; then the reamp.
3. **BF-3 follow-up** (`plans/bf-3-followup.json`): Rate 5:00 with clicks every 21.3 ms
   (the study's 25 ms clicks saw its ~10 Hz sweep at four phases only), and the reamp.
4. **Reamps** for every pedal with a NAM-X type: `tr-2-reamp`, `ge-7-reamp`,
   `dd-8-reamp` (one take per voicing), `ce-5-reamp`, `ns-2-reamp` — ~25 min of audio
   in all. One session per pedal (`<date>-reamp`, `init --from` an earlier session).

Then: each NAM-X type run over the same played files at the same settings, against the
pedal (by ear, and over the existing takes' played signals for the numbers).

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
