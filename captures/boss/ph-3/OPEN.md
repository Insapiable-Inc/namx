# PH-3: open problems and what was tried

Hand-off notes for the next analysis session, written 2026-10-28 (updated the same night with
the 12 Stage, Res and Step findings). The study (`README.md`) and `tools/capture/analyze_ph3.py`
don't exist yet. Everything below comes from exploratory scripts that weren't kept. Sessions:

| Session | Contents |
|---|---|
| [`2026-10-28/`](2026-10-28/) | the Stage modes |
| [`2026-10-28-modes/`](2026-10-28-modes/) | Step, Rise, Fall |
| [`2026-10-28-slow/`](2026-10-28-slow/) | Rate 7:00 and 8:00 |
| [`2026-10-28-followup/`](2026-10-28-followup/) | fast Rates, frozen 8 and 10 Stage, Rise Rates |

Next captures: `tools/capture/plans/ph-3-followup2.json` (29 takes, 6.6 min) — see "What the next
session should capture" at the end.

## Methods that work

- **Sine-sweep takes:**
  - Get the impulse response with `analyze_dd8.impulse_response(session, name)`.
  - Take the FFT of the first 24000 samples (48 kHz).
  - Divide by the session's `bypass-sweep`.
- **Noise takes:**
  - Window the played and recorded signals (Hann; 256 samples for 4 Stage at Rate 3:00 and up, otherwise 1024–4096).
  - Compute H = Σ Y·X̄ / Σ |X|² per frame.
  - **Divide by the same session's `bypass-noise` H.** Without this the result is about 8 dB low: the interface gain differs from the sweep takes.
- **Folding a moving sweep:**
  - Assign each frame to a bin of LFO phase, and average Y·X̄ and |X|² per bin.
  - Find the period as the one whose fold explains the most variance of the frame dB.
  - Then prefer the shortest multiple that scores at least 90 % as well.
  - Let the search go down to 0.01 s. A 0.03 s floor folded Rate 5:00 (21.5 Hz) as two cycles, and it was misread as 10.8 Hz.
  - Fold the *slow* takes (Rate 8:00) when fitting a static model per bin: at Rate 12:00 with 16 bins the sweep moves too much within a bin and the notches smear (3–7 dB per-bin errors even with Res off).
- **Fitting:**
  - Fit the complex response, weighted by 1/√f, with several starts. Differential evolution helps.
  - Avoid magnitude-only fits of notched responses: they land in local minima.
  - When unwrapping phase, remove the 0.544 ms latency first, or the unwrap invents a phase-only filter.
  - For *structure* questions (which loop, which filter) a dB fit (clipped at −30 dB, 60 Hz–15 kHz) with the Res-7:00 sections held fixed is far more robust than a free complex fit; every free complex fit of the Res takes wandered into degenerate sections (Q 0.1).
  - Any fit that puts a feedback loop's gain above 1 somewhere is meaningless even if it scores well: the frequency-domain evaluation doesn't know the loop would be unstable.
- **Notch order:** the slope of dB against log|f − f0| on each side of a notch: 6 dB per halving of the distance = a simple zero, 12 = a double zero. (4 Stage: 5.9/5.9, 6.4/5.4; 8 Stage: 5.1–6.6; 12 Stage: 12.0/9.6 and 11.1/10.2.)
- **Sweep tracking (4 Stage):**
  - In each folded bin, fit one scale c on all section frequencies of the static model; the mix and tone stay fixed.
  - Medians come out around 1 dB.
  - Check the method on the model first: the model's own sweep at 21.5 Hz, tracked with 256-sample frames, comes back at its full 0.27–12×.
- **Step mode:** the hold time from the spikes of spectral change between frames (2048-sample frames, 64 hop, 250–7000 Hz, peaks by prominence, then the histogram of the gaps) works where the steps are big (Rate 12:00: quartiles 79–87 ms). At Rate 8:00 each step is tiny and the detector reads noise (it said 55–80 ms); read the plateaus off a notch track (local minima below −10 dB per frame, plotted against time) instead — they are about 0.35 s.

## Solved (to go into the study)

### Static filter and mix

- **The pedal:** digital, 0.544 ms latency, a wall just under 20 kHz, assumed to run at 44.1 kHz.
- **Static model (Res 7:00, Depth 7:00 stops the sweep):** out = delay · tone · (gd + gw·A).
  - tone = a one-pole high-pass near 56 Hz and a first-order high shelf of +3.5 dB at about 11–12.8 kHz.
  - gd ≈ gw ≈ 0.68–0.74, no feedback.
  - A = a product of RBJ second-order all-pass sections at 44.1 kHz.

