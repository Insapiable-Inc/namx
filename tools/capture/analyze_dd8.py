#!/usr/bin/env python3
"""Measures the Boss DD-8's Standard mode from a session recorded with plans/dd-8-standard.json
and plans/dd-8-dynamics.json.

    analyze_dd8.py SESSION        # e.g. captures/boss/dd-8/2026-10-26-standard; writes SESSION/fit/

The pedal is a dry path plus a delay: each sweep take is deconvolved into the impulse
response from what was played to what the pedal put out (dry at time 0, repeats after).
The bursts (1 kHz tones with gaps the repeat lands in) give the wet path's level against
the input level, and so its clipping. Findings of the 2026-10-26 session are in
captures/boss/dd-8/README.md.
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import soundfile

import analysis

KNOB_POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
BURST_LEVELS_DB = [-30, -24, -18, -12, -6, 0, 3]
BURST_PERIOD_S = 1.2
DELAY_AT_NOON_S = 0.3856  # where the burst repeats land (Time 12:00), measured from time-1200


def clock_fraction(position):
    """A clock position as a fraction of the knob's 300° rotation (7:00 = 0, 5:00 = 1)."""
    hours, minutes = position.split(":")
    hour = (int(hours) - 7) % 12 + int(minutes) / 60
    return round(hour / 10, 4)


def tag(position):
    return position.replace(":", "").zfill(4)


def impulse_response(session, name):
    """Deconvolves a sweep take: what came out of the pedal over what was played."""
    take = analysis.Take(session, name)
    played, rate = soundfile.read(session / "played" / f"{name}.wav")
    x = played[take.meta["signal_start"]:]
    y = take.pedal[take.start:take.start + len(x)]
    x = x[:len(y)]
    n = 1 << int(np.ceil(np.log2(2 * len(x))))
    big_x, big_y = np.fft.rfft(x, n), np.fft.rfft(y, n)
    f = np.fft.rfftfreq(n, 1 / rate)
    band = (f > 25) & (f < 19000)
    h = np.zeros_like(big_y)
    h[band] = big_y[band] / big_x[band]
    return np.fft.irfft(h, n), rate


def first_repeat(h, rate, after=200):
    """Sample index of the dry impulse and of the strongest later arrival (the repeat)."""
    dry = int(np.argmax(np.abs(h[:2000])))
    env = np.abs(h[:int(2.5 * rate)])
    env[:dry + after] = 0
    return dry, int(np.argmax(env))


