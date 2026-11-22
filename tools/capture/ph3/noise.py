#!/usr/bin/env python3
"""The PH-3's Rise and Fall read from a noise take, at every frequency at once.

Over the sweep cycle the response to a tone is H(u) = h0 + h1 u + h2 u^2 + ..., u = exp(j*2*pi*t/T)
(`cycle.py`: nine tones). White noise gives the same terms h_n(f) on a dense frequency grid: short-time
cross-spectra of the pedal channel against the loopback channel, projected on exp(-j*n*2*pi*t/T), over the
bypass take's.

  ph3/noise.py stages SESSION TAKE [WINDOW [PERIOD_LOW PERIOD_HIGH]]
      Res at its minimum: the series stops after u^3 (three stages, no loop). Each stage's wet over its dry
      along frequency, free of the phase network the stages share: with Q2 = h2 h0 / h1^2 and
      Q3 = h3 h0^2 / h1^3 (smooth in f), the roots of 1 + w + Q2 w^2 + Q3 w^3. Where two stages are alike
      the roots repel each other (a bump on one, a dip on the other): read the curves away from crossings.
  ph3/noise.py poles SESSION TAKE [WINDOW [PERIOD_LOW PERIOD_HIGH]]
      Res up: the two main poles in u per frequency (size = that loop's gain there, angle = when in the
      cycle its resonance passes).

  e.g. ph3/noise.py stages 2026-10-28-slow rise-rate-0800 8192 19 23
       ph3/noise.py stages 2026-10-28-modes rise-rate-1200
       ph3/noise.py poles 2026-10-28-modes rise-depth-0500-res-0500

WINDOW (samples, default 2048) must be short against the cycle (the terms turn n times a cycle) and long
against the pedal's own ringing: 2048 at Rate 12:00, 8192 at Rate 8:00. Writes SESSION/fit/noise-*.png.
Run from the repository root."""
import itertools, json, pathlib, sys
import numpy as np, scipy.optimize, scipy.signal, soundfile
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = pathlib.Path("captures/boss/ph-3")
RATE = 48000


