# DSP quality research

State-of-the-art options for the built-in effects where the current quality falls short,
compiled 2026-10-01 for the "DSP quality" milestone (ROADMAP Phase 22). The amp itself
(NAM) is out of scope: it is already state of the art.

**Real-time flags** used below (the audio thread has a few ms per block, see
`CPP_GUIDELINES.md` → Audio thread):

- ✅ **Real-time proven:** published or shipping as a real-time CPU effect.
- ⚠️ **Real-time with a cost:** runs live, but adds noticeable latency or CPU, or needs
  work to get there. The cost is stated.
- ⛔ **Not real-time today:** offline, too slow, or research-only. Listed so it isn't
  forgotten; not a candidate yet.

**Licenses:** the app is GPL-3.0-or-later, so MIT, BSD, LGPL and GPL-2.0-or-later code can
all be used. That reopens Rubber Band, which Phase 17 avoided while the app was MIT.

## Summary

| Effect | Today | Gap | First pick | RT |
|---|---|---|---|---|
| **Transpose** (pitch) | Filter-bank shifter (was two-tap WSOLA) | ~20 ms in the bass, ~4 ms above; faint beat on long held chords | Done 2026-10-03 (see §1); Signalsmith for Shimmer/Crystals | ✅ |
| **Reverb** | Plate (Dattorro), Hall and Room (FDN) — was Freeverb | Decay can't go very short on Plate (~0.6 s) | Done 2026-10-03 (see §2) | ✅ |
| **Cabinet / convolution** | Partitioned FFT convolution (was brute-force FIR) | Uniform partitions: fine for 1 s cab IRs; multi-second reverb IRs will want non-uniform ones | Done 2026-10-03 (see §3) | ✅ |
| **Drive** | Digital TS approximation | Approximate circuit, aliasing | A NAM "pedal" capture slot before the amp; WDF Tube Screamer | ✅ |
| **Chorus / Flanger** | Modulated delay lines | Clean and digital | Bucket-brigade (BBD) model | ✅ |
| **Phaser** | MXR Phase 90 circuit model (was a generic 4-stage all-pass + an approximate Vibe) | — | Done 2026-10-04 (see §5); a Uni-Vibe would be its own effect | ✅ |
| **Delay** | Clean digital line + saturation | No analog character | BBD and tape models | ✅ |
| **Wah** | Resonant SVF band-pass | Not a real wah's response | Crybaby circuit model (nodal DK) | ✅ |
| **Compressor** | Feed-forward peak | Basic but fine | Better detector/knee; optional neural opto | ✅ / ⚠️ |
| Gate, EQ, Tremolo, Tuner | — | Adequate | No change needed for this milestone | — |

Suggested order: **pitch shifter → partitioned convolution → reverb → drive slot →
modulation → wah/delay/compressor.** The first two are the biggest audible and CPU wins,
and the convolution engine unlocks IR reverbs.

## 1. Pitch shifting (Transpose) — top priority

**Today:** a two-tap delay-line shifter with Hann crossfades and WSOLA re-alignment
(`PitchShifter.h`). Measured (ROADMAP ideas list): pitch accurate to ±0.3 cents, but
**~30 ms latency** while shifting and a ±10–15 cent wobble at crossfades on upward shifts.
User verdict on distorted guitar: "not bad, not professional grade".

**What guitar needs:** *polyphonic* (chords), **low latency**, ideally under ~10 ms (feel
starts to suffer beyond that), clean *transients* (pick attack), and above all good
**downward** shifts (drop tunings). Time-domain methods are low-latency but mostly
monophonic; frequency-domain methods are polyphonic and higher quality but cost latency
([Juillerat et al.][jl-td], [Polak & Erkut][stn]).

