"""The TR-2 model in Python, as src/dsp/Tr2Tremolo.h computes it (the frozen SUPPLY voicing), for
fitting candidates against measured gain cycles; and the cycles themselves, read from a steady
9 kHz tone at full resolution (2.5 kHz either side: 0.2 ms)."""
import json, pathlib, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

RATE = scorecard.RATE
PROBE_HZ = 9000.0
BINS = 1024
WIDE = scipy.signal.butter(4, 2500, "lowpass", fs=RATE, output="sos")
PCT = {"7:00": 0.0, "9:00": 20.0, "9:30": 25.0, "12:00": 50.0, "2:30": 75.0, "3:00": 80.0, "5:00": 100.0}

# src/dsp/Tr2Tremolo.h, SUPPLY.
SUPPLY = {
    "u0": 1.10998, "beta": 0.43044, "kappa": 0.80716, "sharpness": 32.3874,
    "tau_down": 0.00352, "tau_up": 0.00921,
    "wave": [0.6086, 0.8973, 1.4543, 2.9215, 3.5130],
    "depth": [0.0278, 0.1515, 0.4603, 0.7878, 1.0],
    "top": [1.0452, 1.0578, 1.0690, 1.0410, 0.9952],
}


def knob_map(table, pct):
    return float(np.interp(pct / 25.0, np.arange(5), table))


def soft_clamp(v, sharpness):
    return (np.logaddexp(0, sharpness * v) - np.logaddexp(0, sharpness * (v - 1))) / sharpness


def follower(target, dt, tau_down, tau_up, cycles=3):
    down, up = 1 - np.exp(-dt / tau_down), 1 - np.exp(-dt / tau_up)
    y = target[-1]
    out = np.empty_like(target)
    for _ in range(cycles):
        for i, t in enumerate(target):
            y += (up if t > y else down) * (t - y)
            out[i] = y
    return out


def soft_floor(x, floor, sharpness):
    """x, but no lower than `floor` (a rounded corner)."""
    return floor + np.logaddexp(0, sharpness * (x - floor)) / sharpness


def cycle(voicing, wave_gain, depth, top, lfo_hz, bins=BINS, candidate=None):
    """One period of the model's gain, the LFO's phase 0 at the first point. `candidate`: None for
    the frozen model, or {"under": d, "rail": L, "rail_sharpness": s}: the control may sink to -d
    under silence before the follower (the gain itself stops at 0), and the squared wave stops at L
    before Depth scales it."""
    phase = (np.arange(bins) + 0.5) / bins
    tri = 1 - 4 * np.minimum(phase, 1 - phase)
    shaped = wave_gain * (tri + voicing["beta"])
    under = 0.0
    if candidate:
        shaped = soft_floor(shaped, candidate["rail"], candidate["rail_sharpness"])
        under = candidate["under"]
    v = voicing["u0"] + depth * (shaped - voicing["kappa"])
    # The clamp's window is [-under, 1]: the frozen model's with under = 0.
    target = (1 + under) * soft_clamp((v + under) / (1 + under), voicing["sharpness"] * (1 + under)) - under
    g = follower(target, 1 / (lfo_hz * bins), voicing["tau_down"], voicing["tau_up"])
    return top * np.maximum(g, 0.0)


def line(x, low_pass=WIDE, carrier=PROBE_HZ):
    z = x * np.exp(-2j * np.pi * carrier * np.arange(len(x)) / RATE)
    return 2 * np.abs(scipy.signal.sosfiltfilt(low_pass, z.real) + 1j * scipy.signal.sosfiltfilt(low_pass, z.imag))


def period_of(g):
    x = g - g.mean()
    ac = scipy.signal.correlate(x, x, "full", method="fft")[len(x) - 1:]
    lo = max(int(0.03 * RATE), int(np.argmax(ac < 0)))  # past the first lobe: a slow cycle's is wide
    f0 = RATE / (lo + np.argmax(ac[lo:4 * RATE]))
    for _ in range(3):
        z = x * np.exp(-2j * np.pi * f0 * np.arange(len(x)) / RATE)
        span = int(max(RATE, 2 * RATE / f0))
        hop = span // 2
        phases = np.unwrap([np.angle(z[i:i + span].sum()) for i in range(0, len(x) - span, hop)])
        if len(phases) > 1:
            f0 += np.polyfit(np.arange(len(phases)) * hop / RATE, phases, 1)[0] / (2 * np.pi)
    return RATE / f0


def fold(g, period, bins=BINS):
    phase = (np.arange(len(g)) / period) % 1.0
    index = np.minimum((phase * bins).astype(int), bins - 1)
    out = np.array([np.median(g[index == k]) if np.any(index == k) else np.nan for k in range(bins)])
    bad = np.isnan(out)
    out[bad] = np.interp(np.flatnonzero(bad), np.flatnonzero(~bad), out[~bad], period=bins)
    return out


def align(reference, other):
    """`other` turned onto `reference` (a fraction of a point too)."""
    a, b = np.fft.rfft(reference - reference.mean()), np.fft.rfft(other - other.mean())
    k = np.arange(len(a))
    cross = np.fft.irfft(a * np.conj(b), 16 * len(reference))  # the turn, to a 16th of a point
    turn = np.argmax(cross) / len(cross)
    return np.fft.irfft(np.fft.rfft(other) * np.exp(-2j * np.pi * k * turn), len(reference))


NARROW = scipy.signal.butter(4, 400, "lowpass", fs=RATE, output="sos")


def measured(session, bypass="bypass", only_tone=True, carrier=PROBE_HZ):
    """{take: (settings, lfo_hz, cycle)} for a session's steady-tone takes, gain 1 = the bypass take.
    A 1 kHz carrier (the 2026-10-26 sessions) is read 400 Hz either side."""
    session = pathlib.Path(session)
    low_pass = WIDE if carrier > 5000 else NARROW

    def line(x):  # noqa: F811 — this session's carrier
        z = x * np.exp(-2j * np.pi * carrier * np.arange(len(x)) / RATE)
        return 2 * np.abs(scipy.signal.sosfiltfilt(low_pass, z.real) + 1j * scipy.signal.sosfiltfilt(low_pass, z.imag))

    meta, played, pedal, _ = scorecard.load(session, bypass)
    a, b = meta["signal_start"] + RATE // 5, meta["signal_start"] + int(meta["signal"]["seconds"] * RATE) - RATE // 5
    unity = np.median(line(pedal)[a:b] / line(played)[a:b])
    out = {}
    for path in sorted(session.glob("*.json")):
        meta = json.loads(path.read_text())
        if (path.name == "rig.json" or meta["bypass"] or (only_tone and meta["signal"]["type"] != "sine")
                or meta["signal"].get("freq", carrier) != carrier or meta["settings"]["Depth"] == "7:00"):
            continue
        meta, played, pedal, _ = scorecard.load(session, path.stem)
        a = meta["signal_start"] + RATE // 5
        b = meta["signal_start"] + int(meta["signal"]["seconds"] * RATE) - RATE // 5
        g = line(pedal)[a:b] / line(played)[a:b] / unity
        period = period_of(g)
        out[path.stem] = (meta["settings"], RATE / period, fold(g, period))
    return out
