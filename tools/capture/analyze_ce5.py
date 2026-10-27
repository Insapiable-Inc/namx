#!/usr/bin/env python3
"""Measures the Boss CE-5 Chorus Ensemble from its two sessions: the wet-only one (a plug in
Output B, so Output A carries only the effect sound; plans/ce-5-wet.json) and the mono one
(Output A alone, dry + effect; plans/ce-5.json). Findings: captures/boss/ce-5/README.md.

    analyze_ce5.py WET_SESSION MONO_SESSION     # writes WET_SESSION/fit/ce-5.{json,svg}

What it measures:
  dry path         mono dry-sweep (E.Level down) against the bypass sweep: delay, gain, flatness
  wet tone         wet sweep (Depth at minimum): delay, response re 1 kHz, group delay
  mono vs wet      the mix's wet part against the wet-only output
  E.Level          wet gain at 1 kHz per position, both sessions
  filter           Low and High sweeps against the open one: corner (-3 dB), slope, floor;
                   both-0700 against the sum of the two
  LFO              held 1 kHz tones: rate, absolute delay (min, mean, max), cycle shape
                   (delay = min + swing * triangle^power), the level wobble
  bursts           the wet path's gain and harmonics against the input level
  note-off, noise  the noise floor after a note stops, and with nothing played

Every sweep is deconvolved into an impulse response; the dry arrives at ~1 ms, the wet at
~6.4 ms, so a window split at 3.5 ms separates them (the sweeps are at minimum Depth, where
the delay moves 0.03 ms). The dry path's low end rings past the split, so comparisons of a
mono take's wet part hold from ~125 Hz up. The held tones' delay comes from their phase
against the input, which is ambiguous by whole periods (1 ms at 1 kHz): the period is
picked that puts the mean nearest the Depth-minimum delay (checked once against a sweep at
Rate 7:00 / Depth 12:00, whose low end fixes the delay without ambiguity — see the study).
"""

import argparse
import functools
import json
import pathlib
import sys

import numpy as np
import scipy.signal
import soundfile

import analysis
import analyze_dd8

RATE = 48000
N_IR = RATE // 2  # 0.5 s of impulse response: 2 Hz resolution, for the filters' low end
SPLIT_S = 0.0035  # dry before, wet after
BANDS = analysis.THIRD_OCTAVES_HZ
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
FREQS = np.fft.rfftfreq(N_IR, 1 / RATE)


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def rounded(values, digits=2):
    return [round(float(v), digits) for v in values]


@functools.lru_cache(maxsize=None)
def parts(session, name):
    """A sweep take's dry and wet frequency responses (what came out over what was played)."""
    h, _ = analyze_dd8.impulse_response(session, name)
    h = h[:N_IR]
    split = int(SPLIT_S * RATE)
    return np.fft.rfft(np.where(np.arange(N_IR) < split, h, 0)), \
        np.fft.rfft(np.where(np.arange(N_IR) >= split, h, 0))


def dry_of(session, name):
    return parts(session, name)[0]


def wet_of(session, name):
    return parts(session, name)[1]


def bands_db(spectrum, centres=BANDS, edge=2 ** (1 / 6)):
    """Mean power per band (at least five bins wide, so narrow bands at the low end aren't
    empty), in dB."""
    step = FREQS[1]
    power = np.abs(spectrum) ** 2
    return np.array([10 * np.log10(np.mean(power[(FREQS >= min(f / edge, f - 2 * step))
                                                 & (FREQS <= max(f * edge, f + 2 * step))]) + 1e-24)
                     for f in centres])


def at_db(spectrum, f):
    return float(bands_db(spectrum, [f], 1.03)[0])


def group_delay_ms(spectrum, centres):
    padded = np.fft.rfft(np.fft.irfft(spectrum, N_IR), 1 << 16)
    f = np.fft.rfftfreq(1 << 16, 1 / RATE)
    g = -np.gradient(np.unwrap(np.angle(padded)), f) / (2 * np.pi) * 1000
    return {f"{c / 1000:g}k": round(float(np.median(g[(f > c * 0.95) & (f < c * 1.05)])), 3)
            for c in centres}


