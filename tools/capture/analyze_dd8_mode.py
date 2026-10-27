#!/usr/bin/env python3
"""Measures one Boss DD-8 mode from a session recorded with plans/dd-8-<mode>.json (the
Analog and Tape plans; Standard has its own, analyze_dd8.py).

    analyze_dd8_mode.py SESSION       # e.g. captures/boss/dd-8/2026-10-26-tape; writes SESSION/fit/

Takes it uses when present (each part is skipped if its takes are missing):
  bursts / bypass-bursts            repeat level, distortion and ceiling against input level
  bursts-fine / bypass-bursts-fine  the same in finer steps
  steps / bypass-steps              how fast the repeat's gain follows a level change
  time-1200-m12                     the repeat's tone (deconvolved sweep)
  feedback-*-m12                    the fade per repeat, at 1, 2 and 4 kHz
  feedback-0500                     maximum Feedback: hold, fade or run away
  hold-sine                         the repeat's pitch and level over time: wow and flutter
  time-glide-turn                   Time turned by hand while a tone plays
  time-*  (clicks)                  the delay time
  noise-floor                       noise
Every delay is measured per take (Time is set by hand, a few ms apart each time). The dry
path is analog and unity, so "wet" = the take minus its bypass take, or minus the dry
tone fitted before the first repeat arrives.
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import scipy.signal

import analysis
import analyze_dd8

RATE = 48000


def take(session, name):
    path = session / f"{name}.json"
    return analysis.Take(session, name) if path.exists() else None


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def db(x):
    return float(20 * np.log10(max(x, 1e-12)))


def delay_between(a, b, low=0.015, high=1.2):
    """The lag (samples) at which |b| best matches |a| delayed, between low and high s."""
    n = min(len(a), len(b))
    c = scipy.signal.correlate(np.abs(b[:n]), np.abs(a[:n]), mode="full", method="fft")
    lags = np.arange(-n + 1, n)
    m = (lags > int(low * RATE)) & (lags < int(high * RATE))
    return int(lags[m][np.argmax(c[m])])


def self_delay(x, low=0.3, high=1.2):
    """The strongest period of |x| between low and high s (its autocorrelation)."""
    e = np.abs(x)
    lags = np.fft.irfft(np.abs(np.fft.rfft(e, 2 * len(e))) ** 2)[:len(e)]
    return int(low * RATE) + int(np.argmax(lags[int(low * RATE):int(high * RATE)]))


def bursts(session, name, reference):
    out, ref = take(session, name), take(session, reference)
    if not out or not ref:
        return None
    spec = out.meta["signal"]
    o, r = out.signal(1), ref.signal(1)
    n = min(len(o), len(r))
    wet = o[:n] - r[:n]
    lag = delay_between(r[:n], wet)
    period = spec["step_seconds"] + spec.get("gap_seconds", 0)
    rows, first = [], None
    for k, level in enumerate(spec["levels_db"]):
        start = int(k * period * RATE)
        w = slice(start + int(0.08 * RATE), start + int(0.24 * RATE))
        wr = slice(w.start + lag, w.stop + lag)
        gain = db(rms(wet[wr]) / rms(r[w]))
        first = gain if first is None else first
        h = analysis.harmonics_db(wet[wr], RATE, spec["freq"], 5)
        rows.append({"input_db": level, "gain_db": round(gain, 2),
                     "re_linear_db": round(gain - first, 2),
                     "peak_dbfs": round(db(np.abs(wet[wr]).max()), 2),
                     "h2_db": round(h[0], 1), "h3_db": round(h[1], 1), "h5_db": round(h[3], 1)})
    return {"delay_ms": round(lag / RATE * 1000, 2), "rows": rows}


def steps(session):
    out, ref = take(session, "steps"), take(session, "bypass-steps")
    if not out or not ref:
        return None
    o, r = out.signal(2), ref.signal(2)
    n = min(len(o), len(r))
    wet = o[:n] - r[:n]
    lag = delay_between(r[:n], wet)
    b, a = scipy.signal.butter(4, 200 / (RATE / 2))
    env = lambda x: scipy.signal.filtfilt(b, a, np.abs(scipy.signal.hilbert(x)))
    ew, ei = env(wet), env(r[:n])
    g = 20 * np.log10(np.maximum(ew[lag:] / np.maximum(ei[:n - lag], 1e-7), 1e-9))
    t = np.arange(len(g)) / RATE
    step = out.meta["signal"]["step_seconds"]
    points = {"settled, first level": (step - 1.0, step - 0.1),
              "2-6 ms after the jump": (step + 0.002, step + 0.006),
              "10-20 ms after": (step + 0.01, step + 0.02),
              "50-100 ms after": (step + 0.05, step + 0.1),
              "settled, second level": (2 * step - 1.0, 2 * step - 0.1),
              "2-6 ms after the jump back": (2 * step + 0.002, 2 * step + 0.006),
              "50-100 ms after it": (2 * step + 0.05, 2 * step + 0.1),
              "0.3-0.5 s after it": (2 * step + 0.3, 2 * step + 0.5),
              "settled, back": (3 * step - 1.0, 3 * step - 0.1)}
    return {"delay_ms": round(lag / RATE * 1000, 2), "levels_db": out.meta["signal"]["levels_db"],
            "wet_gain_db": {k: round(float(np.median(g[(t > a0) & (t < a1)])), 2)
                            for k, (a0, a1) in points.items()}}


def tone(session):
    if not take(session, "time-1200-m12"):
        return None
    h, _ = analyze_dd8.impulse_response(session, "time-1200-m12")
    dry, rep = analyze_dd8.first_repeat(h, RATE)
    dry_db = analysis.third_octave_db(np.pad(h[:1024] * np.hanning(2048)[1024:], (0, RATE - 1024)),
                                      RATE)
    segment = h[rep - 256:rep - 256 + 1024] * np.hanning(1024)
    rep_db = analysis.third_octave_db(np.pad(segment, (0, RATE - 1024)), RATE)
    shape = rep_db - dry_db
    shape -= shape[analysis.THIRD_OCTAVES_HZ.index(1000)]
    return {"delay_ms": round((rep - dry) / RATE * 1000, 2), "bands_hz": analysis.THIRD_OCTAVES_HZ,
            "db_re_1khz": [round(float(v), 2) for v in shape]}


def feedback(session):
    out = {}
    for position in ("9:00", "12:00", "3:00"):
        name = f"feedback-{analyze_dd8.tag(position)}-m12"
        if not take(session, name):
            continue
        h, _ = analyze_dd8.impulse_response(session, name)
        dry, rep = analyze_dd8.first_repeat(h, RATE)
        period = rep - dry
        at = [rep]
        for _ in range(3):
            guess = at[-1] + period
            window = np.abs(h[guess - 2000:guess + 2000])
            at.append(guess - 2000 + int(np.argmax(window)))
        out[position] = {"delay_ms": round(period / RATE * 1000, 2)}
        for f0 in (1000, 2000, 4000):
            levels = [analyze_dd8.band_db(h, k, RATE, f0) for k in at]
            out[position][f"{f0} Hz"] = [round(float(b - a), 2) for a, b in zip(levels, levels[1:])]
    return out


def feedback_max(session):
    t = take(session, "feedback-0500")
    if not t:
        return None
    env = np.abs(t.signal(0.5))
    period = self_delay(env)
    click = int(np.argmax(env[:300]))
    reps = []
    for n in range(1, 64):
        at = click + n * period
        if at + 100 > len(env):
            break
        reps.append(round(db(env[at - 100:at + 100].max() / env[click]), 2))
    trend = float(np.polyfit(np.arange(len(reps)), reps, 1)[0])
    return {"delay_ms": round(period / RATE * 1000, 2), "click_repeats_db": reps,
            "trend_db_per_repeat": round(trend, 3)}


def burst_decays(session):
    """Feedback from a 50 ms burst: each repeat's level read directly (robust to wow, which
    spoils the deconvolved sweeps). Names feedback-burst (3:00, the Analog plan) and
    feedback-burst-HHMM."""
    out = {}
    for path in sorted(session.glob("feedback-burst*.json")):
        stem = path.stem
        position = "3:00" if stem == "feedback-burst" else \
            f"{int(stem[-4:-2])}:{stem[-2:]}"
        x = take(session, stem).signal(8)
        period = self_delay(x)
        levels = []
        for k in range(0, 64):
            start = k * period
            if start + int(0.05 * RATE) > len(x):
                break
            levels.append(db(rms(x[start + int(0.01 * RATE):start + int(0.04 * RATE)])))
        steps = np.diff(levels[1:])  # repeat to repeat (the first entry is the burst itself)
        audible = [s for s, level in zip(steps, levels[2:]) if level > -80]
        out[position] = {"delay_ms": round(period / RATE * 1000, 2),
                         "first_repeat_re_burst_db": round(levels[1] - levels[0], 2),
                         "db_per_repeat": [round(float(v), 2) for v in steps[:12]],
                         "median_db_per_repeat":
                             round(float(np.median(audible)), 2) if audible else None}
    return out


def wet_of_tone(t, seconds):
    """A held tone's repeats alone: the dry tone, fitted before the first repeat, removed."""
    freq = t.meta["signal"]["freq"]
    x = t.signal(2)
    time = np.arange(len(x)) / RATE
    fit = slice(int(0.05 * RATE), int(0.3 * RATE))
    basis = np.vstack([np.sin(2 * np.pi * freq * time), np.cos(2 * np.pi * freq * time)]).T
    coef, *_ = np.linalg.lstsq(basis[fit], x[fit], rcond=None)
    return (x - basis @ coef)[:int(seconds * RATE)], freq


