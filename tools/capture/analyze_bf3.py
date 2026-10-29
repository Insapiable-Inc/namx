#!/usr/bin/env python3
"""Measures the Boss BF-3 Flanger (STANDARD mode) from one mono session (Guitar In ->
Output A; plans/bf-3-standard.json). Findings: captures/boss/bf-3/README.md.

    analyze_bf3.py SESSION [CHECKS [QUIET]]     # writes SESSION/fit/bf-3.{json,svg}

CHECKS (plans/bf-3-standard-checks.json): the model's sweep against click takes at other
knob combinations, and whether a plug in Output B changes what Output A carries. QUIET
(plans/bf-3-res-quiet.json): Res 5:00 again at -24 dB — at -12 dB the comb's peaks reached
the output clip, which flattens the ripple.

What it measures:
  dry path         the dry part of a static sweep against the bypass sweep: delay, gain
  wet tone         Manual 7:00 (the longest delay, 14.7 ms after the dry, so the two split
                   in time): delay, gain and response against the bypass, group delay
  Manual           the wet's delay per position, from the comb (sweep minus the dry, over
                   the wet's own response: the phase slope)
  Res              the loop gain per pass: the clean echo at Manual 7:00 (response), and per
                   position the depth of the comb's ripple at Manual 12:00 (phase-free)
  bursts           gain and harmonics against the input level (Res minimum and maximum):
                   one hard ceiling on the summed output
  ring, noise      Res 5:00 after one click; the floor with nothing played
  sweep            click trains (a click every 25 ms, its echo at the current delay): the
                   delay per click, LFO rate, floor and swing per Rate, Depth and Manual
                   position, the cycle's shape (a smoothed triangle to a power)

Takes are aligned to the played signal through the loopback, but the pedal runs on its own
clock (~44.1 kHz): its output lands up to a sample apart from take to take, so a take is
only compared with itself or with its dry part re-fitted (the clicks' dry template is
shifted and scaled per take).
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import scipy.optimize
import scipy.signal
import soundfile

import analysis
import analyze_dd8

RATE = 48000
N_IR = RATE // 2
FREQS = np.fft.rfftfreq(N_IR, 1 / RATE)
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
WET_SPLIT_MS = 7.3  # Manual 7:00: the dry before, the wet (14.7 ms later) after
CLICK_RATE = 40.0  # clicks per second
SHAPE_BINS = 64
REPORT_HZ = [63, 125, 250, 500, 1000, 2000, 4000, 6300, 8000, 10000, 12500, 16000]


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rounded(values, digits=2):
    return [round(float(v), digits) for v in values]


def impulse(session, name):
    return analyze_dd8.head(analyze_dd8.impulse_response(session, name)[0], N_IR)


def window(h, start_ms, stop_ms):
    t = analyze_dd8.head_times(N_IR, RATE) * 1000
    return np.fft.rfft(np.where((t >= start_ms) & (t < stop_ms), h, 0.0))


def band_db(spectrum, f, width=1.06):
    keep = (FREQS > f / width) & (FREQS < f * width)
    return float(10 * np.log10(np.mean(np.abs(spectrum[keep]) ** 2) + 1e-30))


def phase_delay_ms(ratio, low=300, high=5000):
    keep = (FREQS > low) & (FREQS < high)
    slope = np.polyfit(FREQS[keep], np.unwrap(np.angle(ratio[keep])), 1)[0]
    return float(-slope / (2 * np.pi) * 1000)


def group_delay_ms(ratio, f):
    return phase_delay_ms(ratio, f / 1.1, f * 1.1)


class Static:
    """The bypass and Manual 7:00 sweeps, split: the dry and the wet against the bypass."""

    def __init__(self, session):
        self.session = session
        self.bypass = window(impulse(session, "bypass-sweep"), -1, 14)
        h = impulse(session, "manual-0700")
        self.dry = window(h, -1, WET_SPLIT_MS)
        self.wet = window(h, WET_SPLIT_MS, 500)
        self.dry_ms = phase_delay_ms(self.dry / self.bypass)
        self.wet_ms = phase_delay_ms(self.wet / self.bypass)  # re the bypass
        # the wet's own response, its delay taken out
        self.wet_tone = self.wet / self.bypass * np.exp(2j * np.pi * FREQS * self.wet_ms / 1000)


def dry_path(static):
    ratio = static.dry / static.bypass
    return {"delay_ms": round(static.dry_ms, 3),
            "gain_db_1khz": round(band_db(static.dry, 1000) - band_db(static.bypass, 1000), 2),
            "db_re_bypass": dict(zip(map(str, REPORT_HZ), rounded(
                [band_db(static.dry, f) - band_db(static.bypass, f) for f in REPORT_HZ]))),
            "group_delay_ms": {str(f): round(group_delay_ms(ratio, f), 3)
                               for f in (1000, 4000, 12500)}}


def wet_tone(static):
    level = [band_db(static.wet, f) - band_db(static.bypass, f) for f in REPORT_HZ]
    at_1k = level[REPORT_HZ.index(1000)]
    ratio = static.wet / static.dry
    return {"delay_ms_after_dry": round(static.wet_ms - static.dry_ms, 3),
            "gain_db_1khz_re_bypass": round(at_1k, 2),
            "gain_db_1khz_re_dry": round(at_1k - (band_db(static.dry, 1000)
                                                  - band_db(static.bypass, 1000)), 2),
            "db_re_1khz": dict(zip(map(str, REPORT_HZ), rounded(np.array(level) - at_1k))),
            "group_delay_after_dry_ms": {str(f): round(group_delay_ms(ratio, f), 3)
                                         for f in (250, 1000, 4000, 10000)}}


def manual_map(static):
    """The wet's delay after the dry per Manual position (Depth minimum)."""
    bypass_h = np.fft.rfft(impulse(static.session, "bypass-sweep"))
    dry_ratio = static.dry / static.bypass
    out = {}
    for position in POSITIONS:
        name = f"manual-{analyze_dd8.tag(position)}"
        comb = np.fft.rfft(impulse(static.session, name)) / bypass_h - dry_ratio
        out[position] = round(phase_delay_ms(comb / static.wet_tone, 300, 6000) - static.dry_ms, 3)
    return out