def phase_delay_ms(spectrum, low, high):
    """The delay from the slope of the unwrapped phase between low and high Hz."""
    band = (FREQS > low) & (FREQS < high)
    slope = np.polyfit(FREQS[band], np.unwrap(np.angle(spectrum[band])), 1)[0]
    return float(-slope / (2 * np.pi) * 1000)


def dry_path(mono):
    bypass, dry = dry_of(mono, "bypass-sweep"), dry_of(mono, "dry-sweep")
    ratio = bands_db(dry) - bands_db(bypass)
    in_band = [i for i, f in enumerate(BANDS) if 50 <= f <= 16000]
    return {"delay_ms_re_bypass": round(phase_delay_ms(dry, 300, 14000)
                                        - phase_delay_ms(bypass, 300, 14000), 3),
            "gain_db_re_bypass_1khz": round(at_db(dry, 1000) - at_db(bypass, 1000), 2),
            "flatness_db_50_16k": round(float(np.ptp(ratio[in_band])), 2),
            "group_delay_ms": group_delay_ms(dry, [300, 1000, 3000, 10000, 14000]),
            "filters_closed_minus_open_db_1khz": round(at_db(dry_of(mono, "both-0700-dry"), 1000)
                                                       - at_db(dry, 1000), 2)}


def wet_tone(wet, mono):
    open_wet = wet_of(wet, "sweep")
    tone = bands_db(open_wet)
    delay = phase_delay_ms(open_wet, 1000, 8000)
    return {
        "delay_ms": round(delay, 3),
        "delay_ms_re_dry": round(delay - phase_delay_ms(dry_of(mono, "dry-sweep"), 300, 14000), 3),
        "gain_db_1khz": round(at_db(open_wet, 1000), 2),
        "bands_hz": BANDS,
        "db_re_1khz": rounded(tone - tone[BANDS.index(1000)]),
        "high_end_db_re_1khz": {f"{f // 1000}k": round(at_db(open_wet, f) - at_db(open_wet, 1000),
                                                       1)
                                for f in (8000, 10000, 12000, 14000, 16000, 18000)},
        "group_delay_ms": group_delay_ms(open_wet, [300, 1000, 3000, 8000, 10000, 12000, 14000]),
        "mono_wet_minus_wet_only_db": {name: rounded(bands_db(wet_of(mono, name)) - tone)
                                       for name in ("sweep", "sweep-check")},
        "wet_only_check_minus_sweep_db": rounded(bands_db(wet_of(wet, "sweep-check")) - tone),
        "mono_wet_re_dry_1khz_db": round(at_db(wet_of(mono, "sweep"), 1000)
                                         - at_db(dry_of(mono, "sweep"), 1000), 2),
    }


def elevel(wet, mono):
    out = {"7:00": {"wet_only_db": None,
                    "mono_db": round(at_db(wet_of(mono, "dry-sweep"), 1000), 2)}}
    for position in POSITIONS[1:]:
        name = "sweep" if position == "5:00" else f"sweep-level-{analyze_dd8.tag(position)}"
        out[position] = {"wet_only_db": round(at_db(wet_of(wet, name), 1000), 2),
                         "mono_db": round(at_db(wet_of(mono, name), 1000), 2)}
    return out


def fine_db(spectrum, reference, low=20, high=16000, points=400):
    f = np.geomspace(low, high, points)
    return f, bands_db(spectrum, f, 2 ** (1 / 24)) - bands_db(reference, f, 2 ** (1 / 24))


