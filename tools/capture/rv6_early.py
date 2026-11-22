#!/usr/bin/env python3
"""Measure the RV-6's early responses for NAM-X's Room, Hall, Plate and Spring (src/dsp/Rv6EarlyData.h).

    python -I tools/capture/rv6_early.py --render BINARY --audio FIRST_WAVS --followup-audio SECOND_WAVS
        [--write]

BINARY is tools/capture/render_rv6.cpp built against the current header; the WAV directories
hold the 2026-10-31 sessions' recordings (captures/boss/rv-6/2026-10-31 and -followup: only the
sidecars are in git). Per mode, from the pedal's sweep at noon (A and B):

  response   the pedal's wet over its first few hundred milliseconds, windowed: zero before
             10 ms ahead of its onset (what is there is the dry path's mismatch), a fade out
             over FADE_S at the end. Stored as it is; the engine subtracts its own tank over
             the same window at its own rate (Rv6Early.h), so tank plus kernel is the pedal
             at noon, and the tank alone carries the tail after.
  decay from where Time's extra decay starts on the kernel (the engine weights it by
             exp(-extra * (t - decay_from)), extra = the tank's decay rate at that Time less
             noon's) and
  gain       the kernel's level at the five Time positions, on top of the wet's own Time gain
             (Hall's swell keeps its level whatever Time does to the tank): least squares
             against the pedal's sweeps at Time 7:00, 9:00, 3:00 and 5:00 (the follow-up
             session) over the kernel's window, decay_from the one whose envelope (10 ms
             steps) matches best.

Prints each mode's envelope error against the pedal at the five Time positions, with and
without the kernel. --write rewrites src/dsp/Rv6EarlyData.h (then run clang-format on it).
After a refit of the tanks (fit_rv6.py) run it again: Time's decay start and gains are fitted
with the tank as it is (the responses themselves don't change). Since 2026-11-17 the gains at
7:00 and 9:00 (Hall's at 3:00 and 5:00 too) are rv6/time.py's, fitted on the focused session's
bursts with the tanks' tables: --write here puts the follow-up's back, so run rv6/time.py --write
after it.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_rv6 as rv  # noqa: E402
import fit_rv6  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
STUDY = REPOSITORY / "captures/boss/rv-6"
DATA = REPOSITORY / "src/dsp/Rv6EarlyData.h"
FS = rv.FS
MODE_INDEX = {"spring": 1, "plate": 2, "hall": 3, "room": 4}
WINDOW_S = {"room": 0.35, "plate": 0.45, "hall": 0.45, "spring": 0.8}  # the kernel's end
FADE_S = 0.03
POSITIONS = ["0700", "0900", "1200", "0300", "0500"]
MODES_MEASURED = ["room", "hall", "plate", "spring"]
SECONDS = 2.0  # rendered and compared
NEPERS_PER_RT60 = 3 * np.log(10)


def render(binary, mode, position, early):
    x = np.zeros(int((SECONDS + 0.2) * FS), np.float32)
    x[0] = 1.0
    knobs = [str(MODE_INDEX[mode]), f"{rv.clock_fraction(position[:2] + ':' + position[2:]):.6g}", "0.5", "0.5"]
    out = subprocess.run([str(binary), *knobs, f"early={int(early)}"], input=x.tobytes(), capture_output=True,
                         check=True).stdout
    y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
    return [rv.bandlimit(y[:, c] - x)[:int(SECONDS * FS)] for c in (0, 1)]


def pedal(first, second, mode, position):
    if position == "1200":
        return [first.wet_ir(f"{mode}-sweep", ch)[:int(SECONDS * FS)] for ch in (0, 2)]
    return [second.wet_ir(f"{mode}-sweep-time-{position}", ch)[:int(SECONDS * FS)] for ch in (0, 2)]


RISE_S = 0.001


def window(n, start, end_s):
    """The kernel's window (Rv6Early.h applies the same to the tank): zero before `start`, a
    raised-cosine rise over RISE_S, a fall over FADE_S to end_s."""
    w = np.zeros(n)
    end, fade, rise = int(end_s * FS), int(FADE_S * FS), int(RISE_S * FS)
    w[start:end] = 1.0
    w[start:start + rise] = 0.5 - 0.5 * np.cos(np.pi * np.arange(rise) / rise)
    w[end - fade:end] = 0.5 + 0.5 * np.cos(np.pi * np.arange(fade) / fade)
    return w


def kernel(p, h, gain, end_s):
    """The engine's kernel ((pedal - tank) / gain over the window) and the window's start: 10 ms
    before the pedal's onset (the study's: -20 dB re its peak; before it the response holds only
    the dry path's mismatch)."""
    start = max(int((rv.onset_ms(p)[0] / 1000 - 0.010) * FS), int(0.015 * FS))
    return window(len(p), start, end_s) * (p - h) / gain, start