def pitch_and_level(wet, cutoff=30.0):
    z = scipy.signal.hilbert(wet)
    b, a = scipy.signal.butter(2, cutoff / (RATE / 2))
    level = scipy.signal.filtfilt(b, a, np.abs(z))[:-1]
    freq = scipy.signal.filtfilt(b, a, np.diff(np.unwrap(np.angle(z))) * RATE / (2 * np.pi))
    return freq, level


def wow_flutter(session):
    t = take(session, "hold-sine")
    if not t:
        return None
    wet, freq = wet_of_tone(t, t.meta["signal"]["seconds"])
    f, level = pitch_and_level(wet, cutoff=100.0)
    steady = slice(int(1.0 * RATE), len(f) - int(0.5 * RATE))
    cents = 1200 * np.log2(np.maximum(f[steady], 1) / freq)
    spectrum = np.abs(np.fft.rfft((cents - cents.mean()) * np.hanning(len(cents))))
    fr = np.fft.rfftfreq(len(cents), 1 / RATE)
    bands = {"wow (0.3-6 Hz)": (0.3, 6), "flutter (6-60 Hz)": (6, 60)}
    out = {"mean_cents": round(float(cents.mean()), 2), "rms_cents": round(float(cents.std()), 2),
           "peak_to_peak_cents": round(float(cents.max() - cents.min()), 2),
           "level_range_db": round(db(level[steady].max()) - db(level[steady].min()), 2)}
    for label, (lo, hi) in bands.items():
        m = (fr > lo) & (fr < hi)
        out[label] = {"strongest_hz": round(float(fr[m][np.argmax(spectrum[m])]), 2),
                      "rms_cents": round(float(np.sqrt(2 * np.sum(spectrum[m] ** 2))
                                               / np.sum(np.hanning(len(cents)))), 2)}
    return out, (np.arange(len(f)) / RATE, 1200 * np.log2(np.maximum(f, 1) / freq), level)