| Candidate | What it is | Latency | Quality / notes | License | RT |
|---|---|---|---|---|---|
| **Filter-bank / ring-modulation shifter** ([Juillerat, Schubiger-Banz, Müller Arisona 2008][jl-td]) | IIR filter bank, oscillators and ring modulation: a completely different approach from overlap-add or STFT | **< 10 ms** (the paper's target for live use) | Built for live performance; artifact behavior on distorted chords needs listening tests | Paper (implement ourselves) | ✅ |
| **Low-latency STFT shifter** ([Juillerat & Hirsbrunner 2010][jl-fd]) | Phase-vocoder variant that tolerates small FFTs, with an alternative fix for phasiness | **~12 ms** | "Good quality" at that latency, where other STFT methods degrade badly | Paper | ✅ |
| **Signalsmith Stretch** ([repo][ss], [design notes][ss-design], [ADC22 talk][ss-talk]) | Polyphonic STFT pitch/time library; frequency and time mapping in one spectral step | Configurable; tens of ms with default block sizes | Handles multi-octave shifts well; modern, actively used | **MIT** | ⚠️ latency |
| **Rubber Band R3 / R2** ([library][rb], [why][rb-why]) | Reference-quality pitch/time library with a lock-free real-time mode | ~30–50 ms reported in real-time use ([JUCE forum][rb-lat]); one report of 819 samples (~17 ms at 48 kHz) | R3 is the best quality, at high CPU cost | **GPL-2.0-or-later** (now compatible) | ⚠️ latency, CPU |
| **Phase Vocoder Done Right / RTPGHI** ([Průša & Holighaus][pvdr]) | Phase reconstruction by phase-gradient integration: no peak picking, no transient detection | STFT-window bound (tens of ms) | Excellent for stretching; latency too high for live drop-tune | Paper / reference code | ⚠️ latency |
| **Sines + transients + noise split** ([Polak & Erkut, DAGA 2025][stn]) | Decomposes the signal and shifts each part with a specialized method; implemented as a real-time VST | Low (paper target) | Specifically about polyphonic *low-latency* shifting with clean transients; very recent | Paper | ⚠️ new research |
| **Neural pitch shifting** | — | — | No real-time polyphonic neural shifter found. ML is real-time for *pitch estimation* ([PESTO][pesto], < 5 ms, 130 k parameters), which could drive pitch-synchronous shifting (PSOLA) for single notes | — | ⛔ shifting / ✅ estimation |

**How commercial drop-tune pedals do it is unpublished.** The DigiTech Drop is polyphonic,
and reviewers describe its latency as "a tiny bit … felt but not really heard" at the
largest drops ([Sweetwater review][drop]). That is the bar.

**Outcome (2026-10-03):** Transpose now uses the filter-bank shifter
(`FilterBankShifter.h`): a complex 4th-order gammatone bank (2 bands per ERB, 0.7 ×
Hohmann's bandwidth), each band rotated at (ratio − 1) × the frequency of the spectral
peak it belongs to, Hohmann's delay/phase-aligned resynthesis. ~20 ms latency at the low
E string, ~4 ms above; ±0.1 cents; −68…−76 dB artifacts on sines in both directions. The
user preferred it to WSOLA by ear, especially after distortion. Tried and rejected by
ear: one-pole (zero-delay) bands (couldn't separate partials), 1 and 1.5 bands per ERB
(9 Hz tremolo on single notes), 3 narrow bands per ERB (40 ms bass, smeared fast notes),
smoothed control, phase reset at attacks, and leak correction (half the chord tremolo,
but cracks on fast notes and +45 % CPU). Known limit: a faint beat on long held chords
— some of each partial leaks into the neighbouring partial's bands and gets that
partial's shift, beating at (1 − ratio) × their spacing (4.5 Hz on an E5 at −2 st).
**Signalsmith Stretch** was measured too: at playable block sizes (20–60 ms) the bass
comes out at the wrong pitch (82 → 67 Hz at −2 st with 60 ms blocks; correct only at
~250 ms), so it's out for Transpose — but a good fit for Shimmer/Crystals, where latency
doesn't matter: since 2026-10-03 they run on it (`StretchShifter`, 120 ms block, 150 ms
latency; octave up within ~1 cent at 110/440 Hz, +16 cents on a low E; Crystals repeats
compensated to land on time), approved by ear ("sound amazing").

**Recommendation (before the outcome above):**
1. **Quick win:** wrap Signalsmith Stretch (MIT, header-only) behind the existing
   `PitchShifter` interface. A/B it against today's WSOLA on distorted chords. Measure
   latency at small block sizes with the same scratch harness used on 2026-10-01.
2. **Research track:** prototype the Juillerat filter-bank shifter as a **"Low latency"**
   mode for drop tuning (≤ 10 ms target), keeping the higher-quality STFT path as a
   **"High quality"** mode. Users pick feel or fidelity.
3. Re-enable **Shimmer** (Reverb) and **Crystals** (Delay) once the new shifter is in.
   Inside a reverb or delay tail, latency doesn't matter, so the high-quality path fits.

## 2. Reverb

**Done (2026-10-03):** Plate (Dattorro, the paper's lengths and taps), Hall (8-line FDN,
44–109 ms, Hadamard feedback, per-line Jot shelves, 0.6 ms modulation, sparse early
reflections) and Room (16 lines, 11–48 ms, 0.5 ms modulation — 8 short lines rang
metallic behind a long pre-delay: tail colouration 8.8 → 6.8 dB against ~5.6 dB for an
ideal diffuse tail; Plate 6.2, Hall 6.4). Level-matched, Decay in seconds (BENCHMARKS).
Chosen by ear; no IR reverb inside Reverb (user: a separate effect if ever).

**Before:** Freeverb (8 parallel combs + 4 series all-passes). It's cheap and well known,
but its tail is metallic and grainy, and its early echoes are sparse.

| Candidate | What it is | Notes | RT |
|---|---|---|---|
| **Dattorro plate** ([Dattorro 1997, JAES][dattorro], [overview][erbe]) | Griesinger-style "figure-8" tank with modulated all-passes | The classic studio plate. Every delay length is published; many open implementations (e.g. VCV [Plateau][plateau]) | ✅ cheap |
| **Feedback delay network (FDN)** ([Schlecht & Välimäki survey list][fdn-list], [velvet-noise FDN][vfdn]) | N delay lines mixed through an orthogonal matrix, with frequency-dependent decay | The modern algorithmic-reverb workhorse for halls and rooms. Velvet-noise input/output filters fix the slow echo build-up | ✅ cheap |
| **Convolution reverb (IRs)** ([Gardner 1995 / Wefers thesis][wefers], [Battenberg & Avižienis][battenberg]) | Real measured rooms, plates or springs, convolved with **non-uniformly partitioned** FFT convolution | Zero I/O latency is possible; CPU grows only logarithmically with IR length. Same idea as the cabinet IR, so it reuses the engine from section 3 | ✅ needs partitioned convolution |
| **Differentiable FDN** ([Dal Santo et al. — colorless DiffFDN][diff-fdn], [RIR synthesis][diff-rir]) | ML *training* fits FDN parameters to a measured IR or a perceptual target | Training is offline; at run time it's a plain FDN. Close to convolution quality at a fraction of the CPU | ✅ run time / ⛔ training |

**Recommendation:** replace Freeverb with a **Dattorro plate** (small, well documented)
plus an **FDN hall/room**, selected by a mode switch. Then add **IR reverb** once
partitioned convolution lands. Differentiable FDNs are a later step: fit a few FDN
presets offline to favorite IRs.

## 3. Cabinet and convolution engine

**Done (2026-10-03):** `IRLoader` runs the first 64 taps directly and the rest as uniformly
partitioned FFT convolution (overlap-save, 64-sample partitions, a frequency-domain delay
line; FFT from `signalsmith-linear`, MIT). Zero added latency; IRs up to 1 s (faded out
beyond); 3× cheaper than the old 4,096-tap brute force on a typical cab IR, and a whole
second costs what 85 ms did. For multi-second reverb IRs, add larger partitions further
out (non-uniform, Gardner) on the same engine.

**Before:** brute-force FIR convolution, with the IR truncated to 4,096 taps (no fade).
CPU cost grew linearly with IR length.

**Better:** **uniformly or non-uniformly partitioned FFT convolution**
([Wefers thesis][wefers], [Gardner's zero-latency non-uniform scheme][wefers],
[Battenberg][battenberg]). It's the standard for cab and reverb IRs, keeps zero added
latency (a short direct-form head, longer FFT partitions for the tail), and makes multi-
second stereo reverb IRs affordable. ✅

**Recommendation:** implement it once (or adopt a small permissively licensed engine after
checking its license) and use it for both the cabinet and the IR reverb. It's
prerequisite work, not user-visible on its own, but it removes the IR length cap and frees
CPU.

## 4. Drive

**Today:** a digital approximation of a Tube Screamer's topology (high-passed signal
through gain and soft clip, added back, tone low-pass). No oversampling, so high-drive
harmonics alias.

| Candidate | Notes | RT |
|---|---|---|
| **NAM pedal captures** ([TONE3000 boost/OD captures][t3k-od], [Pedal + Amp slots in TONE3000's plugin][t3k-plugin]) | Load a NAM capture of a real overdrive or boost **before** the amp NAM. It's the same engine we already run, uses a community library of real pedals, and is exactly how TONE3000's own player does it. Costs one more model's CPU | ✅ proven |
| **Wave digital filter (WDF) circuit model** ([chowdsp_wdf][wdf-lib], [Werner et al. diode clipper][wdf-diode]) | A real Tube Screamer circuit simulated component by component, with knobs mapped to the actual pots | ✅ |
| **Anti-aliasing** ([ADAA, Parker et al. DAFx-16][adaa], [practical notes][adaa-notes]) | Antiderivative anti-aliasing for the clipper, or 2–4× oversampling: needed by any nonlinear drive | ✅ |
| **Neural black-box (WaveNet-style)** ([Wright et al., SMC 2019][smc-dist]) | Generic: this is what NAM already is | ✅ |

**Recommendation:** add a **second NAM slot ("Pedal")** in front of the amp. It's the
highest-leverage option, using the existing engine and TONE3000. Keep the built-in Drive,
and later rebuild it as a WDF Tube Screamer with ADAA, for users without captures.

## 5. Modulation: chorus, flanger, phaser, vibe

**Today:** LFO-modulated digital delay lines (chorus, flanger); the phaser is an MXR
Phase 90 circuit model (2026-10-04, below). Until then it was a generic 4-stage digital
all-pass with a "Vibe" variant (staggered stages, lamp-shaped LFO) — both removed.

| Candidate | Effect | Notes | RT |
|---|---|---|---|
| **Bucket-brigade (BBD) model** ([Holters & Parker, DAFx-18][bbd], [chowdsp BBD][bbd-chow]) | Chorus, flanger, analog delay | Variable-rate BBD including its input/output anti-aliasing filters: the darkening and grit of a CE-2, Small Clone or Electric Mistress | ✅ |
| **Grey-box differentiable phaser** ([Carson et al., DAFx-23][phaser-gb]) | Phaser | Learns a real phaser's all-pass stages and LFO from recordings, but runs as a light DSP model | ✅ |
| **Neural LFO-conditioned models** ([Wright & Välimäki, DAFx-20][nn-lfo], [phaser & flanger, JAES][nn-pf]) | Phaser, flanger, chorus | A small recurrent network fed audio + LFO, so rate and shape stay controllable. Ran in real time on a desktop CPU with low load; ~1.3 % error-to-signal ratio on a flanger, "nearly indistinguishable" | ✅ needs a training rig |
| **Uni-Vibe circuit model** ([VA techniques comparison][va-cmp]) | Vibe | The real thing: 4 staggered stages, each with a photocell lit by one lamp. The lamp's thermal lag and the photocells' nonlinear response are what make it throb. A WDF or state-space model of those stages, driven by a lamp/photocell model | ✅ |

**Recommendation:** a **BBD core** shared by Chorus and Flanger (and an "analog" Delay
mode), and a **virtual-analog Vibe**: lamp thermal model + photocell curve + 4 staggered
stages. Neural LFO models are worth it only if we capture specific pedals.

**Phaser — done 2026-10-04 (chosen by ear):** the MXR Phase 90 after Giampiccolo et al.,
"Wave Digital Model of the MXR Phase 90 based on a Time-Varying Resistor Approximation of
JFET Elements" ([code][p90], MATLAB reference + LTspice schematic, MIT; their plugin
binary is CC BY-NC and unused). `Phase90Circuit.h` reduces their WDF to closed form and
matches the MATLAB to ~1e-6 V: a 33 Hz input buffer, four op-amp all-pass stages (47 nF;
24 kΩ ∥ a 2N5952 JFET channel whose resistance follows the LFO on the gate *and* Vds —
asymmetric grit at the sweep's low end), dry + wet through a 17 Hz output coupling. No
oversampling needed (aliasing 52–74 dB under the note at 48 kHz). Feedback (not in the
script circuit) is solved without a one-sample delay and AC-coupled at 20 Hz; the loop
rings on its own and runs away from ~98 %, so the knob stops at 90 % (clamping at
op-amp rails instead was tried and sounded harsh). Digital full scale = 1 V at the input.
Other open-source phasers looked at: [ChowPhaser][chowphaser] (BSD-3; loosely a Schulte
Compact Phasing 'A', more a creative effect — drive/dirt in the feedback, LFO skew) and
a grey-box PH-10 student plugin. A Boss PH-3 is available for by-ear reference.

## 6. Delay

**Today:** a clean digital line with saturation, Low/High Cut and modulation (modulation
and cross-feed currently disabled in the UI).

- **Analog (BBD) mode:** reuses the BBD model from section 5. ✅
- **Tape mode:** **ChowTape** ([DAFx-19 paper][chowtape], [code][chowtape-code]), a
  physical model of tape hysteresis plus wow/flutter that feeds a modulated delay line.
  Open source (GPL-3, compatible). ✅

## 7. Wah

**Today:** a resonant SVF band-pass swept by an envelope or by Position. It reacts like a
wah but doesn't have a real pedal's curve: the Crybaby's inductor resonance and its
asymmetric sweep.

- **Nodal DK-method Crybaby model** ([Holters & Zölzer, DAFx-11 case study][wah-dk]):
  derives a nonlinear state-space model straight from the schematic. ✅
- **Weeping Demon** ([DAFx-15][wah-demon]) and port-Hamiltonian
  ([Falaize et al.][wah-ph]) models of other wahs. ✅

**Recommendation:** a DK-method Crybaby, with the existing envelope follower and MIDI
pedal driving its pot position.

## 8. Compressor

**Today:** a feed-forward peak compressor. It's adequate.

- **Better classic design:** the [Giannoulis, Massberg & Reiss tutorial (JAES 2012)][comp-tut]
  compares detectors (peak/RMS, smooth branching, log domain), soft knees and feedback vs
  feed-forward. Cheap, audible improvements to how the compressor feels. ✅
- **Neural opto character:** an efficient TCN model of the LA-2A
  ([Steinmetz & Reiss, JAES 2022][comp-nn], [code][comp-nn-code]): causal, real-time on
  CPU, trained on 10 minutes of audio. Heavier than classic DSP. ⚠️ CPU

**Recommendation:** adopt the tutorial's smooth-branching detector and soft knee; keep the
neural opto as an optional "character" mode later.

## 9. Adequate as they are

- **Noise gate:** possible refinements (hysteresis, hold, a sidechain high-pass) are small
  and need no research.
- **EQ:** RBJ cookbook biquads are standard. Shelves and peaks compress ("cramp") near
  Nyquist at 44.1/48 kHz; matched-response designs fix that. Minor for guitar, not
  researched here.
- **Tremolo:** fine. An optional harmonic (Brownface-style) tremolo would be a feature,
  not a quality fix. A real Boss TR-2 was captured and modelled on 2026-10-26 (soft-clamped
  offset triangle, fast-fall/slow-rise follower, 70 Hz low cut; median error 1.5 % of full
  gain): [`captures/boss/tr-2/README.md`](captures/boss/tr-2/README.md).
- **Tuner:** McLeod (NSDF) is a solid choice. [PESTO][pesto] (neural, < 5 ms) is an option
  if accuracy on low or noisy notes ever becomes a problem.

## Sources

[jl-td]: https://www.semanticscholar.org/paper/Low-latency-audio-pitch-shifting-in-the-time-domain-Juillerat-Schubiger-Banz/9fa827c688688157cc93ec9b296f24a45c6dfdf6
[jl-fd]: https://www.researchgate.net/publication/261078164_Low_latency_audio_pitch_shifting_in_the_frequency_domain
[stn]: https://pub.dega-akustik.de/DAS-DAGA_2025/files/upload/paper/635.pdf
[ss]: https://github.com/Signalsmith-Audio/signalsmith-stretch
[ss-design]: https://signalsmith-audio.co.uk/writing/2023/stretch-design/
[ss-talk]: https://www.youtube.com/watch?v=fJUmmcGKZMI
[rb]: https://github.com/breakfastquay/rubberband
[rb-why]: https://breakfastquay.com/rubberband/why.html
[rb-lat]: https://forum.juce.com/t/how-to-handle-latency-in-pitch-shifter-rubberband/45799
[pvdr]: https://arxiv.org/pdf/2202.07382
[pesto]: https://arxiv.org/pdf/2508.01488
[drop]: https://www.sweetwater.com/insync/hands-on-review-digitech-drop-polyphonic-drop-tune-pitch-shift-pedal/
[dattorro]: https://ses.library.usyd.edu.au/bitstream/handle/2123/8436/FinalTechnologyReview_DarioRizo.p?sequence=2
[erbe]: https://tre.ucsd.edu/wordpress/wp-content/uploads/2018/10/reverbtopo.pdf
[plateau]: https://valleyaudio.github.io/rack/plateau/
[fdn-list]: https://github.com/gdalsanto/delay-network-reverbs
[vfdn]: https://aaltodoc.aalto.fi/items/7c058b9b-8738-4a00-97eb-97b111bb97fe
[wefers]: http://publications.rwth-aachen.de/record/466561/files/466561.pdf?subformat=pdfa
[battenberg]: https://www.ericbattenberg.com/school/partconvDAFx2011.pdf
[diff-fdn]: https://www.researchgate.net/publication/373756296_Differentiable_Feedback_Delay_Network_For_Colorless_Reverberation
[diff-rir]: https://arxiv.org/abs/2510.00238
[t3k-od]: https://www.tone3000.com/tones/boost-and-overdrive-pedals-80015
[t3k-plugin]: https://www.musicradar.com/guitars/tone3000-has-launched-a-free-open-source-plugin-that-puts-hundreds-of-thousands-of-nam-a2-device-captures-at-your-fingertips
[wdf-lib]: https://github.com/Chowdhury-DSP/chowdsp_wdf
[wdf-diode]: https://www.researchgate.net/publication/299514713_An_Improved_and_Generalized_Diode_Clipper_Model_for_Wave_Digital_Filters
[adaa]: https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf
[adaa-notes]: https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510
[smc-dist]: https://www.smc2019.uma.es/articles/S5/S5_02_SMC2019_paper.pdf
[bbd]: https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2018/09/Holters-Parker-2018-A-Combined-Model-for-a-Bucket-Brigade-Device-and-its-Input-and-Output-Filters.pdf
[bbd-chow]: https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils/namespacechowdsp_1_1BBD.html
[phaser-gb]: https://arxiv.org/pdf/2306.01332
[p90]: https://github.com/polimi-ispl/mxrphase90
[chowphaser]: https://github.com/jatinchowdhury18/ChowPhaser
[nn-lfo]: https://www.dafx.de/paper-archive/2020/proceedings/papers/DAFx2020_paper_49.pdf
[nn-pf]: https://www.researchgate.net/publication/356164520_Neural_Modeling_of_Phaser_and_Flanging_Effects
[va-cmp]: https://arxiv.org/pdf/2009.02833
[chowtape]: https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf
[chowtape-code]: https://github.com/jatinchowdhury18/AnalogTapeModel
[wah-dk]: http://recherche.ircam.fr/pub/dafx11/Papers/21_e.pdf
[wah-demon]: https://www.dafx.de/paper-archive/2015/DAFx-15_submission_73.pdf
[wah-ph]: https://www.researchgate.net/publication/278817144_Simulation_of_an_analog_circuit_of_a_wah_pedal_a_port-Hamiltonian_approach
[comp-tut]: https://www.researchgate.net/publication/277772168_Digital_Dynamic_Range_Compressor_Design-A_Tutorial_and_Analysis
[comp-nn]: https://arxiv.org/pdf/2102.06200
[comp-nn-code]: https://github.com/csteinmetz1/micro-tcn

- Pitch shifting: [Juillerat et al., time domain][jl-td] · [Juillerat & Hirsbrunner, frequency domain][jl-fd] · [Polak & Erkut, STN][stn] · [Signalsmith Stretch][ss] · [its design][ss-design] · [ADC22 talk][ss-talk] · [Rubber Band][rb] · [Rubber Band notes][rb-why] · [Rubber Band latency discussion][rb-lat] · [Phase Vocoder Done Right][pvdr] · [PESTO][pesto] · [DigiTech Drop review][drop]
- Reverb: [Dattorro plate overview][dattorro] · [Tom Erbe, reverb topologies][erbe] · [Plateau][plateau] · [delay-network reverb papers][fdn-list] · [velvet-noise FDN][vfdn] · [differentiable FDN][diff-fdn] · [FDN RIR synthesis][diff-rir]
- Convolution: [Wefers thesis][wefers] · [Battenberg & Avižienis][battenberg]
- Drive: [TONE3000 OD/boost captures][t3k-od] · [TONE3000 plugin][t3k-plugin] · [chowdsp_wdf][wdf-lib] · [WDF diode clipper][wdf-diode] · [ADAA][adaa] · [ADAA notes][adaa-notes] · [neural distortion, SMC 2019][smc-dist]
- Modulation and delay: [Holters & Parker, BBD][bbd] · [chowdsp BBD][bbd-chow] · [grey-box phaser][phaser-gb] · [neural LFO effects][nn-lfo] · [neural phaser/flanger][nn-pf] · [VA techniques comparison][va-cmp] · [ChowTape paper][chowtape] · [ChowTape code][chowtape-code]
- Wah: [Crybaby nodal DK][wah-dk] · [Weeping Demon][wah-demon] · [port-Hamiltonian wah][wah-ph]
- Compressor: [Giannoulis, Massberg & Reiss][comp-tut] · [Steinmetz & Reiss][comp-nn] · [micro-tcn code][comp-nn-code]
