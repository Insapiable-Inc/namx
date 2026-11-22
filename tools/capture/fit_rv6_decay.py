#!/usr/bin/env python3
"""Fit the RV-6 tails' finer shape (src/dsp/Rv6Reverb.h: the DECAYS block) by simulation.

    python -I tools/capture/fit_rv6_decay.py --render BINARY --audio FIRST_WAVS --followup-audio SECOND_WAVS
        [--modes room hall plate modulate] [--max-nfev N] [--write] [--report JSON]

BINARY is tools/capture/render_rv6.cpp built against the current header; the WAV directories hold
the 2026-10-31 sessions' recordings (captures/boss/rv-6/2026-10-31 and -followup: only the
sidecars are in git). The pedal's decay bends — each 10 dB takes longer than the last — where one
tank's is straight; DecayShape adds a second, slower tank under the first, scales the tank's own
low and high RT ratios and gives it a band of its own (Rv6Reverb.h). Per mode, the pedal's
octave bands (125 Hz-4 kHz; 2.85-3.1 kHz notched out, the pedal's whine) in 100 ms steps, from
the end of the measured early response (Room 350 ms, Hall and Plate 450, Modulate 100) to where
the band comes within 10 dB of the pedal's noise floor (its last second), at every Time position
the sessions have — the sweeps at noon and, in the follow-up, at 7:00, 9:00, 3:00 and 5:00
(Modulate: noon's sweep only; its noise bursts don't deconvolve, the tank moves) — against
NAM-X's (an impulse through render_rv6 with decay.*= overrides): the difference in dB, every
band and position weighted alike. Free: DecayShape's nine values; the voicing (RT60 table, EQ,
gain) stays as fit_rv6.py left it, so the two are run in turn (then rv6_early.py: its Time fit
depends on the tank). --write rewrites the DECAYS block (then run clang-format on the header).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
import fit_rv6  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
HEADER = REPOSITORY / "src/dsp/Rv6Reverb.h"
STUDY = REPOSITORY / "captures/boss/rv-6"
FS = rv.FS
MODES = ["modulate", "spring", "plate", "hall", "room", "shimmer"]  # Mode order
START_S = {"room": 0.35, "hall": 0.45, "plate": 0.45, "modulate": 0.1}  # after the measured start
BANDS = [125, 250, 500, 1000, 2000, 4000]
STEP_S = 0.1
FIELDS = ["low_scale", "high_scale", "mid_hz", "mid_q", "mid_ratio", "slow_rt", "slow_gain", "slow_low_scale",
          "slow_high_scale"]
DEFAULT = {"low_scale": 1.0, "high_scale": 1.0, "mid_hz": 250.0, "mid_q": 1.0, "mid_ratio": 1.0, "slow_rt": 1.0,
           "slow_gain": 0.0, "slow_low_scale": 1.0, "slow_high_scale": 1.0, "slow_at": [1.0] * 5}
# slow_at (the slower tank's level per Time position) is set by hand, not fitted. Fading Plate's out
# at 7:00 and 9:00 (where the pedal's Plate decays straight) broke the measured start there: its
# kernel takes the tanks off as they are at noon, the slower one included.
START = {**DEFAULT, "mid_ratio": 0.97, "slow_rt": 1.6, "slow_gain": 0.25}
BOUNDS = {"low_scale": (0.5, 2.0), "high_scale": (0.5, 2.0), "mid_hz": (100.0, 2000.0), "mid_q": (0.5, 1.5),
          "mid_ratio": (0.5, 2.0), "slow_rt": (1.02, 4.0), "slow_gain": (0.003, 1.5), "slow_low_scale": (0.5, 2.0),
          "slow_high_scale": (0.5, 2.0)}  # all fitted in logs; the band broad (Q <= 1.5): the pedal's
# decay changes over octaves, and a narrow dip on one note (a fit once put one on each of the
# plucks' notes) would make that note die early in play.
PLUCK_WEIGHT = 0.0  # the plucks are pure sines: each samples a few modes; reported, not fitted
HELD_WEIGHT = 1.0  # held notes (--held): the Schroeder curves average energy, so a fit on them alone
# let the typical note die ~1 dB fast (Plate) while their energy matched


def positions(mode):
    """(Time position, session, take) per measured position."""
    if mode == "modulate":  # its noise bursts at 7:00 and 5:00 don't deconvolve (a moving tank)
        return [("12:00", "first", "modulate-sweep")]
    out = [("12:00", "first", f"{mode}-sweep")]
    for pos in ("7:00", "9:00", "3:00", "5:00"):
        out.append((pos, "second", f"{mode}-sweep-time-{pos.replace(':', '').zfill(4)}"))
    return out


BIN_S = 0.01


def band_bins(irs, n):
    """Per octave band (rows = BANDS), the energy (A and B averaged) in BIN_S bins."""
    step = int(BIN_S * FS)
    count = n // step
    f = np.fft.rfftfreq(n, 1 / FS)
    spectra = [np.fft.rfft(rv.notch(h[:n]), n) for h in irs]
    out = []
    for fc in BANDS:
        mask = (f >= fc / math.sqrt(2)) & (f < fc * math.sqrt(2))
        e = np.mean([np.fft.irfft(s * mask, n) ** 2 for s in spectra], axis=0)
        out.append(e[:count * step].reshape(count, step).sum(1))
    return np.array(out)


def schroeder_db(bins, end):
    """The energy left after each bin up to `end` (Schroeder's backward integral), dB."""
    left = np.cumsum(bins[:end][::-1])[::-1]
    return 10 * np.log10(left + 1e-30)


def pedal_targets(sessions, mode):
    """Per position: its knob fraction, the IR length rendered, and per band the Schroeder curve's
    steps compared (from START_S while it falls its first 40 dB, and no further than 80 % of the
    way to where the band meets twice the pedal's noise floor, the last second's mean, which is
    subtracted) with the pedal's curve there."""
    targets = []
    step = int(round(STEP_S / BIN_S))
    for pos, which, take in positions(mode):
        s = sessions[which]
        irs = [s.wet_ir(take, ch) for ch in (0, 2)]
        n = min(len(irs[0]), int(20 * FS))
        bins = band_bins(irs, n)
        first = int(round(START_S[mode] / BIN_S))
        rows = []
        for b, row in enumerate(bins):
            floor = np.mean(row[-int(1.0 / BIN_S):])
            smooth = np.convolve(row, np.ones(5) / 5, "same")
            peak = int(np.argmax(smooth))
            low = np.nonzero(smooth[peak:] < 2 * floor)[0]
            end = peak + int(low[0]) if len(low) else len(row) - int(1.0 / BIN_S)
            if end - first < 30:
                continue
            curve = schroeder_db(np.maximum(row - floor, 0.0), end)
            drop = np.nonzero(curve[first:] < curve[first] - 40)[0]
            last = min(first + (int(drop[0]) if len(drop) else end - first), first + int(0.8 * (end - first)))
            steps = np.arange(first, last, step)
            if len(steps) >= 3:
                rows.append((b, end, steps, curve[steps]))
        targets.append({"pos": pos, "fraction": rv.clock_fraction(pos), "n": n, "rows": rows})
    return targets


def pluck_targets(sessions, mode):
    """The plucks take (noon; decaying sines at 82, 110, 196 and 330 Hz, 3 s apart): the dry at A
    and B, and per note the wet's level in a third octave around it, in 0.25 s steps from 1 s
    after it until 0.5 s after the next (the last: while the pedal is 10 dB over its floor, the
    take's last second), re the level 0.5-1 s after it: how each note's tail decays."""
    s = sessions["first"]
    name = f"{mode}-plucks"
    m = s.meta[name]
    _, y = s.load(name)
    dries = [s.dry_whole(name, ch) for ch in (0, 2)]
    wets = [y[:, ch] - d for ch, d in zip((0, 2), dries)]
    o = m["signal_start"]
    notes = list(zip(m["signal"]["freq"], m["signal"]["onsets"]))
    rows = []
    for k, (ref, row, floor) in enumerate(pluck_levels(wets, o, notes, len(wets[0]))):
        keep = np.nonzero(row < floor + 10)[0]
        last = int(keep[0]) if len(keep) else len(row)
        if last >= 2:
            rows.append((k, last, row[:last] - ref))
    return {"dries": dries, "start": o, "notes": notes, "rows": rows,
            "knobs": [rv.clock_fraction(m["settings"][k]) for k in ("Time", "Tone", "Level")]}


def pluck_levels(wets, start, notes, length):
    """Per note: the level 0.5-1 s after it, its levels in 0.25 s steps from 1 s after it (to
    0.5 s after the next note, or 6 s), and the floor (the take's last second)."""
    n = len(wets[0])
    f = np.fft.rfftfreq(n, 1 / FS)
    spectra = [np.fft.rfft(w) for w in wets]
    quarter = int(0.25 * FS)
    out = []
    for k, (hz, onset) in enumerate(notes):
        mask = (f >= hz * 2 ** (-1 / 6)) & (f < hz * 2 ** (1 / 6))
        e = np.mean([np.fft.irfft(s * mask, n) ** 2 for s in spectra], axis=0)
        at = start + int(onset * FS)
        ref = 10 * np.log10(np.mean(e[at + 2 * quarter:at + 4 * quarter]) + 1e-30)
        until = start + int((notes[k + 1][1] + 0.5) * FS) if k + 1 < len(notes) else min(at + 6 * 4 * quarter, length)
        count = max(0, (until - (at + 4 * quarter)) // quarter)
        steps = [10 * np.log10(np.mean(e[at + (4 + i) * quarter:at + (5 + i) * quarter]) + 1e-30)
                 for i in range(count)]
        floor = 10 * np.log10(np.mean(e[n - 4 * quarter:]) + 1e-30)
        out.append((ref, np.array(steps), floor))
    return out


HELD_T = (0.5, 1.0, 2.0)
HELD_POSITIONS = ("12:00", "3:00", "5:00")


def held_notes(irs, n, noise_from=None):
    """How held notes die: for a sine held and then stopped, the level after the stop is the
    IR's tail from t on seen at its frequency, |FT of h(tau >= t)|^2 re |FT of h|^2. Per octave
    band (125 Hz-1 kHz, A and B averaged per bin), the median over the band's bins at HELD_T.
    With noise_from (s): what the IR's noise alone (from there to the end) gives, re the same
    held level."""
    f = np.fft.rfftfreq(n, 1 / FS)
    held = np.mean([np.abs(np.fft.rfft(h[:n])) ** 2 for h in irs], axis=0)
    out = np.zeros((4, len(HELD_T)))
    for j, t in enumerate(HELD_T):
        k = int((noise_from if noise_from is not None else t) * FS)
        left = np.mean([np.abs(np.fft.rfft(np.r_[np.zeros(k), h[k:n]])) ** 2 for h in irs], axis=0)
        ratio = 10 * np.log10(left / (held + 1e-30) + 1e-30)
        for b, fc in enumerate(BANDS[:4]):
            sel = (f >= fc / math.sqrt(2)) & (f < fc * math.sqrt(2))
            out[b, j] = np.median(ratio[sel])
    return out


def held_targets(sessions, mode):
    """The pedal's held notes at the Time positions long enough for them (noon, 3:00, 5:00), and
    where they stand 10 dB over what its noise alone gives (its IR's last second as a tail)."""
    out = []
    for pos, which, take in positions(mode):
        if pos not in HELD_POSITIONS:
            continue
        irs = [sessions[which].wet_ir(take, ch) for ch in (0, 2)]
        n = min(len(irs[0]), int(20 * FS))
        pedal = held_notes(irs, n)
        noise = held_notes(irs, n, noise_from=(n - FS) / FS)
        out.append({"pos": pos, "fraction": rv.clock_fraction(pos), "n": n, "pedal": pedal,
                    "usable": pedal > noise + 10})
    return out


def render(binary, mode, fraction, n, decay, x=None, knobs=None):
    if x is None:
        x = np.zeros(n + int(0.2 * FS), np.float32)
        x[0] = 1.0
    knobs = knobs or [fraction, 0.5, 0.5]
    args = [str(binary), str(MODES.index(mode))] + [f"{k:.6g}" for k in knobs]
    args += [f"decay.{k}={decay[k]:.6g}" for k in FIELDS]
    args.append("decay.slow_at=" + ",".join(f"{v:.6g}" for v in decay.get("slow_at", DEFAULT["slow_at"])))
    out = subprocess.run(args, input=np.asarray(x, np.float32).tobytes(), capture_output=True, check=True).stdout
    y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
    return y, np.asarray(x, float)


def residual(binary, mode, targets, plucks, decay, details=False, held=None):
    out, table = [], {}
    for h in held or []:
        y, x = render(binary, mode, h["fraction"], h["n"], decay)
        model = held_notes([rv.bandlimit(y[:, c] - x)[:h["n"]] for c in (0, 1)], h["n"])
        d = np.where(h["usable"], model - h["pedal"], 0.0)
        for b in range(d.shape[0]):
            out.extend(HELD_WEIGHT * d[b] / math.sqrt(len(HELD_T)))
        table[f"held {h['pos']}"] = {BANDS[b]: round(float(np.sqrt(np.mean(d[b] ** 2))), 1) for b in range(d.shape[0])}
    for t in targets:
        y, x = render(binary, mode, t["fraction"], t["n"], decay)
        bins = band_bins([rv.bandlimit(y[:, c] - x)[:t["n"]] for c in (0, 1)], t["n"])
        cells = {}
        for b, end, steps, pedal in t["rows"]:
            d = schroeder_db(bins[b], end)[steps] - pedal
            out.extend(d / math.sqrt(len(d)))
            cells[BANDS[b]] = round(float(np.sqrt(np.mean(d ** 2))), 1)
        table[t["pos"]] = cells
    if plucks:
        wets = []
        for ch, dry in enumerate(plucks["dries"]):
            y, x = render(binary, mode, 0, 0, decay, dry, plucks["knobs"])
            wets.append(y[:, ch] - x)
        levels = pluck_levels(wets, plucks["start"], plucks["notes"], len(wets[0]))
        cells = {}
        for k, last, pedal in plucks["rows"]:
            ref, row, _ = levels[k]
            d = (row[:last] - ref) - pedal
            out.extend(PLUCK_WEIGHT * d / math.sqrt(len(d)))
            cells[round(plucks["notes"][k][0])] = round(float(np.sqrt(np.mean(d ** 2))), 1)
        table["plucks"] = cells
    out = np.array(out)
    return (out, table) if details else out


def fit(binary, mode, targets, plucks, start, max_nfev, held=None):
    """least_squares over the shape's values in logs; with the slower tank off (slow_gain 0, as
    Modulate's: a fit found no use for it) its four stay as they are."""
    free = [k for k in FIELDS if not (start["slow_gain"] == 0.0 and k.startswith("slow"))]
    fixed = {k: v for k, v in start.items() if k not in free}

    def to_vector(d):
        return np.array([math.log(d[k]) for k in free])

    def from_vector(v):
        return {**fixed, **{k: math.exp(x) for k, x in zip(free, v)}}

    lo = np.array([math.log(BOUNDS[k][0]) for k in free])
    hi = np.array([math.log(BOUNDS[k][1]) for k in free])
    step = 0.02
    last = {}

    def f(v):
        r = residual(binary, mode, targets, plucks, from_vector(v), held=held)
        last["x"], last["f"] = v.copy(), r
        return r

    def jacobian(v):
        f0 = last["f"] if "x" in last and np.array_equal(last["x"], v) else f(v)
        h = np.where(v + step > hi, -step, step)

        def column(i):
            x = v.copy()
            x[i] += h[i]
            return (residual(binary, mode, targets, plucks, from_vector(x), held=held) - f0) / h[i]

        with ThreadPoolExecutor(max_workers=len(v)) as pool:
            return np.column_stack(list(pool.map(column, range(len(v)))))

    x0 = np.clip(to_vector(start), lo + 1e-6, hi - 1e-6)
    result = optimize.least_squares(f, x0, jac=jacobian, bounds=(lo, hi), max_nfev=max_nfev)
    return from_vector(result.x)


def read_decays():
    text = HEADER.read_text()
    block = text[text.index("// BEGIN FITTED DECAY"):text.index("// END FITTED DECAY")]
    decays = [dict(DEFAULT) for _ in MODES]
    at = block.index("DECAYS{")
    for m in range(len(MODES)):
        start = block.index("DecayShape{", at) + len("DecayShape")
        end = fit_rv6.braces(block, start)
        values = fit_rv6.numbers(block[start + 1:end - 1])
        for k, v in zip(FIELDS, values):
            decays[m][k] = v
        if len(values) >= len(FIELDS) + 5:
            decays[m]["slow_at"] = values[len(FIELDS):len(FIELDS) + 5]
        at = end
    return decays


def write_decays(decays):
    f = fit_rv6.f
    text = HEADER.read_text()
    begin, end = text.index("// BEGIN FITTED DECAY"), text.index("// END FITTED DECAY")
    entries = []
    for d in decays:
        if d == DEFAULT:
            entries.append("DecayShape{}")
        else:
            at = d.get("slow_at", DEFAULT["slow_at"])
            tail = "" if list(at) == DEFAULT["slow_at"] else ", {" + ", ".join(f(v) for v in at) + "}"
            entries.append("DecayShape{" + ", ".join(f(d[k]) for k in FIELDS) + tail + "}")
    body = ("// BEGIN FITTED DECAY (tools/capture/fit_rv6_decay.py --write rewrites this block)\n"
            "constexpr array<DecayShape, MODE_COUNT> DECAYS{{" + ", ".join(entries) + "}};\n")
    HEADER.write_text(text[:begin] + body + text[end:])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--render", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--followup-audio", type=Path, required=True)
    parser.add_argument("--modes", nargs="+", default=["room", "hall", "modulate"],
                        choices=list(START_S))
    parser.add_argument("--max-nfev", type=int, default=30)
    parser.add_argument("--evaluate", action="store_true", help="only measure the header's shapes")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--held", action="store_true", help="fit the held notes' decay too")
    args = parser.parse_args()
    sessions = {"first": rv.Session(STUDY / "2026-10-31", args.audio),
                "second": rv.Session(STUDY / "2026-10-31-followup", args.followup_audio)}
    decays = read_decays()
    report = {}

    def one(mode):
        targets = pedal_targets(sessions, mode)
        plucks = pluck_targets(sessions, mode)
        held = held_targets(sessions, mode)
        current = decays[MODES.index(mode)]
        start = current if current != DEFAULT else START
        _, before = residual(args.render, mode, targets, plucks, current, True, held)
        fitted = current if args.evaluate else fit(args.render, mode, targets, plucks, start, args.max_nfev,
                                                   held if args.held else None)
        r, after = residual(args.render, mode, targets, plucks, fitted, True, held)
        return mode, fitted, before, after, float(np.sqrt(np.mean(r ** 2)))

    with ThreadPoolExecutor(max_workers=len(args.modes)) as pool:
        results = list(pool.map(one, args.modes))
    for mode, fitted, before, after, rms in results:
        decays[MODES.index(mode)] = fitted
        report[mode] = {"decay": fitted, "before_db": before, "after_db": after}
        print(f"{mode}: " + ", ".join(f"{k} {fitted[k]:.4g}" for k in FIELDS), flush=True)
        for pos in after:
            keys = list(after[pos]) if pos == "plucks" or pos.startswith("held") else BANDS
            print(f"  {pos:>6s}  dB rms before/after: " + " ".join(
                f"{b}:{before[pos].get(b, '-')}/{after[pos].get(b, '-')}" for b in keys), flush=True)
    if args.report:
        args.report.write_text(json.dumps(report, indent=1) + "\n")
    if args.write and not args.evaluate:
        write_decays(decays)
        print(f"Wrote {HEADER}")


if __name__ == "__main__":
    main()
