# Boss AC-3 Acoustic Simulator — first look

Measured 2026-10-31, a quick first look (user: is it simple enough to model?), 9 V
adapter, mono: Input → Guitar Amp output, Line Out empty. Plan:
[`ac-3.json`](../../../tools/capture/plans/ac-3.json), 24 takes; the same rig and
calibration as the other pedals (guitar peak −13.5 dBFS at the recording input). Numbers
below come from exploratory scripts (not kept), not an analysis script.

**Verdict: modelable.** Fixed per-mode filters, two knob-weighted paths, an output
ceiling and a short reverb — no dynamics found so far. The plan's −24 dB sweeps and
white-noise staircases ran into the ceiling in the treble, so a modelling session needs
quieter signals (below).

## What it does

- **Unpowered it passes nothing**, even bypassed (the first three takes, discarded).
- **Below about −30 dB (re the guitar's peak) it is linear** (coherence 0.9–1.0 on the
  −48/−42 dB noise steps). Response re bypass, Body/Top/Level at 12:00, Reverb off:

  | Hz | 50 | 100 | 200 | 300 | 500 | 1k | 1.5k | 3k | 5k | 8k | 12k | 16k |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
  | Standard | −19 | −2 | +6 | +1 | +1 | 0 | −9 | −13 | −3 | +25 | +32 | +33 |
  | Jumbo | −21 | −2 | +3 | −6 | −5 | −5 | −4 | −7 | +3 | +28 | +36 | +36 |
  | Enhance | −20 | −5 | +4 | 0 | +1 | +4 | −3 | −12 | +2 | +25 | +32 | +33 |
  | Piezo | −5 | +3 | 0 | +2 | −6 | −5 | −3 | −9 | −4 | +18 | +25 | +22 |

  A body peak near 200 Hz, a deep bass cut below ~80 Hz (not in Piezo), a scooped
  1.5–3 kHz, and a very large treble lift (+25 to +36 dB at 8–16 kHz): it rebuilds the
  top a magnetic pickup doesn't have.
- **An output ceiling near the guitar's peak level** (±0.20 in the recording, the guitar
  peak being 0.21): white noise above −30 dB, its treble lifted 30 dB, saturates — crest
  factor 13 → 8 dB, the peaks creeping 0.137 → 0.208 over 27 dB — and the lows/mids lose
  coherence to the distortion. Every −24 dB sweep peaks at −14.1…−14.9 dBFS whatever the
  knobs, because the lifted treble reaches it. Below ~5 kHz the sweeps stay linear.
  Static clipper or fast limiter: not yet told apart (no jump take).
- **Body and Top look like the levels of two paths.** From the sweeps (valid below
  ~5 kHz): Body 5:00 = +6 dB from 50 Hz to 3 kHz and nothing at 5 kHz; Body 7:00 = −8 to
  −13 dB over the same band. Top 7:00 removes the top (−25 dB at 5 kHz); Top 5:00 adds
  +9 dB at 5 kHz and takes ~4 dB from 100–150 Hz.
- **Level:** 7:00 mutes (−60 dB); 5:00 = +6 dB over 12:00 where the sweep is unclipped.
- **Reverb: a short room.** After a sweep, the tail falls ~5–6 dB per 100 ms at 12:00
  (RT60 ≈ 0.7 s; the first 100 ms 30 dB over the dry-only tail) and ~4 dB per 100 ms at
  5:00 (RT60 ≈ 1.2 s, 6 dB louder). On plucked notes it adds only 1–2 dB of ambience.

## Next: the modelling session (to do, user 2026-10-31: "later")

### Capture (~30 takes; every signal quiet enough for the +36 dB treble)

Keep Reverb fully down except in its own takes, Level 12:00 unless stated, the Guitar Amp
output, and record every bypass again in the new session.

1. **Bypass:** a −48 dB sweep (tail 4 s), the −48/0/−48 jump (`noise-steps`, seed 2),
   plucks.
2. **Each mode at noon** (Standard, Jumbo, Enhance, Piezo): a −48 dB sweep. Standard only:
   a −54…−24 dB noise staircase (finer steps, 1.5 s) to find where the ceiling starts.
3. **Body and Top, Standard:** a −48 dB sweep at 7:00, 9:00, 3:00 (12:00 and 5:00 too
   if this session's numbers don't hold at −48 dB), Body first with Top at noon, then Top
   with Body at noon; then the four corners (Body 7/5 × Top 7/5) — the two paths' phase
   decides how they sum, so the corners test "two levels" against a single filter.
   Body/Top in one other mode (Jumbo) at 7:00 and 5:00, to see whether the paths change
   with the mode.
4. **The ceiling:** the −48/0/−48 jump and the plucks in Standard (does the gain move
   over time → limiter; or not → a static clipper), plus a 1 kHz and an 8 kHz tone
   staircase (`steps`) from −36 to +3 dB (the curve's shape, below and above the treble
   lift).
5. **Level:** −48 dB sweeps at 9:00 and 3:00 (the taper; 7:00 mutes, 5:00 = +6 dB).
6. **Reverb:** a click train or short noise bursts (`noise-steps` −120/−12/−120, 0.05 s
   bursts, long tails) at 9:00, 12:00, 3:00, 5:00; one −48 dB sweep at 5:00 with a 5 s
   tail. Check whether the reverb is before or after the ceiling (a loud burst at 5:00).
7. Optional: Standard's −48 dB sweep from the Line Out.

### Analysis

1. **Responses:** deconvolve each −48 dB sweep against its bypass, keeping negative times
   (`analyze_aw3.Session.response` does this); check coherence on the −48/−42 noise steps.
   Linear below about −30 dB: each mode's response is an IR to fit or to keep as a short
   minimum-phase FIR.
2. **Body/Top structure:** fit `H = H_mode·(b(Body)·B(f) + t(Top)·T(f))` jointly to the
   Body, Top and corner sweeps (complex, not magnitude: B and T may overlap in phase);
   compare with a cascade of shelves `H_mode·S_body(f)·S_top(f)`. Keep whichever fits the
   corners. Knob laws b(), t() from the five positions.
3. **Ceiling:** from the jump's gain envelope (time constants, if any) and the tone
   staircases (static curve, soft or hard, symmetric?), measured at the output against
   the linear model's prediction.
4. **Reverb:** subtract the dry model from the reverb takes; RT60 per octave band from the
   bursts (Schroeder integration), level and pre-delay against the knob, stereo-free (the
   pedal is mono here). Compare with NAM-X's Room: a voicing of it may be enough.
5. **Validate** on the plucks (rendered against the recording, as for the AW-3) and decide
   the NAM-X shape: a new Acoustic effect before the amp (it replaces the amp's role),
   Mode/Body/Top/Level/Reverb with the pedal's names.

This session's data stays useful below ~5 kHz (sweeps) and below −30 dB (noise).
