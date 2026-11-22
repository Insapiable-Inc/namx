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

    {"type": "sine", "freq": 1000, "seconds": 20}              # freq may be a list: a chord
    {"type": "sweep", "f0": 20, "f1": 20000, "seconds": 10}    # exponential (Farina)
    {"type": "noise", "seconds": 10, "seed": 1}                # white, peak-normalized
    {"type": "clicks", "period": 2.0, "count": 5}              # one-sample impulses
    {"type": "silence", "seconds": 10}
    {"type": "steps", "freq": 1000, "levels_db": [-24, 0, -24], "step_seconds": 3,
     "gap_seconds": 0}                                         # a sine through levels
    {"type": "noise-steps", "levels_db": [-48, 0, -48], "step_seconds": 3, "seed": 1}
                                                               # white noise through levels
    {"type": "plucks", "freq": 1000, "period": 3, "count": 4, "decay_seconds": 0.3}
                                                               # picked-note-like bursts
    {"type": "pluck-train", "freq": 1000, "onsets": [0, 2.9, 6.6], "decay_seconds": 0.3,
     "seconds": 10, "repick": true}                            # plucks at given times; with
                                                               # repick, each ends the last;
                                                               # freq may be a list, one per
                                                               # onset (equal onsets: a chord)
    {"type": "file", "path": "captures/di/phrases.wav"}       # a recording (capture.py di);
                                                               # "start" and "seconds" play
                                                               # an excerpt of it (faded
                                                               # 10 ms at each cut); "probe":
                                                               # {"freq": 9000, "level_db":
                                                               # -30} a steady sine under it
                                                               # (re the reference whatever
                                                               # the file's level_db): the
                                                               # pedal's gain read all along

    {"type": "sequence", "parts": [{…}, {…}]}                  # several signals in one
                                                               # take, each at its own
                                                               # level_db and followed by
                                                               # its own tail (part_starts
                                                               # says where each begins)

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
    "noise-steps": {"levels_db", "step_seconds"},
    "plucks": {"freq", "period", "count", "decay_seconds"},
    "pluck-train": {"freq", "onsets", "decay_seconds", "seconds"},
    "file": {"path"},
    "sequence": {"parts"},
}
OPTIONAL = {"level_db", "tail", "seed", "gap_seconds", "repick"}
FILE_OPTIONAL = {"start", "seconds", "probe"}  # an excerpt of the recording; a sine under it
REPICK_SECONDS = 0.002  # a re-picked string: the ringing note dies in the pick's 2 ms


def validate(spec):
    kind = spec.get("type")
    if kind not in TYPES:
        raise ValueError(f"unknown signal type {kind!r} (known: {', '.join(TYPES)})")
    if kind == "sequence":
        if not spec.get("parts") or any(part.get("type") == "sequence" for part in spec["parts"]):
            raise ValueError("a sequence needs parts, none of them a sequence")
        if "level_db" in spec:
            raise ValueError("a sequence has no level_db of its own: each part has one")
        for part in spec["parts"]:
            validate(part)
    missing = TYPES[kind] - spec.keys()
    if missing:
        raise ValueError(f"{kind} signal needs {', '.join(sorted(missing))}")
    optional = OPTIONAL | FILE_OPTIONAL if kind == "file" else OPTIONAL
    unknown = spec.keys() - TYPES[kind] - optional - {"type"}
    if unknown:
        raise ValueError(f"{kind} signal: unknown keys {', '.join(sorted(unknown))}")


def pluck_cut_db(spec):
    """How far a pluck has decayed (dB) where the signal stops it: the end of a plucks
    period, or a pluck-train's end for its last pluck; None for other signals. A pluck
    still ringing there is cut, a step into the pedal that a reverb then rings with."""
    if spec["type"] == "plucks":
        ring = spec["period"]
    elif spec["type"] == "pluck-train":
        ring = spec["seconds"] - max(spec["onsets"])
    else:
        return None
    return -20 * np.log10(np.e) * ring / spec["decay_seconds"]


def recording(spec):
    path = REPOSITORY / spec["path"]
    if not path.exists():
        raise ValueError(f"{spec['path']} is missing: record it with 'capture.py di'")
    return path


