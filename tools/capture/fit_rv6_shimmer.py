#!/usr/bin/env python3
"""Fit the RV-6 Shimmer (src/dsp/Rv6Reverb.h: the SHIMMER block and Shimmer's VOICINGS entry)
to the Shimmer session by simulation.

    python -I tools/capture/fit_rv6_shimmer.py --render BINARY --audio WAV_DIRECTORY [--focused]
        [--grain-ms 299 ...] [--block-ms 80] [--forward-delay-ms 0 40 ...] [--write] [--evaluate]
        [--report JSON] [--figure SVG]

BINARY is tools/capture/render_rv6.cpp built against the current header; WAV_DIRECTORY holds
the session's recordings and played/ files (captures/boss/rv-6/2026-11-01-shimmer: only the
sidecars and fit/rv-6-shimmer.json are in git). NAM-X renders each held note and the 82 Hz
pluck at the take's knob settings, from the played file (silence where the marker went to the
loopback only), and is measured by analyze_rv6's own line code (note_lines, pluck_lines_of)
against the pedal's numbers in the session's rv-6-shimmer.json:

  steady   each line's held level re the dry note, A and B averaged in power, where the
           pedal's is 20 dB over its floor;
  decay    each line's fall after the note, re its held level, 0.2-2.5 s after the stop,
           where the pedal's is 10 dB over its floor;
  pluck    the 82 Hz pluck's 1x-8x re the dry pluck's peak, its first 4 s, and each line's
           peak on its own;
  spectrum the wet in third octaves (80 Hz-10 kHz) while held and in three windows of its
           tail, every take but the plucks' second (cut) note, the chord and the fifth included,
           re the dry's loudest band; where the pedal is down at its noise floor, NAM-X only
           pays for sounding over it;
  spread   how much of each octave sits on its line (within 0.3 %, re 6 % around): the
           pedal's are spread by its grains (82-1319 Hz, 2x and 4x);
  chord    the chord's and the fifth's lines, each note's octaves re its own dry (a
           frequency two notes share counts once).
The chord, the fifth and the pluck count more (TAKE_WEIGHT): what a player plays; a held
sine samples one point of a tank's irregular response.

--focused adds the focused session of 2026-11-15 (its WAVs under captures/boss/rv-6/, run from
the repository root): the fit above, on held notes and plucks alone, left the response to a noise
burst and to the guitar's stabs far off (the tail after a burst half as long below Time 3:00, the
stabs 7-9 dB quiet). Per take, the wet after the -12 dB burst (FOCUS_BURSTS: every Time and Tone
position measured) and over the guitar's stabs (FOCUS_STABS) is followed per octave band,
125 Hz-8 kHz, in 100 ms steps, both outputs' mean energy, where the pedal stands 6 dB over its
floor: NAM-X's levels against the pedal's (the pedal's wet as rv6/takes.py makes it: slips undone,
the dry taken out; NAM-X through the same chain), every step of every band: where the pedal has
gone under its floor NAM-X pays for sounding on (focus_residual). With it Time's RT60 and energy
at 9:00 and 3:00 are free too (bursts at all five positions), and the climb's gain at the four
Time positions off noon (ShimmerShape::back_at: the pedal's top octaves die with the note at
short Time and build at long). check-a and check-b stay out of the fit.

Onsets aren't fitted (10 ms steps), nor is the grain (the spread jumps with it): the
shifter (--grain-ms, the delay-line one; 0 for the STFT one at --block-ms) and the forward
octave's delay set them, so each pair gets its own fit. Free: the forward gain and the shifted
share at Tone noon and 5:00 (7:00 is none of either), the shared recirculation's gain, the
share of it the loop loses, the climb's, the forward octave's and the 1x's bands (the
latter's floor Q too), the longer tank's level, RT60, low and high decay, both tanks' high
decay and modulation depth, the tank's RT60 at 1 kHz with nothing shifted at Time 7:00,
noon and 5:00 (9:00 and 3:00 follow the pedal's 1 kHz law), the wet's gain and its energy
at Time 7:00 and 5:00. The Jacobian's columns render in parallel (JACOBIAN_WORKERS). --write rewrites the header with the best block's fit (then run
clang-format); --evaluate only measures the header as it stands. --figure draws the pedal's
lines (thin) and NAM-X's (thick) for the held notes, A and B in power.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from scipy import optimize
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_rv6 as rv  # noqa: E402
import fit_rv6  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
HEADER = REPOSITORY / "src/dsp/Rv6Reverb.h"
SESSION = REPOSITORY / "captures/boss/rv-6/2026-11-01-shimmer"
FS = rv.FS
SHIMMER_MODE = 5
# The pedal's RT60 at 1 kHz across Time (2026-10-31 bursts): 9:00 and 3:00 follow it.
PEDAL_RT_LAW = [2.7, 2.9, 8.5, 11.8, 14.9]
PLUCK_F0 = 82.41
NAMES = ["forward_noon", "forward_5", "share_noon", "share_5", "back_gain", "share_loss", "shift_hp",
         "shift_lp", "forward_hp", "forward_lp", "band_hp", "band_lp", "long_gain", "long_high_scale", "rt_noon",
         "rt_7_ratio", "rt_5_ratio", "gain_db", "energy_7", "energy_5", "long_rt_scale", "long_low_scale",
         "high_scale", "mod_scale", "band_q"]
# With --focused: Time's 9:00 and 3:00 on their own (else they follow PEDAL_RT_LAW and 0.6 of the
# ends' energy).
FOCUS_NAMES = ["rt_9_ratio", "rt_3_ratio", "energy_9", "energy_3", "back_7", "back_9", "back_3", "back_5"]
SCALES = ["long_high_scale", "long_rt_scale", "long_low_scale", "high_scale", "mod_scale"]
# Shape values the fit carries as the header has them (or these defaults) but doesn't vary: the
# LFO rates' spread across the lines was set by ear (one shared rate made a chord's notes take
# turns: each note's octaves peaked at its own point of the cycle).
FIXED = {"mod_spread": 0.0}
LOG = {"forward_noon", "forward_5", "back_gain", "shift_hp", "shift_lp", "forward_hp", "forward_lp", "band_hp",
       "band_lp", "long_gain", "rt_noon", "rt_7_ratio", "rt_5_ratio", "rt_9_ratio", "rt_3_ratio", "band_q", "back_7", "back_9",
       "back_3", "back_5", *SCALES}
BOUNDS = {"forward_noon": (0.02, 10), "forward_5": (0.02, 12), "share_noon": (0.0, 0.45),
          "share_5": (0.0, 0.5), "back_gain": (0.1, 12), "share_loss": (0.0, 1.0), "shift_hp": (20, 500), "shift_lp": (800, 14000),
          "forward_hp": (150, 600), "forward_lp": (1000, 16000), "long_gain": (0.01, 3.0), "long_high_scale": (0.2, 1.5),
          "long_rt_scale": (0.5, 4.0), "long_low_scale": (0.3, 4.0), "high_scale": (0.2, 1.5), "mod_scale": (0.2, 8.0),
          "band_hp": (40, 800), "band_q": (0.4, 3.0), "band_lp": (300, 8000), "rt_noon": (0.8, 40), "rt_7_ratio": (0.05, 1.2),
          "rt_5_ratio": (0.9, 4.0), "gain_db": (-50, 10), "energy_7": (-12, 12), "energy_5": (-12, 12),
          "rt_9_ratio": (0.05, 1.2), "rt_3_ratio": (0.9, 4.0), "energy_9": (-12, 12), "energy_3": (-12, 12),
          "back_7": (0.02, 1.5), "back_9": (0.02, 1.5), "back_3": (0.5, 3.0), "back_5": (0.5, 3.0)}


def power_mean(a, b):
    return 10 * np.log10((10 ** (np.asarray(a) / 10) + 10 ** (np.asarray(b) / 10)) / 2)


def clock_fraction(position):
    return rv.clock_fraction(position)


def takes():
    """The session's Shimmer takes this fit uses: held single notes and the plucks."""
    meta = {p.stem: json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.stem != "rig"}
    used = {}
    for name, m in meta.items():
        spec = m["signal"]
        if m["settings"].get("Mode") != "Shimmer":
            continue
        if spec["type"] == "sine" or name == "shimmer-plucks":
            used[name] = m
    return used