def glide(session):
    t = take(session, "time-glide-turn")
    if not t:
        return None
    wet, freq = wet_of_tone(t, t.meta["signal"]["seconds"])
    f, level = pitch_and_level(wet)
    delay = np.cumsum(1 - f / freq) / RATE * 1000
    cents = 1200 * np.log2(np.maximum(f, 1) / freq)
    steady = slice(int(0.5 * RATE), len(f) - int(0.2 * RATE))
    return ({"delay_change_ms": round(float(delay[steady][-1] - delay[steady][0]), 1),
             "deepest_cents": round(float(cents[steady].min()), 1),
             "level_range_db": round(db(level[steady].max()) - db(level[steady].min()), 2)},
            (np.arange(len(f)) / RATE, cents, delay - delay[int(0.5 * RATE)], level))


def click_delays(session):
    out = {}
    for position in analyze_dd8.KNOB_POSITIONS:
        t = take(session, f"time-{analyze_dd8.tag(position)}")
        if not t or t.meta["signal"]["type"] != "clicks":
            continue
        x, period = t.signal(1.5), t.meta["signal"]["period"]
        seg = np.abs(x[int(period * RATE):int(2 * period * RATE)])
        dry = int(np.argmax(seg[:100]))
        seg[:dry + 200] = 0
        out[position] = round((int(np.argmax(seg)) - dry) / RATE * 1000, 2)
    return out