def ripple_db(session, base, name, split_ms, centres):
    """|loop gain| from the comb's ripple: the wet over its Res-minimum twin swings between
    1/(1 - x) and 1/(1 + x) once per 1/delay Hz, whatever the loop's phase."""
    h = impulse(session, base)
    dry, wet = window(h, -1, split_ms), window(h, split_ms, 500)
    ratio = np.abs((window(impulse(session, name), -1, 500) - dry) / wet)
    out = []
    for f in centres:
        keep = (FREQS > f * 0.75) & (FREQS < f * 1.25)
        high, low = np.percentile(ratio[keep], 98), np.percentile(ratio[keep], 2)
        out.append(float(db((high - low) / (high + low))))
    return out


def res(static):
    session = static.session
    # Manual 7:00, Res 3:00: the first echo of the wet, clean
    h = impulse(session, "res-0300-manual-0700")
    period = static.wet_ms - static.dry_ms
    first = window(h, WET_SPLIT_MS, WET_SPLIT_MS + period)
    second = window(h, WET_SPLIT_MS + period, WET_SPLIT_MS + 2 * period)
    passes = [band_db(second, f) - band_db(first, f) for f in REPORT_HZ]
    centres = [500, 1000, 2000, 4000]
    per_position = {"7:00": None}
    for position in POSITIONS[1:]:
        per_position[position] = dict(zip(map(str, centres), rounded(ripple_db(
            session, "manual-1200", f"res-{analyze_dd8.tag(position)}", 1.6, centres))))
    unchanged = window(impulse(session, "manual-0700"), -1, WET_SPLIT_MS)
    dry_change = band_db(window(h, -1, WET_SPLIT_MS), 1000) - band_db(unchanged, 1000)
    first_change = band_db(first, 1000) - band_db(static.wet, 1000)
    return {"pass_db_res_0300_manual_0700": dict(zip(map(str, REPORT_HZ), rounded(passes))),
            "pass_delay_ms": round(group_delay_ms(second / first, 1000), 3),
            "pass_db_from_ripple_manual_1200": per_position,
            "res_0300_manual_0700_from_ripple": dict(zip(map(str, centres), rounded(
                ripple_db(session, "manual-0700", "res-0300-manual-0700", WET_SPLIT_MS,
                          centres)))),
            "res_0300_changes_dry_db": round(dry_change, 2),
            "res_0300_changes_first_wet_db": round(first_change, 2)}