def pluck_freqs(spec):
    """A pluck-train's frequency per onset: one for all, or a list, one each."""
    if not isinstance(spec["freq"], list):
        return [spec["freq"]] * len(spec["onsets"])
    if len(spec["freq"]) != len(spec["onsets"]):
        raise ValueError("pluck-train: freq needs one value per onset")
    return spec["freq"]


def part_starts(spec):
    """Where each part of a sequence begins, seconds from the signal's start."""
    starts, at = [], 0.0
    for part in spec["parts"]:
        starts.append(at)
        at += body_seconds(part) + part.get("tail", DEFAULT_TAIL_SECONDS)
    return starts


def body_seconds(spec):
    if spec["type"] == "sequence":  # every part with its tail
        last = spec["parts"][-1]
        return part_starts(spec)[-1] + body_seconds(last) + last.get("tail", DEFAULT_TAIL_SECONDS)
    if spec["type"] == "clicks":
        return spec["period"] * spec["count"]
    if spec["type"] in ("steps", "noise-steps"):
        return len(spec["levels_db"]) * (spec["step_seconds"] + spec.get("gap_seconds", 0))
    if spec["type"] == "plucks":
        return spec["period"] * spec["count"]
    if spec["type"] == "file":
        left = soundfile.info(recording(spec)).duration - spec.get("start", 0)
        if left <= 0 or spec.get("seconds", left) > left + 1e-9:
            raise ValueError(f"{spec['path']}: the excerpt runs past the recording's end")
        return spec.get("seconds", left)
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
    if kind == "sequence":
        for start, part in zip(part_starts(spec), spec["parts"]):
            windows += [(start + a, start + b) for a, b in silent_windows(part)]
    if kind in ("steps", "noise-steps") and spec.get("gap_seconds", 0) > 0.1:
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
    if kind == "sequence":
        return f"{len(spec['parts'])} signals, {body_seconds(spec):.0f} s: " + "; ".join(
            f"{describe(part)} + {part.get('tail', DEFAULT_TAIL_SECONDS):g} s" for part in spec["parts"])
    if kind == "sine":
        if isinstance(spec["freq"], list):
            notes = "/".join(f"{f:g}" for f in spec["freq"])
            return f"{notes} Hz sines, {spec['seconds']:g} s{level_text}"
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
        repick = ", re-picked" if spec.get("repick") else ""
        if isinstance(spec["freq"], list):
            return (f"{len(spec['onsets'])} plucks, {min(spec['freq']):g}-{max(spec['freq']):g} Hz, "
                    f"{spec['seconds']:g} s (decay {spec['decay_seconds']:g} s{repick}){level_text}")
        onsets = ", ".join(f"{t:g}" for t in spec["onsets"])
        return (f"{spec['freq']:g} Hz plucks at {onsets} s (decay {spec['decay_seconds']:g} s"
                f"{repick}){level_text}")
    if kind == "file":
        excerpt = f" from {spec['start']:g} s" if spec.get("start") else ""
        probe = (" + " + "/".join(f"{f:g}" for f in np.atleast_1d(spec["probe"]["freq"])) + " Hz probe"
                 if "probe" in spec else "")
        return f"{spec['path']}{excerpt}, {body_seconds(spec):.1f} s{level_text}{probe}"
    if kind == "steps":
        gap = spec.get("gap_seconds", 0)
        gap_text = f" + {gap:g} s gaps" if gap else ""
        levels = "/".join(f"{v:g}" for v in spec["levels_db"])
        return f"{spec['freq']:g} Hz at {levels} dB, {spec['step_seconds']:g} s each{gap_text}"
    if kind == "noise-steps":
        gap = spec.get("gap_seconds", 0)
        gap_text = f" + {gap:g} s gaps" if gap else ""
        levels = "/".join(f"{v:g}" for v in spec["levels_db"])
        return f"white noise at {levels} dB, {spec['step_seconds']:g} s each{gap_text}"
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