def envelope_db(irs, upto_s):
    e = sum(h ** 2 for h in irs)[:int(upto_s * FS)]
    return 10 * np.log10(e.reshape(-1, FS // 100).sum(1) + 1e-30)


def envelope_error(pedal_irs, model_irs, upto_s):
    """dB rms between the envelopes (10 ms steps) from 10 ms before the pedal's onset (before
    it, the dry path's mismatch) while the pedal is within 45 dB of its peak."""
    a, b = envelope_db(pedal_irs, upto_s), envelope_db(model_irs, upto_s)
    keep = a > a.max() - 45
    keep[:max(int(min(rv.onset_ms(h)[0] for h in pedal_irs) / 10) - 1, 2)] = False
    return float(np.sqrt(np.mean((b - a)[keep] ** 2)))


def time_gain(voicing, i):
    """The engine's wet gain at Time position i re noon (Engine::set_controls)."""
    rt, energy = voicing["rt"], voicing["energy"]
    return np.sqrt(rt[2] / rt[i]) * 10 ** (energy[i] / 20)


def measure(binary, first, second, voicings):
    results = {}
    for mode in MODES_MEASURED:
        v = voicings[MODE_INDEX[mode]]
        gain = 10 ** (v["gain"] / 20)
        p_noon, h_noon = pedal(first, second, mode, "1200"), render(binary, mode, "1200", False)
        pieces = [kernel(p, h, gain, WINDOW_S[mode]) for p, h in zip(p_noon, h_noon)]
        kernels = [k for k, _ in pieces]
        t = np.arange(len(kernels[0])) / FS
        upto = min(WINDOW_S[mode] + 0.4, SECONDS)
        renders = {pos: (pedal(first, second, mode, pos), render(binary, mode, pos, False)) for pos in POSITIONS}

        span = int(WINDOW_S[mode] * FS)

        def hybrid(pos, decay_from):
            """The tank plus the kernel at `pos`, the kernel's own gain fitted (least squares over
            its window); returns (irs, gain)."""
            i = POSITIONS.index(pos)
            extra = NEPERS_PER_RT60 * (1 / v["rt"][i] - 1 / v["rt"][2])
            weight = np.exp(-extra * np.maximum(0.0, t - decay_from))
            shaped = [gain * time_gain(v, i) * weight * k for k in kernels]
            p, h = renders[pos]
            num = sum(np.dot((a - b)[:span], s[:span]) for a, b, s in zip(p, h, shaped))
            den = sum(np.dot(s[:span], s[:span]) for s in shaped)
            c = 1.0 if pos == "1200" else max(num / den, 1e-3)
            return [b + c * s for b, s in zip(h, shaped)], c

        def score(decay_from):
            return np.mean([envelope_error(renders[pos][0], hybrid(pos, decay_from)[0], upto)
                            for pos in POSITIONS if pos != "1200"])

        candidates = np.arange(0.0, 0.401, 0.01)
        decay_from = float(candidates[np.argmin([score(c) for c in candidates])])
        fitted = {pos: hybrid(pos, decay_from) for pos in POSITIONS}
        rows = {pos: (envelope_error(renders[pos][0], fitted[pos][0], upto),
                      envelope_error(renders[pos][0], renders[pos][1], upto)) for pos in POSITIONS}
        gains_db = [float(20 * np.log10(fitted[pos][1])) for pos in POSITIONS]
        start = min(s for _, s in pieces)
        stop = int(WINDOW_S[mode] * FS)
        stored = [window(len(p), start, WINDOW_S[mode]) * p for p in p_noon]  # the pedal's own
        results[mode] = {"a": stored[0][start:stop], "b": stored[1][start:stop], "offset": start,
                         "decay_from_ms": 1000 * decay_from, "gains_db": gains_db, "rows": rows}
    return results


def write(results):
    order = ["modulate", "spring", "plate", "hall", "room", "shimmer"]
    lines = ["#pragma once", "", "#include <array>", "#include <cstdint>", "", '#include "Rv6Early.h"', "",
             "namespace namx::rv6", "{", "",
             "// BEGIN GENERATED EARLY (tools/capture/rv6_early.py --write rewrites this file)",
             "// The RV-6's Room, Hall, Plate and Spring early responses (re the dry, Level and Time noon),",
             "// 48 kHz.",
             "",
             "// clang-format off"]
    entries = []
    for mode in order:
        if mode not in results:
            entries.append("    EarlyKernel{}")
            continue
        r = results[mode]
        scale = max(np.max(np.abs(r["a"])), np.max(np.abs(r["b"]))) / 32767.0
        for side in ("a", "b"):
            q = np.round(r[side] / scale).astype(int)
            name = f"{mode.upper()}_EARLY_{side.upper()}"
            rows = [",".join(str(v) for v in q[i:i + 24]) for i in range(0, len(q), 24)]
            lines.append(f"inline constexpr std::array<int16_t, {len(q)}> {name}{{{{\n"
                         + ",\n".join(rows) + "}};")
        entries.append(f"    EarlyKernel{{{mode.upper()}_EARLY_A.data(), {mode.upper()}_EARLY_B.data(), "
                       f"{len(r['a'])}, {r['offset']}, {int(RISE_S * FS)}, {int(FADE_S * FS)}, {scale:.6e}f, "
                       f"48000.0f, {fit_rv6.f(r['decay_from_ms'])}, "
                       "{" + ", ".join(fit_rv6.f(g) for g in r["gains_db"]) + "}}")
    lines += ["// clang-format on", "", "// In Mode order: Room, Hall, Plate and Spring have one.",
              "inline constexpr std::array<EarlyKernel, 6> EARLY{{", ",\n".join(entries), "}};", "",
              "// END GENERATED EARLY", "", "} // namespace namx::rv6", ""]
    DATA.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--render", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--followup-audio", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    first = rv.Session(STUDY / "2026-10-31", args.audio)
    second = rv.Session(STUDY / "2026-10-31-followup", args.followup_audio)
    _, _, _, voicings = fit_rv6.read_block()
    results = measure(args.render, first, second, voicings)
    for mode, r in results.items():
        print(f"{mode:5s} kernel {r['offset'] / FS * 1000:.0f}-{WINDOW_S[mode] * 1000:.0f} ms, Time's decay from "
              f"{r['decay_from_ms']:.0f} ms, gain " + " ".join(f"{g:+.1f}" for g in r["gains_db"])
              + " dB; envelope error (dB rms, with / tank alone): "
              + " ".join(f"{pos[:2]}:{a:.1f}/{b:.1f}" for pos, (a, b) in r["rows"].items()))
    if args.write:
        write(results)
        print(f"Wrote {DATA}")


if __name__ == "__main__":
    main()