def tables(p):
    fn, f5, sn, s5 = p["forward_noon"], p["forward_5"], p["share_noon"], p["share_5"]
    rn = p["rt_noon"]
    r7, r5 = rn * p["rt_7_ratio"], rn * p["rt_5_ratio"]
    nine = r7 * PEDAL_RT_LAW[1] / PEDAL_RT_LAW[0]
    three = rn * (r5 / rn) ** (math.log(PEDAL_RT_LAW[3] / PEDAL_RT_LAW[2]) /
                               math.log(PEDAL_RT_LAW[4] / PEDAL_RT_LAW[2]))
    if "rt_9_ratio" in p:
        nine, three = rn * p["rt_9_ratio"], rn * p["rt_3_ratio"]
    e7, e5 = p["energy_7"], p["energy_5"]
    # Tone 9:00: a share of noon's (0.4 until check-a's take set it: p["forward_9"], p["share_9"]).
    return {"forward": [0.0, p.get("forward_9", 0.4) * fn, fn, fn + 0.6 * (f5 - fn), f5],
            "share": [0.0, p.get("share_9", 0.4) * sn, sn, sn + 0.6 * (s5 - sn), s5],
            "rt60": [r7, nine, rn, three, r5],
            "energy": [e7, p.get("energy_9", 0.6 * e7), 0.0, p.get("energy_3", 0.6 * e5), e5],
            "back_at": [p.get("back_7", 1.0), p.get("back_9", 1.0), 1.0, p.get("back_3", 1.0), p.get("back_5", 1.0)]}


