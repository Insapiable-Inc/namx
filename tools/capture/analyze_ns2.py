#!/usr/bin/env python3
"""Measures the Boss NS-2 Noise Suppressor from two sessions (plans/ns-2.json, then
plans/ns-2-dynamics.json) and fits a grey-box model of its gain.
Findings: captures/boss/ns-2/README.md.

    analyze_ns2.py SESSION DYNAMICS          # writes SESSION/fit/ns-2.{json,svg}
    analyze_ns2.py SESSION DYNAMICS --fit    # refits the model first (slow: minutes)

What it measures:
  open path        the -12 dB sweep re bypass; harmonics of the staircases' 0 dB step
  thresholds       the 1 kHz staircase per Threshold position (up and back down): the gain
                   per step against the bypass
  jumps            -84/-12/-84 dB: how fast the gate opens; when it closes (Decay)
  noise            silence, closed
  model            the gain envelope (1 ms, a lock-in at the test tone) of every staircase,
                   jump, pluck and phrase against the model below, simulated on the input's
                   level

The dynamics session's "bypass" takes were recorded with the pedal on (Threshold 5:00,
Decay 12:00, as the first session ended — their open gain is the pedal's +0.6 dB, the slow
staircase is gated): they count as takes at that setting, and every take of that session is
measured against the played signal and the first session's bypass gain instead.

The model (levels in dB re the guitar's peak, 1 ms steps):
  L       the input's level (the test tone's amplitude)
  E       fast level: rises with L at once, falls toward it exponentially in dB (Decay's tau)
  V       slow level: the same, falling RATIO times slower
  switch  opens once E > T + OPEN, closes once E < T + OPEN - HYSTERESIS
  G       0 dB while open; closed, it sinks toward -JUMP (tau x CLOSE)
  gain    = OPEN_GAIN + G - SLOPE * max(0, T - V)
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import scipy.optimize
import soundfile

import analysis
import analyze_dd8

RATE = 48000
HOP = RATE // 1000
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
TAGS = [analyze_dd8.tag(p) for p in POSITIONS]
PRE_MS = 1000  # simulated before a take's signal: silence, the 50 ms marker at 0 dB, the gap
FLOOR_DBFS = -110  # gain points whose output is under this are the rig's noise, not the pedal

# The fitted model (--fit rewrites SESSION/fit/ns-2.json; copy the result here).
MODEL = {"jump_db": 7.564, "open_db": 1.286, "hysteresis_db": 10.473, "close": 4.419,
         "slope": 0.889, "ratio": 7.937, "open_gain_db": 0.6,
         "threshold_db": [-69.4, -69.02, -66.72, -49.4, -27.33],
         "decay_ms": [24.58, 63.49, 92.97, 145.11, 460.26]}


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def lock_in(y, freq, seconds=0.01):
    """The amplitude of the `freq` component, over a sliding window."""
    t = np.arange(len(y)) / RATE
    z = y * np.exp(-2j * np.pi * freq * t)
    n = int(seconds * RATE)
    return 2 * np.abs(np.convolve(z, np.ones(n) / n, "same"))


def simulate(levels, model, threshold, decay_ms):
    """The model's gain (dB) for an input level per ms (dB re the guitar's peak)."""
    t = threshold
    a_fast = 1 - np.exp(-1 / decay_ms)
    a_slow = 1 - np.exp(-1 / (decay_ms * model["ratio"]))
    a_close = 1 - np.exp(-1 / (decay_ms * model["close"]))
    jump, slope, gain0 = model["jump_db"], model["slope"], model["open_gain_db"]
    open_at, close_at = t + model["open_db"], t + model["open_db"] - model["hysteresis_db"]
    fast = slow = -140.0
    g = -jump
    is_open = False
    out = np.empty(len(levels))
    for i, level in enumerate(levels.tolist()):
        fast = level if level > fast else fast + (level - fast) * a_fast
        slow = level if level > slow else slow + (level - slow) * a_slow
        if fast > open_at:
            is_open = True
        elif fast < close_at:
            is_open = False
        g = 0.0 if is_open else g + (-jump - g) * a_close
        out[i] = gain0 + g - slope * max(0.0, t - slow)
    return out


def edges_masked(ok, spec):
    if spec["type"] == "steps":
        for k in range(len(spec["levels_db"]) + 1):
            e = int(k * spec["step_seconds"] * 1000)
            ok[max(0, e - 6):e + 6] = False
    return ok


class Session:
    """Gain envelopes (1 ms) of one session's takes, with their input levels."""

    def __init__(self, path, bypass_gain_db=None):
        self.path = path
        self.bypass_gain_db = bypass_gain_db
        self.ref_in = 10 ** (json.loads((path / "rig.json").read_text())["ref_dbfs"] / 20)

    def take(self, name, bypass=None, extra=2.5):
        """(levels, gain, trust) per ms: against a bypass take, or the played signal."""
        a = analysis.Take(self.path, name)
        spec = a.meta["signal"]
        y = a.signal(extra)
        if bypass:
            b = analysis.Take(self.path, bypass).signal(extra)
            n = min(len(y), len(b))
            out = db(lock_in(y[:n], spec["freq"]))[::HOP]
            gain = out - db(lock_in(b[:n], spec["freq"]))[::HOP]
            levels = np.full(len(out), -140.0)
            for k, level in enumerate(spec["levels_db"]):
                step = spec["step_seconds"] * 1000
                levels[int(k * step):int((k + 1) * step)] = level
            out_dbfs = out
        else:
            played, _ = soundfile.read(self.path / "played" / f"{name}.wav")
            played = played if played.ndim == 1 else played[:, 0]
            start = a.meta["signal_start"]
            x = np.pad(played[start:start + len(y)], (0, max(0, len(y) - len(played) + start)))
            levels = db(lock_in(x[:len(y)], spec["freq"]) / self.ref_in)[::HOP]
            out_dbfs = db(lock_in(y, spec["freq"]))[::HOP]
            gain = out_dbfs - levels - self.bypass_gain_db
        ok = edges_masked((out_dbfs > FLOOR_DBFS) & (levels > -95), spec)
        return levels, gain, ok