def corner(curve, highpass):
    """The -3 dB frequency, the slope over the octave from 2 to 4 times as far out (dB per
    octave, where the curve is above -50 dB), and a high-pass's floor (below 40 Hz)."""
    f, g = curve
    span = (f < 3000) if highpass else (f > 100) & (f < 13000)
    f, g = f[span], g[span]
    below = np.where(g < -3)[0]
    if not len(below) or (highpass and below[-1] + 1 >= len(g)) or (not highpass and below[0] == 0):
        return {"corner_hz": None, "slope_db_per_octave": None}
    k = below[-1] if highpass else below[0]
    j = k + 1 if highpass else k - 1
    fc = float(np.exp(np.interp(-3, [min(g[j], g[k]), max(g[j], g[k])],
                                [np.log(f[k]), np.log(f[j])])))
    lo, hi = (fc / 4, fc / 2) if highpass else (2 * fc, 4 * fc)
    use = (f >= lo) & (f <= hi) & (g > -50)
    slope = abs(np.polyfit(np.log2(f[use]), g[use], 1)[0]) if use.sum() > 3 else None
    out = {"corner_hz": round(fc, 1),
           "slope_db_per_octave": round(float(slope), 1) if slope is not None else None}
    if highpass:
        out["floor_db"] = round(float(np.median(g[(f > 25) & (f < 40)])), 1)
    return out


def filters(wet):
    open_wet = wet_of(wet, "sweep")
    out, curves = {"low": {}, "high": {}}, {}
    for knob, highpass in (("low", True), ("high", False)):
        for position in ["3:00", "12:00", "9:00", "7:00"]:
            name = f"{knob}-{analyze_dd8.tag(position)}"
            curve = fine_db(wet_of(wet, name), open_wet)
            curves[(knob, position)] = curve
            out[knob][position] = {**corner(curve, highpass),
                                   "db_re_open": rounded(bands_db(wet_of(wet, name))
                                                         - bands_db(open_wet), 1)}
    both = fine_db(wet_of(wet, "both-0700"), open_wet)
    total = curves[("low", "7:00")][1] + curves[("high", "7:00")][1]
    audible = (both[0] > 40) & (both[0] < 10000) & (both[1] > -35)  # above, the wet's own cut
    out["both_0700_minus_sum_db_max"] = round(float(np.max(np.abs(both[1] - total)[audible])), 2)
    curves["both"] = both
    return out, curves


def delay_trace(t, played):
    """A held tone's delay (ms) from its phase against the input (modulo one period), and
    its level."""
    freq = t.meta["signal"]["freq"]
    x = played[t.meta["signal_start"]:][:int(t.meta["signal"]["seconds"] * RATE)]
    y = t.pedal[t.start:t.start + len(x)]
    z = scipy.signal.hilbert(y) * np.conj(scipy.signal.hilbert(x))
    b, a = scipy.signal.butter(2, 40 / (RATE / 2))
    z = scipy.signal.filtfilt(b, a, z.real) + 1j * scipy.signal.filtfilt(b, a, z.imag)
    delay = -np.unwrap(np.angle(z)) / (2 * np.pi * freq) * 1000
    level = scipy.signal.filtfilt(b, a, np.abs(scipy.signal.hilbert(y)))
    steady = slice(int(0.5 * RATE), len(delay) - int(0.5 * RATE))
    return delay[steady], level[steady], 1000 / freq


def shape_fit(cycle):
    """delay = min + swing * triangle^power (triangle 1 at the cycle's peak): the best
    power, offset and residual."""
    x = np.arange(len(cycle)) / len(cycle)
    best = None
    for shift in np.linspace(-0.08, 0.08, 81):
        for power in np.linspace(0.6, 2.0, 57):
            basis = np.abs(2 * ((x + shift) % 1) - 1) ** power
            design = np.vstack([basis, np.ones_like(basis)]).T
            coef, *_ = np.linalg.lstsq(design, cycle, rcond=None)
            error = rms(design @ coef - cycle)
            if best is None or error < best[0]:
                best = (error, power, coef)
    error, power, (swing, low) = best
    return {"min_ms": round(float(low), 3), "swing_ms": round(float(swing), 3),
            "power": round(float(power), 2), "rms_us": round(error * 1000, 1)}