def spectra(session, name, window):
    """Short-time spectra of the pedal and the loopback channel over the take's noise."""
    meta = json.loads((ROOT / session / f"{name}.json").read_text())
    x, _ = soundfile.read(ROOT / session / f"{name}.wav", dtype="float64")
    a = meta["signal_start"] + meta["latency_samples"]
    n = int(meta["signal"]["seconds"] * RATE)
    seg = x[a + RATE // 2:a + n - RATE // 4]
    args = dict(window="hann", nperseg=window, noverlap=window - window // 4, boundary=None, padded=False)
    f, t, pedal = scipy.signal.stft(seg[:, 0], RATE, **args)
    loop = scipy.signal.stft(seg[:, 1], RATE, **args)[2]
    return f, t, pedal, loop


def terms(session, name, window, low, high, grid, count):
    """h_n(f) for n = 0 .. count-1 on the side that carries the series, re bypass; and the period."""
    f, t, pedal, loop = spectra(session, name, window)
    _, _, pedal_b, loop_b = spectra(session, "bypass-noise", window)
    step = f[1] - f[0]
    bins = [np.where(np.abs(f - q) <= max(q * (2 ** (1 / 32) - 1), 0.75 * step))[0] for q in grid]
    cross = np.array([np.sum(pedal[i] * np.conj(loop[i]), axis=0) for i in bins])
    power = np.array([np.sum(np.abs(loop[i]) ** 2, axis=0) for i in bins])
    bypass = np.array([np.sum(pedal_b[i] * np.conj(loop_b[i])) / np.sum(np.abs(loop_b[i]) ** 2) for i in bins])
    orders = np.arange(-count + 1, count)
    mid = (grid > 500) & (grid < 3000)

    def project(period):
        m = int(np.floor((t[-1] - t[0]) / period) * period / (t[1] - t[0]))    # whole cycles
        e = np.exp(-2j * np.pi * np.outer(orders, t[:m] - t[0]) / period)
        return (cross[:, None, :m] * e[None]).sum(axis=2) / power[:, :m].sum(axis=1)[:, None]

    def moving(period):
        return np.sum(np.abs(project(period)[mid][:, orders != 0]) ** 2)

    period = max(np.arange(low, high, (high - low) / 400), key=moving)
    period = max(np.linspace(period * 0.997, period * 1.003, 61), key=moving)
    h = project(period) / bypass[:, None]
    up = np.sum(np.abs(h[:, orders > 0]) ** 2) > np.sum(np.abs(h[:, orders < 0]) ** 2)
    h = h[:, count - 1:] if up else h[:, count - 1::-1]
    return h, period


def smooth(z, half=7):
    kernel = np.hanning(2 * half + 1)
    kernel /= kernel.sum()
    return np.convolve(np.r_[z[half:0:-1], z, z[-2:-half - 2:-1]], kernel, "valid")


def stages(session, name, window, low, high):
    grid = np.geomspace(90, 11000, 280)
    h, period = terms(session, name, window, low, high, grid, 5)
    q2 = smooth(h[:, 2] * h[:, 0] / h[:, 1] ** 2)
    q3 = smooth(h[:, 3] * h[:, 0] ** 2 / h[:, 1] ** 3)
    first = smooth(np.abs(h[:, 1] / h[:, 0]))
    start = int(np.argmin(np.abs(grid - 4000)))                 # the stages are well apart there
    share = np.zeros((len(grid), 3), complex)
    previous = None
    for j in list(range(start, len(grid))) + list(range(start - 1, -1, -1)):
        if j == start - 1:
            previous = share[start]
        c = -1 / np.roots([q3[j], q2[j], 1, 1])
        if previous is None:
            c = np.array(sorted(c, key=lambda v: -abs(v)))
        else:
            c = c[list(min(itertools.permutations(range(3)),
                           key=lambda order: sum(abs(c[order[i]] - previous[i]) for i in range(3))))]
        share[j] = previous = c
    fig, ax = plt.subplots(3, 1, figsize=(15, 12), sharex=True)
    for n in range(4):
        ax[0].semilogx(grid, 20 * np.log10(np.abs(h[:, n]) + 1e-9), label=f"h{n}" + (" (unshifted)" if n == 0 else ""))
    ax[0].set_ylim(-35, 3)
    ax[0].set_ylabel("dB re bypass")
    ax[0].set_title(f"{session} {name}: the response's terms over the sweep cycle (period {period:.3f} s)")
    ax[1].semilogx(grid, np.abs(q2), label="|h2 h0 / h1²| (two equal stages: 0.25)")
    ax[1].semilogx(grid, np.abs(q3), label="|h3 h0² / h1³|")
    ax[1].axhline(0.25, color="gray", lw=0.5)
    ax[1].set_ylim(0, 0.6)
    for i in range(3):
        ax[2].semilogx(grid, np.abs(share[:, i]) * first, label=f"stage {i + 1}: wet over dry")
    ax[2].set_ylim(0, 1.6)
    ax[2].set_xlabel("Hz")
    for a in ax:
        a.legend()
        a.grid(alpha=0.3, which="both")
    fig.tight_layout()
    out = ROOT / session / "fit" / f"noise-stages-{name}.png"
    out.parent.mkdir(exist_ok=True)
    fig.savefig(out, dpi=62)
    print(f"{name}: period {period:.3f} s -> {out}")
    print("     Hz   h0 dB  h1 dB  h2 dB  h3 dB   each stage's wet over dry")
    for q in (100, 150, 200, 300, 450, 700, 1000, 1500, 2200, 3300, 5000, 7500, 10000):
        j = int(np.argmin(np.abs(grid - q)))
        print(f"  {grid[j]:6.0f}  " + " ".join(f"{20 * np.log10(abs(v) + 1e-9):+6.1f}" for v in h[j, :4])
              + "   " + "  ".join(f"{abs(v) * first[j]:.2f}" for v in share[j]))


def rational(h):
    """h0 .. h12 -> the two poles of the best (quadratic over quadratic) in u, and its relative error."""
    count = len(h)
    u = np.exp(2j * np.pi * np.arange(256) / 256)
    rows = []
    for k in range(count):
        row = np.zeros(5, complex)
        if k <= 2:
            row[k] = 1
        for j in (1, 2):
            if k - j >= 0:
                row[2 + j] = -h[k - j]
        rows.append(row)
    start = np.linalg.lstsq(np.array(rows), h, rcond=None)[0]
    weight = 1 / np.sqrt(np.sum(np.abs(h) ** 2))

    def series(c):
        return np.fft.fft((c[0] + c[1] * u + c[2] * u ** 2) / (1 + c[3] * u + c[4] * u ** 2))[:count] / len(u)

    fit = scipy.optimize.least_squares(lambda x: ((series(x.view(complex)) - h) * weight).view(float),
                                       start.view(float), max_nfev=200)
    c = fit.x.view(complex)
    return 1 / np.roots([c[4], c[3], 1]), np.sqrt(np.sum(np.abs(series(c) - h) ** 2)) * weight


def poles(session, name, window, low, high):
    grid = np.geomspace(150, 9000, 200)
    h, period = terms(session, name, window, low, high, grid, 13)
    found = []
    previous = None
    for row in h:
        p, error = rational(row)
        p = np.array(sorted(p, key=lambda v: -abs(v)))
        if previous is not None:                                 # follow each along f (about -190 degrees an octave)
            turn = np.exp(-1j * 0.11)
            if (abs(p[1] - previous[0] * turn) + abs(p[0] - previous[1] * turn)
                    < abs(p[0] - previous[0] * turn) + abs(p[1] - previous[1] * turn)):
                p = p[::-1]
        previous = p
        found.append(np.r_[p, error])
    found = np.array(found)
    apart = np.degrees(np.angle(found[:, 1] / found[:, 0]))
    fig, ax = plt.subplots(3, 1, figsize=(15, 12), sharex=True)
    for i in (0, 1):
        ax[0].semilogx(grid, np.abs(found[:, i]), ".", label=f"pole {i + 1}: size")
        ax[1].semilogx(grid, np.degrees(np.unwrap(np.angle(found[:, i]))), ".", label=f"pole {i + 1}: angle, degrees")
    ax[0].semilogx(grid, np.abs(found[:, 2]), color="gray", lw=0.6, label="the fit's relative error")
    ax[0].set_ylim(0, 1.1)
    ax[0].set_title(f"{session} {name}: the two loops' poles over the sweep cycle (period {period:.3f} s)")
    ax[2].semilogx(grid, apart, ".", label="pole 2 re pole 1, degrees")
    ax[2].set_xlabel("Hz")
    for a in ax:
        a.legend()
        a.grid(alpha=0.3, which="both")
    fig.tight_layout()
    out = ROOT / session / "fit" / f"noise-poles-{name}.png"
    out.parent.mkdir(exist_ok=True)
    fig.savefig(out, dpi=62)
    print(f"{name}: period {period:.3f} s -> {out}")
    for q in (197, 331, 577, 997, 1723, 2971, 5147):
        j = int(np.argmin(np.abs(grid - q)))
        print(f"  {grid[j]:6.0f} Hz  sizes {abs(found[j, 0]):.3f} {abs(found[j, 1]):.3f}   pole 2 re pole 1 {apart[j]:+5.0f}°"
              f"   error {abs(found[j, 2]):.3f}")


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("stages", "poles"):
        sys.exit(__doc__)
    session, name = sys.argv[2], sys.argv[3]
    window = int(sys.argv[4]) if len(sys.argv) > 4 else 2048
    low, high = (float(sys.argv[5]), float(sys.argv[6])) if len(sys.argv) > 6 else (1.2, 1.8)
    (stages if sys.argv[1] == "stages" else poles)(session, name, window, low, high)


if __name__ == "__main__":
    main()
