# Blind tests on a pedal the user chose

2026-11-11. The user picks a pedal and its settings and tells nothing; the pedal, its mode and its knobs
are read from ten general takes (`tools/capture/plans/mystery-reamp.json`, run with
`tools/capture/reamp.py mystery --session NAME`): a sweep, clicks, a steady 440 Hz tone, five tones
together, a noise staircase, a 1 kHz staircase, plucks, and the funk and stabs excerpts. The first take
is the pedal off. One output only (the mono one), so a stereo pedal is heard as its mono sound.

**These sessions are validation data: in no fit.** What a reading gets wrong says where a model (or the
reading) is weak; each such point is listed in the pedal's own study.

| Session | Pedal and mode | Set by the user (aimed, by eye) | Read from the takes |
|---|---|---|---|
| `2026-11-11-mystery` | BF-3 Flanger, Standard | Manual 1:30, Res 4:00, Depth 8:00, Rate 10:30 | BF-3, Standard; Manual 1:50 (the delay 1.04–1.10 ms), Res 4:30–5:00 (about 85 % a pass), Depth 7:05–7:15 (the delay swings 0.06 ms), Rate 10:45 (a sweep every 4.6 s) |
| `2026-11-11-mystery-2` | RV-6 Reverb, Room | Time 1:30, Tone 10:30, Level 10:30 | RV-6, Room; Time 1:00 (60 dB in about 1.7 s at 1 kHz), Tone 12:30, Level 9:50 |
| `2026-11-11-mystery-3` | PH-3 Phase Shifter, Fall | Rate 11:00, Depth 1:00, Res 4:00 | PH-3, Fall; Rate 10:45 (a new peak every 2.35 s); Res "3:00 or above"; Depth not readable |
| `2026-11-11-mystery-4` | RT-2 Rotary Ensemble, Mode III | Fast on (LED red), Fast (inner knob) 8:00, Slow (outer) 11:00, Rise/Fall SLOW, the Drive Knob toggle on DRIVE with the ring at 10:00, Level 1:00 | RT-2, Mode III, Fast on; Fast 7:30 (the drum at 2.646 Hz); Level about 12:00; Balance fully to the drum (the horn 50 dB down at 4 kHz); Drive 7:30–8:00 |
| `2026-11-11-mystery-5` | DD-8 Digital Delay, **Warm** | Level about 1:00, Feedback about 1:30, Time about 2:00 | DD-8, **Standard** (wrong); Time 2:10 (535.4 ms); Feedback 1:25 (4.0 dB a repeat); Level 11:30 (wrong: read on the clicks) |

The AW-3's two blind sessions are in its own folder (`../aw-3/2026-11-11-blind`, `-blind-2`: both Up, read
right, with Sens right and Manual within 40–60 minutes; its Decay does nothing from 7:00 to 12:00), the
DD-8's other modes at the fifth test's knobs in `../dd-8/2026-11-11-samesettings`, and two blind settings
of the PH-3 inside its Res session (`../ph-3/2026-11-11-res`, V1 and V2).

`2026-11-11-reamp` (untracked) is the first test's false start, the pedal left off: not used.

## How each pedal was told apart

- **Flanger:** the response is a comb, peaks every 940 Hz with single notches between, and a click comes
  back as a train 1.07 ms apart. Ultra would show a second, denser set of peaks.
- **Reverb:** the dry passes untouched and undelayed; a dense tail follows that is the same after every
  click (correlation 0.98–0.99 between clicks). The mode by when the tail starts (about 60 ms: Room 58,
  Plate 57, Hall 26, Modulate 22, Spring 75 in the study) and by its first 400 ms against the model's
  measured starts (Room 0.37–0.40, the other five 0.03–0.05). The first click of a take sits on the
  marker's tail: use the later ones, with the dry taken out.
- **Phaser:** notches and peaks that slide, a click whose polarity flips from one to the next. Fall: the
  peaks only move down, then a new one appears above.
- **Rotary:** a steady tone's level and pitch wobble together, repeating exactly each turn; with the horn
  off, nearly nothing above 2 kHz. Slow cannot reach 2.65 Hz at the drum (its top is 2.43): Fast was on.
- **Delay:** the dry click untouched, separate copies at even intervals. Mod and Tape move the repeats'
  timing (0.5–0.7 ms from click to click); Standard, Warm and Analog land on the same sample.

## What the misses said

- **BF-3, Depth near the bottom:** at 8:00 the delay swung 0.06 ms; the model's straight line from 7:00
  (none) to 9:00 (1.1 ms) says 0.55. Depth was only measured at 7:00, 9:00 and up. The Flanger's knobs
  are in real units, so only the conversion of old presets depends on that curve.
- **RV-6, Tone in Room:** the reading (by matching the model) was two hours off. If the knob stood near
  10:30, the model's Room is 4–6 dB too dark at 4–8 kHz there (Tone's curve was measured in Hall and
  Spring only), and its tail at Time 1:30 about 18 % long (2.05 s against 1.73). For the RV-6's focused
  session.
- **PH-3, Res in Rise and Fall:** no setting of the model came within 7 dB. The Res session followed:
  the whole Rise/Fall engine is off, by 8–10 dB over the cycle (the PH-3 study).
- **RT-2, Drive in Mode III:** the pedal compresses loud noise by 3–5 dB at Drive 10:00 with the drum
  alone; the model at the same Drive by 11–17 dB, and only matches with Drive nearly off. Also: with
  the toggle on DRIVE the ring leaves Balance where it was stored, and nothing shows it — the pedal had
  been playing without its horn, probably since the isolated-rotor session.
- **DD-8, Warm read as Standard, Level read low:** both from the clicks. The pedal returns a one-sample
  click about 6 dB quieter and thinner in the bass than it returns a guitar's repeat, in every mode; the
  model does not. On a guitar stab the model holds in all five modes (the DD-8 study). **Never read a
  delay's level or tone from a click:** use a short chord, its repeat clear of the dry in time.

Across the five (and the AW-3's two): the pedal right every time, the mode six times out of seven, the
knobs within half an hour where a measured table stands behind the reading.