def overrides(p, block_ms, delay_ms, grain_ms=0.0):
    t = tables(p)
    out = [f"{k}={','.join(f'{v:.6g}' for v in t[k])}" for k in ("forward", "share", "rt60", "energy", "back_at")]
    out += [f"{k}={p[k]:.6g}" for k in ("back_gain", "share_loss", "shift_hp", "shift_lp", "forward_hp",
                                         "forward_lp", "band_hp", "band_q", "band_lp", "long_gain",
                                         "gain_db")]
    out += [f"{k}={p.get(k, 1.0):.6g}" for k in SCALES]
    out += [f"{k}={p.get(k, d):.6g}" for k, d in FIXED.items()]
    return out + [f"block_ms={block_ms:g}", f"forward_delay_ms={delay_ms:g}", f"grain_ms={grain_ms:g}"]


def render_take(binary, audio, name, m, args):
    played, _ = sf.read(audio / "played" / f"{name}.wav", dtype="float32")
    x = played.copy()
    x[:m["signal_start"]] = 0.0  # the marker went to the loopback only
    s = m["settings"]
    knobs = [str(SHIMMER_MODE)] + [f"{clock_fraction(s[k]):.6g}" for k in ("Time", "Tone", "Level")]
    out = subprocess.run([str(binary), *knobs, *args], input=x.tobytes(), capture_output=True, check=True).stdout
    y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
    dry = x.astype(float)
    offset = m["signal_start"] / FS
    return [(rv.bandlimit(y[:, ch] - dry), rv.bandlimit(dry), offset) for ch in (0, 1)]


THIRD_HZ = 1000 * 2 ** (np.arange(-11, 11) / 3)  # 80 Hz .. 10 kHz
HELD_WINDOWS = [(0.5, 4.0), (4.3, 5.5), (5.5, 7.5), (7.5, 10.0)]
PLUCK_WINDOWS = [(0.0, 1.0), (1.0, 3.0), (3.0, 6.0), (6.0, 10.0)]
SPREAD_F0 = {"shimmer-tone-0082": 82.41, "shimmer-tone-0165": 164.81, "shimmer-tone-0330": 329.63,
             "shimmer-tone-0659": 659.26, "shimmer-tone-1319": 1318.51}


def third_levels(v, a, b):
    """Power per third octave of v[a:b] (samples), dB."""
    from scipy import signal
    f, power = signal.welch(v[a:b], FS, nperseg=4096)
    return np.array([10 * np.log10(np.sum(power[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))]) + 1e-30)
                     for c in THIRD_HZ])


def octave_spread(wet, offset, f):
    """dB of the line's energy within ±0.3 % of f, re ±6 % around it, over the held part."""
    from scipy import signal
    seg = wet[int((offset + 0.8) * FS):int((offset + 3.9) * FS)]
    s = np.abs(np.fft.rfft(seg * signal.windows.blackmanharris(len(seg)), 8 * len(seg))) ** 2
    fr = np.fft.rfftfreq(8 * len(seg), 1 / FS)
    return float(10 * np.log10(np.sum(s[np.abs(fr / f - 1) < 0.003]) / np.sum(s[np.abs(fr / f - 1) < 0.06])))


def chord_lines(outs, spec):
    """A chord's or a fifth's lines, each note's octaves re that note's dry: {Hz: [held dB
    (A and B in power), floor dB]}; a frequency two notes share counts once (the lower note's)."""
    lines = {}
    for f0 in sorted(spec["freq"]):
        a, b = (rv.note_lines(*parts, spec["seconds"], f0)["lines"] for parts in outs)
        for k, line in a.items():
            hz = round(float(k[:-1]) * f0)
            if all(abs(hz - other) > 2 for other in lines):
                lines[hz] = [float(power_mean(line["steady_db"], b[k]["steady_db"])),
                             max(line["floor_db"], b[k]["floor_db"])]
    return lines


def analyse(name, m, outs):
    """Everything compared for one take, from its outputs' (wet, dry, offset)."""
    spec = m["signal"]
    result = {}
    if name == "shimmer-plucks":
        for out, parts in zip("AB", outs):
            result[out] = rv.pluck_lines_of(*parts, PLUCK_F0, 0.0)
        windows, note = PLUCK_WINDOWS, (0.0, 1.0)
    else:
        if not isinstance(spec["freq"], list):
            for out, parts in zip("AB", outs):
                result[out] = rv.note_lines(*parts, spec["seconds"], spec["freq"])
        else:
            result["chord_lines"] = chord_lines(outs, spec)
        windows, note = HELD_WINDOWS, (0.5, 4.0)
    offset = outs[0][2]
    o = int(offset * FS)
    reference = max(np.max(third_levels(dry, o + int(note[0] * FS), o + int(note[1] * FS))) for _, dry, _ in outs)

    def mean_levels(a, b):
        return 10 * np.log10(np.mean([10 ** (third_levels(wet, a, b) / 10) for wet, _, _ in outs], axis=0))

    result["spectra"] = [mean_levels(o + int(a * FS), o + int(b * FS)) - reference for a, b in windows]
    result["floor"] = mean_levels(int(0.05 * FS), o - int(0.05 * FS)) - reference
    if name in SPREAD_F0:
        f0 = SPREAD_F0[name]
        result["spread"] = {k: float(np.mean([octave_spread(wet, off, k * f0) for wet, _, off in outs]))
                            for k in (2, 4)}
    return result