def step_gains(rate, levels_db, step_seconds, gap_seconds):
    """The gain through `levels_db` (re 1), each held `step_seconds` and followed by
    `gap_seconds` of silence. Level changes take 1 ms (raised cosine) so a step is sharp
    for a compressor but not a broadband click."""
    step, gap, ramp = int(step_seconds * rate), int(gap_seconds * rate), int(0.001 * rate)
    gains = []
    for level in levels_db:
        gains += [np.full(step, 10 ** (level / 20)), np.zeros(gap)]
    g = np.concatenate(gains)
    if ramp > 1:
        kernel = np.hanning(2 * ramp + 1)
        g = np.convolve(g, kernel / kernel.sum(), mode="same")
    return g


def steps(rate, freq, levels_db, step_seconds, gap_seconds):
    """A sine through `levels_db` (peak re 1); the phase runs on through the steps."""
    g = step_gains(rate, levels_db, step_seconds, gap_seconds)
    t = np.arange(len(g)) / rate
    return g * np.sin(2 * np.pi * freq * t)


def body(spec, rate):
    """The signal itself, peak 1 (silence: zeros; steps: peak per its levels; a file: 1 is
    the guitar's peak)."""
    kind = spec["type"]
    if kind == "sequence":  # each part at its own level, then its tail of silence
        pieces = []
        for part in spec["parts"]:
            pieces += [body(part, rate) * 10 ** (part.get("level_db", 0) / 20),
                       np.zeros(int(part.get("tail", DEFAULT_TAIL_SECONDS) * rate))]
        return np.concatenate(pieces)
    if kind == "sine":
        t = np.arange(int(spec["seconds"] * rate)) / rate
        freqs = spec["freq"] if isinstance(spec["freq"], list) else [spec["freq"]]
        x = sum(np.sin(2 * np.pi * f * t) for f in freqs)
        return fade(x / np.max(np.abs(x)), rate)  # a chord: equal sines, peak 1
    if kind == "sweep":
        return fade(exp_sweep(rate, spec["f0"], spec["f1"], spec["seconds"]), rate)
    if kind == "noise":
        rng = np.random.default_rng(spec.get("seed", 1))
        x = fade(rng.standard_normal(int(spec["seconds"] * rate)), rate)
        return x / np.max(np.abs(x))
    if kind == "steps":
        return steps(rate, spec["freq"], spec["levels_db"], spec["step_seconds"],
                     spec.get("gap_seconds", 0))
    if kind == "noise-steps":
        # One peak-normalized noise for the whole take: a step's level is that noise's
        # peak at the step's gain (a 1.5 s step's own peak lands up to ~2 dB under it).
        g = step_gains(rate, spec["levels_db"], spec["step_seconds"], spec.get("gap_seconds", 0))
        rng = np.random.default_rng(spec.get("seed", 1))
        x = rng.standard_normal(len(g))
        return g * x / np.max(np.abs(x))
    if kind == "pluck-train":
        x = np.zeros(int(spec["seconds"] * rate))
        ends = [int(t * rate) for t in spec["onsets"][1:]] + [len(x)]
        freqs = pluck_freqs(spec)
        for onset, end, freq in zip(spec["onsets"], ends, freqs):
            start = int(onset * rate)
            stop = end if spec.get("repick") else len(x)
            t = np.arange(stop - start) / rate
            note = (np.minimum(t / 0.002, 1.0) * np.exp(-t / spec["decay_seconds"])
                    * np.sin(2 * np.pi * freq * t))
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
        if {"start", "seconds"} & spec.keys():
            first = int(spec.get("start", 0) * rate)
            x = x[first:first + int(body_seconds(spec) * rate)].copy()
            ramp = np.linspace(0, 1, int(FADE_SECONDS * rate))
            x[:len(ramp)] *= ramp
            x[-len(ramp):] *= ramp[::-1]
        if "probe" in spec:
            probe = spec["probe"]
            if probe.keys() != {"freq", "level_db"}:
                raise ValueError("a probe needs freq and level_db")
            tone = sum(np.sin(2 * np.pi * f * np.arange(len(x)) / rate)
                       for f in np.atleast_1d(probe["freq"]))  # a list: each tone at level_db
            ramp = np.linspace(0, 1, int(FADE_SECONDS * rate))
            tone[:len(ramp)] *= ramp
            tone[-len(ramp):] *= ramp[::-1]
            x = x + tone * 10 ** ((probe["level_db"] - spec.get("level_db", 0)) / 20)
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
