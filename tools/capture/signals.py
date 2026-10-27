"""Test signals for pedal captures.

Every signal is built from its spec alone (noise has a fixed seed), so a take can be
regenerated exactly; capture.py still saves what it played next to each recording.
A take is laid out as

    [lead silence][marker chirp][gap][signal][tail]

The marker is a short log chirp: the loopback channel finds it by cross-correlation,
which gives the round-trip latency even when the signal itself is periodic (a sine).
Levels are peaks in dB relative to the rig's reference level (the guitar's peak at the
pedal input), so `"level_db": 0` hits the pedal as hard as the guitar does.

Specs (JSON objects in a plan):

    {"type": "sine", "freq": 1000, "seconds": 20}
    {"type": "sweep", "f0": 20, "f1": 20000, "seconds": 10}    # exponential (Farina)
    {"type": "noise", "seconds": 10, "seed": 1}                # white, peak-normalized
    {"type": "clicks", "period": 2.0, "count": 5}              # one-sample impulses
    {"type": "silence", "seconds": 10}
    {"type": "steps", "freq": 1000, "levels_db": [-24, 0, -24], "step_seconds": 3,
     "gap_seconds": 0}                                         # a sine through levels

plus optional "level_db" (default 0) and "tail" (seconds after the signal, default 1).
"""

import numpy as np

LEAD_SECONDS = 0.3
MARKER_SECONDS = 0.05
GAP_SECONDS = 0.5
DEFAULT_TAIL_SECONDS = 1.0
FADE_SECONDS = 0.01
MARKER_F0, MARKER_F1 = 200.0, 10000.0

TYPES = {
    "sine": {"freq", "seconds"},
    "sweep": {"f0", "f1", "seconds"},
    "noise": {"seconds"},
    "clicks": {"period", "count"},
    "silence": {"seconds"},
    "steps": {"freq", "levels_db", "step_seconds"},
}
OPTIONAL = {"level_db", "tail", "seed", "gap_seconds"}


def validate(spec):
    kind = spec.get("type")
    if kind not in TYPES:
        raise ValueError(f"unknown signal type {kind!r} (known: {', '.join(TYPES)})")
    missing = TYPES[kind] - spec.keys()
    if missing:
        raise ValueError(f"{kind} signal needs {', '.join(sorted(missing))}")
    unknown = spec.keys() - TYPES[kind] - OPTIONAL - {"type"}
    if unknown:
        raise ValueError(f"{kind} signal: unknown keys {', '.join(sorted(unknown))}")


def body_seconds(spec):
    if spec["type"] == "clicks":
        return spec["period"] * spec["count"]
    if spec["type"] == "steps":
        return len(spec["levels_db"]) * (spec["step_seconds"] + spec.get("gap_seconds", 0))
    return spec["seconds"]


def total_seconds(spec):
    return (LEAD_SECONDS + MARKER_SECONDS + GAP_SECONDS + body_seconds(spec)
            + spec.get("tail", DEFAULT_TAIL_SECONDS))


def describe(spec):
    level = spec.get("level_db", 0)
    level_text = f" at {level:+g} dB" if level else ""
    kind = spec["type"]
    if kind == "sine":
        return f"{spec['freq']:g} Hz sine, {spec['seconds']:g} s{level_text}"
    if kind == "sweep":
        return f"sweep {spec['f0']:g}-{spec['f1']:g} Hz, {spec['seconds']:g} s{level_text}"
    if kind == "noise":
        return f"white noise, {spec['seconds']:g} s{level_text}"
    if kind == "clicks":
        return f"{spec['count']} clicks every {spec['period']:g} s{level_text}"
    if kind == "steps":
        gap = spec.get("gap_seconds", 0)
        gap_text = f" + {gap:g} s gaps" if gap else ""
        levels = "/".join(f"{v:g}" for v in spec["levels_db"])
        return f"{spec['freq']:g} Hz at {levels} dB, {spec['step_seconds']:g} s each{gap_text}"
    return f"silence, {spec['seconds']:g} s"


def fade(x, rate):
    n = min(int(FADE_SECONDS * rate), len(x) // 2)
    if n > 0:
        ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n)
        x[:n] *= ramp
        x[-n:] *= ramp[::-1]
    return x


def exp_sweep(rate, f0, f1, seconds):
    t = np.arange(int(seconds * rate)) / rate
    length = seconds / np.log(f1 / f0)
    return np.sin(2 * np.pi * f0 * length * np.expm1(t / length))


def marker(rate):
    """The latency marker: a Hann-windowed log chirp at full reference level."""
    x = exp_sweep(rate, MARKER_F0, MARKER_F1, MARKER_SECONDS)
    return x * np.hanning(len(x))


def steps(rate, freq, levels_db, step_seconds, gap_seconds):
    """A sine through `levels_db` (peak re 1), each held `step_seconds` and followed by
    `gap_seconds` of silence; the phase runs on through the steps. Level changes take 1 ms
    (raised cosine) so a step is sharp for a compressor but not a broadband click."""
    step, gap, ramp = int(step_seconds * rate), int(gap_seconds * rate), int(0.001 * rate)
    gains = []
    for level in levels_db:
        gains += [np.full(step, 10 ** (level / 20)), np.zeros(gap)]
    g = np.concatenate(gains)
    if ramp > 1:
        kernel = np.hanning(2 * ramp + 1)
        g = np.convolve(g, kernel / kernel.sum(), mode="same")
    t = np.arange(len(g)) / rate
    return g * np.sin(2 * np.pi * freq * t)


def body(spec, rate):
    """The signal itself, peak 1 (silence: zeros; steps: peak per its levels)."""
    kind = spec["type"]
    if kind == "sine":
        t = np.arange(int(spec["seconds"] * rate)) / rate
        return fade(np.sin(2 * np.pi * spec["freq"] * t), rate)
    if kind == "sweep":
        return fade(exp_sweep(rate, spec["f0"], spec["f1"], spec["seconds"]), rate)
    if kind == "noise":
        rng = np.random.default_rng(spec.get("seed", 1))
        x = fade(rng.standard_normal(int(spec["seconds"] * rate)), rate)
        return x / np.max(np.abs(x))
    if kind == "steps":
        return steps(rate, spec["freq"], spec["levels_db"], spec["step_seconds"],
                     spec.get("gap_seconds", 0))
    if kind == "clicks":
        period = int(spec["period"] * rate)
        x = np.zeros(period * spec["count"])
        x[::period] = 1.0
        return x
    return np.zeros(int(spec["seconds"] * rate))


def build(spec, rate, ref_dbfs):
    """The whole take as played: float32 samples, plus where the marker and signal start."""
    validate(spec)
    ref = 10 ** (ref_dbfs / 20)
    gain = ref * 10 ** (spec.get("level_db", 0) / 20)
    parts = [
        np.zeros(int(LEAD_SECONDS * rate)),
        marker(rate) * ref,
        np.zeros(int(GAP_SECONDS * rate)),
        body(spec, rate) * gain,
        np.zeros(int(spec.get("tail", DEFAULT_TAIL_SECONDS) * rate)),
    ]
    marker_start = len(parts[0])
    body_start = marker_start + len(parts[1]) + len(parts[2])
    played = np.concatenate(parts).astype(np.float32)
    if np.max(np.abs(played)) > 1.0:
        raise ValueError(f"{describe(spec)} would clip: lower its level_db or the rig's reference")
    return played, marker_start, body_start
