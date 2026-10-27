# Pedal captures

Tools for measuring real pedals through an audio interface and fitting models of them,
so NAM-X's own effects can be voiced after the real thing. Started 2026-10-26 with a
collection of Boss pedals; the first one done is the TR-2 tremolo
([`captures/boss/tr-2/README.md`](../../captures/boss/tr-2/README.md)).

| File | What it does |
|---|---|
| `capture.py` | Guided recording through JACK: ports, metering, level calibration, plan-driven takes |
| `signals.py` | Deterministic test signals (sine, exponential sweep, noise, clicks, silence) and the take layout |
| `analysis.py` | Reading sessions back: aligned takes, envelopes, LFO frequency and cycle folding, band levels, harmonics |
| `fit_tr2.py` | The TR-2 model fit: parameters, fit errors, tone, and the figures |
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
  amixer -c 3 cset numid=26 19     # Analogue Output 03 ← PCM 3
  amixer -c 3 cset numid=27 20     # Analogue Output 04 ← PCM 4
  # undo: amixer -c 3 cset numid=26 11; amixer -c 3 cset numid=27 12   (Mix C / Mix D)
  ```

  This also takes over headphone output 2, which shares those channels. The card number
  (3) and the numids are this machine's; `amixer -c N controls` lists them.
- **Ports used:** pedal feed `playback_AUX2` (out 3), loopback `playback_AUX3` (out 4)
  → line input 5 = `capture_AUX4`, pedal into front input 2 = `capture_AUX1` (input 1 is
  taken by a microphone).
- **Input 2:** INST on, AIR off, **pad on** (`amixer -c 3 sset "Line In 2 Pad" cap`;
  without it a bridge humbucker peaked at −6.1 dBFS at minimum gain, leaving too little
  headroom for a drive with its Level up), **gain fully counter-clockwise**: the one knob
  position that can always be found again.
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
  pactl set-card-profile alsa_card.usb-Focusrite_Scarlett_8i6_USB_F8FXD3B190BDA0-00 \
      output:analog-surround-21+input:multichannel-input
  amixer -c 3 cset numid=26 11; amixer -c 3 cset numid=27 12
  amixer -c 3 sset "Line In 2 Pad" nocap
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

## Sessions and file formats

A session is one pedal on one day: `captures/<maker>/<pedal>/<date>/`.

- `rig.json`: ports, reference level (`ref_dbfs`), calibration, free-text note.
- `<take>.wav`: float32, 2 channels: 1 = pedal output, 2 = loopback. **Git-ignored**
  (~10 MB per take, ~240 MB per pedal); back them up separately.
- `played/<take>.wav`: exactly what was played (mono float32). Git-ignored too.
- `<take>.json`: the take's sidecar: pedal, bypass flag, knob settings, signal spec,
  sample rate, reference level, `marker_start` and `signal_start` (samples, in the
  played file), `latency_samples` and its correlation quality, peaks, xruns, SHA-256 of
  what was played, time, the tool's git commit, and a copy of the rig.
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

## Lessons from the TR-2

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
