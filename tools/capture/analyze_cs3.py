#!/usr/bin/env python3
"""Measures the Boss CS-3 Compression Sustainer from one session (plans/cs-3.json).
Findings: captures/boss/cs-3/README.md.

    analyze_cs3.py SESSION      # writes SESSION/fit/cs-3.{json,svg}

What it measures:
  static curve     the 1 kHz staircase per Sustain position: the settled output level per
                   input step (the last 40 % of each 1.5 s step) against the bypass
  attack/release   the -30/0/-30 dB jump per Attack position (and the Sustain extremes):
                   the gain's 1 ms envelope against the bypass take; the onset overshoot,
                   the time to settle within 1 dB, the recovery after the drop (63 %, 90 %)
  plucks           picked-note bursts per Attack position: the pick's spike over the note,
                   the note's decay against the bypass's
  tone             quiet sweeps per Tone position (Sustain minimum): the response re bypass
  level, noise     a steady -24 dB tone per Level position; silence at both Sustain ends

The pedal is analog (a VCA and a transistor peak detector): the takes are aligned by the
loopback to the sample, no clock of its own to fit.
"""

import argparse
import json
import pathlib
import sys

import numpy as np

import analysis
import analyze_dd8

RATE = 48000
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
TONE_HZ = [50, 100, 200, 500, 1000, 2000, 3000, 5000, 8000, 12000, 16000]


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rms_db(y):
    return float(db(np.sqrt(np.mean(np.square(y)))))


def rounded(values, digits=2):
    return [round(float(v), digits) for v in values]


def steps_db(session, name):
    """The settled level of each step of a `steps` take (the last 40 % of the step)."""
    t = analysis.Take(session, name)
    spec = t.meta["signal"]
    step = spec["step_seconds"]
    out = []
    for k in range(len(spec["levels_db"])):
        a = int((k + 0.6) * step * RATE)
        b = int((k + 1) * step * RATE)
        out.append(rms_db(t.pedal[t.start + a:t.start + b]))
    return np.array(out)


def static_curve(session):
    bypass = steps_db(session, "bypass-stairs")
    levels = analysis.Take(session, "bypass-stairs").meta["signal"]["levels_db"]
    out = {"input_db": levels, "bypass_dbfs": rounded(bypass, 1), "per_sustain": {}}
    for position in POSITIONS:
        level = steps_db(session, f"stairs-sustain-{analyze_dd8.tag(position)}")
        gain = level - bypass
        out["per_sustain"][position] = {
            "output_dbfs": rounded(level, 1), "gain_db": rounded(gain, 1),
            "low_level_gain_db": round(float(gain[0]), 1),
            "ceiling_dbfs": round(float(level[-1]), 1),
            # the slope over the top steps: dB out per dB in
            "slope_top": round(float(np.polyfit(levels[-5:], level[-5:], 1)[0]), 3)}
    return out


def gain_envelope(session, name):
    """The gain (dB) every 1 ms against the bypass take of the same signal."""
    def envelope(take_name):
        t = analysis.Take(session, take_name)
        spec = t.meta["signal"]
        n = int((len(spec["levels_db"]) * spec["step_seconds"] + 1) * RATE)
        y = t.pedal[t.start:t.start + n]
        e = np.sqrt(np.convolve(np.square(y), np.ones(48) / 48, "same"))  # one 1 kHz period
        return db(e[::48])
    return envelope(name) - envelope("bypass-jump")


def dynamics(session):
    out = {}
    names = [f"jump-attack-{analyze_dd8.tag(p)}" for p in POSITIONS]
    names += ["jump-sustain-0700", "jump-sustain-0500"]
    for name in names:
        g = gain_envelope(session, name)
        up, down = 4000, 8000  # ms
        low = float(np.median(g[2000:3900]))
        high = float(np.median(g[up + 3000:up + 3900]))
        low_after = float(np.median(g[down + 3000:down + 3900]))
        settle = int(np.argmax(np.abs(g[up:up + 3000] - high) < 1.0))
        rel = g[down:down + 4000]
        start = float(rel[5])
        t63 = int(np.argmax(rel >= start + 0.632 * (low_after - start)))
        t90 = int(np.argmax(rel >= start + 0.9 * (low_after - start)))
        out[name] = {"settings": analysis.Take(session, name).meta["settings"],
                     "gain_db": {"quiet": round(low, 1), "loud": round(high, 1)},
                     "onset_overshoot_db": round(float(g[up:up + 400].max() - high), 1),
                     "attack_settle_ms": settle,
                     "release_ms": {"63%": t63, "90%": t90}}
    return out


