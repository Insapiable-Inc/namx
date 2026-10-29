# PH-3: open problems and what was tried

Hand-off notes for the next analysis session, written 2026-10-28 and updated the same night
after the follow-up 2 session. The study (`README.md`) and `tools/capture/analyze_ph3.py`
don't exist yet. Everything below comes from exploratory scripts that weren't kept. Sessions:

| Session | Contents |
|---|---|
| [`2026-10-28/`](2026-10-28/) | the Stage modes |
| [`2026-10-28-modes/`](2026-10-28-modes/) | Step, Rise, Fall |
| [`2026-10-28-slow/`](2026-10-28-slow/) | Rate 7:00 and 8:00 |
| [`2026-10-28-followup/`](2026-10-28-followup/) | fast Rates, frozen 8 and 10 Stage, Rise Rates |
| [`2026-10-28-followup2/`](2026-10-28-followup2/) | Step at six Rates, 12 Stage frozen, the 8 Stage Res ladder, Res off/on pairs at frozen points (`plans/ph-3-followup2.json`) |

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
- **Frozen points (Rate 7:00 after a nudge) are the way to study anything static**: a Res 7:00 take and a Res 5:00 take at the same frozen point give the Res factor directly as their ratio, and the sections from the Res 7:00 fit. Frozen fits need the tone free per take (the tone fitted in session 1 doesn't carry: 1–3 dB with it fixed, 0.7–1.3 dB free).
- **Fitting:**
  - Fit the complex response, weighted by 1/√f, with several starts. Differential evolution helps.
  - Avoid magnitude-only fits of notched responses: they land in local minima.
  - When unwrapping phase, remove the 0.544 ms latency first, or the unwrap invents a phase-only filter.
  - For *structure* questions (which loop, which filter) a dB fit (clipped at −30 dB, 60 Hz–15 kHz) with the Res-7:00 sections held fixed is far more robust than a free complex fit; every free complex fit of the Res takes wandered into degenerate sections (Q 0.1).
  - Check a loop's stability on its closed-loop poles (multiply the polynomials, `np.roots`), not on the open-loop gain: a loop gain above 1 is fine where the loop phase is near π (it is what makes a deep sag), only the crossings of 0 phase must be below 1. A fit with a pole outside the unit circle is meaningless.
- **Notch order:** the slope of dB against log|f − f0| on each side of a notch: 6 dB per halving of the distance = a simple zero, 12 = a double zero. (4 Stage: 5.9/5.9, 6.4/5.4; 8 Stage: 5.1–6.6; 12 Stage: 12.0/9.6 and 11.1/10.2.)
- **Sweep tracking (4 Stage):**
  - In each folded bin, fit one scale c on all section frequencies of the static model; the mix and tone stay fixed.
  - Medians come out around 1 dB.
  - Check the method on the model first: the model's own sweep at 21.5 Hz, tracked with 256-sample frames, comes back at its full 0.27–12×.
- **Step mode:** the hold time is the spectral line of the frame-to-frame change signal (1024-sample frames, hop 32, mean |ΔdB| over 250–7000 Hz, FFT of that): a clean line at 1/hold where the LFO's own harmonics don't land on it (9:00 to 1:30). At Rate 3:00 and up the LFO's harmonics (8×, 5×) sit where the hold line would be and the method can't tell them apart. Peak-picking the change signal (2048/64 frames, prominence) agrees from 10:30 to 1:30 and reads noise at 8:00 and 9:00; the notch track (local minima per frame, plotted) is the fallback at the slow end (8:00 ≈ 0.35 s, 9 plateaus in 3 s).
- **Session level:** the follow-up 2 session's `bypass-sweep` sits 3 dB below session 1's while the pedal-on takes don't, so the mix fitted to its takes comes out ≈ 1.0 instead of 0.7. Compare levels only within a session (the frozen pairs); the ratio to bypass is fine for shapes.

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

- **Structure:** out = delay · tone · (gd + gw·A3) · (gd′ + gw′·A3′), the same three sections in both passes. One chain of six sections can't do it: its notches would be simple zeros, and the 12 Stage notches at 482 and 1730 Hz are **double zeros** (12 dB per halving of the distance, against 6 for every 4/8 Stage notch). Only a product of two (dry + wet) phasers with coincident notches gives that. A retailer listing found by web search says the 12-stage mode "combines two six stage phasers"; the measurement agrees.
- **Fit (Depth 7:00 sweep):** sections 1153 Hz Q0.96, 1437 Q0.27, 2999 Q0.78; mix 0.68/0.62 per pass; 1.10 dB rms, 0.085 complex. Six free sections in one chain: 8.2 dB; two free 3-section chains: 3.7 dB (the double zeros force the shared sections). The fitted tone came out different from the other modes' (high-pass 76 Hz, a −1.4 dB shelf from 500 Hz, at its bound) — the mixes and tone trade off; take the tone from the other modes and refit.
- **The two passes aren't quite at the same place.** At Depth 7:00 the top notch is a split pair (5930 and 6248 Hz). At the four frozen points of follow-up 2: point 2 and 4 (high in the sweep: notches 2096/7124/15886 and 1164/4362/11966) are clean double zeros (slopes 12–16 dB per halving) and fit as identical twins (point 4: 2864 Q0.83, 3855 Q0.28, 6786 Q0.84, 1.00 dB); points 1 and 3 (low: 128/484/1764 and 136/516/1876) have blurred notches (slopes 8–10) and only fit as twins with the second pass **detuned 10–18 % on the two lower sections** (point 1: 184/489/1095 Hz, pass 2 ×1.18/×1.10/×1.00, 0.78 dB; point 3: 231/568/1160, ×0.84/×0.91/×1.00, 0.81 dB) — in opposite directions at the two points. That is what a second pass running on a *delayed copy of the LFO* does: frozen on a falling ramp the second pass sits higher, on a rising ramp lower. The lag isn't measured (the Rate before the freeze isn't known). Two free 3-section chains without the twin constraint: 0.61 dB at point 1 but a meaningless section set.
- **Sweep:** folding `s12-rate-0800` (16 bins) and fitting one common scale c on the shared sections per bin: c runs 0.32–5.1 (×16; 4 Stage sweeps ×46, 8 and 10 Stage ×11 at the same Depth 5:00) in a triangle-like law; 0.13 complex error where c > 1, 0.3–0.4 at the bottom of the sweep — the bottom is where the passes drift apart (above), so a twin model with a fixed LFO lag between the passes should fix it.

### Res: a feedback loop around the whole engine

- **10 Stage (clean, 0.28 dB):** out = s · tone · (gd + gw·A) / (1 − k · z⁻¹ · HP · A), with the dry taken *inside* the loop (so the notches stay exactly where they are: every 8 and 10 Stage notch is within 0.1 % and as deep with Res 5:00 as without), one sample of delay in the loop, a first-order high-pass near 50 Hz in the loop (a DC blocker), k = 0.68 at Res 5:00, and the mix scaled by s = 0.88. The sections don't change (a common frequency scale and Q factor fitted to 0.999 and 1.00). Without the one-sample delay: 2.5 dB; without the high-pass: 0.82 dB.
- **8 Stage:** the same loop, but it also passes through a **fifth all-pass section parked near 8.3 kHz (Q ≈ 0.6–0.9)** — the engine is the 10 Stage chain, the 8 Stage wet is tapped after four sections and the feedback comes from the end of all five (the parked one is close to the 10 Stage's fifth, 9497 Hz Q0.54). Depth 7:00: k = 0.70, s = 0.75, high-pass ≈ 200 Hz, no delay, 0.77 dB (2.5 dB without the parked section). **Confirmed across the sweep** by the three frozen pairs: one shared loop (k 0.71, s 0.76, HP 242 Hz, parked section at 8342 Hz Q0.60) fits the three Res 5:00 takes to 1.0–1.9 dB, the same as their Res 7:00 fits — and the parked section is at a fixed frequency, not scaled with the sweep (2.5 dB scaled).
- **The Res knob (8 Stage ladder, Depth 7:00, same loop):** k = 0.25 at Res 9:00, 0.44 at 12:00, 0.63 at 3:00, 0.70 at 5:00 (fits 0.58–0.64 dB; the parked section lands at 8.1–8.4 kHz every time). Read off the peaks alone (res/off at even multiples of the chain phase): +4.5, +6.5, +9.5 dB at 9:00, 12:00, 3:00. The mix scale s from the ladder (1.26, 1.16, 1.07 relative to session 1's model) is contaminated by the session level offset; within follow-up 2 the pairs give s ≈ 0.75 at 5:00. A law like k = 0.7·(Res fraction)^0.6 fits the four points to ±0.02 (fractions 0.2, 0.5, 0.8, 1.0).
- **Reading the loop directly:** the ratio res/off plotted against the chain's phase φ (from the static fit) is a clean periodic function for 8 and 10 Stage: +9 dB at even multiples of π, −5.5 dB at odd ones, phase a ±45° sawtooth — exactly s/(1 − k·e^{jφ}) with k ≈ 0.68, s ≈ 0.89. That is the quickest check of any Res take.
- **12 Stage Res (frozen point 4, 1.5 dB):** the notches move up 8–11 % and fill to −25…−31 dB (1164 → 1256, 4362 → 4838, 11966 → 13000 Hz), so here the dry is **outside** the loop. Of seven arrangements tried on the twin structure, only "each pass a phaser with the loop around its own three sections, dry outside, one sample of delay" works (k 0.71, s 0.58, HP 103 Hz; the others 3.7–8 dB; the same with the dry inside 4.8 dB). Why the dry sits outside here and inside in 8/10 Stage is unexplained (the two are the same transfer function if the wet gain rises as 1 + k with Res; maybe the twice-6 block simply lacks that).
- **4 Stage: not solved, now well characterised.** The frozen pairs (notches 1916/9458, 132/770, 188/1086 Hz, plus 154/895 at Depth 7:00) all show the same thing, so it is tied to the sections, not to a fixed filter:
  - the loop is there: peaks at the even multiples of the chain phase (+9 to +12 dB: 244 Hz at point 1, 338 at 2, 464 at 3, 400 at Depth 7:00; k ≈ 0.72–0.78), the first notch gets *deeper* (−38 → −52 dB, as 1/(1 + k) predicts) and stays put (≤ 1 %);
  - the **top notch loses its phase flip and fills** to −19…−26 dB at a frequency 1–2 % lower (zoom: a wide shallow dip, the phase makes a bump instead of jumping 180°) — its zero has left the unit circle, which no loop with the dry inside can do, and the first notch rules out the dry being outside;
  - above the top notch the response **sags by up to 10 dB** at about 1.6–1.75× the top notch (1346 Hz for 770, ~1.4 kHz for 895, ~1.9 kHz for 1086), then rises to +3–4 dB at 10–15 kHz.
  - Tried on the four points jointly (shared k, s, high-pass; sections from each point's own fit; stability checked on the poles) and rejected: the plain loop (3.4 dB mean), a one-sample loop delay (4.4), 1–2 extra all-pass sections in the loop at fixed frequencies (3.3) or scaled with the sweep (3.3), a free biquad in the loop fixed or scaled (2.7–3.0, and the best of those had a pole outside the unit circle), the biquad in the wet path too (2.3–2.9, unstable), the unused sections as pure delays (worse), the sections' Q or frequency changing with Res (3.3), and **a pole/zero mismatch of the top section** (its all-pass zeros no longer matching its poles, with or without the loop: 2.6–2.7 dB; of the bottom section or both: 2.8–5.1). Free fits keep asking for a sharp all-pass (Q 4–6) at 1.1–1.2× the top notch — not a model.
  - For NAM-X: use the 8/10 Stage loop with k(Res) for 4 Stage too and accept the difference (the peaks, the first notch and the level are right; the pedal's filled top notch and the sag are not reproduced). The next idea worth trying is a feedback path that enters *between* the two sections with the dry taken from a different node than the wet — enumerate the two-section topologies systematically (12 combinations of injection node, dry node, wet node) against the four points with the pole check; nothing else cheap is left.
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

- **Rate** (LFO speed; Rate 7:00 stops the LFO wherever it is; the 10:30 and 1:30 values are from the Step takes, which sweep the same way):

| Rate | 8:00 | 9:00 | 10:30 | 12:00 | 1:30 | 3:00 | ~4:00 | 5:00 |
|---|---|---|---|---|---|---|---|---|
| LFO | 0.047 Hz | 0.225 Hz | 0.394 Hz | 0.654 Hz | 1.77 Hz | 4.64 Hz | 12.1 Hz | 21.5 Hz |

  (Follow-up 2 read 0.625 Hz at 12:00 and 4.31 at 3:00 — knob positions repeat to about 5 %; "4:00" came back at 13.5 Hz.)

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

- **The 8 Stage sweep sampled and held:** four notches, the triangle law, the LFO value frozen between steps (the jumps are uniform in time and vary in size — big mid-ramp, small at the turns — so it is a sample-and-hold of the LFO, not a quantised LFO value; the plain 4 Stage sweep at Rate 8:00 shows no steps at all). **Depth 7:00 sits still** (no steps, the notches fixed).
- **Hold time against Rate** (Depth 5:00 and 12:00 the same):

| Rate | 8:00 | 9:00 | 10:30 | 12:00 | 1:30 | 3:00 | 4:00, 5:00 |
|---|---|---|---|---|---|---|---|
| Hold | ≈ 350 ms | 178 ms | 121 ms | 86 ms | 71 ms | 29 ms? | unresolved |
| Steps per LFO cycle | ≈ 60 | 25 | 21 | 18.6 | 8.0 | 8.0? | |

  Neither a fixed time nor a fixed count: at the slow end the hold goes roughly as 77 ms·√(period in s) (354, 162, 123, 97, 58 predicted), at 1:30 and 3:00 it is exactly the LFO period / 8 — the 3:00 reading is the LFO's 8th harmonic, which is the same thing if there really are 8 steps per cycle and an artefact if not. Interpolate the table in log Rate; the fast end needs a different measurement (a held sine where a notch passes, so the steps show as level jumps).
- **Rate 4:00 and 5:00:** the steps can't be resolved; the pattern looks random, which is what sampling a 47–74 ms triangle at a comparable hold time would do (and what reviews call Step's "random" character).

## Still open, in priority order

1. **4 Stage Res** (above): the filled top notch and the sag. All cheap structures are exhausted; next is the systematic two-section topology enumeration, or accept the 8/10 Stage loop for it.
2. **Step's hold at Rate 3:00 and up** (a sine-based take, see above), and whether 8 steps per cycle is a real lock at fast Rates.
3. **12 Stage's second pass:** the LFO lag between the passes (a frozen pair at a known Rate before the freeze, or two sweeping takes and the fold).
4. **Res in Rise** (needs an idea first — the pattern never stops) and **Res and distortion** (`s4-bursts-res-0500` against `s4-bursts`, not analysed).
5. **Fast-Rate swing** stays a table.

No web source describes the PH-3's DSP (searched 2026-10-28: only retailer copy, reviews, repair threads naming a fried IC; no service notes or schematic online). The one useful claim is the "two six stage phasers" for 12 Stage.