def measure_take(binary, audio, name, m, args):
    return analyse(name, m, render_take(binary, audio, name, m, args))


def measure_all(binary, audio, used, args):
    with ThreadPoolExecutor(max_workers=len(used)) as pool:
        futures = {name: pool.submit(measure_take, binary, audio, name, m, args) for name, m in used.items()}
        return {name: f.result() for name, f in futures.items()}


def pedal_numbers(audio, used):
    """The pedal measured by the same code, from the session's recordings."""
    session = rv.Session(SESSION, audio)
    return {name: analyse(name, m, [rv.take_parts(session, name, ch) for ch in (0, 2)])
            for name, m in used.items()}


def compare_spectra(p, q, residual, rows):
    """Third octaves per window: where the pedal is 6 dB over its floor, the difference;
    where it isn't, NAM-X pays only for sounding 6 dB over that floor."""
    errors = []
    for i, (a, b) in enumerate(zip(p["spectra"], q["spectra"])):
        heard = a > p["floor"] + 6
        d = np.where(heard, b - a, np.maximum(b - (p["floor"] + 6), 0.0))
        residual.extend(d / 3 / math.sqrt(len(d) / 5))
        errors.append(round(float(np.sqrt(np.mean(d ** 2))), 1))
    rows["spectra_rms_db"] = errors


# What a player plays counts more than the single sines: the chord's and the plucks' spectra
# say what is heard (the sines sample a tank's irregular response one point at a time).
TAKE_WEIGHT = {"shimmer-chord": 2.0, "shimmer-fifth": 1.5, "shimmer-plucks": 2.0}


def compare(pedal, model, used, details=False):
    """The residual vector (dB, weighted) and, with details, a per-take table."""
    residual, table = [], {}
    for name, m in used.items():
        weight_from = len(residual)
        compare_take(name, m, pedal[name], model[name], residual, table)
        residual[weight_from:] = [TAKE_WEIGHT.get(name, 1.0) * r for r in residual[weight_from:]]
    residual = np.array(residual)
    return (residual, table) if details else residual


def compare_take(name, m, p, q, residual, table):
    """One take's residual entries (appended) and table rows."""
    rows = {}
    if name == "shimmer-plucks":
        compare_spectra(p, q, residual, rows)
        for k in ("1x", "2x", "4x", "8x"):
            a = power_mean(p["A"]["lines"][k]["trajectory_100ms"], p["B"]["lines"][k]["trajectory_100ms"])
            b = power_mean(q["A"]["lines"][k]["trajectory_100ms"], q["B"]["lines"][k]["trajectory_100ms"])
            n = min(len(a), len(b), 42)  # -0.2 to 4 s
            keep = a[:n] > np.max(a) - 40
            d = (b[:n] - a[:n])[keep]
            residual.extend(d / 3 / math.sqrt(max(1, len(d)) / 5))
            # The peak itself (the low E's octave), weighted: one number against many.
            residual.append(2 * float(np.max(b[:n]) - np.max(a)))
            rows[k] = {"pedal_peak_db": float(np.max(a)), "model_peak_db": float(np.max(b[:n])),
                       "rms_db": float(np.sqrt(np.mean(d ** 2))) if len(d) else None}
        table[name] = rows
        return
    compare_spectra(p, q, residual, rows)
    if "A" not in p:  # the chord and the fifth: spectra and each note's lines
        for hz, (held, floor) in p["chord_lines"].items():
            model_held = q["chord_lines"][hz][0]
            if held > floor + 20:
                residual.append((model_held - held) / 2)
                rows[f"{hz} Hz"] = [round(held, 1), round(model_held, 1)]
        table[name] = rows
        return
    seconds = m["signal"]["seconds"]
    for k, pa in p["A"]["lines"].items():
        pb = p["B"]["lines"][k]
        ma, mb = q["A"]["lines"][k], q["B"]["lines"][k]
        floor = max(pa["floor_db"], pb["floor_db"])
        held = float(power_mean(pa["steady_db"], pb["steady_db"]))
        model_held = float(power_mean(ma["steady_db"], mb["steady_db"]))
        row = {"pedal_db": round(held, 1), "model_db": round(model_held, 1)}
        if held > floor + 20:
            residual.append(model_held - held)
            tp = power_mean(pa["trajectory_100ms"], pb["trajectory_100ms"]) - held
            tm = power_mean(ma["trajectory_100ms"], mb["trajectory_100ms"]) - model_held
            t = -0.2 + 0.1 * np.arange(min(len(tp), len(tm)))
            span = (t >= seconds + 0.2) & (t <= seconds + 2.5) & (tp[:len(t)] > floor + 10 - held)
            d = (tm[:len(t)] - tp[:len(t)])[span]
            if len(d):
                residual.extend(d / 4 / math.sqrt(len(d) / 5))
                row["decay_rms_db"] = round(float(np.sqrt(np.mean(d ** 2))), 1)
            for side, a, b in (("A", pa, ma), ("B", pb, mb)):
                if a.get("onset_ms") is not None and b.get("onset_ms") is not None:
                    row[f"onset_{side}"] = [round(a["onset_ms"]), round(b["onset_ms"])]
        else:  # an octave the pedal hasn't (an entry either way: the vector keeps its length)
            residual.append(max(model_held - floor - 10, 0.0) / 2)
        rows[k] = row
    if "spread" in p:
        for k in (2, 4):
            residual.append((q["spread"][k] - p["spread"][k]) / 4)
            rows[f"spread_{k}x"] = [round(p["spread"][k], 1), round(q["spread"][k], 1)]
    table[name] = rows