def settings_of(path, name):
    meta = json.loads((path / f"{name}.json").read_text())
    s = meta.get("settings") or {"Threshold": "5:00", "Decay": "12:00"}  # the pedal-on "bypass"
    return analyze_dd8.tag(s["Threshold"]), analyze_dd8.tag(s["Decay"])


def model_takes(session, dynamics):
    """Every take the model is fitted to: (threshold tag, decay tag, name, levels, gain, ok)."""
    first = Session(session)
    jump = analysis.Take(session, "bypass-jump").signal(0)
    # the rig's bypassed output (dBFS) for a 0 dB input: the jump's -12 dB part, plus 12
    bypass_gain = float(db(np.median(lock_in(jump[5 * RATE:int(7.5 * RATE)], 1000)))) + 12
    out = []
    for name in sorted(p.stem for p in session.glob("*.json")):
        if name.startswith(("stairs-threshold", "jump-")):
            bypass = "bypass-stairs" if name.startswith("stairs") else "bypass-jump"
            out.append((*settings_of(session, name), name, *first.take(name, bypass)))
    second = Session(dynamics, bypass_gain)
    for name in sorted(p.stem for p in dynamics.glob("*.json")):
        if name != "rig":
            out.append((*settings_of(dynamics, name), f"dynamics/{name}", *second.take(name)))
    return out


def prepend(levels):
    pre = np.full(PRE_MS, -140.0)
    pre[450:500] = 0.0
    return np.concatenate([pre, levels])


def evaluate(takes, model):
    rows = {}
    for th, dc, name, levels, gain, ok in takes:
        m = simulate(prepend(levels), model, model["threshold_db"][TAGS.index(th)],
                     model["decay_ms"][TAGS.index(dc)])[PRE_MS:]
        rows[name] = round(float(np.sqrt(np.mean((m - gain)[ok] ** 2))), 2)
    return rows