def noise(session):
    t = take(session, "noise-floor")
    return round(analysis.rms_db(t.signal()[int(5 * RATE):]), 1) if t else None


def plot(out_dir, result, traces):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 2, figsize=(15, 9))
    if result.get("tone"):
        axes[0, 0].semilogx(result["tone"]["bands_hz"], result["tone"]["db_re_1khz"], "o-")
        axes[0, 0].set_ylim(-60, 5)
    axes[0, 0].set_title("The first repeat's tone, re 1 kHz (-12 dB sweep)", fontsize=10)
    axes[0, 0].set_xlabel("Hz")
    axes[0, 0].set_ylabel("dB")
    curve = result.get("bursts-fine") or result.get("bursts")
    if curve:
        rows = curve["rows"]
        x = [r["input_db"] for r in rows]
        axes[0, 1].plot(x, [r["re_linear_db"] for r in rows], "o-", label="output re linear (dB)")
        h3 = axes[0, 1].twinx()
        h3.plot(x, [r["h3_db"] for r in rows], "x:", color="C3", label="3rd harmonic (dB)")
        h3.set_ylabel("3rd harmonic of the repeat, dB (dotted)")
        axes[0, 1].legend(loc="lower left", fontsize=8)
    axes[0, 1].set_title("The repeat against the input level (bursts)", fontsize=10)
    axes[0, 1].set_xlabel("input, dB re the guitar's peak")
    if "hold" in traces:
        t, cents, level = traces["hold"]
        k = slice(int(RATE), len(t) - int(0.5 * RATE), 48)
        axes[1, 0].plot(t[k], cents[k], lw=0.7)
    axes[1, 0].set_title("Held 1 kHz tone, the repeat's pitch (cents): wow and flutter",
                         fontsize=10)
    axes[1, 0].set_xlabel("seconds")
    if "glide" in traces:
        t, cents, delay, level = traces["glide"]
        k = slice(int(0.5 * RATE), len(t) - int(0.2 * RATE), 48)
        axes[1, 1].plot(t[k], delay[k], label="delay change (ms)")
        twin = axes[1, 1].twinx()
        twin.plot(t[k], cents[k], color="C1", lw=0.6, label="pitch (cents)")
        twin.set_ylabel("repeat pitch (cents, orange)")
    axes[1, 1].set_title("Turning Time by hand (12:00 -> 3:00) during a held tone", fontsize=10)
    axes[1, 1].set_xlabel("seconds")
    axes[1, 1].set_ylabel("delay change (ms)")
    for axis in axes.flat:
        axis.grid(alpha=0.3, which="both")
    figure.suptitle(f"Boss DD-8, {result['mode']} mode")
    figure.tight_layout()
    figure.savefig(out_dir / f"dd-8-{result['mode'].lower()}.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")
    mode = session.name.split("-")[-1].capitalize()

    result = {"pedal": f"Boss DD-8, {mode} mode", "mode": mode, "session": session.name,
              "delay_ms_clicks": click_delays(session)}
    for name, reference in (("bursts", "bypass-bursts"), ("bursts-fine", "bypass-bursts-fine"),
                            ("bursts-level1200", "bypass-bursts")):
        measured = bursts(session, name, reference)
        if measured:
            result[name] = measured
    result["steps"] = steps(session)
    result["tone"] = tone(session)
    result["feedback_db_per_repeat"] = feedback(session)
    result["feedback_0500"] = feedback_max(session)
    result["feedback_bursts"] = burst_decays(session)
    traces = {}
    hold = wow_flutter(session)
    if hold:
        result["wow_flutter"], traces["hold"] = hold
    turn = glide(session)
    if turn:
        result["time_glide"], traces["glide"] = turn
    result["noise_dbfs"] = noise(session)

    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / f"dd-8-{mode.lower()}.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, traces)
    print(json.dumps({k: v for k, v in result.items() if k not in ("pedal", "session")}, indent=1))


if __name__ == "__main__":
    main()
