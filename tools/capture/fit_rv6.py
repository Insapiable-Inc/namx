#!/usr/bin/env python3
"""Fit the RV-6 types' corrections in src/dsp/Rv6Reverb.h (its FITTED block) to the study.

    python -I tools/capture/fit_rv6.py --render BINARY [--write] [--rounds N] [--build CMD]
        [--modes plate hall ...] [--audio FIRST_SESSION_WAVS] [--keep-rt]

The targets are the study's reports (captures/boss/rv-6/2026-10-31*/fit/*.json): no audio
is needed. BINARY is tools/capture/render_reverb.cpp built against the current header.
Per mode, against the pedal:

  rt60_1k    the tank's RT60 table, scaled until the rendered decay at 1 kHz (T30, A/B
             mean) matches at each of the five Time positions;
  energy_db  the wet's energy against Time, re noon, as the pedal keeps it;
  eq         the colour: the first 2 s's 1/3-octave spectrum re 1 kHz (least squares on
             the chain's own topology: high-pass, low shelf, peak, high shelf — Spring
             high-pass, two low-passes, peak);
  gain_db    the wet's energy at Level and Time noon, re the dry.

(The Tone filter was fitted here too, on Hall's sweeps, until 2026-11-17: it is
rv6/tone.py's now, on the focused session, in a block of its own.) Shimmer
is left as it is: its impulse responses carry the octaves' tails into every band, so
fit_rv6_shimmer.py fits it (its VOICINGS entry and the SHIMMER block) on the Shimmer
session's held notes.
Without --write it prints the comparison; with it the block is rewritten (then run
clang-format on the header). --build CMD rebuilds BINARY between --rounds. --modes fits only
those modes and leaves the others as they are. Where a mode starts with
the pedal's measured early response (Rv6EarlyData.h: Room, Hall, Plate, Spring) its EQ shapes
only the tail, and the first 2 s are mostly that exact start: with --audio (the first
session's WAVs) the colour is fitted on the 2 s after the measured start instead, the pedal's
from its noon sweep. --keep-rt leaves the RT60 tables as they are (Room, Hall and Plate: their
decay shapes are fitted on whole decay curves by fit_rv6_decay.py, which this one's T30 at
1 kHz would only pull back and forth).
The tanks' structure (lines, taps, diffusion, the spring's geometry, Shimmer's loop) is
set by hand in the header against the same reports and the impulse responses' early
envelopes; captures/boss/rv-6/README.md → "In NAM-X" has the comparison. Room, Hall and
Plate start with the pedal's own early response (rv6_early.py, Rv6EarlyData.h): after
refitting them run rv6_early.py again (its Time fit depends on the tank).
"""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from scipy import optimize

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_rv6 as rv  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
HEADER = REPOSITORY / "src/dsp/Rv6Reverb.h"
STUDY = REPOSITORY / "captures/boss/rv-6"
FS = rv.FS
MODES = ["modulate", "spring", "plate", "hall", "room", "shimmer"]  # Mode order
TIME = ["7:00", "9:00", "12:00", "3:00", "5:00"]
THIRDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 12500]
KINDS = ["NONE", "LOW_PASS", "HIGH_PASS", "PEAK", "LOW_SHELF", "HIGH_SHELF"]
DEFAULT_EQ = [("HIGH_PASS", 20.0, 0.7, 0.0), ("LOW_SHELF", 200.0, 0.7, 0.0),
              ("PEAK", 2000.0, 1.0, 0.0), ("HIGH_SHELF", 6000.0, 0.7, 0.0)]


def targets():
    first = json.loads((STUDY / "2026-10-31/fit/rv-6.json").read_text())
    second = json.loads((STUDY / "2026-10-31-followup/fit/rv-6-followup.json").read_text())
    out = {}
    for mode in MODES:
        law = second["time_law"][mode]
        out[mode] = {
            "rt": [law[p]["rt60_s"]["1000"] for p in TIME],
            "energy": [law[p].get("energy_re_noon_db", 0.0) for p in TIME],
            "spectrum": [first["character"][mode]["spectrum_re_1k_db"][str(f)] for f in THIRDS],
            "gain": first["character"][mode]["wet_energy_re_dry_db"],
        }
    tone = second["tone_law"]
    return out, np.array(tone["centres_hz"]), {p: np.array(v) for p, v in tone["re_noon_db"].items()}


# --- The header's FITTED block ---

def braces(text, start):
    """The index just past the brace block opening at text[start]."""
    depth = 0
    for i in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return i + 1
    raise ValueError("unbalanced braces")