def fit(takes, model):
    """Least squares over every take (each weighs alike per second); Decay kept monotonic."""
    def unpack(p):
        decay = p[11] * np.exp(np.concatenate([[0], np.cumsum(p[12:16])]))
        return dict(model, jump_db=p[0], open_db=p[1], hysteresis_db=p[2], close=p[3],
                    slope=p[4], ratio=p[5], threshold_db=list(p[6:11]), decay_ms=list(decay))

    def residuals(p):
        m = unpack(p)
        r = []
        for th, dc, _, levels, gain, ok in takes:
            g = simulate(prepend(levels), m, m["threshold_db"][TAGS.index(th)],
                         m["decay_ms"][TAGS.index(dc)])[PRE_MS:]
            r.append((g - gain)[ok] / np.sqrt(ok.sum() / 1000))
        return np.concatenate(r)

    d = np.array(model["decay_ms"])
    p0 = [model["jump_db"], model["open_db"], model["hysteresis_db"], model["close"],
          model["slope"], model["ratio"]] + model["threshold_db"] + [d[0]]
    p0 += list(np.diff(np.log(d)))
    lower = [0, -30, 0, .05, 0, .05] + [-100] * 5 + [.5] + [0] * 4
    upper = [45, 30, 30, 50, 2, 100] + [-10] * 5 + [1e4] + [5] * 4
    result = scipy.optimize.least_squares(residuals, p0, bounds=(lower, upper), diff_step=1e-3)
    m = unpack(result.x)
    return {k: (round(float(v), 3) if not isinstance(v, list) else [round(float(x), 2) for x in v])
            for k, v in m.items()}


def staircase(session, name, bypass):
    t, b = analysis.Take(session, name), analysis.Take(session, bypass)
    spec = t.meta["signal"]
    step = spec["step_seconds"]
    out = []
    for k in range(len(spec["levels_db"])):
        a, e = int((k + 0.6) * step * RATE), int((k + 1) * step * RATE)
        ya, yb = t.pedal[t.start + a:t.start + e], b.pedal[b.start + a:b.start + e]
        out.append(float(db(np.median(lock_in(ya, spec["freq"], 0.1)))
                         - db(np.median(lock_in(yb, spec["freq"], 0.1)))))
    return spec["levels_db"], out


def thresholds(session):
    out = {}
    for position, tag in zip(POSITIONS, TAGS):
        levels, gain = staircase(session, f"stairs-threshold-{tag}", "bypass-stairs")
        half = len(levels) // 2 + 1
        up = [lv for lv, g in zip(levels[:half], gain[:half]) if g > -1]
        down = [lv for lv, g in zip(levels[half:], gain[half:]) if g > -1]
        out[position] = {"opens_at_db": min(up), "still_open_down_to_db": min(down),
                         "gain_db": [round(g, 1) for g in gain]}
    return out


def jumps(session):
    out = {}
    b = analysis.Take(session, "bypass-jump").signal(2)
    for name in sorted(p.stem for p in session.glob("jump-*.json")):
        y = analysis.Take(session, name).signal(2)
        n = min(len(y), len(b))
        g = (db(lock_in(y[:n], 1000, 0.02)) - db(lock_in(b[:n], 1000, 0.02)))[::HOP]
        g1 = (db(lock_in(y[:n], 1000, 0.001)) - db(lock_in(b[:n], 1000, 0.001)))[::HOP]
        open_db = float(np.median(g[7000:7900]))
        after = g[8000:]
        out[name] = {"settings": analysis.Take(session, name).meta["settings"],
                     "open_db": round(open_db, 2),
                     "opens_within_ms": int(np.argmax(g1[4000:4200] > open_db - 1)),
                     "down_3db_ms": int(np.argmax(after < open_db - 3)),
                     "down_10db_ms": int(np.argmax(after < open_db - 10))}
    return out


def open_path(session):
    n = RATE
    f = np.fft.rfftfreq(n, 1 / RATE)

    def response(name):
        h, _ = analyze_dd8.impulse_response(session, name)
        return np.fft.rfft(h[:n])

    a, b = response("sweep"), response("bypass-sweep")
    hz = [30, 50, 100, 200, 500, 1000, 2000, 3000, 5000, 8000, 12000, 16000, 20000]
    band = [float(10 * np.log10(np.mean(np.abs(a[(f > c / 1.06) & (f < c * 1.06)]) ** 2)
                                / np.mean(np.abs(b[(f > c / 1.06) & (f < c * 1.06)]) ** 2)))
            for c in hz]
    harmonics = {}
    for name in ["bypass-stairs", "stairs-threshold-1200", "bypass-stairs-3000", "stairs-3000"]:
        t = analysis.Take(session, name)
        at = t.start + int(20.6 * 1.5 * RATE)
        harmonics[name] = [round(float(v), 1) for v in analysis.harmonics_db(
            t.pedal[at:at + int(0.6 * RATE)], RATE, t.meta["signal"]["freq"])]
    return {"hz": hz, "db_re_bypass": [round(v, 2) for v in band],
            "harmonics_2_to_5_db_at_0db": harmonics}