def bursts(session, name):
    t = analysis.Take(session, name)
    played, _ = soundfile.read(session / "played" / f"{name}.wav")
    spec = t.meta["signal"]
    x = played[t.meta["signal_start"]:]
    period = spec["step_seconds"] + spec["gap_seconds"]
    rows = []
    for k, level in enumerate(spec["levels_db"]):
        w = slice(int((k * period + 0.08) * RATE), int((k * period + 0.24) * RATE))
        y = t.pedal[t.start + w.start:t.start + w.stop]
        h = analysis.harmonics_db(y, RATE, spec["freq"], 5)
        rows.append({"input_db": level,
                     "gain_db": round(float(db(np.std(y) / np.std(x[w]))), 2),
                     "h2_db": round(h[0], 1), "h3_db": round(h[1], 1)})
    return rows


def clip_ceiling(bypass_rows, burst_sets):
    """One hard clip on the output, its ceiling re the guitar's peak (in the bypass path's
    units): fitted to every burst take's gain loss."""
    bypass = bypass_rows[0]["gain_db"]
    tone = np.sin(2 * np.pi * 1000 * np.arange(4800) / RATE)

    def loss(peak_db, ceiling_db):
        a = 10 ** (peak_db / 20)
        c = 10 ** (ceiling_db / 20)
        return float(db(np.std(np.clip(a * tone, -c, c)) / np.std(a * tone)))

    def error(ceiling_db):
        e = []
        for rows in burst_sets:
            linear = rows[0]["gain_db"]
            e += [linear + loss(r["input_db"] + linear - bypass, ceiling_db) - r["gain_db"]
                  for r in rows]
        return np.array(e)

    grid = np.arange(-2, 4, 0.01)
    best = grid[np.argmin([np.sum(error(c) ** 2) for c in grid])]
    return {"ceiling_db_re_guitar_peak": round(float(best), 2),
            "rms_error_db": round(float(np.sqrt(np.mean(error(best) ** 2))), 2)}


def ring(session):
    t = analysis.Take(session, "res-0500-click")
    y = t.pedal[t.start:t.start + 4 * RATE]
    return {"rms_dbfs_per_50ms": rounded([analysis.rms_db(y[i:i + 2400])
                                          for i in range(0, 12000, 2400)], 1)}


