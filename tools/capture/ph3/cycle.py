#!/usr/bin/env python3
"""The PH-3's Rise and Fall over one sweep cycle, without a model (the Res session, 2026-11-11-res).

Each of the nine tones' complex gain (the pedal over the loopback, re the bypass take) is periodic in the
sweep: its Fourier series over the cycle turns out one-sided, H(u) = h0 + h1 u + h2 u^2 + ... with
u = exp(j*2*pi*t/T), and from the third term on it obeys a two-term recurrence: H(u) is a ratio of two
quadratics in u. Two poles in u = two feedback loops, one per pass; a pole's size is that loop's gain at
the tone, its angle when in the cycle the resonance passes the tone. Printed per tone and take: the poles
and zeros in u and how well the recurrence holds.

  ph3/cycle.py TAKE [TAKE ...]      e.g. a-res700-rise a-res1200-rise a-res300-rise a-res500-rise

Run from the repository root."""
import json, pathlib, sys
import numpy as np, scipy.signal, soundfile

SESSION = pathlib.Path("captures/boss/ph-3/2026-11-11-res")
RATE = 48000
TONES = np.array([113, 197, 331, 577, 997, 1723, 2971, 5147, 8887.0])
TERMS = 16
_reference = None


def lines(name, step=24):
    """Each tone's complex gain, pedal over loopback, every `step` samples; and the times."""
    meta = json.loads((SESSION / f"{name}.json").read_text())
    x, _ = soundfile.read(SESSION / f"{name}.wav", dtype="float64")
    a = meta["signal_start"] + meta["latency_samples"]
    n = int(meta["signal"]["seconds"] * RATE)
    t = np.arange(n) / RATE
    out = []
    for f in TONES:
        gap = min(abs(f - g) for g in TONES if g != f)
        low = scipy.signal.butter(4, min(110, 0.42 * gap), fs=RATE, output="sos")
        e = np.exp(-2j * np.pi * f * t)
        out.append((scipy.signal.sosfiltfilt(low, x[a:a + n, 0] * e) /
                    scipy.signal.sosfiltfilt(low, x[a:a + n, 1] * e))[4800:-4800:step])
    return np.array(out), np.arange(4800, n - 4800, step) / RATE


def gains(name):
    """The pedal's gain re its bypass at each tone, over the take."""
    global _reference
    if _reference is None:
        _reference = lines("bypass-tones")[0].mean(axis=1)
    g, t = lines(name)
    return g / _reference[:, None], t


def period(g, t, low=1.2, high=1.8):
    """The sweep's period from the 997 Hz tone's level (searched between `low` and `high` seconds)."""
    x = np.abs(g[4]) - np.abs(g[4]).mean()
    dt = t[1] - t[0]
    ac = np.correlate(x, x, "full")[len(x) - 1:]
    k = int(low / dt) + np.argmax(ac[int(low / dt):int(high / dt)])
    y0, y1, y2 = ac[k - 1:k + 2]
    return (k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)) * dt


def series(name):
    """Fourier coefficients h0, h1, ... of each tone's gain over the cycle (on the side that carries them:
    Rise and Fall mirror each other), and the period."""
    g, t = gains(name)
    p = period(g, t)
    best = (0.0, p)
    for dp in np.linspace(-0.004, 0.004, 41):   # the period, refined: the most energy in the harmonics
        q = p + dp
        m = int(np.floor((t[-1] - t[0]) / q) * q / (t[1] - t[0]))
        ph = 2 * np.pi * (t[:m] - t[0]) / q
        e = sum(abs(np.mean(g[4, :m] * np.exp(-1j * k * ph))) ** 2 for k in range(-TERMS, TERMS + 1))
        if e > best[0]:
            best = (e, q)
    p = best[1]
    m = int(np.floor((t[-1] - t[0]) / p) * p / (t[1] - t[0]))
    ph = 2 * np.pi * (t[:m] - t[0]) / p
    h = np.array([[np.mean(g[i, :m] * np.exp(-1j * k * ph)) for k in range(-TERMS, TERMS + 1)]
                  for i in range(len(TONES))])
    rows = []
    for row in h:
        side = 1 if np.abs(row[TERMS + 1:]).sum() > np.abs(row[:TERMS]).sum() else -1
        rows.append(row[TERMS::side][:13])
    return np.array(rows), p


def identify(h, first=3, last=11):
    """H(u) = P(u) / Q(u) from h_n + q1 h_(n-1) + q2 h_(n-2) = 0 for n >= first: the poles (as p in
    1/(1 - p u)), the zeros, and the recurrence's relative error."""
    rows = [[h[n - 1], h[n - 2]] for n in range(first, last)]
    q = np.linalg.lstsq(np.array(rows), -np.array(h[first:last]), rcond=None)[0]
    Q = np.r_[1, q]
    P = np.array([sum(Q[j] * h[n - j] for j in range(0, min(n, 2) + 1)) for n in range(first)])
    left = [abs(h[n] + q[0] * h[n - 1] + q[1] * h[n - 2]) ** 2 for n in range(first, last)]
    error = np.sqrt(np.mean(left) / np.mean(np.abs(h[first:last]) ** 2))
    return 1 / np.roots(Q[::-1]), np.roots(P[::-1]), error


def main():
    polar = lambda v: " ".join(f"{abs(q):.3f}∠{np.degrees(np.angle(q)):+4.0f}°" for q in v)
    for name in sys.argv[1:]:
        h, p = series(name)
        print(f"{name}: period {p:.4f} s")
        for i in range(1, 8):
            poles, zeros, error = identify(h[i])
            levels = " ".join(f"{20 * np.log10(abs(v)):+5.1f}" for v in h[i][:5])
            print(f"  {TONES[i]:5.0f} Hz  |h0..h4| dB {levels}   poles {polar(sorted(poles, key=lambda q: -abs(q)))}"
                  f"   zeros {polar(sorted(zeros, key=abs))}   recurrence {error:.3f}")


if __name__ == "__main__":
    main()