def lfo(wet, base_ms):
    out, cycles = {}, {}
    for path in sorted(wet.glob("hold-*.json")):
        t = analysis.Take(wet, path.stem)
        played, _ = soundfile.read(wet / "played" / f"{path.stem}.wav")
        delay, level, period_ms = delay_trace(t, played)
        delay += period_ms * np.round((base_ms - delay.mean()) / period_ms)
        rate, phase = analysis.lfo_frequency(delay[::48], RATE / 48, low=0.05, high=20)
        cycle = analysis.fold_cycle(delay[::48], RATE / 48, rate, phase, 128)
        settings = t.meta["settings"]
        out[path.stem] = {
            "rate": settings["Rate"], "depth": settings["Depth"],
            "lfo_hz": round(float(rate), 3),
            "delay_ms": {"min": round(float(np.percentile(delay, 0.5)), 3),
                         "mean": round(float(delay.mean()), 3),
                         "max": round(float(np.percentile(delay, 99.5)), 3)},
            "swing_ms_peak_to_peak": round(float(np.percentile(delay, 99.5)
                                                 - np.percentile(delay, 0.5)), 3),
            "shape": shape_fit(cycle) if np.ptp(cycle) > 0.1 else None,
            "level_wobble_db": round(float(np.percentile(db(level), 99)
                                           - np.percentile(db(level), 1)), 2)}
        cycles[path.stem] = cycle
    return out, cycles


def bursts(wet, base_ms):
    t = analysis.Take(wet, "bursts")
    played, _ = soundfile.read(wet / "played" / "bursts.wav")
    spec = t.meta["signal"]
    x = played[t.meta["signal_start"]:]
    lag = int(round(base_ms / 1000 * RATE))
    period = spec["step_seconds"] + spec["gap_seconds"]
    rows, first = [], None
    for k, level in enumerate(spec["levels_db"]):
        w = slice(int((k * period + 0.08) * RATE), int((k * period + 0.24) * RATE))
        y = t.pedal[t.start + w.start + lag:t.start + w.stop + lag]
        gain = db(rms(y) / rms(x[w]))
        first = gain if first is None else first
        h = analysis.harmonics_db(y, RATE, spec["freq"], 5)
        rows.append({"input_db": level, "gain_db": round(float(gain), 2),
                     "re_linear_db": round(float(gain - first), 2),
                     "h2_db": round(h[0], 1), "h3_db": round(h[1], 1)})
    return rows


def note_off(wet):
    t = analysis.Take(wet, "note-off")
    note = t.meta["signal"]["step_seconds"]
    y = t.signal(t.meta["signal"].get("tail", 1))
    floor = analysis.rms_db(analysis.Take(wet, "noise-floor").signal()[RATE:])
    windows = [(0.02, 0.05), (0.05, 0.1), (0.1, 0.15), (0.15, 0.3), (0.3, 0.6), (0.6, 1.0),
               (1.0, 2.0), (2.0, 3.5)]
    return {"noise_floor_dbfs": round(floor, 1),
            "after_note_dbfs": {f"{a}-{b} s": round(analysis.rms_db(
                y[int((note + a) * RATE):int((note + b) * RATE)]), 1) for a, b in windows}}


def noise(session):
    x = analysis.Take(session, "noise-floor").signal()[RATE:]
    shape = analysis.third_octave_db(x[:RATE * 8], RATE)
    return {"rms_dbfs": round(analysis.rms_db(x), 1),
            "peak_dbfs": round(float(db(np.abs(x).max())), 1),
            "third_octaves_db_re_1khz": rounded(shape - shape[BANDS.index(1000)], 1)}


