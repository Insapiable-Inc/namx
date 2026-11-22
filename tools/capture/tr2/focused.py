"""The TR-2 focused reamp against NAM-X's Tremolo (the TR-2 model). The tremolo runs free, so the
pedal's gain is read from the probe (a steady 9 kHz sine under every take): the pedal channel's
9 kHz line over the played one's, re the bypass take's (gain 1 = bypass).

  1. The gain's cycle per take: the period from the line's phase over the take, the gain folded
     over it (400 points, medians).
  2. The pedal against itself: each guitar take's cycle against its setting's probe-alone cycle —
     does playing, quiet or loud, change the gain? (rms, in units of full gain)
  3. The model against the pedal: NAM-X's processor (render_tr2) at the take's knobs (clock
     positions as % of the travel) and the take's measured rate, its cycle read from the same
     probe the same way; rms and largest difference, units of full gain.
  4. The scorecard on the guitar takes: the model rendered from the played signal with its LFO
     placed on the pedal's (the input padded by the cycles' offset), per third-octave band.

  tr2/focused.py [SESSION]   (default captures/boss/tr-2/2026-11-09-focused)

Run from the repository root, with build-tr2/render_tr2 built (the command in
tools/capture/render_tr2.cpp's header)."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/tr-2/2026-11-09-focused")
RATE = scorecard.RATE
PROBE_HZ = 9000.0
POINTS = 400
LOW_PASS = scipy.signal.butter(4, 400, "lowpass", fs=RATE, output="sos")
PCT = {"7:00": 0.0, "9:00": 20.0, "12:00": 50.0, "3:00": 80.0, "5:00": 100.0}  # of the knob's travel


def line(x):
    """The 9 kHz line's amplitude over time (400 Hz either side, no delay)."""
    z = x * np.exp(-2j * np.pi * PROBE_HZ * np.arange(len(x)) / RATE)
    return 2 * np.abs(scipy.signal.sosfiltfilt(LOW_PASS, z.real) + 1j * scipy.signal.sosfiltfilt(LOW_PASS, z.imag))


def body(meta):
    a = meta["signal_start"]
    return a + RATE // 10, a + int(meta["signal"].get("seconds", 0) * RATE) - RATE // 10


def period_of(g):
    """The gain's period in samples: the autocorrelation's peak, refined by the fundamental's phase
    over the take (a line through the phase of one-second stretches)."""
    x = g - g.mean()
    ac = scipy.signal.correlate(x, x, "full")[len(x) - 1:]
    lo = int(0.03 * RATE)
    f0 = RATE / (lo + np.argmax(ac[lo:3 * RATE]))
    for _ in range(3):
        z = x * np.exp(-2j * np.pi * f0 * np.arange(len(x)) / RATE)
        hop = RATE // 2
        phases = np.unwrap([np.angle(z[i:i + RATE].sum()) for i in range(0, len(x) - RATE, hop)])
        f0 += np.polyfit(np.arange(len(phases)) * hop / RATE, phases, 1)[0] / (2 * np.pi)
    return RATE / f0


def fold(g, period):
    phase = (np.arange(len(g)) / period) % 1.0
    bins = np.minimum((phase * POINTS).astype(int), POINTS - 1)
    return np.array([np.median(g[bins == k]) for k in range(POINTS)])


def align(reference, cycle):
    """`cycle` turned (a fraction of a point too) onto `reference`; returns it and the turn, in cycles."""
    a, b = np.fft.rfft(reference - reference.mean()), np.fft.rfft(cycle - cycle.mean())
    k = np.arange(len(a))
    turns = np.linspace(0, 1, 4000, endpoint=False)
    best = turns[np.argmax([np.real(np.sum(a * np.conj(b * np.exp(-2j * np.pi * k * t)))) for t in turns])]
    spectrum = np.fft.rfft(cycle) * np.exp(-2j * np.pi * k * best)
    return np.fft.irfft(spectrum, POINTS), best


def render(played, meta, rate_hz, pad=0):
    s = meta["settings"]
    x = np.concatenate([np.zeros(pad), played]).astype(np.float32)
    y = subprocess.run(["build-tr2/render_tr2", f"{rate_hz:.6f}", f"{PCT[s['Depth']]}", f"{PCT[s['Wave']]}"],
                       input=x.tobytes(), capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64)[pad:]


def main():
    takes = sorted((json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    meta, played, pedal, _ = scorecard.load(SESSION, "bypass-chords")
    h = scorecard.chain_filter(played, pedal)
    a, b = body(meta)
    unity = np.median(line(pedal)[a:b] / line(played)[a:b])
    reading_noise = np.std(line(pedal)[a:b] / line(played)[a:b] / unity)

    cycles, rows, scores = {}, [], []
    for meta in takes:
        name = meta["take"]
        if meta["bypass"]:
            continue
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        a, b = body(meta)
        probe_in = line(played)[a:b]
        g = line(pedal)[a:b] / probe_in / unity
        if g.std() < 0.01:  # Depth 7:00: no cycle
            period, cycle = float("nan"), np.full(POINTS, np.median(g))
        else:
            period = period_of(g)
            cycle = fold(g, period)
        setting = json.dumps(meta["settings"])
        first = cycles.setdefault(setting, (name, cycle))
        own = float("nan")
        if first[0] != name:
            own = np.sqrt(np.mean((align(first[1], cycle)[0] - first[1]) ** 2))
        # The model: the same played signal, its probe read the same way. Its LFO adds single-precision
        # steps, so its rate is a hair off the one asked: ask again by that much.
        rate_hz = 5.0 if np.isnan(period) else RATE / period
        asked = rate_hz
        if not np.isnan(period):
            for _ in range(2):
                got = RATE / period_of(line(render(played, meta, asked))[a:b] / probe_in)
                asked *= rate_hz / got
        model = render(played, meta, asked)
        gm = line(model)[a:b] / probe_in
        model_cycle = fold(gm, RATE / rate_hz)
        turned, turn = align(cycle, model_cycle)
        diff = turned - cycle
        rows.append((name, rate_hz, cycle.min(), cycle.max(), own, np.sqrt(np.mean(diff ** 2)), np.abs(diff).max()))
        if meta["signal"]["type"] == "file":
            # The model's LFO must run `turn` of a cycle later: start it (1 - turn) of a cycle early.
            pad = 0 if np.isnan(period) else int(round((1.0 - turn) * period))
            placed = scorecard.through(h, render(played, meta, asked, pad))
            scores.append((name, scorecard.score(pedal, placed, pre, meta["signal_start"], time_invariant=False)))

    print(f"probe reading: bypass gain steady to {reading_noise:.4f} of full gain\n")
    print(f"{'take':40s} rate Hz  gain min   max │ vs probe alone │ model: rms    max   (units of full gain)")
    for name, rate_hz, lo, hi, own, rms, worst in rows:
        own_text = "      —       " if np.isnan(own) else f"    {own:.4f}    "
        print(f"{name:40s} {rate_hz:7.3f}   {lo:6.3f} {hi:6.3f} │{own_text}│      {rms:.4f} {worst:.4f}")
    print()
    print(scorecard.HEADER)
    for name, s in scores:
        print(scorecard.row(name, s))
    print()
    print(scorecard.tone_rows(scores, step=2))


if __name__ == "__main__":
    main()