def noise(session):
    t = analysis.Take(session, "noise-floor")
    y = t.pedal[t.start + RATE // 2:t.start + 9 * RATE]
    return {"rms_dbfs": round(analysis.rms_db(y), 1),
            "peak_dbfs": round(float(db(np.max(np.abs(y)))), 1),
            "third_octaves_dbfs": rounded(analysis.third_octave_db(y, RATE), 1)}


# --- the sweep, from click trains


def shifted(x, samples):
    n = len(x)
    f = np.fft.rfftfreq(2 * n)
    return np.fft.irfft(np.fft.rfft(x, 2 * n) * np.exp(-2j * np.pi * f * samples), 2 * n)[:n]


def click_delays(session, name, static):
    """The wet's delay (ms after the dry) for every click: the click's dry (the Manual 7:00
    sweep's dry, shifted and scaled to the take's median frame) is taken out, and the rest
    matched against the wet's own impulse (16x upsampled cross-correlation)."""
    t = analysis.Take(session, name)
    spec = t.meta["signal"]
    period = int(round(spec["period"] * RATE))
    frames = t.pedal[t.start:t.start + spec["count"] * period].reshape(spec["count"], period)
    dry = np.fft.irfft(static.dry, N_IR)[:period]  # a one-sample click: the dry's impulse
    wet = np.fft.irfft(static.wet_tone * static.bypass, N_IR)[:400]
    median = np.median(frames, 0)
    best = None
    for samples in np.arange(-2, 2.001, 0.02):
        d = shifted(dry, samples)[15:45]
        g = np.dot(median[15:45], d) / np.dot(d, d)
        e = np.sum((median[15:45] - g * d) ** 2)
        if best is None or e < best[0]:
            best = (e, samples, g)
    dry = best[2] * shifted(dry, best[1])
    up, n = 16, 1 << 14
    template = np.conj(np.fft.rfft(wet, n))
    lags_ms = np.arange(n * up) / up / RATE * 1000
    search = np.flatnonzero((lags_ms >= 0.1) & (lags_ms <= 16))
    delays = []
    for frame in frames:
        xc = np.fft.irfft(np.fft.rfft(frame - dry, n) * template, n * up)
        i = search[np.argmax(xc[search])]
        a, b, c = xc[i - 1], xc[i], xc[i + 1]
        delays.append((i + 0.5 * (a - c) / (a - 2 * b + c)) / up / RATE * 1000)
    return np.array(delays) - static.dry_ms, t.meta["settings"]


def lfo_shape(rate, cutoff_hz, power, bins=SHAPE_BINS, per_second=4000):
    """One cycle of the model's sweep in 0..1: a triangle, one-pole smoothed, to a power;
    rolled so its minimum comes first."""
    n = int(round(per_second / rate))
    phase = (np.arange(8 * n) / n) % 1
    triangle = 1 - np.abs(2 * phase - 1)
    a = np.exp(-2 * np.pi * cutoff_hz / per_second)
    x = scipy.signal.lfilter([1 - a], [1, -a], triangle, zi=[0.0])[0][-n:]
    y = np.clip(x, 0, 1) ** power
    y = y[(np.arange(bins) * n // bins)]
    return np.roll(y, -int(np.argmin(y)))


def sweep(session, static):
    rows, cycles = {}, {}
    names = sorted(p.stem for p in session.glob("*.json")
                   if p.stem not in ("rig",) and json.load(open(p)).get("signal", {})
                   .get("period") == 0.025 and p.stem != "bypass-clicks")
    for name in names:
        delays, settings = click_delays(session, name, static)
        row = {"settings": settings,
               "delay_ms": {"min": round(float(np.percentile(delays, 0.5)), 3),
                            "mean": round(float(delays.mean()), 3),
                            "max": round(float(np.percentile(delays, 99.5)), 3)}}
        if np.ptp(delays) > 0.05:
            rate, phase = analysis.lfo_frequency(delays, CLICK_RATE, low=0.03, high=19.9)
            row["lfo_hz"] = round(float(rate), 4)
            if rate < 5:
                cycle = analysis.fold_cycle(delays, CLICK_RATE, rate, phase, SHAPE_BINS)
                cycles[name] = np.roll(cycle, -int(np.argmin(cycle)))
            else:
                # ~10 Hz against 40 clicks a second: only four phases ever come round, so
                # the swing is read from their fundamental
                four = delays[:len(delays) // 4 * 4].reshape(-1, 4).mean(0)
                row["four_phases_ms"] = rounded(four, 3)
                row["fundamental_ms_peak_to_peak"] = round(
                    float(4 * np.abs(np.fft.fft(four)[1]) / 4), 3)
        rows[name] = row
    return rows, cycles


def fit_shape(rows, cycles):
    """The cycle's shape over the Rate takes (Depth and Manual 12:00, so one floor and one
    swing for all): min + swing * smooth(triangle)^power, one smoothing cutoff for all
    rates. Rate 5:00 (four phases only) is predicted, not fitted."""
    takes = sorted((n for n in cycles if n.startswith("rate-")), key=lambda n: rows[n]["lfo_hz"])

    def model(p, name, bins=SHAPE_BINS):
        return p[2] + p[3] * lfo_shape(rows[name]["lfo_hz"], p[0], p[1], bins)

    def error(p):
        return np.concatenate([model(p, name) - cycles[name] for name in takes])

    fit = scipy.optimize.least_squares(error, [1.5, 2.4, 0.9, 4.8], diff_step=1e-3)
    p = fit.x
    result = {"smoothing_hz": round(float(p[0]), 3), "power": round(float(p[1]), 3),
              "min_ms": round(float(p[2]), 3), "swing_ms": round(float(p[3]), 3),
              "rms_us": round(float(np.sqrt(np.mean(fit.fun ** 2)) * 1000), 1), "per_take": {}}
    for name in takes:
        m, c = model(p, name), cycles[name]
        result["per_take"][name] = {
            "measured_ms": [round(float(c.min()), 3), round(float(c.max()), 3)],
            "model_ms": [round(float(m.min()), 3), round(float(m.max()), 3)],
            "rms_us": round(float(np.sqrt(np.mean((m - c) ** 2)) * 1000), 1)}
    for name in rows:
        if "fundamental_ms_peak_to_peak" in rows[name]:
            m = model(p, name, 400)
            result["per_take"][name] = {
                "measured_fundamental_ms": rows[name]["fundamental_ms_peak_to_peak"],
                "model_fundamental_ms": round(float(4 * np.abs(np.fft.fft(m)[1]) / 400), 3)}
    return result


def depth_law(rows, shape, manual):
    """Per Depth position (Manual 12:00, Rate 12:00): the floor and the unsmoothed swing.
    At Depth 12:00 the floor follows Manual's delay in a straight line (three Manual
    positions): floor = (1 - a) * manual + a * low, and the swing doesn't move."""
    reduce = np.ptp(lfo_shape(rows["rate-1200"]["lfo_hz"], shape["smoothing_hz"],
                              shape["power"], bins=400))
    names = {"9:00": "depth-0900", "12:00": "rate-1200", "3:00": "depth-0300",
             "5:00": "depth-0500"}
    at_1200 = [("9:00", "sweep-manual-0900"), ("12:00", "rate-1200"),
               ("3:00", "sweep-manual-0300")]
    m = np.array([manual[p] for p, _ in at_1200])
    floors = np.array([rows[n]["delay_ms"]["min"] for _, n in at_1200])
    slope, intercept = np.polyfit(m, floors, 1)
    low = intercept / (1 - slope)
    out = {"manual_12": manual["12:00"], "low_ms": round(float(low), 3),
           "depth_1200_floor_fit": {"slope": round(float(slope), 3),
                                    "intercept_ms": round(float(intercept), 3),
                                    "rms_ms": round(float(np.std(floors - (slope * m + intercept))), 3)},
           "depth_1200_swing_by_manual_ms": {p: round(rows[n]["delay_ms"]["max"]
                                                      - rows[n]["delay_ms"]["min"], 3)
                                             for p, n in at_1200},
           "positions": {"7:00": {"blend": 0.0, "swing_ms": 0.0}}}
    for position, name in names.items():
        floor = rows[name]["delay_ms"]["min"]
        swing = (rows[name]["delay_ms"]["max"] - floor) / reduce
        blend = (manual["12:00"] - floor) / (manual["12:00"] - low)
        out["positions"][position] = {"floor_ms": floor, "blend": round(float(blend), 3),
                                      "swing_ms": round(float(swing), 3)}
    deep = [rows[n]["delay_ms"] for n in ("depth-0500", "depth-0500-manual-0700")]
    out["depth_0500_manual_1200_vs_0700_ms"] = [[d["min"], d["max"]] for d in deep]
    return out


def checks(session, static, result):
    """Each check take's sweep against the model's (fed the take's own LFO rate: the knobs
    are set by eye, a second time), and the Manual 7:00 sweep with and without a plug in
    Output B."""
    shape, depth = result["shape"], result["depth"]
    manual = result["manual_delay_ms_after_dry"]
    out = {}
    for path in sorted(session.glob("*.json")):
        meta = json.load(open(path))
        if meta.get("signal", {}).get("period") != 0.025 or not meta.get("settings"):
            continue
        delays, settings = click_delays(session, path.stem, static)
        row = {"settings": settings,
               "measured_ms": rounded(np.percentile(delays, [0.5, 99.5]), 3)}
        position = depth["positions"][settings["Depth"]]
        floor = ((1 - position["blend"]) * manual[settings["Manual"]]
                 + position["blend"] * depth["low_ms"])
        if np.ptp(delays) > 0.05:
            rate, _ = analysis.lfo_frequency(delays, CLICK_RATE, low=0.03, high=19.9)
            s = lfo_shape(rate, shape["smoothing_hz"], shape["power"], bins=400)
            row["lfo_hz"] = round(float(rate), 3)
            row["model_ms"] = rounded([floor + position["swing_ms"] * s.min(),
                                       floor + position["swing_ms"] * s.max()], 3)
        else:
            row["model_ms"] = [round(float(floor), 3)] * 2
        out[path.stem] = row
    split = {}
    for name in ("manual-0700", "b-plugged-manual-0700"):
        h = impulse(session, name)
        split[name] = (window(h, -1, WET_SPLIT_MS), window(h, WET_SPLIT_MS, 500))
    plain, plugged = split["manual-0700"], split["b-plugged-manual-0700"]
    out["output_b_plugged_db"] = {
        part: {str(f): round(band_db(plugged[k], f) - band_db(plain[k], f), 2)
               for f in (250, 1000, 4000, 10000)}
        for k, part in enumerate(("dry", "wet"))}
    return out


def plot(out_dir, result, static, cycles):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 3, figsize=(18, 9.5))
    axis = axes[0, 0]
    f = np.geomspace(40, 20000, 400)
    for label, spectrum in (("dry", static.dry), ("wet (Manual 7:00)", static.wet)):
        axis.semilogx(f, [band_db(spectrum, x, 1.03) - band_db(static.bypass, x, 1.03)
                          for x in f], label=label)
    axis.set_ylim(-40, 8)
    axis.set_title("Dry and wet against the bypass, dB", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[0, 1]
    manual = result["manual_delay_ms_after_dry"]
    x = [analyze_dd8.clock_fraction(p) for p in POSITIONS]
    axis.semilogy(x, [manual[p] for p in POSITIONS], "o-")
    axis.set_xticks(x, POSITIONS)
    axis.set_ylabel("wet delay after the dry, ms")
    axis.set_title("Manual (Depth minimum)", fontsize=10)
    axis = axes[0, 2]
    passes = result["res"]["pass_db_from_ripple_manual_1200"]
    for position in POSITIONS[1:]:
        row = passes[position]
        axis.semilogx([int(k) for k in row], list(row.values()), "o-", label=f"Res {position}")
    row = result["res"]["pass_db_res_0300_manual_0700"]
    axis.semilogx([int(k) for k in row], list(row.values()), "k:",
                  label="Res 3:00, Manual 7:00 (echo)")
    axis.set_ylim(-30, 0)
    axis.set_title("Res: gain per pass round the loop, dB", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[1, 0]
    rates = [f"rate-{analyze_dd8.tag(p)}" for p in POSITIONS]
    axis.semilogy(x, [result["sweep"][n]["lfo_hz"] for n in rates], "o-")
    axis.set_xticks(x, POSITIONS)
    axis.set_ylabel("LFO, Hz")
    axis.set_title("Rate (Depth and Manual 12:00)", fontsize=10)
    axis = axes[1, 1]
    for name in ("depth-0900", "rate-1200", "depth-0300", "depth-0500", "sweep-manual-0900",
                 "sweep-manual-0300", "rate-0300"):
        cycle = cycles[name]
        axis.plot(np.arange(len(cycle)) / len(cycle), cycle, label=name)
    axis.set_xlabel("LFO cycle")
    axis.set_ylabel("delay after the dry, ms")
    axis.set_title("One cycle, folded", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[1, 2]
    for key in ("bursts", "bursts_res_0500"):
        rows = result[key]
        line, = axis.plot([r["input_db"] for r in rows],
                          [r["gain_db"] - rows[0]["gain_db"] for r in rows], "o-",
                          label=f"{key}: gain re linear")
        axis.plot([r["input_db"] for r in rows], [r["h3_db"] / 10 for r in rows], "x:",
                  color=line.get_color(), label=f"{key}: H3 / 10")
    axis.set_xlabel("input, dB re the guitar's peak")
    axis.set_title("Against the input level (Manual 12:00, Depth minimum)", fontsize=10)
    axis.legend(fontsize=8)
    for axis in axes.flat:
        axis.grid(alpha=0.3, which="both")
    figure.suptitle("Boss BF-3 Flanger, STANDARD")
    figure.tight_layout()
    figure.savefig(out_dir / "bf-3.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    parser.add_argument("checks", nargs="?")
    parser.add_argument("quiet", nargs="?")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")

    static = Static(session)
    result = {"pedal": "Boss BF-3 Flanger, STANDARD", "session": session.name,
              "dry_path": dry_path(static), "wet_tone": wet_tone(static),
              "manual_delay_ms_after_dry": manual_map(static), "res": res(static),
              "bypass_bursts": bursts(session, "bypass-bursts"),
              "bursts": bursts(session, "bursts"),
              "bursts_res_0500": bursts(session, "bursts-res-0500")}
    result["clip"] = clip_ceiling(result["bypass_bursts"],
                                  [result["bursts"], result["bursts_res_0500"]])
    result["ring_res_0500"] = ring(session)
    result["noise"] = noise(session)
    result["sweep"], cycles = sweep(session, static)
    result["shape"] = fit_shape(result["sweep"], cycles)
    result["depth"] = depth_law(result["sweep"], result["shape"],
                                result["manual_delay_ms_after_dry"])

    if args.checks:
        checks_session = pathlib.Path(args.checks).expanduser()
        result["checks"] = checks(checks_session, static, result)
        result["sessions"] = [session.name, checks_session.name]

    if args.quiet:
        quiet = pathlib.Path(args.quiet).expanduser()
        result["res"]["pass_db_from_ripple_manual_1200"]["5:00"] = dict(zip(
            ("500", "1000", "2000", "4000"), rounded(ripple_db(
                quiet, "std-manual-1200-24", "std-res-0500-24", 1.6, [500, 1000, 2000, 4000]))))
        result["res"]["res_0500_note"] = ("from the -24 dB redo (" + quiet.name
                                          + "); the -12 dB take clipped")
        result["sessions"] = result.get("sessions", [session.name]) + [quiet.name]

    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "bf-3.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, static, cycles)
    print(json.dumps({k: v for k, v in result.items()
                      if k not in ("pedal", "session", "sweep", "bypass_bursts")}, indent=1))


if __name__ == "__main__":
    main()