def plot(out_dir, result, curves, cycles):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 3, figsize=(18, 9.5))
    tone = result["wet_tone"]
    axis = axes[0, 0]
    axis.semilogx(BANDS, tone["db_re_1khz"], "o-", label="wet only, Depth min")
    mono = np.array(tone["db_re_1khz"]) + tone["mono_wet_minus_wet_only_db"]["sweep"]
    valid = np.array(BANDS) >= 125  # below, the dry path's ringing gets into the wet window
    axis.semilogx(np.array(BANDS)[valid], mono[valid], "x--", label="its part of the mono mix")
    axis.set_ylim(-60, 5)
    axis.set_title("The chorus sound's tone, re 1 kHz (filters open)", fontsize=10)
    axis.legend(fontsize=8)
    for col, knob in ((1, "low"), (2, "high")):
        axis = axes[0, col]
        for position in ["3:00", "12:00", "9:00", "7:00"]:
            f, g = curves[(knob, position)]
            axis.semilogx(f, g, label=f"{knob.capitalize()} {position}")
        if knob == "high":
            f, g = curves["both"]
            axis.semilogx(f, g, "k:", label="both 7:00")
        axis.set_ylim(-60, 5)
        axis.set_title(f"{knob.capitalize()} filter, re fully clockwise (wet only)", fontsize=10)
        axis.legend(fontsize=8)
    lfo_rows = result["lfo"]
    axis = axes[1, 0]
    rate_rows = [lfo_rows[f"hold-rate-{analyze_dd8.tag(p)}"] for p in POSITIONS]
    axis.semilogy([analyze_dd8.clock_fraction(p) for p in POSITIONS],
                  [r["lfo_hz"] for r in rate_rows], "o-")
    axis.set_xticks([analyze_dd8.clock_fraction(p) for p in POSITIONS], POSITIONS)
    axis.set_ylabel("LFO, Hz (Depth 12:00)")
    axis.set_title("Rate knob → LFO rate", fontsize=10)
    axis = axes[1, 1]
    depth_names = {p: (f"hold-depth-{analyze_dd8.tag(p)}" if p != "12:00" else "hold-rate-1200")
                   for p in POSITIONS}
    for position, name in depth_names.items():
        cycle = cycles[name]
        axis.plot(np.arange(len(cycle)) / len(cycle), cycle, label=f"Depth {position}")
    axis.set_xlabel("LFO cycle")
    axis.set_ylabel("delay, ms")
    axis.set_title("One LFO cycle, folded (Rate 12:00)", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[1, 2]
    rows = result["bursts"]
    axis.plot([r["input_db"] for r in rows], [r["re_linear_db"] for r in rows], "o-",
              label="gain re linear, dB")
    axis.plot([r["input_db"] for r in rows], [r["h3_db"] / 10 for r in rows], "x:",
              label="3rd harmonic, dB / 10")
    axis.set_xlabel("input, dB re the guitar's peak")
    axis.set_title("The wet path against the input level (bursts, Depth min)", fontsize=10)
    axis.legend(fontsize=8)
    for axis in axes.flat:
        axis.grid(alpha=0.3, which="both")
    figure.suptitle("Boss CE-5 Chorus Ensemble")
    figure.tight_layout()
    figure.savefig(out_dir / "ce-5.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("wet_session")
    parser.add_argument("mono_session")
    args = parser.parse_args()
    wet = pathlib.Path(args.wet_session).expanduser()
    mono = pathlib.Path(args.mono_session).expanduser()
    for session in (wet, mono):
        if not (session / "rig.json").exists():
            sys.exit(f"{session} is not a capture session")

    result = {"pedal": "Boss CE-5 Chorus Ensemble", "sessions": [wet.name, mono.name],
              "dry_path": dry_path(mono), "wet_tone": wet_tone(wet, mono),
              "elevel_wet_db_1khz": elevel(wet, mono)}
    result["filter"], curves = filters(wet)
    result["lfo"], cycles = lfo(wet, result["wet_tone"]["delay_ms"])
    result["bursts"] = bursts(wet, result["wet_tone"]["delay_ms"])
    result["note_off"] = note_off(wet)
    result["noise"] = {"wet_only": noise(wet), "mono": noise(mono)}

    out_dir = wet / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "ce-5.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, curves, cycles)
    print(json.dumps({k: v for k, v in result.items() if k not in ("pedal", "sessions")},
                     indent=1))


if __name__ == "__main__":
    main()