def band_db(h, at, rate, f0, width=2048):
    """Energy of the response around sample `at`, in the third-octave band at f0."""
    segment = h[at - width // 4:at - width // 4 + width] * np.hanning(width)
    spectrum = np.abs(np.fft.rfft(segment, rate)) ** 2
    f = np.fft.rfftfreq(rate, 1 / rate)
    edge = 2 ** (1 / 6)
    return 10 * np.log10(np.sum(spectrum[(f > f0 / edge) & (f < f0 * edge)]))


def dry_entry(h, dry, bypass_at, bypass_gain):
    """Where the dry impulse arrives and how loud, against the bypass take's."""
    return {"delay_samples_re_bypass": dry - bypass_at,
            "gain_db_re_bypass": round(float(20 * np.log10(abs(h[dry] / bypass_gain))), 2)}


def measure_sweeps(session):
    bypass, rate = impulse_response(session, "bypass-sweep")
    bypass_at = int(np.argmax(np.abs(bypass[:2000])))
    bypass_gain = bypass[bypass_at]
    out = {"time_ms": {}, "level_db": {}, "feedback_db_per_repeat": {}, "dry": {}}
    responses = {}
    for position in KNOB_POSITIONS:
        h, _ = impulse_response(session, f"time-{tag(position)}")
        dry, rep = first_repeat(h, rate)
        out["time_ms"][position] = round((rep - dry) / rate * 1000, 2)
        out["dry"][f"time-{tag(position)}"] = dry_entry(h, dry, bypass_at, bypass_gain)
        responses[f"time-{tag(position)}"] = h
    noon = int(round(out["time_ms"]["12:00"] / 1000 * rate))
    reference = band_db(responses["time-1200"], noon, rate, 1000)
    out["level_db"]["7:00"] = None  # no repeat at all: Level at minimum is the dry path alone
    for position in ("9:00", "12:00", "3:00"):
        h, _ = impulse_response(session, f"level-{tag(position)}")
        out["level_db"][position] = round(float(band_db(h, noon, rate, 1000) - reference), 2)
    out["level_db"]["5:00"] = 0.0
    h, _ = impulse_response(session, "level-0700")
    dry = int(np.argmax(np.abs(h[:2000])))
    out["dry"]["level-0700"] = dry_entry(h, dry, bypass_at, bypass_gain)
    # Feedback from the -12 dB takes (below the wet path's clip): repeat n+1 against n.
    for position in ("9:00", "12:00", "3:00"):
        h, _ = impulse_response(session, f"feedback-{tag(position)}-m12")
        at = [k * noon for k in (1, 2, 3, 4)]
        steps = {}
        for f0 in (1000, 4000, 8000):
            levels = [band_db(h, k, rate, f0) for k in at]
            steps[f"{f0} Hz"] = [round(float(b - a), 2) for a, b in zip(levels, levels[1:])]
        out["feedback_db_per_repeat"][position] = steps
        responses[f"feedback-{tag(position)}-m12"] = h
    # The wet path's tone: the first repeat against the dry impulse (Feedback 7:00, Time 12:00).
    h = responses["time-1200"]
    dry_window = h[:1024] * np.hanning(2048)[1024:]
    dry_db = analysis.third_octave_db(np.pad(dry_window, (0, rate - 1024)), rate)
    segment = h[noon - 256:noon - 256 + 1024] * np.hanning(1024)
    rep_db = analysis.third_octave_db(np.pad(segment, (0, rate - 1024)), rate)
    out["wet_minus_dry_db"] = {"bands_hz": analysis.THIRD_OCTAVES_HZ,
                               "db": [round(float(v), 2) for v in rep_db - dry_db]}
    return out, responses, rate


def measure_bursts(session, name):
    """Wet gain, repeat peak and 3rd harmonic per burst level (the repeat lands in the gap)."""
    reference = analysis.Take(session, "bypass-bursts").signal(1)
    take = analysis.Take(session, name)
    out_signal, rate = take.signal(1), take.rate
    rows = []
    for k, level in enumerate(BURST_LEVELS_DB):
        start = int(k * BURST_PERIOD_S * rate)
        repeat_at = start + int(DELAY_AT_NOON_S * rate)
        rep = out_signal[repeat_at + int(0.05 * rate):repeat_at + int(0.25 * rate)]
        inp = reference[start + int(0.05 * rate):start + int(0.25 * rate)]
        gain = 20 * np.log10(np.sqrt(np.mean(rep ** 2)) / np.sqrt(np.mean(inp ** 2)))
        rows.append({"input_db": level, "wet_gain_db": round(float(gain), 2),
                     "repeat_peak_dbfs": round(float(20 * np.log10(np.abs(rep).max())), 2),
                     "h3_db": round(analysis.harmonics_db(rep, rate, 1000.0, 3)[1], 1)})
    return rows


def plot(out_dir, result, responses, rate):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(1, 2, figsize=(15, 5))
    noon = int(round(result["time_ms"]["12:00"] / 1000 * rate))
    for position, color in (("9:00", "C0"), ("12:00", "C1"), ("3:00", "C2")):
        h = responses[f"feedback-{tag(position)}-m12"]
        dry = band_db(np.roll(h, 1024), 1024, rate, 1000)  # a window centred on the dry impulse
        repeats = [k for k in range(1, 9) if (k + 1) * noon < len(h) // 2]
        levels = [band_db(h, k * noon, rate, 1000) - dry for k in repeats]
        visible = [(k, v) for k, v in zip(repeats, levels) if v > -75]
        step = np.mean(result["feedback_db_per_repeat"][position]["1000 Hz"])
        axes[0].plot([k * result["time_ms"]["12:00"] / 1000 for k, _ in visible],
                     [v for _, v in visible], "o-", color=color,
                     label=f"Feedback {position}: {step:+.1f} dB per repeat")
    axes[0].set_xlabel("seconds after the note (Time 12:00: one repeat every 385.6 ms)")
    axes[0].set_ylabel("repeat level at 1 kHz, dB re the dry note")
    axes[0].set_title("Repeats at Level 5:00 (-12 dB sweeps, below the clip)", fontsize=10)
    axes[0].legend(fontsize=9)
    axes[0].grid(alpha=0.3)

    harmonic_axis = axes[1].twinx()
    for name, label, color in (("bursts", "Level 5:00", "C0"),
                               ("bursts-level1200", "Level 12:00", "C3")):
        rows = result["bursts"][name]
        x = [r["input_db"] for r in rows]
        axes[1].plot(x, [r["wet_gain_db"] for r in rows], "o-", color=color,
                     label=f"wet gain, {label}")
        harmonic_axis.plot(x, [r["h3_db"] for r in rows], "x:", color=color,
                           label=f"3rd harmonic, {label}")
    axes[1].set_xlabel("input level, dB re the guitar's peak")
    axes[1].set_ylabel("repeat level re the input, dB (solid)")
    harmonic_axis.set_ylabel("3rd harmonic of the repeat, dB re its fundamental (dotted)")
    axes[1].set_title("The repeats against the input level: flat, then hard clipping at ~0 dB at "
                      "either Level\n(the clip is before the Level knob; the dry path stays clean)",
                      fontsize=10)
    lines = axes[1].get_legend_handles_labels()
    more = harmonic_axis.get_legend_handles_labels()
    axes[1].legend(lines[0] + more[0], lines[1] + more[1], fontsize=8, loc="center left")
    axes[1].grid(alpha=0.3)
    figure.tight_layout()
    figure.savefig(out_dir / "dd-8-standard.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")

    sweeps, responses, rate = measure_sweeps(session)
    result = {
        "pedal": "Boss DD-8, Standard mode",
        "session": session.name,
        "knob_fraction": {p: clock_fraction(p) for p in KNOB_POSITIONS},
        **sweeps,
        "bursts": {name: measure_bursts(session, name) for name in ("bursts", "bursts-level1200")},
    }
    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "dd-8-standard.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, responses, rate)

    print("Time (ms):     " + "  ".join(f"{p} {v}" for p, v in result["time_ms"].items()))
    print("Level (dB):    " + "  ".join(f"{p} {v}" for p, v in result["level_db"].items()))
    for p, steps in result["feedback_db_per_repeat"].items():
        print(f"Feedback {p:>5}: {steps['1000 Hz']} dB per repeat at 1 kHz")
    print("Dry:           " + "  ".join(
        f"{k} {v['delay_samples_re_bypass']} smp {v['gain_db_re_bypass']:+} dB"
        for k, v in result["dry"].items()))
    for row in result["bursts"]["bursts"]:
        print(f"Burst {row['input_db']:+3d} dB: wet {row['wet_gain_db']:+.2f} dB, "
              f"H3 {row['h3_db']} dB")
    print(f"Wrote {out_dir / 'dd-8-standard.json'} and dd-8-standard.svg")


if __name__ == "__main__":
    main()
