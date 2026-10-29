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
    {"type": "plucks", "freq": 1000, "period": 3, "count": 4, "decay_seconds": 0.3}
                                                               # picked-note-like bursts
    {"type": "pluck-train", "freq": 1000, "onsets": [0, 2.9, 6.6], "decay_seconds": 0.3,
     "seconds": 10, "repick": true}                            # plucks at given times; with
                                                               # repick, each ends the last
    {"type": "file", "path": "captures/di/phrases.wav"}       # a recording (capture.py di)

plus optional "level_db" (default 0) and "tail" (seconds after the signal, default 1).
A file's path is relative to the repository; it holds the guitar as it reached the pedal,
scaled so 1.0 is the guitar's peak, at the rig's rate, and plays at the level it was played.
"""

import pathlib

import numpy as np
import soundfile

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]

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
    "plucks": {"freq", "period", "count", "decay_seconds"},
    "pluck-train": {"freq", "onsets", "decay_seconds", "seconds"},
    "file": {"path"},
}
OPTIONAL = {"level_db", "tail", "seed", "gap_seconds", "repick"}
REPICK_SECONDS = 0.002  # a re-picked string: the ringing note dies in the pick's 2 ms


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


def recording(spec):
    path = REPOSITORY / spec["path"]
    if not path.exists():
        raise ValueError(f"{spec['path']} is missing: record it with 'capture.py di'")
    return path


def body_seconds(spec):
    if spec["type"] == "clicks":
        return spec["period"] * spec["count"]
    if spec["type"] == "steps":
        return len(spec["levels_db"]) * (spec["step_seconds"] + spec.get("gap_seconds", 0))
    if spec["type"] == "plucks":
        return spec["period"] * spec["count"]
    if spec["type"] == "file":
        return soundfile.info(recording(spec)).duration
    return spec["seconds"]


def total_seconds(spec):
    return (LEAD_SECONDS + MARKER_SECONDS + GAP_SECONDS + body_seconds(spec)
            + spec.get("tail", DEFAULT_TAIL_SECONDS))


def silent_windows(spec):
    """Stretches (start, end), seconds from the signal's start, where nothing is played:
    the gaps between bursts, the space between clicks, the tail. A bypassed pedal's output
    must be silent there too."""
    windows = []
    kind = spec["type"]
    if kind == "steps" and spec.get("gap_seconds", 0) > 0.1:
        period = spec["step_seconds"] + spec["gap_seconds"]
        for i in range(len(spec["levels_db"])):
            windows.append((i * period + spec["step_seconds"] + 0.05, (i + 1) * period - 0.01))
    if kind == "plucks":
        quiet_after = 7 * spec["decay_seconds"]  # -60 dB
        for k in range(spec["count"]):
            if quiet_after + 0.05 < spec["period"]:
                windows.append((k * spec["period"] + quiet_after, (k + 1) * spec["period"] - 0.01))
    if kind == "pluck-train":
        ends = list(spec["onsets"][1:]) + [spec["seconds"]]
        for onset, end in zip(spec["onsets"], ends):
            if onset + 7 * spec["decay_seconds"] + 0.05 < end:
                windows.append((onset + 7 * spec["decay_seconds"], end - 0.01))
    if kind == "clicks" and spec["period"] > 0.1:
        for k in range(spec["count"]):
            windows.append((k * spec["period"] + 0.05, (k + 1) * spec["period"] - 0.01))
    tail = spec.get("tail", DEFAULT_TAIL_SECONDS)
    if tail > 0.2:
        start = body_seconds(spec)
        windows.append((start + 0.1, start + tail))
    return windows


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
    if kind == "plucks":
        return (f"{spec['count']} {spec['freq']:g} Hz plucks every {spec['period']:g} s "
                f"(decay {spec['decay_seconds']:g} s){level_text}")
    if kind == "pluck-train":
        onsets = ", ".join(f"{t:g}" for t in spec["onsets"])
        repick = ", re-picked" if spec.get("repick") else ""
        return (f"{spec['freq']:g} Hz plucks at {onsets} s (decay {spec['decay_seconds']:g} s"
                f"{repick}){level_text}")
    if kind == "file":
        return f"{spec['path']}, {body_seconds(spec):.1f} s{level_text}"
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
    """The signal itself, peak 1 (silence: zeros; steps: peak per its levels; a file: 1 is
    the guitar's peak)."""
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
    if kind == "pluck-train":
        x = np.zeros(int(spec["seconds"] * rate))
        ends = [int(t * rate) for t in spec["onsets"][1:]] + [len(x)]
        for onset, end in zip(spec["onsets"], ends):
            start = int(onset * rate)
            stop = end if spec.get("repick") else len(x)
            t = np.arange(stop - start) / rate
            note = (np.minimum(t / 0.002, 1.0) * np.exp(-t / spec["decay_seconds"])
                    * np.sin(2 * np.pi * spec["freq"] * t))
            if stop < len(x):  # damped by the next pick, which starts as this one ends
                n = int(REPICK_SECONDS * rate)
                note[-n:] *= np.linspace(1, 0, n)
            x[start:stop] += note
        return x / np.max(np.abs(x))
    if kind == "file":
        x, file_rate = soundfile.read(recording(spec), dtype="float64")
        if file_rate != rate or x.ndim != 1:
            raise ValueError(f"{spec['path']}: needs mono at {rate} Hz (it is {file_rate} Hz"
                             f"{'' if x.ndim == 1 else ', not mono'})")
        return x
    if kind == "plucks":
        period = int(spec["period"] * rate)
        t = np.arange(period) / rate
        attack = np.minimum(t / 0.002, 1.0)  # a 2 ms rise: sharp, but not a click
        one = attack * np.exp(-t / spec["decay_seconds"]) * np.sin(2 * np.pi * spec["freq"] * t)
        return np.tile(one, spec["count"])
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