def plucks(session):
    def envelope(name):
        t = analysis.Take(session, name)
        y = t.pedal[t.start:t.start + 12 * RATE]
        return db(np.sqrt(np.convolve(np.square(y), np.ones(240) / 240, "same")))  # 5 ms
    bypass = envelope("bypass-plucks")
    out = {}
    for position in ["7:00", "12:00", "5:00"]:
        e = envelope(f"plucks-attack-{analyze_dd8.tag(position)}")
        rows = []
        for k in range(4):
            o = k * 3 * RATE
            at = lambda env, s: env[o + int(s * RATE)]
            rows.append((e[o:o + RATE // 20].max() - at(e, 0.1), at(e, 0.1) - at(e, 0.5),
                         at(e, 0.5) - at(e, 1.5), at(bypass, 0.1) - at(bypass, 0.5)))
        r = np.mean(rows, 0)
        out[position] = {"spike_over_note_db": round(float(r[0]), 1),
                         "decay_100_500ms_db": round(float(r[1]), 2),
                         "decay_500_1500ms_db": round(float(r[2]), 2),
                         "bypass_decay_100_500ms_db": round(float(r[3]), 2)}
    return out


def tone(session):
    n = RATE
    f = np.fft.rfftfreq(n, 1 / RATE)

    def response(name):
        h, _ = analyze_dd8.impulse_response(session, name)
        return np.fft.rfft(analyze_dd8.head(h, n))

    def band(x, centre):
        keep = (f > centre / 1.06) & (f < centre * 1.06)
        return float(10 * np.log10(np.mean(np.abs(x[keep]) ** 2)))

    bypass = response("bypass-sweep")
    out = {"hz": TONE_HZ}
    for position in POSITIONS:
        x = response(f"tone-{analyze_dd8.tag(position)}")
        level = np.array([band(x, c) - band(bypass, c) for c in TONE_HZ])
        out[position] = {"db_re_bypass": rounded(level, 1),
                         "db_re_1khz": rounded(level - level[TONE_HZ.index(1000)], 1)}
    return out


def levels(session):
    out = {}
    for position in POSITIONS:
        t = analysis.Take(session, f"level-{analyze_dd8.tag(position)}")
        out[position] = round(rms_db(t.pedal[t.start + RATE:t.start + int(2.8 * RATE)]), 1)
    return out


def noise(session):
    out = {}
    for position in ["7:00", "5:00"]:
        t = analysis.Take(session, f"noise-sustain-{analyze_dd8.tag(position)}")
        y = t.pedal[t.start + RATE // 2:t.start + 9 * RATE]
        out[position] = {"rms_dbfs": round(analysis.rms_db(y), 1),
                         "third_octaves_dbfs": rounded(analysis.third_octave_db(y, RATE), 1)}
    return out


def plot(out_dir, result, session):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 2, figsize=(14, 9.5))
    axis = axes[0, 0]
    curve = result["static_curve"]
    axis.plot(curve["input_db"], curve["bypass_dbfs"], "k:", label="bypass")
    for position in POSITIONS:
        axis.plot(curve["input_db"], curve["per_sustain"][position]["output_dbfs"], "o-",
                  label=f"Sustain {position}")
    axis.set_xlabel("input, dB re the guitar's peak")
    axis.set_ylabel("output, dBFS rms (Level 12:00)")
    axis.set_title("The static curve: Sustain sets the gain under a fixed ceiling", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[0, 1]
    for position in POSITIONS:
        g = gain_envelope(session, f"jump-attack-{analyze_dd8.tag(position)}")
        axis.plot(np.arange(len(g)) / 1000, g, lw=0.8, label=f"Attack {position}")
    axis.set_xlim(3.5, 9.5)
    axis.set_xlabel("s (input: -30, 0 dB from 4 s, -30 dB from 8 s)")
    axis.set_ylabel("gain re bypass, dB")
    axis.set_title("Attack sets the release (and a little of the attack)", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[1, 0]
    t = result["tone"]
    for position in POSITIONS:
        axis.semilogx(t["hz"], t[position]["db_re_1khz"], "o-", label=f"Tone {position}")
    axis.set_xlabel("Hz")
    axis.set_ylabel("dB re 1 kHz")
    axis.set_title("Tone: a tilt around 1 kHz (Sustain minimum, quiet sweep)", fontsize=10)
    axis.legend(fontsize=8)
    axis = axes[1, 1]
    x = [analyze_dd8.clock_fraction(p) for p in POSITIONS]
    axis.plot(x, [result["level_dbfs"][p] for p in POSITIONS], "o-")
    axis.set_xticks(x, POSITIONS)
    axis.set_ylabel("output, dBFS rms (a -24 dB tone, Sustain 12:00)")
    axis.set_title("Level (7:00 mutes)", fontsize=10)
    for axis in axes.flat:
        axis.grid(alpha=0.3, which="both")
    figure.suptitle("Boss CS-3 Compression Sustainer")
    figure.tight_layout()
    figure.savefig(out_dir / "cs-3.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    if not (session / "rig.json").exists():
        sys.exit(f"{session} is not a capture session")
    result = {"pedal": "Boss CS-3 Compression Sustainer", "session": session.name,
              "static_curve": static_curve(session), "dynamics": dynamics(session),
              "plucks": plucks(session), "tone": tone(session), "level_dbfs": levels(session),
              "noise": noise(session)}
    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "cs-3.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, session)
    print(json.dumps({k: v for k, v in result.items() if k in ("dynamics", "plucks", "level_dbfs")},
                     indent=1))


if __name__ == "__main__":
    main()
