#!/usr/bin/env python3
"""Fits the Boss TR-2 model to a capture session recorded with plans/tr-2.json.

    fit_tr2.py SESSION        # e.g. captures/boss/tr-2/2026-10-26; writes SESSION/fit/

The model, found from the 2026-10-26 captures (each take's averaged LFO cycle, 1 kHz
carrier, gain relative to bypass):

    tri(phase)  triangle, +1 at phase 0, -1 at phase 0.5
    v = u0 + D * (G * (tri + beta) - kappa)      D = Depth map (1 at 5:00), G = Wave map
    gain = T * smooth(clamp(v))                  clamp: soft limit to [0, 1] (sharpness s)
                                                 smooth: one-pole, falling with tau_down,
                                                 rising with tau_up (slower)
                                                 T = top level, a Depth map too

Wave is a plain gain on an offset triangle: the clamp is what squares it off, and Depth
pulls the whole swing towards a rest point above the clamp's top (full level at Depth 0).
The gain drops about twice as fast as it recovers (fast attack, slow release), which is
what rounds the fast settings' rising edges and makes them creep up to the top. The top
level is a Depth map: on a 9 V supply it drops ~0.6 dB as Depth nears maximum (on the
tired battery of the first session it stayed flat, and the map comes out flat there).
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
BINS = 256
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

GLOBALS = ["u0", "beta", "kappa", "sharpness", "tau_down_s", "tau_up_s"]


def triangle(phase):
    return 1 - 4 * np.minimum(phase, 1 - phase)


def soft_clamp(v, sharpness):
    return (np.logaddexp(0, sharpness * v) - np.logaddexp(0, sharpness * (v - 1))) / sharpness


def smooth_cycle(x, lfo_hz, tau_down, tau_up, cycles=3):
    """A one-pole follower with separate falling and rising time constants, run over a
    periodic cycle until it settles (the last of `cycles` passes is returned)."""
    dt = 1 / (lfo_hz * len(x))
    down, up = 1 - np.exp(-dt / tau_down), 1 - np.exp(-dt / tau_up)
    y = x[-1]
    out = np.empty_like(x)
    for _ in range(cycles):
        for i, target in enumerate(x):
            y += (up if target > y else down) * (target - y)
            out[i] = y
    return out


def model_cycle(constants, wave_gain, depth, top, lfo_hz, phase_offset, bins=BINS):
    u0, beta, kappa, sharpness, tau_down, tau_up = constants
    phase = (np.arange(bins) + 0.5) / bins
    v = u0 + depth * (wave_gain * (triangle(np.mod(phase - phase_offset, 1)) + beta) - kappa)
    return top * smooth_cycle(soft_clamp(v, sharpness), lfo_hz, tau_down, tau_up)


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
        top = q[count + 9:count + 14]
        return constants, wave, depth, top, q[count + 14:]

    def residuals(q):
        constants, wave, depth, top, offsets = unpack(q)
        return np.concatenate([
            model_cycle(constants, wave[SHAPE_TAKES[n][0]], depth[SHAPE_TAKES[n][1]],
                        top[SHAPE_TAKES[n][1]], cycles[n][0], offsets[i]) - cycles[n][1]
            for i, n in enumerate(names)])

    # Starting point and bounds: the values the exploration settled on, widely bracketed.
    start = [1.24, 0.38, 0.93, 25, 0.003, 0.008]
    start += [0.6, 1.1, 1.5, 2.8, 4] + [0.05, 0.3, 0.5, 0.8] + [1.05] * 5
    low = [0, -2, -5, 1, 1e-5, 1e-5] + [0] * 9 + [0.8] * 5
    high = [5, 3, 5, 300, 0.05, 0.05] + [50] * 5 + [3] * 4 + [1.3] * 5
    start += [0] * len(names)
    low += [-0.2] * len(names)
    high += [0.2] * len(names)
    result = scipy.optimize.least_squares(residuals, start, bounds=(low, high), max_nfev=50000)
    constants, wave, depth, top, offsets = unpack(result.x)
    errors = result.fun.reshape(len(names), BINS)
    fit_error = {n: {"rms": round(float(np.sqrt(np.mean(errors[i] ** 2))), 4),
                     "max": round(float(np.max(np.abs(errors[i]))), 4)}
                 for i, n in enumerate(names)}
    return constants, wave, depth, top, fit_error, offsets


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


def plot(path, cycles, constants, wave, depth, top, offsets):
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
        axis.plot(phase, model_cycle(constants, wave[wi], depth[di], top[di], lfo_hz, offsets[i]),
                  "--", label="model")
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


def plot_explainers(out, session, constants, wave, depth, top, rates, tone):
    """Figures that explain the fit: the raw data, the model's stages, Depth, the knob maps."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    u0, beta, kappa, sharpness, tau_down, tau_up = constants
    phase = (np.arange(512) + 0.5) / 512
    tri = triangle(phase)
    lfo_hz = rates["12:00"]
    colors = ["C0", "C1", "C2", "C3", "C4"]

    def stages(g, d, t):
        v = u0 + d * (g * (tri + beta) - kappa)
        clamped = soft_clamp(v, sharpness)
        return v, clamped, t * smooth_cycle(clamped, lfo_hz, tau_down, tau_up)

    # What the fit is made from: one second of a take and its envelope.
    take = analysis.Take(session, "wave-1200")
    reference = analysis.Take(session, "bypass-sine")
    rate = take.rate
    x = take.signal()
    env = analysis.envelope(x, rate)
    level = np.median(analysis.envelope(reference.signal(), rate)[rate:-rate])
    window = slice(5 * rate, 6 * rate)
    t = np.arange(rate) / rate
    figure, axes = plt.subplots(2, 1, figsize=(13, 5), sharex=True)
    axes[0].plot(t, x[window], lw=0.3, label="pedal output (1 kHz sine through the TR-2)")
    axes[0].plot(t, env[window], "C3", label="envelope (Hilbert, 300 Hz low-pass)")
    axes[0].plot(t, -env[window], "C3")
    axes[0].legend(fontsize=8, loc="upper right")
    axes[0].set_ylabel("sample value")
    axes[1].plot(t, env[window] / level, "C3")
    axes[1].set_ylabel("gain re bypass")
    axes[1].set_xlabel("seconds")
    axes[1].grid(alpha=0.3)
    figure.suptitle("Take wave-1200 (Rate 12:00, Wave 12:00, Depth 5:00), one second: each take's "
                    "envelope is averaged into one LFO cycle, and the model is fitted to those")
    figure.tight_layout()
    figure.savefig(out / "tr-2-raw.png", dpi=110)
    plt.close(figure)

    # The model's stages, for three Wave settings at full Depth.
    figure, axes = plt.subplots(1, 4, figsize=(17, 4))
    axes[0].plot(phase, tri, "k", lw=1)
    for position, color in (("7:00", "C0"), ("12:00", "C1"), ("5:00", "C3")):
        g = wave[POSITIONS.index(position)]
        v, clamped, gain = stages(g, 1.0, top[4])
        axes[1].plot(phase, v, color, label=f"Wave {position} (G = {g:.2f})")
        axes[2].plot(phase, clamped, color)
        axes[3].plot(phase, gain, color)
    axes[1].axhspan(0, 1, color="0.9", zorder=0)
    axes[1].legend(fontsize=8, loc="upper center")
    titles = ["1. triangle LFO: tri", r"2. control: $v = u_0 + D\,(G\,(tri+\beta) - \kappa)$",
              f"3. soft clamp to [0, 1] (s = {sharpness:.1f})",
              f"4. smoothing (down {tau_down * 1000:.1f} ms, up {tau_up * 1000:.1f} ms) × T"]
    for axis, title in zip(axes, titles):
        axis.set_title(title, fontsize=10)
        axis.set_xlabel("LFO phase (cycles)")
        axis.grid(alpha=0.3)
    figure.suptitle(f"The model, Wave at three settings (Depth 5:00, Rate 12:00 = "
                    f"{lfo_hz:.2f} Hz); grey band = the clamp's window")
    figure.tight_layout()
    figure.savefig(out / "tr-2-model.png", dpi=110)
    plt.close(figure)

    # Depth: the swing pivots around the rest point u0, above the clamp.
    figure, axes = plt.subplots(1, 2, figsize=(12, 4))
    g = wave[POSITIONS.index("12:00")]
    for position, d, t, color in zip(POSITIONS, depth, top, colors):
        v, _, gain = stages(g, d, t)
        axes[0].plot(phase, v, color, label=f"Depth {position} (D = {d:.2f})")
        axes[1].plot(phase, gain, color)
    axes[0].axhspan(0, 1, color="0.9", zorder=0)
    axes[0].axhline(u0, ls=":", c="k", lw=1)
    axes[0].annotate(f"rest point u0 = {u0:.2f}", (0.5, u0), (0.5, u0 + 0.6), ha="center",
                     fontsize=8, arrowprops={"arrowstyle": "->"})
    axes[0].set_title("control v: Depth scales the swing toward u0 (above the clamp)", fontsize=10)
    axes[1].set_title("resulting gain", fontsize=10)
    for axis in axes:
        axis.set_xlabel("LFO phase (cycles)")
        axis.grid(alpha=0.3)
    axes[0].legend(fontsize=8, loc="lower right")
    figure.suptitle("Depth at Wave 12:00, Rate 12:00")
    figure.tight_layout()
    figure.savefig(out / "tr-2-depth.png", dpi=110)
    plt.close(figure)

    # Knob maps and the pedal's own tone.
    figure, axes = plt.subplots(1, 4, figsize=(18, 4))
    fraction = np.array(KNOB_FRACTION)
    axes[0].semilogy(fraction, [rates[p] for p in POSITIONS], "o-")
    axes[0].set_title("Rate → f (Hz, log scale)", fontsize=10)
    axes[1].plot(fraction, wave, "o-")
    axes[1].set_title("Wave → G", fontsize=10)
    axes[2].plot(fraction, depth, "o-")
    axes[2].set_title("Depth → D (5:00 fixed at 1)", fontsize=10)
    for axis in axes[:3]:
        axis.set_xticks(fraction, POSITIONS)
        axis.set_xlabel("knob (clock)")
        axis.grid(alpha=0.3, which="both")
    for label, values in tone["on_minus_bypass_db"].items():
        axes[3].semilogx(tone["bands_hz"], values, "o-", label=f"input {label}")
    f = np.logspace(np.log10(20), np.log10(20000), 200)
    axes[3].semilogx(f, 20 * np.log10(top[0]) - 10 * np.log10(1 + (70 / f) ** 2), "k:",
                     label="first-order 70 Hz low cut × T(7:00)")
    axes[3].set_title("pedal on (Depth 7:00) minus bypass, dB", fontsize=10)
    axes[3].set_xlabel("Hz")
    axes[3].legend(fontsize=7)
    axes[3].grid(alpha=0.3, which="both")
    figure.suptitle("Knob maps (7:00 = fully counter-clockwise) and the pedal's own tone")
    figure.tight_layout()
    figure.savefig(out / "tr-2-knobs-tone.png", dpi=110)
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")

    cycles = measure_cycles(session)
    constants, wave, depth, top, fit_error, offsets = fit(cycles)
    rates = {p: round(float(cycles[RATE_TAKES[p]][0]), 4) for p in POSITIONS}
    result = {
        "pedal": "Boss TR-2",
        "session": session.name,
        "model": "gain = T * follow(clamp01(u0 + D * (G * (tri + beta) - kappa)));"
                 " follow = one-pole, tau_down falling, tau_up rising; T = top level by Depth",
        "constants": {k: round(float(v), 5) for k, v in zip(GLOBALS, constants)},
        "knob_fraction": KNOB_FRACTION,
        "rate_hz": rates,
        "wave_gain_G": {p: round(float(g), 4) for p, g in zip(POSITIONS, wave)},
        "depth_D": {p: round(float(d), 4) for p, d in zip(POSITIONS, depth)},
        "top_T_by_depth": {p: round(float(t), 4) for p, t in zip(POSITIONS, top)},
        "fit_error_gain": fit_error,
        "tone": measure_tone(session),
    }
    out = session / "fit"
    out.mkdir(exist_ok=True)
    with open(out / "tr-2.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out / "tr-2-cycles.png", cycles, constants, wave, depth, top, offsets)
    plot_explainers(out, session, constants, wave, depth, top, rates, result["tone"])

    print("Constants: " + ", ".join(f"{k} {v:g}" for k, v in result["constants"].items()))
    for title, values in (("Rate (Hz)", rates), ("Wave gain G", result["wave_gain_G"]),
                          ("Depth D", result["depth_D"]), ("Top T", result["top_T_by_depth"])):
        print(f"{title:<12} " + "  ".join(f"{p} {v:g}" for p, v in values.items()))
    worst = max(fit_error, key=lambda n: fit_error[n]["rms"])
    print(f"Fit error (gain, rms): worst {worst} {fit_error[worst]['rms']}, median "
          f"{np.median([e['rms'] for e in fit_error.values()]):.4f}")
    print(f"Wrote {out / 'tr-2.json'} and the figures (tr-2-*.png) next to it")


if __name__ == "__main__":
    main()