# --- The focused session (2026-11-15): bursts and stabs ---

FOCUS_BURSTS = ["noon", "time-0700", "time-0900", "time-0300", "time-0500", "tone-0700", "tone-0300", "tone-0500",
                "tone-0700-time-0700", "tone-0700-time-0500", "tone-0500-time-0700", "tone-0500-time-0500"]
FOCUS_STABS = ["noon-guitar", "time-0700", "time-0300", "tone-0700", "tone-0500"]
FOCUS_CHECKS = ["check-a", "check-b"]  # in no fit
FOCUS_BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]
FOCUS_STEP = FS // 10
FOCUS_WEIGHT = {"burst": 1.0, "stabs": 1.5}


def focus_modules():
    """rv6/focused.py and rv6/takes.py (they read the session relative to the repository root)."""
    folder = str(Path(__file__).resolve().parent / "rv6")
    if folder not in sys.path:
        sys.path.insert(0, folder)
    import focused as scorer
    import takes as session
    return scorer, session


def focus_levels(pair, at, length):
    """Per octave band, the two outputs' mean power in 100 ms steps from `at`: bands x steps."""
    scorer, _ = focus_modules()
    rows = []
    for centre in FOCUS_BANDS:
        power = 0.0
        for x in pair:
            y = scorer.octave(x[at - FS // 4:at + length], centre)[FS // 4:]
            n = len(y) // FOCUS_STEP
            power = power + np.mean(y[:n * FOCUS_STEP].reshape(n, FOCUS_STEP) ** 2, axis=1)
        rows.append(0.5 * power)
    return np.array(rows)


def focus_targets(names=None):
    """{key: the pedal's levels, its floor per band and the cells compared} for the session's
    Shimmer bursts and stabs."""
    scorer, session = focus_modules()
    wanted = [(f"{n}-shimmer", "burst") for n in (names or FOCUS_BURSTS)] + \
             ([] if names else [(f"{n}-shimmer", "stabs") for n in FOCUS_STABS])
    out = {}
    for name, kind in wanted:
        meta, *_ = session.load(name)
        for part, start, body, tail in scorer.parts_of(meta):
            if kind == "burst" and part["type"] == "noise-steps":
                step, gap = int(part["step_seconds"] * FS), int(part.get("gap_seconds", 0) * FS)
                at = start + part["levels_db"].index(-12) * (step + gap)
                length = min(gap or tail, 8 * FS)
            elif kind == "stabs" and part["type"] == "file" and "stabs" in part["path"]:
                at, length = start, body + min(tail, 6 * FS)
            else:
                continue
            pedal = session.wet(name)
            levels = focus_levels(pedal, at, length)
            floors = np.array([np.mean(scorer.octave(pedal[0][at - FS // 2:at - FS // 20], c)[FS // 10:] ** 2)
                               for c in FOCUS_BANDS])
            keep = levels > 4 * floors[:, None]
            if kind == "burst":
                keep[:, 0] = False  # the burst itself: the dry's leftovers
            out[f"{name}/{kind}"] = {"name": name, "kind": kind, "at": at, "length": length, "levels": levels,
                                     "floors": floors, "keep": keep}
            break
    return out


def focus_model(binary, target, args):
    """NAM-X's levels for a target's stretch: rendered from the played signal from half a second
    before it (silence before), its wet through the session's dry path, as rv6/takes.py."""
    _, session = focus_modules()
    meta, played, *_ = session.load(target["name"])
    s = meta["settings"]
    knobs = [str(SHIMMER_MODE)] + [f"{session.clock_fraction(s[k]):.4f}" for k in ("Time", "Tone", "E.Level")]
    lo = target["at"] - FS // 2
    fed = played[lo:target["at"] + target["length"] + FS // 4]
    out = subprocess.run([str(binary), *knobs, *args], input=fed.astype(np.float32).tobytes(), capture_output=True,
                         check=True).stdout
    y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
    ha, hb, offset = session.dry_filters()
    pair = [rv_shift(session.through(h, y[:, c] - fed), meta["offset"] - offset) for c, h in ((0, ha), (1, hb))]
    return focus_levels(pair, target["at"] - lo, target["length"])


def rv_shift(x, samples):
    import scorecard
    return scorecard.shift(x, samples)


def focus_differences(binary, targets, args):
    """{key: NAM-X's level less the pedal's per band and step, dB (the pedal's floor under NAM-X)}."""
    with ThreadPoolExecutor(max_workers=8) as pool:
        models = dict(zip(targets, pool.map(lambda t: focus_model(binary, t, args), targets.values())))
    return {key: 10 * np.log10((models[key] + t["floors"][:, None]) / t["levels"]) for key, t in targets.items()}


def focus_residual(binary, targets, args, details=False):
    """The focused targets' residual entries: every step of every band (the pedal's floor is under
    NAM-X, so where neither sounds the difference is nothing, and where NAM-X sounds on after the
    pedal has gone under its floor it pays: a first version compared only the steps where the pedal
    stood over its floor, and let NAM-X's top octaves hang a second after each stab unseen). Each
    band of each stretch is scaled as five numbers over the steps the pedal sounds in."""
    residual, table = [], {}
    for key, d in focus_differences(binary, targets, args).items():
        t = targets[key]
        heard, over, per_band = [], [], []
        for band in range(len(FOCUS_BANDS)):
            cells = d[band].copy()
            if t["kind"] == "burst":
                cells[0] = 0.0  # the burst itself: the dry's leftovers
            sounding = max(int(t["keep"][band].sum()), 10)
            residual.extend(FOCUS_WEIGHT[t["kind"]] * cells / 3 / math.sqrt(sounding / 5))
            heard.extend(cells[t["keep"][band]])
            silent = cells[~t["keep"][band]]
            over.extend(silent[silent > 3])
            per_band.append(round(float(np.sqrt(np.sum(cells ** 2) / sounding)), 1))
        table[key] = {"rms_db": round(float(np.sqrt(np.mean(np.square(heard)))), 2), "mean_db": round(float(np.mean(heard)), 2),
                      "over_silence_steps": len(over), "over_silence_db": round(float(np.mean(over)), 1) if over else 0.0,
                      "per_band": per_band}
    return (np.array(residual), table) if details else np.array(residual)


def to_vector(p, names=NAMES):
    return np.array([math.log(p[k]) if k in LOG else p[k] for k in names])


def from_vector(v, names=NAMES):
    return {k: (math.exp(x) if k in LOG else float(x)) for k, x in zip(names, v)}


def bounds(names=NAMES):
    lo = [math.log(BOUNDS[k][0]) if k in LOG else BOUNDS[k][0] for k in names]
    hi = [math.log(BOUNDS[k][1]) if k in LOG else BOUNDS[k][1] for k in names]
    return np.array(lo), np.array(hi)


FIGURE_TAKES = ["shimmer-tone-0082", "shimmer-tone-0165", "shimmer-tone-0330", "shimmer-tone-0659",
                "shimmer-tone-1319", "shimmer-tone-2637", "shimmer-tone-0330-tone-0700",
                "shimmer-tone-0330-tone-0500", "shimmer-tone-0330-time-0700", "shimmer-tone-0330-time-0500"]


def figure(path, pedal, model, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    colours = {"1x": "black", "2x": "tab:blue", "4x": "tab:orange", "8x": "tab:green", "16x": "tab:red"}
    fig, axes = plt.subplots(2, 5, figsize=(22, 8.5), sharey=True)
    for ax, name in zip(axes.flat, FIGURE_TAKES):
        for k, colour in colours.items():
            if k not in pedal[name]["A"]["lines"]:
                continue
            for source, width in ((pedal[name], 0.7), (model[name], 1.8)):
                y = power_mean(source["A"]["lines"][k]["trajectory_100ms"], source["B"]["lines"][k]["trajectory_100ms"])
                ax.plot(-0.2 + 0.1 * np.arange(len(y)), y, color=colour, lw=width,
                        label=k if width > 1 else None)
        ax.axvspan(0, 4, color="0.93")
        ax.set_title(name.replace("shimmer-tone-", "") + " (pedal thin, NAM-X thick)", fontsize=9)
        ax.set_ylim(-90, 15)
        ax.set_xlim(-0.2, 12)
        ax.set_xlabel("s")
        ax.grid(alpha=0.3)
    axes[0, 0].set_ylabel("dB re the dry note, A and B in power")
    axes[0, 0].legend(fontsize=8)
    fig.suptitle(title)
    rv.save_figure(fig, path.parent, path.stem)


# --- The header ---

# ShimmerShape's fields after the forward and share tables, in its order.
SHAPE_FIELDS = ["back_gain", "shift_hp", "shift_lp", "band_hp", "band_q", "band_lp", "block_ms", "delay_ms",
                "share_loss", "forward_hp", "forward_lp", "long_gain", "grain_ms", *SCALES, *FIXED]


def read_shape(focus=False):
    text = HEADER.read_text()
    block = text[text.index("// BEGIN FITTED SHIMMER"):text.index("// END FITTED SHIMMER")]
    v = fit_rv6.numbers(block[block.index("SHIMMER{"):])
    forward, share = v[0:5], v[5:10]
    shape = dict(zip(SHAPE_FIELDS, v[10:]))
    back_at = v[10 + len(SHAPE_FIELDS):10 + len(SHAPE_FIELDS) + 5]
    back_at = back_at if len(back_at) == 5 else [1.0] * 5
    _, _, _, voicings = fit_rv6.read_block()
    s = voicings[SHIMMER_MODE]
    p = {"forward_noon": forward[2] or 0.02, "forward_5": forward[4] or 0.02, "share_noon": share[2],
         "share_5": share[4],
         **{k: shape[k] for k in ("back_gain", "share_loss", "shift_hp", "shift_lp", "forward_hp", "forward_lp",
                                  "band_hp", "band_q", "band_lp")},
         "long_gain": max(shape["long_gain"], 0.01), **{k: shape.get(k, 1.0) for k in SCALES},
         **{k: shape.get(k, d) for k, d in FIXED.items()},
         "rt_noon": s["rt"][2], "rt_7_ratio": s["rt"][0] / s["rt"][2], "rt_5_ratio": s["rt"][4] / s["rt"][2],
         "gain_db": s["gain"], "energy_7": s["energy"][0], "energy_5": s["energy"][4]}
    if focus:
        p.update({"rt_9_ratio": s["rt"][1] / s["rt"][2], "rt_3_ratio": s["rt"][3] / s["rt"][2],
                  "energy_9": s["energy"][1], "energy_3": s["energy"][3]})
    if focus or back_at != [1.0] * 5:
        p.update({"back_7": back_at[0], "back_9": back_at[1], "back_3": back_at[3], "back_5": back_at[4]})
    if forward[2] > 0 and share[2] > 0:
        p.update({"forward_9": forward[1] / forward[2], "share_9": share[1] / share[2]})
    return p, shape["block_ms"], shape["delay_ms"], shape["grain_ms"]


def write_shape(p, block_ms, delay_ms, grain_ms=0.0):
    t = tables(p)
    f = fit_rv6.f
    text, begin, end, voicings = fit_rv6.read_block()
    v = voicings[SHIMMER_MODE]
    v["rt"], v["energy"], v["gain"], v["eq"] = t["rt60"], t["energy"], p["gain_db"], []
    fit_rv6.write_block(text, begin, end, voicings)
    text = HEADER.read_text()
    begin, end = text.index("// BEGIN FITTED SHIMMER"), text.index("// END FITTED SHIMMER")
    given = {"block_ms": block_ms, "delay_ms": delay_ms, "grain_ms": grain_ms}
    defaults = {**{k: 1.0 for k in SCALES}, **FIXED}
    fields = [given[k] if k in given else p.get(k, defaults[k]) if k in defaults else p[k] for k in SHAPE_FIELDS]
    body = (f"// BEGIN FITTED SHIMMER (tools/capture/fit_rv6_shimmer.py --write rewrites this block)\n"
            f"constexpr ShimmerShape SHIMMER{{{{{', '.join(map(f, t['forward']))}}}, "
            f"{{{', '.join(map(f, t['share']))}}}, " + ", ".join(map(f, fields))
            + f", {{{', '.join(map(f, t['back_at']))}}}}};\n")
    HEADER.write_text(text[:begin] + body + text[end:])


DIFF_STEP = 0.03
JACOBIAN_WORKERS = int(os.environ.get("FIT_WORKERS", "4"))  # whole evaluations at once: one alone leaves most cores waiting on its longest take


def fit(binary, audio, used, pedal, start, block_ms, delay_ms, grain_ms, max_nfev, focus=None):
    names = NAMES + (FOCUS_NAMES if focus else [])
    lo, hi = bounds(names)
    calls = [0]
    last = {}
    fixed = {k: start.get(k, d) for k, d in FIXED.items()}

    def full(v):
        return {**from_vector(v, names), **fixed}

    def both(v):
        args = overrides(full(v), block_ms, delay_ms, grain_ms)
        r = compare(pedal, measure_all(binary, audio, used, args), used)
        return np.concatenate([r, focus_residual(binary, focus, args)]) if focus else r

    def residual(v):
        calls[0] += 1
        r = both(v)
        last["x"], last["f"] = v.copy(), r
        print(f"  evaluation {calls[0]}: rms {float(np.sqrt(np.mean(r ** 2))):.3f}", flush=True)
        return r

    def jacobian(v):
        """Forward differences as scipy's '2-point' takes them, the columns evaluated in parallel."""
        f0 = last["f"] if "x" in last and np.array_equal(last["x"], v) else residual(v)
        h = DIFF_STEP * np.where(v >= 0, 1.0, -1.0) * np.maximum(1.0, np.abs(v))
        h = np.where((v + h > hi) | (v + h < lo), -h, h)

        def column(i):
            x = v.copy()
            x[i] += h[i]
            return (both(x) - f0) / h[i]

        with ThreadPoolExecutor(max_workers=JACOBIAN_WORKERS) as pool:
            columns = list(pool.map(column, range(len(v))))
        calls[0] += len(v)
        return np.column_stack(columns)

    x0 = np.clip(to_vector(start, names), lo + 1e-6, hi - 1e-6)
    result = optimize.least_squares(residual, x0, jac=jacobian, bounds=(lo, hi), loss="soft_l1", f_scale=3.0,
                                    max_nfev=max_nfev, verbose=0)
    return full(result.x), float(np.sqrt(np.mean(result.fun ** 2))), calls[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--render", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--block-ms", type=float)
    parser.add_argument("--grain-ms", type=float, nargs="+")
    parser.add_argument("--forward-delay-ms", type=float, nargs="+")
    parser.add_argument("--max-nfev", type=int, default=40)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--evaluate", action="store_true")
    parser.add_argument("--focused", action="store_true", help="add the focused session's bursts and stabs")
    parser.add_argument("--report", type=Path, help="write the comparison here (JSON)")
    parser.add_argument("--figure", type=Path, help="draw the best fit against the pedal here (SVG)")
    args = parser.parse_args()
    used = takes()
    pedal = pedal_numbers(args.audio, used)
    start, header_block, header_delay, header_grain = read_shape(args.focused)
    focus = focus_targets() if args.focused else None
    checks = focus_targets(FOCUS_CHECKS) if args.focused else None
    block_ms = args.block_ms or header_block
    results = {}
    for grain_ms in (args.grain_ms or [header_grain]):
        for delay_ms in (args.forward_delay_ms or [header_delay]):
            if args.evaluate:
                p, calls = start, 0
            else:
                p, _, calls = fit(args.render, args.audio, used, pedal, start, block_ms, delay_ms, grain_ms,
                                  args.max_nfev, focus)
            model = measure_all(args.render, args.audio, used, overrides(p, block_ms, delay_ms, grain_ms))
            r, table = compare(pedal, model, used, True)
            rms = float(np.sqrt(np.mean(r ** 2)))
            results[(block_ms, delay_ms, grain_ms)] = {"block_ms": block_ms, "forward_delay_ms": delay_ms,
                                                       "grain_ms": grain_ms, "params": p, "tables": tables(p),
                                                       "residual_rms": rms, "evaluations": calls, "takes": table}
            print(f"grain {grain_ms:g} ms (block {block_ms:g}), forward delay {delay_ms:g} ms: residual rms "
                  f"{rms:.3f} ({calls} evaluations)", flush=True)
            print("  " + ", ".join(f"{k} {v:.4g}" for k, v in p.items()), flush=True)
            if focus:
                shimmer_args = overrides(p, block_ms, delay_ms, grain_ms)
                focus_r, focus_table = focus_residual(args.render, focus, shimmer_args, True)
                _, check_table = focus_residual(args.render, checks, shimmer_args, True)
                results[(block_ms, delay_ms, grain_ms)]["focused"] = focus_table
                results[(block_ms, delay_ms, grain_ms)]["focused_checks"] = check_table
                print(f"  the focused session's bursts and stabs: residual rms {float(np.sqrt(np.mean(focus_r ** 2))):.3f}; "
                      "levels per stretch, rms (mean) dB:")
                print(f"    {'':44s} where the pedal sounds | NAM-X alone over the pedal's floor: steps, mean dB | per band "
                      + "/".join(str(b) for b in FOCUS_BANDS) + " Hz, all steps")
                for key, row in {**focus_table, **{k + " (check)": v for k, v in check_table.items()}}.items():
                    print(f"    {key:44s} {row['rms_db']:5.2f} ({row['mean_db']:+.2f}) | {row['over_silence_steps']:4d} {row['over_silence_db']:5.1f} | "
                          + " ".join(f"{v:4.1f}" for v in row["per_band"]), flush=True)
    best = min(results, key=lambda b: results[b]["residual_rms"])
    if args.figure:
        model = measure_all(args.render, args.audio, used, overrides(results[best]["params"], *best))
        figure(args.figure, pedal, model, f"RV-6 Shimmer against NAM-X's (grain {best[2]:g} ms, forward delay "
                                          f"{best[1]:g} ms): residual {results[best]['residual_rms']:.2f}")
    if args.report:
        args.report.write_text(json.dumps(list(results.values()), indent=1) + "\n")
    if args.write and not args.evaluate:
        write_shape(results[best]["params"], *best)
        print(f"Wrote the fit at grain {best[2]:g} ms, forward delay {best[1]:g} ms into {HEADER}")


if __name__ == "__main__":
    main()
