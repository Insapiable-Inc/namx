#!/usr/bin/env python3
"""Fits the Boss TR-2 model to a capture session recorded with plans/tr-2.json.

    fit_tr2.py SESSION        # e.g. captures/boss/tr-2/2026-10-26; writes SESSION/fit/

The model, found from the 2026-10-26 captures (each take's averaged LFO cycle, 1 kHz
carrier, gain relative to bypass):

    tri(phase)  triangle, +1 at phase 0, -1 at phase 0.5
    v = u0 + D * (G * (tri + beta) - kappa)      D = Depth map (1 at 5:00), G = Wave map
    G is divided by sqrt(1 + (rate / corner)^2): the swing shrinks at fast rates
    gain = top * smooth(clamp(v))                clamp: soft limit to [0, 1] (sharpness s)
                                                 smooth: one-pole low-pass, time constant tau

Wave is a plain gain on an offset triangle: the clamp is what squares it off, and Depth
pulls the whole swing towards a rest point above the clamp's top (full level at Depth 0).
Rate is measured directly per take. Outside the LFO: the pedal's level and low cut
(pedal on at minimum depth against bypass), distortion and noise.
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import scipy.optimize

import analysis

POSITIONS = ["7:00", "9:30", "12:00", "2:30", "5:00"]
KNOB_FRACTION = [0.0, 0.25, 0.5, 0.75, 1.0]  # of the 300° rotation
BINS = 128
CARRIER_HZ = 1000.0


def tag(position):
    return position.replace(":", "").zfill(4)


# Which Wave and Depth positions each sine take used (indexes into POSITIONS).
SHAPE_TAKES = {f"depth-{tag(p)}": (2, i) for i, p in enumerate(POSITIONS[:4])}
SHAPE_TAKES |= {f"wave-{tag(p)}": (i, 4) for i, p in enumerate(POSITIONS)}
SHAPE_TAKES |= {f"rate-{tag(p)}": (2, 4) for p in POSITIONS if p != "12:00"}
SHAPE_TAKES |= {"cross-max": (4, 4), "cross-min": (0, 3)}
RATE_TAKES = {"7:00": "rate-0700", "9:30": "rate-0930", "12:00": "wave-1200",
              "2:30": "rate-0230", "5:00": "rate-0500"}

GLOBALS = ["u0", "beta", "kappa", "sharpness", "top", "tau_s", "rate_corner_hz"]


def triangle(phase):
    return 1 - 4 * np.minimum(phase, 1 - phase)


def soft_clamp(v, sharpness):
    return (np.logaddexp(0, sharpness * v) - np.logaddexp(0, sharpness * (v - 1))) / sharpness


def smooth_cycle(x, lfo_hz, tau):
    """A one-pole low-pass applied to a periodic cycle (in the frequency domain)."""
    k = np.fft.rfftfreq(len(x), 1 / len(x))
    return np.fft.irfft(np.fft.rfft(x) / (1 + 2j * np.pi * k * lfo_hz * tau), len(x))


def model_cycle(constants, wave_gain, depth, lfo_hz, phase_offset, bins=BINS):
    u0, beta, kappa, sharpness, top, tau, corner = constants
    phase = (np.arange(bins) + 0.5) / bins
    gain = wave_gain / np.sqrt(1 + (lfo_hz / corner) ** 2)
    v = u0 + depth * (gain * (triangle(np.mod(phase - phase_offset, 1)) + beta) - kappa)
    return top * smooth_cycle(soft_clamp(v, sharpness), lfo_hz, tau)


def measure_cycles(session):
    reference = analysis.Take(session, "bypass-sine")
    steady = slice(reference.rate, -reference.rate)
    level = np.median(analysis.envelope(reference.signal(), reference.rate)[steady])
    cycles = {}
    for name in SHAPE_TAKES:
        take = analysis.Take(session, name)
        half = take.rate // 2
        g = analysis.envelope(take.signal(), take.rate)[half:-half] / level
        lfo_hz, phase = analysis.lfo_frequency(g, take.rate)
        cycles[name] = (lfo_hz, analysis.fold_cycle(g, take.rate, lfo_hz, phase, BINS))
    return cycles


def fit(cycles):
    names = list(SHAPE_TAKES)
    count = len(GLOBALS)

    def unpack(q):
        constants = q[:count]
        wave = q[count:count + 5]
        depth = np.append(q[count + 5:count + 9], 1.0)
        return constants, wave, depth, q[count + 9:]

    def residuals(q):
        constants, wave, depth, offsets = unpack(q)
        return np.concatenate([
            model_cycle(constants, wave[SHAPE_TAKES[n][0]], depth[SHAPE_TAKES[n][1]], cycles[n][0],
                        offsets[i]) - cycles[n][1] for i, n in enumerate(names)])

    # Starting point and bounds: the values the exploration settled on, widely bracketed.
    start = [1.24, 0.38, 0.93, 25, 1.05, 0.006, 40]
    start += [0.6, 1.1, 1.5, 2.8, 4] + [0.05, 0.3, 0.5, 0.8]
    low = [0, -2, -5, 1, 0.8, 0, 2] + [0] * 9
    high = [5, 3, 5, 300, 1.3, 0.05, 1000] + [50] * 5 + [3] * 4
    start += [0] * len(names)
    low += [-0.2] * len(names)
    high += [0.2] * len(names)
    result = scipy.optimize.least_squares(residuals, start, bounds=(low, high), max_nfev=50000)
    constants, wave, depth, _ = unpack(result.x)
    errors = result.fun.reshape(len(names), BINS)
    fit_error = {n: {"rms": round(float(np.sqrt(np.mean(errors[i] ** 2))), 4),
                     "max": round(float(np.max(np.abs(errors[i]))), 4)}
                 for i, n in enumerate(names)}
    return constants, wave, depth, fit_error, unpack(result.x)[3]


def measure_tone(session):
    bypass = {lv: analysis.Take(session, f"bypass-sweep-{lv}") for lv in ("12", "6", "0")}
    pedal = {lv: analysis.Take(session, f"depth0-sweep-{lv}") for lv in ("12", "6", "0")}
    rate = bypass["0"].rate
    response = {}
    for lv in bypass:
        difference = (analysis.third_octave_db(pedal[lv].signal(0.5), rate)
                      - analysis.third_octave_db(bypass[lv].signal(0.5), rate))
        response[f"-{lv} dB" if lv != "0" else "0 dB"] = [round(float(d), 2) for d in difference]
    linearity = (analysis.third_octave_db(pedal["0"].signal(0.5), rate)
                 - analysis.third_octave_db(pedal["12"].signal(0.5), rate))
    harmonics = {}
    for name in ("bypass-sine", "depth-0700", "wave-1200"):
        take = analysis.Take(session, name)
        x = take.signal()[take.rate:-take.rate]
        harmonics[name] = [round(h, 1) for h in analysis.harmonics_db(x, take.rate, CARRIER_HZ)]
    noise = analysis.Take(session, "noise-floor")
    x = noise.signal()[noise.rate // 2:-noise.rate // 2]
    return {
        "bands_hz": analysis.THIRD_OCTAVES_HZ,
        "on_minus_bypass_db": response,
        "level_step_0_vs_-12_db": [round(float(d), 2) for d in linearity],
        "harmonics_db_re_fundamental_h2_to_h5": harmonics,
        "noise_rms_dbfs": round(analysis.rms_db(x), 1),
    }


def plot(path, cycles, constants, wave, depth, offsets):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    names = list(SHAPE_TAKES)
    columns = 5
    rows = -(-len(names) // columns)
    figure, axes = plt.subplots(rows, columns, figsize=(3.2 * columns, 2.4 * rows), sharey=True)
    phase = (np.arange(BINS) + 0.5) / BINS
    for i, (axis, name) in enumerate(zip(axes.flat, names)):
        lfo_hz, cycle = cycles[name]
        wi, di = SHAPE_TAKES[name]
        axis.plot(phase, cycle, label="TR-2")
        axis.plot(phase, model_cycle(constants, wave[wi], depth[di], lfo_hz, offsets[i]), "--",
                  label="model")
        axis.set_title(f"{name} ({lfo_hz:.2f} Hz)", fontsize=9)
        axis.set_ylim(-0.05, 1.15)
    for axis in list(axes.flat)[len(names):]:
        axis.axis("off")
    axes.flat[0].legend(fontsize=8)
    figure.supxlabel("LFO phase (cycles)")
    figure.supylabel("gain re bypass")
    figure.tight_layout()
    figure.savefig(path, dpi=110)
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")

    cycles = measure_cycles(session)
    constants, wave, depth, fit_error, offsets = fit(cycles)
    rates = {p: round(float(cycles[RATE_TAKES[p]][0]), 4) for p in POSITIONS}
    result = {
        "pedal": "Boss TR-2",
        "session": session.name,
        "model": "gain = top * smooth(clamp01(u0 + D * (G / sqrt(1 + (rate / rate_corner_hz)^2)"
                 " * (tri + beta) - kappa)))",
        "constants": {k: round(float(v), 5) for k, v in zip(GLOBALS, constants)},
        "knob_fraction": KNOB_FRACTION,
        "rate_hz": rates,
        "wave_gain_G": {p: round(float(g), 4) for p, g in zip(POSITIONS, wave)},
        "depth_D": {p: round(float(d), 4) for p, d in zip(POSITIONS, depth)},
        "fit_error_gain": fit_error,
        "tone": measure_tone(session),
    }
    out = session / "fit"
    out.mkdir(exist_ok=True)
    with open(out / "tr-2.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out / "tr-2-cycles.png", cycles, constants, wave, depth, offsets)

    print("Constants: " + ", ".join(f"{k} {v:g}" for k, v in result["constants"].items()))
    for title, values in (("Rate (Hz)", rates), ("Wave gain G", result["wave_gain_G"]),
                          ("Depth D", result["depth_D"])):
        print(f"{title:<12} " + "  ".join(f"{p} {v:g}" for p, v in values.items()))
    worst = max(fit_error, key=lambda n: fit_error[n]["rms"])
    print(f"Fit error (gain, rms): worst {worst} {fit_error[worst]['rms']}, median "
          f"{np.median([e['rms'] for e in fit_error.values()]):.4f}")
    print(f"Wrote {out / 'tr-2.json'} and {out / 'tr-2-cycles.png'}")


if __name__ == "__main__":
    main()