def noise(session):
    out = {}
    for name in ["bypass-silence", "silence-threshold-1200", "silence-threshold-0500"]:
        y = analysis.Take(session, name).signal()[RATE // 2:]
        out[name] = round(analysis.rms_db(y), 1)
    return out


def plot(out_dir, takes, model, result):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    shown = ["stairs-threshold-1200", "stairs-threshold-0500", "jump-decay-0700", "jump-12",
             "jump-decay-0500", "dynamics/plucks-t0500-d1200", "dynamics/plucks-t0500-d0500",
             "dynamics/phrase-t0500-d1200", "dynamics/stairs-slow-t1200-d0700"]
    rows = [t for t in takes if t[2] in shown]
    rows.sort(key=lambda t: shown.index(t[2]))
    figure, axes = plt.subplots(len(rows) + 1, 1, figsize=(14, 2.6 * (len(rows) + 1)))
    axis = axes[0]
    for position in POSITIONS:
        axis.plot(result["thresholds"][position]["gain_db"], "o-", ms=3,
                  label=f"Threshold {position}")
    axis.set_xlabel("step (1 kHz, -80 dB up to 0 dB at step 20, back down; 1.5 s each)")
    axis.set_ylabel("gain re bypass, dB")
    axis.set_ylim(-42, 3)
    axis.set_title("The staircase per Threshold (Decay 12:00): opens at a level, closes "
                   "lower and slowly", fontsize=10)
    axis.legend(fontsize=7, ncol=5)
    for axis, (th, dc, name, levels, gain, ok) in zip(axes[1:], rows):
        m = simulate(prepend(levels), model, model["threshold_db"][TAGS.index(th)],
                     model["decay_ms"][TAGS.index(dc)])[PRE_MS:]
        t = np.arange(len(levels))[::5] / 1000  # every 5 ms: a lighter figure
        axis.plot(t, np.where(ok, gain, np.nan)[::5], "k", lw=0.6, label="pedal")
        axis.plot(t, m[::5], "r", lw=0.8, label="model")
        twin = axis.twinx()
        twin.plot(t, levels[::5], "c", lw=0.4)
        twin.set_ylim(-100, 5)
        twin.set_ylabel("input, dB", fontsize=7, color="c")
        axis.set_ylim(-45, 3)
        axis.set_ylabel("gain, dB")
        axis.set_title(f"{name} (Threshold {POSITIONS[TAGS.index(th)]}, Decay "
                       f"{POSITIONS[TAGS.index(dc)]}): rms {result['model_rms_db'][name]} dB",
                       fontsize=9)
        axis.legend(fontsize=7, loc="lower left")
    axes[-1].set_xlabel("s")
    for axis in axes:
        axis.grid(alpha=0.3)
    figure.suptitle("Boss NS-2 Noise Suppressor")
    figure.tight_layout(rect=(0, 0, 1, 0.99))
    figure.savefig(out_dir / "ns-2.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    parser.add_argument("dynamics")
    parser.add_argument("--fit", action="store_true")
    args = parser.parse_args()
    session = pathlib.Path(args.session).expanduser()
    dynamics = pathlib.Path(args.dynamics).expanduser()
    for path in (session, dynamics):
        if not (path / "rig.json").exists():
            sys.exit(f"{path} is not a capture session")
    takes = model_takes(session, dynamics)
    model = fit(takes, MODEL) if args.fit else MODEL
    result = {"pedal": "Boss NS-2 Noise Suppressor", "sessions": [session.name, dynamics.name],
              "open_path": open_path(session), "thresholds": thresholds(session),
              "jumps": jumps(session), "noise_dbfs": noise(session), "model": model}
    result["model_rms_db"] = evaluate(takes, model)
    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "ns-2.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, takes, model, result)
    print(json.dumps({k: result[k] for k in ("model", "model_rms_db", "jumps")}, indent=1))


if __name__ == "__main__":
    main()
