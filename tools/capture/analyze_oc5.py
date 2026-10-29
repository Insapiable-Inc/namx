#!/usr/bin/env python3
"""Measures the Boss OC-5 Octave from its session (plans/oc-5.json, mono: Input -> Output).
Findings: captures/boss/oc-5/README.md.

    analyze_oc5.py SESSION          # writes SESSION/fit/oc-5.{json,svg}

What it measures (every level in dB re the guitar's peak at the pedal input, the rig's
reference; Direct and the other octaves fully down unless said):
  dry path     dry-sweep (Direct 12:00) against the bypass sweep: delay, gain, response
  dry clip     a held 220 Hz sine at the guitar's peak, Direct 12:00 and 5:00: the
               waveform's ceiling, the clip point it implies, the 3rd harmonic
  knob laws    the 220 Hz sine per position: Direct (the dry) and -1 OCT (Vintage)
  waveforms    each octave's output folded over its own period (the 220 Hz sine), and
               its harmonics
  tracking     re-picked notes from 73 Hz to 1.3 kHz: the output's pitch, level (150-600 ms
               after the pick) and lock-on time (half the note's peak) per note
  chords       a low note under a higher one, picked together: the level of each note's
               shifted copy
  fade         two notes dying over 10 s: the shifted copy's level against the input's
"""

import argparse
import json
import pathlib
import sys

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal

import analysis
import analyze_dd8

RATE = 48000
NOTES = [73.42, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 493.88, 659.26, 880.0, 1318.51]
NOTE_SECONDS = 3.0
PAIRS = [(82.41, 196.0), (110.0, 329.63), (146.83, 493.88), (196.0, 659.26)]
PAIR_SECONDS = 4.0
FADE_NOTES = [(0.0, 82.41), (10.0, 196.0)]
FADE_DECAY_S = 2.5
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
# take stem, its octave (output / input frequency), label
TRACKING = [("vintage-down1", 0.5, "Vintage -1"), ("vintage-down2", 0.25, "Vintage -2"),
            ("vintage-up1", 2.0, "Vintage +1"), ("poly-up1", 2.0, "Poly +1 (Range 5:00)"),
            ("poly-down1-range-0500", 0.5, "Poly -1, Range 5:00"),
            ("poly-down1-range-1200", 0.5, "Poly -1, Range 12:00"),
            ("poly-down1-range-0700", 0.5, "Poly -1, Range 7:00")]
CHORDS = [("vintage-down1", 0.5, "Vintage -1"), ("poly-up1", 2.0, "Poly +1 (Range 5:00)"),
          ("poly-down1-range-0500", 0.5, "Poly -1, Range 5:00"),
          ("poly-down1-range-1200", 0.5, "Poly -1, Range 12:00"),
          ("poly-down1-range-0700", 0.5, "Poly -1, Range 7:00")]
WAVEFORMS = [("vintage-down1-sine", 0.5, "Vintage -1"), ("vintage-down2-sine", 0.25, "Vintage -2"),
             ("vintage-up1-sine", 2.0, "Vintage +1"), ("poly-up1-sine", 2.0, "Poly +1 (Range 5:00)"),
             ("poly-down1-range-0700-sine", 0.5, "Poly -1, Range 7:00")]


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))


def take(session, name):
    """The take's signal, in units of the guitar's peak."""
    t = analysis.Take(session, name)
    return t.signal(0) / 10 ** (t.meta["rig"]["calibration"]["target_dbfs"] / 20)


def amplitude_at(x, f):
    """A component's peak amplitude (Blackman window, zero-padded)."""
    window = np.blackman(len(x))
    n = 1 << int(np.ceil(np.log2(8 * len(x))))
    spectrum = np.abs(np.fft.rfft(x * window, n)) / (window.sum() / 2)
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    near = np.abs(freqs - f) < max(1.0, 0.02 * f)
    return float(spectrum[near].max())


def dominant(x, low=12.0):
    window = np.blackman(len(x))
    n = 1 << int(np.ceil(np.log2(8 * len(x))))
    spectrum = np.abs(np.fft.rfft(x * window, n))
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    band = freqs > low
    return float(freqs[band][np.argmax(spectrum[band])])