| Mode | Sections (Depth 7:00 sweep) | Magnitude fit |
|---|---|---|
| 4 Stage | 210 Hz Q0.63, 654 Hz Q0.63 | 0.31 dB |
| 8 Stage | 259 Q0.62, 642 Q0.88, 1798 Q0.64, 3785 Q0.40 | 0.16 dB |
| 10 Stage | 541 Q0.75, 1178 Q0.22, 1265 Q0.71, 3952 Q0.42, 9497 Q0.54 | 0.19 dB |

These section sets aren't unique, and that matters (see 8/10 Stage below).

- **Level:** the mix does not change with Depth. Output rms re bypass over a noise take, 4 Stage at Rate 12:00: Depth 7:00 +3.1 dB, 9:00 +3.0, 12:00 +2.7, 3:00 +2.4, 5:00 +2.3 — the small fall is the notches sweeping through more of the band. By mode at Depth 5:00: 8 Stage +1.5, 10 Stage +0.1, 12 Stage −0.4 (more notches, less energy). The "mix 1.0 at Depth 5:00" of the earlier frozen fits was a fit artefact. Res raises the level (4 Stage Res 5:00 +4.6 dB, Rise Res 5:00 +9.0 dB).

### 12 Stage: a 6 Stage phaser run twice

- **Structure:** out = delay · tone · (gd + gw·A3) · (gd′ + gw′·A3′), the same three sections in both passes. One chain of six sections can't do it: its notches would be simple zeros, and the 12 Stage notches at 482 and 1730 Hz are **double zeros** (12 dB per halving of the distance, against 6 for every 4/8 Stage notch). Only a product of two (dry + wet) phasers with coincident notches gives that. A retailer listing found by web search says the 12-stage mode "combines two six stage phasers"; the measurement agrees. The top notch is a split pair (5930 and 6248 Hz, −43 dB between), so the two passes aren't quite identical at the top; a per-section detune of the second pass didn't improve the fit (it ran to its bounds), so what splits it is open.
- **Fit (Depth 7:00 sweep):** sections 1153 Hz Q0.96, 1437 Q0.27, 2999 Q0.78; mix 0.68/0.62 per pass (0.60/0.46 and 0.77/0.83 with the passes' mixes free — same error); 1.10 dB rms, 0.085 complex. Six free sections in one chain: 8.2 dB; two free 3-section chains: 3.7 dB (the double zeros force the shared sections). The fitted tone came out different from the other modes' (high-pass 76 Hz, a −1.4 dB shelf from 500 Hz, at its bound) — the mixes and tone trade off; take the tone from the other modes and refit.
- **Sweep:** folding `s12-rate-0800` (16 bins) and fitting one common scale c on the shared sections per bin: c runs 0.32–5.1 (×16; 4 Stage sweeps ×46, 8 and 10 Stage ×11 at the same Depth 5:00) in a triangle-like law; 0.13 complex error where c > 1, 0.3–0.4 at the bottom of the sweep. `s12-depth-0500` (Rate 12:00, 0.654 Hz) gives the same c track. Why the bottom fits worse is open: the next session's frozen 12 Stage sweeps are for that.

### Res: a feedback loop around the whole engine

- **10 Stage (clean, 0.28 dB):** out = s · tone · (gd + gw·A) / (1 − k · z⁻¹ · HP · A), with the dry taken *inside* the loop (so the notches stay exactly where they are: every 8 and 10 Stage notch is within 0.1 % and as deep with Res 5:00 as without), one sample of delay in the loop, a first-order high-pass near 50 Hz in the loop (a DC blocker), k = 0.68 at Res 5:00, and the mix scaled by s = 0.88. The sections don't change (a common frequency scale and Q factor fitted to 0.999 and 1.00). Without the one-sample delay: 2.5 dB; without the high-pass: 0.82 dB.
- **8 Stage (0.77 dB):** the same loop, but it also passes through a **fifth all-pass section parked near 8.4–9.3 kHz (Q ≈ 0.8)** — i.e. the engine is the 10 Stage chain, the 8 Stage wet is tapped after four sections and the feedback comes from the end of all five (the parked one is close to the 10 Stage's fifth, 9497 Hz Q0.54). k = 0.70, s = 0.75, high-pass ≈ 200 Hz, no delay. Without the parked section the best fit is 2.5 dB. A −12 dB high shelf at 12 kHz in the loop takes it to 0.46 dB — maybe real (the loop may be band-limited), maybe fitting the top octave's noise.
- **Reading the loop directly:** the ratio res/off plotted against the chain's phase φ (from the static fit) is a clean periodic function for 8 and 10 Stage: +9 dB at even multiples of π, −5.5 dB at odd ones, phase a ±45° sawtooth — exactly s/(1 − k·e^{jφ}) with k ≈ 0.68, s ≈ 0.89. That is the quickest check of any Res take.
- **4 Stage: not solved.** The 400 Hz peak behaves like the loop (Res 9:00 ≈ +4 dB, 12:00 +7, 3:00 +12, 5:00 +14 re bypass; k ≈ 0.75–0.78 from the peak with s ≈ 0.88), the first notch stays (154 → 156 Hz, as deep), but:
  - the second notch moves and fills in (895 Hz −42 dB → 848 Hz −38 at Res 9:00, 876 −26 at 12:00, 884–886 −24 at 3:00/5:00): its zero leaves the unit circle, so the numerator changes, which the loop-with-dry-inside can't do;
  - above it the response **sags by up to 10 dB at 1–3 kHz** (Res 5:00 vs 7:00), far below the −6 dB a loop with k < 1 can produce; then +3 dB above 5 kHz.
  - Tried and rejected: a plain loop (2.4–3.2 dB), a loop delay of 1–12 samples (worse), a one-pole low-pass or high shelf in the loop (no better), the unused engine sections in the loop at their 8 or 10 Stage positions (k runs to 0), the unused sections as pure delays (no), a common scale/Q change of the sections (no), a low-pass/biquad in the wet path and/or the loop (1.15–1.5 dB, but every one of those fits needs a loop gain above 1 somewhere — unstable, so meaningless). Free fits keep asking for a sharp all-pass (Q 4–6) at 910–990 Hz in the loop — that only reshapes the band around the moved notch, it is not an explanation.
  - Next: Res off/on pairs at the *same frozen sweep point* (in the next plan) show whether the sag and the moved notch follow the sections or stay put.
- **Res 5:00 in 12 Stage** moves and fills in the notches too (482 → 430 Hz −29 dB, 1730 → 1834 −30, the 6 kHz pair → 8008 −58; peaks +14 dB). Two passes with the loop around each, or one loop around both, is untested; the frozen pair in the next plan is for that.
- **Rise with Res 5:00** is +9 dB louder than without: the resonance rides the barber pole. Not modelled.

### 4 Stage sweep

- **Shape:** c = a + b·tri², a scale on the static section frequencies. A triangle beats a sine (0.034 vs 0.14 log rms).
- **Depth:**

| Depth | a | b |
|---|---|---|
| 9:00 | 0.912 | 0.947 |
| 12:00 | 0.609 | 4.257 |
| 3:00 | 0.316 | 9.009 |
| 5:00 | 0.244 | 11.043 |

- **Rate** (LFO speed; Rate 7:00 stops the LFO wherever it is):

| Rate | 8:00 | 9:00 | 12:00 | 3:00 | ~4:00 | 5:00 |
|---|---|---|---|---|---|---|
| LFO | 0.047 Hz | 0.225 Hz | 0.654 Hz | 4.64 Hz | 12.1 Hz | 21.5 Hz |

- **Fast Rates shrink the swing** (Depth 5:00), and the Rate 5:00 result repeated in the follow-up session:

| Rate | a | b |
|---|---|---|
| ~4:00 | 0.725 | 9.65 |
| 5:00 | 1.42 | 7.34 |

  A one-pole smoothing of the triangle does not explain the shrink (best 0.14 log rms): the swing shrinks *and* its centre rises (geometric centre 1.7 at 3:00, 2.7 at 4:00, 3.5 at 5:00), which a smoothed control can't do. Use a measured table (a, b per Rate, interpolated in log Rate). At 5:00 the top is also flattened and the rise lopsided; that is not modelled.

### 8 and 10 Stage sweep

- One common scale c on a base section set works only when the base sections are fitted **jointly** to the frozen sweeps and the folded Rate 8:00 cycle (the Depth-7:00-only fit is non-unique). Joint bases: 8 Stage 600 Q0.66, 1025 Q0.42, 1198 Q0.36, 2697 Q0.13; 10 Stage 449 Q0.67, 936 Q1.10, 3146 Q0.89, 3775 Q0.08, 10081 Q1.11 (mix ≈ 0.86/0.75, latency 0.563 ms). c runs 0.46–5.2 (8 Stage) and 0.50–5.3 (10 Stage) at Depth 5:00; complex error ≈ 0.10, worse than 4 Stage. Letting Q rise as c^0.2 helps a little.

### Rise and Fall speed

- **Each LFO period, the pattern slides up (Rise) or down (Fall) by one notch spacing.** It uses the same Rate scale as the Stage modes:

| Rate | 8:00 | 9:00 | 12:00 | 3:00 | 5:00 |
|---|---|---|---|---|---|
| A new notch every | 21.1 s | 4.56 s | 1.53 s | 0.20 s | 0.047 s |

- **The notches are 1.88 octaves apart** (1.7–1.8 at Res 5:00).
- **Depth does nothing:** 7:00, 12:00 and 5:00 all give 1.23 octaves/s at Rate 12:00.
- **Fall is Rise reversed,** at the same speed.
- **Measuring the speed:** use the phase of the pattern's repeat along log frequency, unwrapped over time. Cross-correlating frames is too coarse at Rate 8:00.

### Step

- **The 8 Stage sweep sampled and held:** four notches, the triangle law, the LFO value frozen between steps (the jumps are uniform in time and vary in size — big mid-ramp, small at the turns — so it is a sample-and-hold of the LFO, not a quantised LFO value).
- **Hold time:** 85 ms at Rate 12:00 (quartiles 79–87 ms; Depth 12:00 and 5:00 the same), about 0.35 s at Rate 8:00 (read off the notch track; 9 plateaus in 3 s). That is 18 steps per LFO cycle at 12:00 and about 60 at 8:00: neither a fixed time nor a fixed count per cycle — the hold time has its own Rate law, which needs the intermediate Rates (next plan).
- **Rate 5:00 (LFO 21.5 Hz):** the steps can't be resolved; the pattern looks random, which is what sampling a 47 ms triangle at a comparable hold time would do (and what reviews call Step's "random" character).
- Whether Step still steps at Depth 7:00 is untested (the only Depth 7:00 take has Res 5:00, whose resonance confuses the change detector).

## Still open, in priority order

1. **4 Stage Res** (above): the sag and the moved notch. The frozen pairs decide between "the loop passes through parked sections at fixed frequencies" (features stay put while the notches move) and "the sections themselves change with Res" (everything moves).
2. **Res knob law:** k(Res) is only measured in 4 Stage, where the model is wrong. The 8 Stage ladder (9:00, 12:00, 3:00) in the next plan gives it in a clean mode; the peak height s/(1 − k) reads k straight off each take.
3. **Step's hold time against Rate** (next plan: 9:00, 10:30, 1:30, 3:00, 4:00).
4. **12 Stage at the bottom of its sweep** (frozen 12 Stage sweeps in the next plan) and the split top notch.
5. **Res in 12 Stage and in Rise** (one frozen 12 Stage pair in the next plan; Rise needs an idea first — the pattern never stops).
6. **Fast-Rate swing** stays a table.
7. **Res and distortion:** reviews say more Res = more distortion; `s4-bursts-res-0500` (1 kHz bursts with Res 5:00) is there to check the clip against `s4-bursts`, not analysed yet.

## What the next session should capture

`tools/capture/plans/ph-3-followup2.json` — 29 takes, 6.6 min, modes in switch order (Step, 12, 8, 4), Res 7:00 unless stated:

- **Step at Rate 9:00, 10:30, 12:00, 1:30, 3:00, 4:00** (Depth 5:00, noise) and **Step at Depth 7:00** (Rate 12:00): the hold-time law and whether Depth 7:00 freezes it.
- **12 Stage frozen** (Rate 7:00 after a nudge, Depth 5:00) at four sweep points, sine sweeps; at the fourth point a **Res 5:00 take without touching Rate**.
- **8 Stage Res 9:00, 12:00, 3:00** at Depth 7:00 (sweeps): the k(Res) law.
- **8 Stage and 4 Stage frozen pairs** (three points each): Res 7:00, then Res 5:00 at the same point.

No web source describes the PH-3's DSP (searched 2026-10-28: only retailer copy, reviews, repair threads naming a fried IC; no service notes or schematic online). The one useful claim is the "two six stage phasers" for 12 Stage.