def numbers(text):
    return [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?(?:e[-+]?\d+)?", text)]


def read_block():
    text = HEADER.read_text()
    begin, end = text.index("// BEGIN FITTED"), text.index("// END FITTED")
    block = text[begin:end]
    body = block.index("{{", block.index("VOICINGS")) + 1
    voicings, i = [], body + 1
    while len(voicings) < len(MODES):
        i = block.index("{", i)
        j = braces(block, i)
        entry = block[i + 1:j - 1]
        parts, k = [], 0
        while k < len(entry):
            if entry[k] == "{":
                e = braces(entry, k)
                parts.append(entry[k:e])
                k = e
            elif entry[k].isdigit() or entry[k] == "-":
                m = re.match(r"-?\d+(\.\d+)?f?", entry[k:])
                parts.append(m.group(0))
                k += len(m.group(0))
            else:
                k += 1
        rt, energy, gain, eq = parts[0], parts[1], parts[2], parts[3]
        sections = [(kind, *numbers(rest))
                    for kind, rest in re.findall(r"FilterKind::(\w+),\s*([^}]*)", eq)]
        voicings.append({"rt": numbers(rt),
                         "energy": numbers(energy) or [0.0] * 5,
                         "gain": float(gain.rstrip("f")),
                         "eq": sections})
        i = j
    return text, begin, end, voicings


def f(v):
    v = 0.0 if abs(v) < 1e-6 else v
    text = f"{v:.1f}" if abs(v) >= 1000 else f"{v:.4g}"
    return (text if ("." in text or "e" in text) else text + ".0") + "f"


def write_block(text, begin, end, voicings):
    lines = ["// BEGIN FITTED (tools/capture/fit_rv6.py --write rewrites this block)",
             "constexpr array<Voicing, MODE_COUNT> VOICINGS{{"]
    for v in voicings:
        eq = ", ".join(f"{{FilterKind::{k}, {f(hz)}, {f(q)}, {f(g)}}}" for k, hz, q, g in v["eq"])
        lines.append(f"    {{{{{', '.join(map(f, v['rt']))}}}, {{{', '.join(map(f, v['energy']))}}}, "
                     f"{f(v['gain'])}, {{{{{eq}}}}}}},")
    lines.append("}};")
    HEADER.write_text(text[:begin] + "\n".join(lines) + "\n" + text[end:])


# --- RBJ biquads, as BiquadChain.h designs them ---

def biquad_db(kind, hz, q, gain_db, freqs):
    if kind == "NONE" or hz >= 0.49 * FS:
        return np.zeros_like(freqs)
    w = 2 * math.pi * hz / FS
    c, alpha, a = math.cos(w), math.sin(w) / (2 * q), 10 ** (gain_db / 40)
    if kind == "LOW_PASS":
        b, d = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "HIGH_PASS":
        b, d = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "PEAK":
        b, d = [1 + alpha * a, -2 * c, 1 - alpha * a], [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == "LOW_SHELF":
        r = 2 * math.sqrt(a) * alpha
        b = [a * ((a + 1) - (a - 1) * c + r), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - r)]
        d = [(a + 1) + (a - 1) * c + r, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - r]
    else:  # HIGH_SHELF
        r = 2 * math.sqrt(a) * alpha
        b = [a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - r)]
        d = [(a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - r]
    z = np.exp(-1j * 2 * np.pi * np.asarray(freqs) / FS)
    h = (b[0] + b[1] * z + b[2] * z * z) / (d[0] + d[1] * z + d[2] * z * z)
    return 20 * np.log10(np.abs(h))


def chain_db(sections, freqs, unity=1000.0):
    total = np.zeros(len(freqs))
    for s in sections:
        total += biquad_db(*s, freqs) - biquad_db(*s, [unity])[0]
    return total


# --- Rendering and measuring the model ---

def render(binary, mode, time_pct, seconds):
    x = np.zeros(int(seconds * FS), np.float32)
    x[0] = 1.0
    out = subprocess.run([str(binary), str(3 + MODES.index(mode)), str(time_pct), "50", "0", "0", "50"],
                         input=x.tobytes(), capture_output=True, check=True).stdout
    y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
    return rv.bandlimit(y[:, 0] - x), rv.bandlimit(y[:, 1] - x)


def third_levels(a, b, span, relative=True):
    """1/3-octave levels of (A + B) / 2 over `span` (a slice), re 1 kHz (or absolute)."""
    levels = np.array([10 * np.log10(np.sum(rv.band((a + b) / 2, fc, 3)[span] ** 2) + 1e-30) for fc in THIRDS])
    return levels - levels[THIRDS.index(1000)] if relative else levels


def measure(a, b, tail=None):
    n = len(a)
    noise = slice(n - int(0.6 * FS), n - int(0.1 * FS))
    rt = [rv.rt60(h, 1000, noise)[0] for h in (a, b)]
    onset = rv.onset_ms((a + b) / 2)[0] / 1000
    out = {"rt": float(np.mean(rt)), "energy": float(10 * np.log10((np.sum(a * a) + np.sum(b * b)) / 2)),
           "spectrum": third_levels(a, b, slice(int(onset * FS), int((onset + 2) * FS)))}
    if tail is not None:
        span = slice(int(tail[0] * FS), int(tail[1] * FS))
        out["tail_spectrum"] = third_levels(a, b, span)
        out["tail_energy"] = float(10 * np.log10((np.sum(a[span] ** 2) + np.sum(b[span] ** 2)) / 2))
    return out


def early_ends():
    """Each mode's measured early response's end (Rv6EarlyData.h), s; absent: none."""
    text = (REPOSITORY / "src/dsp/Rv6EarlyData.h").read_text()
    out = {}
    for name, length, offset in re.findall(r"EarlyKernel\{(\w+)_EARLY_A\.data\(\),\s*\w+\.data\(\),\s*(\d+),\s*(\d+)",
                                           text):
        out[name.lower()] = (int(length) + int(offset)) / 48000.0
    return out


def tail_span(mode, end, goal):
    """From the measured start's end over 0.6 x the RT60 at noon (its first ~36 dB), at most 2 s."""
    return end, end + min(2.0, 0.6 * goal[mode]["rt"][2])


def pedal_tails(audio, ends, goal):
    """The pedal's colour over each tail span (its noon sweep, A and B), and which bands stand
    10 dB over its noise there (the sweep's last second, over as long a span)."""
    session = rv.Session(STUDY / "2026-10-31", audio)
    out = {}
    for mode, end in ends.items():
        a, b = (session.wet_ir(f"{mode}-sweep", ch) for ch in (0, 2))
        start, stop = tail_span(mode, end, goal)
        span = slice(int(start * FS), int(stop * FS))
        levels = third_levels(a, b, span, relative=False)
        n = len(a)
        quiet = slice(n - (span.stop - span.start) - int(0.1 * FS), n - int(0.1 * FS))
        noise = third_levels(a, b, quiet, relative=False)
        # The tail's energy re the dry, the noise's (as long a span) taken off.
        energy = ((np.sum(a[span] ** 2) + np.sum(b[span] ** 2)) - (np.sum(a[quiet] ** 2) + np.sum(b[quiet] ** 2))) / 2
        out[mode] = (levels - levels[THIRDS.index(1000)], levels > noise + 10, float(10 * np.log10(energy)))
    return out


# Where each kind of section may sit, Hz (its Q stays as written: 9 band levels can't pin
# down 12 free parameters).
EQ_RANGE = {"HIGH_PASS": (20, 400), "LOW_PASS": (2000, 16000), "LOW_SHELF": (60, 1000),
            "PEAK": (250, 10000), "HIGH_SHELF": (1500, 12000)}


def fit_eq(sections, correction_db):
    """New chain parameters: the old chain's response plus `correction_db`, least squares on
    each section's frequency and gain (a small penalty keeps the gains modest)."""
    freqs = np.array(THIRDS, float)
    goal = chain_db(sections, freqs) + np.nan_to_num(correction_db)
    # A band whose correction is unknown (NaN: the pedal there is its noise) counts for nothing.
    weights = np.where(np.isnan(correction_db), 0.0, np.where(freqs < 100, 0.5, 1.0))

    def unpack(p):
        return [(k, math.exp(p[2 * i]), q, p[2 * i + 1]) for i, (k, _, q, _) in enumerate(sections)]

    def residual(p):
        gains = p[1::2]
        return np.concatenate([weights * (chain_db(unpack(p), freqs) - goal), 0.1 * gains])

    p0 = np.concatenate([[math.log(s[1]), s[3]] for s in sections])
    lo = np.concatenate([[math.log(EQ_RANGE[s[0]][0]), -18] for s in sections])
    hi = np.concatenate([[math.log(EQ_RANGE[s[0]][1]), 18] for s in sections])
    fit = optimize.least_squares(residual, np.clip(p0, lo + 1e-9, hi - 1e-9), bounds=(lo, hi))
    error = (chain_db(unpack(fit.x), freqs) - goal)[weights > 0]
    return unpack(fit.x), float(np.sqrt(np.mean(error ** 2)))


def round_once(binary, write, only=None, tails=None, keep_rt=False):
    goal, _, _ = targets()
    text, begin, end, voicings = read_block()
    ends = early_ends() if tails else {}
    report = {}
    usable = None
    for m, mode in enumerate(MODES):
        if mode == "shimmer" or (only and mode not in only):
            continue  # Shimmer: fit_rv6_shimmer.py
        v, g = voicings[m], goal[mode]
        if not v["eq"]:
            v["eq"] = list(DEFAULT_EQ)
        rows = []
        tail = tail_span(mode, ends[mode], goal) if tails and mode in tails else None
        for i, pos in enumerate(TIME):
            seconds = float(np.clip(3.0 * g["rt"][i] + 1.0, 6.0, 40.0))
            rows.append(measure(*render(binary, mode, rv.clock_fraction(pos) * 100, seconds),
                                tail if i == 2 else None))
        noon = rows[2]
        # With a measured start, the colour of the tail after it (the EQ's only part), where
        # the pedal's stands over its noise.
        spectrum_model = noon["tail_spectrum"] if tail is not None else noon["spectrum"]
        spectrum_pedal = tails[mode][0] if tail is not None else np.array(g["spectrum"])
        usable = tails[mode][1] if tail is not None else np.ones(len(THIRDS), bool)
        report[mode] = {"rt_model": [r["rt"] for r in rows], "rt_pedal": g["rt"],
                        "energy_model": [r["energy"] - noon["energy"] for r in rows], "energy_pedal": g["energy"],
                        "spectrum_model": spectrum_model.round(1).tolist(),
                        "spectrum_pedal": np.round(spectrum_pedal, 1).tolist(),
                        "spectrum_span": "tail" if tail is not None else "first 2 s",
                        "usable": usable.tolist(),
                        "gain_model": noon["energy"], "gain_pedal": g["gain"]}
        if not keep_rt:
            v["rt"] = [old * want / got for old, want, got in zip(v["rt"], g["rt"], report[mode]["rt_model"])]
        v["energy"] = [old + want - got for old, want, got in
                       zip(v["energy"], g["energy"], report[mode]["energy_model"])]
        before = chain_db(v["eq"], np.array(THIRDS, float))
        v["eq"], eq_rms = fit_eq(v["eq"], np.where(usable, spectrum_pedal - spectrum_model, np.nan))
        after = chain_db(v["eq"], np.array(THIRDS, float))
        # The new EQ changes the wet's energy too: estimate it from the band energies' shift.
        band_energy = 10 ** (spectrum_model / 10)
        shift = 10 * np.log10(np.sum(band_energy * 10 ** ((after - before) / 10)) / np.sum(band_energy))
        if tail is not None:
            # The gain only moves the tail here (the measured start doesn't follow it, Rv6Early.h):
            # fitted on the tail's energy, which the whole response's would follow only by the
            # tail's share of it (Spring's: 8 %).
            v["gain"] = v["gain"] + tails[mode][2] - noon["tail_energy"] - shift
            report[mode]["tail_energy_model"] = noon["tail_energy"]
            report[mode]["tail_energy_pedal"] = tails[mode][2]
        else:
            v["gain"] = v["gain"] + g["gain"] - noon["energy"] - shift
        report[mode]["eq_fit_rms_db"] = eq_rms
    if write:
        write_block(text, begin, end, voicings)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--render", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--build")
    parser.add_argument("--modes", nargs="+", choices=[m for m in MODES if m != "shimmer"])
    parser.add_argument("--audio", type=Path, help="the first session's WAVs: fit measured starts' tails")
    parser.add_argument("--keep-rt", action="store_true", help="leave the RT60 tables as they are")
    args = parser.parse_args()
    tails = pedal_tails(args.audio, early_ends(), targets()[0]) if args.audio else None
    for r in range(args.rounds):
        report = round_once(args.render, args.write, args.modes, tails, args.keep_rt)
        print(f"Round {r + 1}")
        for mode in MODES:
            if mode not in report:
                print(f"  {mode:9s} " + ("fitted by fit_rv6_shimmer.py" if mode == "shimmer" else "left as it is"))
                continue
            q = report[mode]
            tail_text = (f" (tail {q['tail_energy_model']:+.1f}/{q['tail_energy_pedal']:+.1f})"
                         if "tail_energy_model" in q else "")
            print(f"  {mode:9s} RT 1k model/pedal " + " ".join(f"{a:.2f}/{b:.2f}" for a, b in zip(q["rt_model"], q["rt_pedal"]))
                  + f" | energy re noon " + " ".join(f"{a:+.1f}/{b:+.1f}" for a, b in zip(q["energy_model"], q["energy_pedal"]))
                  + f" | wet {q['gain_model']:+.1f}/{q['gain_pedal']:+.1f}{tail_text} dB | EQ fit {q['eq_fit_rms_db']:.1f} dB rms")
            print(f"  {'':9s} spectrum ({q['spectrum_span']}) model/pedal " + " ".join(
                f"{fc}:{a:+.0f}/{b:+.0f}" + ("" if ok else "~") for fc, a, b, ok in
                zip(THIRDS, q["spectrum_model"], q["spectrum_pedal"], q["usable"])))
        if args.build and r + 1 < args.rounds:
            subprocess.run(args.build, shell=True, check=True)


if __name__ == "__main__":
    main()