def dry_path(session):
    bypass, _ = analyze_dd8.impulse_response(session, "bypass-sweep")
    dry, _ = analyze_dd8.impulse_response(session, "dry-sweep")
    n = RATE // 2
    ratio = np.fft.rfft(dry[:n]) / np.fft.rfft(bypass[:n])
    freqs = np.fft.rfftfreq(n, 1 / RATE)
    band = (freqs > 300) & (freqs < 10000)
    slope = np.polyfit(freqs[band], np.unwrap(np.angle(ratio[band])), 1)[0]
    bands = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000, 18000, 20000]
    response = {f"{f:g}": round(float(db(np.mean(np.abs(ratio[(freqs > f / 1.06)
                                                              & (freqs < f * 1.06)])))), 2)
                for f in bands}
    return {"delay_ms_re_bypass": round(float(-slope / (2 * np.pi) * 1000), 3),
            "gain_db_re_bypass_1khz_direct_1200": response["1000"],
            "response_db_re_bypass": response}


def held(session, name):
    """The held 220 Hz sine's steady part (0.5-3.5 s)."""
    return take(session, name)[RATE // 2:int(3.5 * RATE)]


def fold(x, f, bins=48):
    """The waveform over one period of f (mean per phase bin)."""
    period = RATE / f
    phase = ((np.arange(len(x)) % period) / period * bins).astype(int)
    return np.array([x[phase == k].mean() for k in range(bins)])


def dry_clip(session, gain_db):
    out = {}
    for position in ("12:00", "5:00"):
        x = held(session, f"direct-{position.replace(':', '').zfill(4)}")
        cycle = fold(x, 220.0)
        out[position] = {"ceiling_db": round(float(db(np.abs(cycle).max())), 2),
                         "fundamental_db": round(float(db(amplitude_at(x, 220))), 2),
                         "h3_db_re_fundamental": round(float(db(amplitude_at(x, 660))
                                                             - db(amplitude_at(x, 220))), 1)}
    # Direct 12:00's ceiling over its small-signal gain: where the input clips
    out["clip_point_db_re_reference"] = round(out["12:00"]["ceiling_db"] - gain_db, 1)
    return out


def knob_laws(session):
    laws = {"Direct (220 Hz)": {}, "-1 OCT, Vintage (110 Hz)": {}}
    for position in POSITIONS:
        tag = position.replace(":", "").zfill(4)
        laws["Direct (220 Hz)"][position] = round(float(db(amplitude_at(
            held(session, f"direct-{tag}"), 220))), 1)
        if position != "7:00":
            name = "vintage-down1-sine" if position == "12:00" else f"vintage-down1-{tag}"
            laws["-1 OCT, Vintage (110 Hz)"][position] = round(float(db(amplitude_at(
                held(session, name), 110))), 1)
    for law in laws.values():
        centre = law["12:00"]
        for position in law:
            law[position] = {"db": law[position], "re_1200": round(law[position] - centre, 1)}
    return laws


def waveforms(session):
    out, cycles = {}, {}
    for name, ratio, label in WAVEFORMS:
        x = held(session, name)
        f = 220.0 * ratio
        cycles[label] = fold(x, f)
        fundamental = amplitude_at(x, f)
        out[label] = {"take": name, "frequency_hz": f,
                      "fundamental_db": round(float(db(fundamental)), 1),
                      "harmonics_db_re_fundamental": [round(float(db(amplitude_at(x, k * f))
                                                                  - db(fundamental)), 1)
                                                      for k in range(2, 7)],
                      "input_leak_db": round(float(db(amplitude_at(x, 220.0))), 1)}
    return out, cycles


def tracking(session):
    b, a = scipy.signal.butter(2, 500 / (RATE / 2))
    out = {}
    for stem, ratio, label in TRACKING:
        x = take(session, f"{stem}-tracking")
        rows = []
        for i, f0 in enumerate(NOTES):
            note = x[int(i * NOTE_SECONDS * RATE):int((i + 1) * NOTE_SECONDS * RATE)]
            envelope = scipy.signal.filtfilt(b, a, np.abs(note))
            steady = note[int(0.15 * RATE):int(0.6 * RATE)]
            peak = envelope[:int(0.6 * RATE)].max()
            pitch = dominant(note[int(0.05 * RATE):int(1.0 * RATE)])
            rows.append({"note_hz": f0, "out_hz": round(pitch, 2),
                         "ratio": round(pitch / f0, 3),
                         "level_db": round(float(db(amplitude_at(steady, f0 * ratio))), 1),
                         "lock_ms": round(float(np.argmax(envelope > peak / 2) / RATE * 1000), 1)})
        out[label] = rows
    return out


def chords(session):
    out = {}
    for stem, ratio, label in CHORDS:
        x = take(session, f"{stem}-dyads")
        rows = []
        for i, (low, high) in enumerate(PAIRS):
            part = x[int((i * PAIR_SECONDS + 0.2) * RATE):int((i * PAIR_SECONDS + 0.8) * RATE)]
            rows.append({"notes_hz": [low, high],
                         "low_shifted_db": round(float(db(amplitude_at(part, low * ratio))), 1),
                         "high_shifted_db": round(float(db(amplitude_at(part, high * ratio))), 1)})
        out[label] = rows
    return out


def fades(session):
    out = {}
    for stem, ratio, label in WAVEFORMS[:3]:
        x = take(session, stem.replace("-sine", "-fade"))
        notes = []
        for start, f0 in FADE_NOTES:
            levels = []
            for k in range(0, 100, 5):  # every 0.5 s, 0.1 s frames
                t = start + k * 0.1
                part = x[int(t * RATE):int((t + 0.1) * RATE)]
                levels.append([round(-20 * np.log10(np.e) * k * 0.1 / FADE_DECAY_S, 1),
                               round(float(db(amplitude_at(part, f0 * ratio))), 1)])
            slope = np.polyfit([v[0] for v in levels[2:]], [v[1] for v in levels[2:]], 1)[0]
            notes.append({"note_hz": f0, "input_db_vs_output_db": levels,
                          "slope_below_minus_3db": round(float(slope), 3)})
        out[label] = notes
    return out


def plot(out_dir, result, cycles):
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.hashsalt"] = "oc-5"  # the same SVG ids on every run
    figure, axes = plt.subplots(2, 2, figsize=(14, 9.5), constrained_layout=True)

    ax = axes[0, 0]
    for label, cycle in cycles.items():
        ax.plot(np.linspace(0, 1, len(cycle), endpoint=False), cycle, label=label)
    ax.set_title("Each octave alone, a held 220 Hz sine: one period of the output")
    ax.set_xlabel("phase (one period of the octave)")
    ax.set_ylabel("re the guitar's peak")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)

    for ax, key, ylabel, title in (
            (axes[0, 1], "level_db", "dB re the guitar's peak",
             "Tracking: the shifted note's level (150-600 ms after the pick)"),
            (axes[1, 0], "lock_ms", "ms", "Tracking: lock-on time (half the note's peak)")):
        for label, rows in result["tracking"].items():
            ax.plot([r["note_hz"] for r in rows], [r[key] for r in rows], "o-", label=label)
        ax.set_xscale("log")
        ax.set_xlabel("note played (Hz)")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.grid(alpha=0.3, which="both")
        ax.legend(fontsize=8)
    axes[0, 1].set_ylim(-60, 5)
    axes[1, 0].set_yscale("log")

    ax = axes[1, 1]
    labels = list(result["chords"])
    width = 0.8 / len(labels)
    for j, label in enumerate(labels):
        rows = result["chords"][label]
        x = np.arange(len(rows)) + j * width
        ax.bar(x - 0.4 + width / 2, [r["low_shifted_db"] - r["high_shifted_db"] for r in rows],
               width, label=label)
    ax.set_xticks(np.arange(len(PAIRS)))
    ax.set_xticklabels([f"{lo:g} + {hi:g} Hz" for lo, hi in PAIRS])
    ax.axhline(0, color="k", lw=0.5)
    ax.set_ylabel("low note's copy minus high note's copy (dB)")
    ax.set_title("Two notes picked together: 0 = both shifted, high = only the low one")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3, axis="y")
    figure.savefig(out_dir / "oc-5.svg", metadata={"Date": None})


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")
    dry = dry_path(session)
    shapes, cycles = waveforms(session)
    result = {"pedal": "Boss OC-5 Octave", "session": session.name, "dry_path": dry,
              "dry_clip": dry_clip(session, dry["gain_db_re_bypass_1khz_direct_1200"]),
              "knob_laws": knob_laws(session), "waveforms": shapes,
              "tracking": tracking(session), "chords": chords(session), "fades": fades(session)}
    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "oc-5.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, cycles)
    print(json.dumps({k: result[k] for k in ("dry_path", "dry_clip", "knob_laws")}, indent=1))


if __name__ == "__main__":
    main()
