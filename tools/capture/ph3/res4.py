#!/usr/bin/env python3
"""4 Stage with Res at standing points (the session 2026-11-10-res4, plans/ph-3-res4.json).

  tools/capture/ph3/res4.py            the corners, the Res-off fit, the poles and zeros per point
  tools/capture/ph3/res4.py --plot     and fit/res4-slide.png: each Res position against frequency / corner

The sweep stands still at five points (Rate fully down); each is read with Res at 7:00, 12:00 and 5:00.
Each take is aligned to a fraction of a sample by its loopback channel: with whole-sample latencies the
Res-off takes fit the four stages to 10-50 %, aligned to 0.05-0.2 %.
"""
import json, pathlib, sys
import numpy as np, soundfile, scipy.optimize

REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
SESSION = REPOSITORY / "captures/boss/ph-3/2026-11-10-res4"
RATE, FS = 48000, 44100.0          # the recording's rate, the pedal's
N = 11 * RATE + RATE // 2
FREQ = np.fft.rfftfreq(N, 1 / RATE)
GRID = np.geomspace(40, 18000, 600)
EDGES = [(np.searchsorted(FREQ, f * 2 ** (-1 / 60)), np.searchsorted(FREQ, f * 2 ** (1 / 60)) + 1) for f in GRID]
POINTS = range(1, 6)


def raw(name):
    """A sweep take's response on GRID (complex, averaged over 1/30 octave), the loopback's delay taken out."""
    meta = json.loads((SESSION / f"{name}.json").read_text())
    recorded, _ = soundfile.read(SESSION / f"{name}.wav")
    played, _ = soundfile.read(SESSION / "played" / f"{name}.wav")
    a, lat = meta["signal_start"], meta["latency_samples"]
    pad = lambda v: np.pad(v, (0, N - len(v)))
    x, y, loop = pad(played[a:a + N]), pad(recorded[lat + a:lat + a + N, 0]), pad(recorded[lat + a:lat + a + N, 1])
    X = np.fft.rfft(x)
    h, hl = np.fft.rfft(y) / X, np.fft.rfft(loop) / X
    band = (FREQ > 200) & (FREQ < 8000)
    slope = np.polyfit(FREQ[band], np.unwrap(np.angle(hl[band])), 1)[0]
    h = h * np.exp(-1j * slope * FREQ)
    return np.array([h[i:j].mean() for i, j in EDGES])


def allpass(f, fc):
    t = np.tan(np.pi * fc / FS)
    a = (t - 1) / (t + 1)
    z = np.exp(-2j * np.pi * f / FS)
    return (a + z) / (1 + a * z)


def load():
    """The takes re the bypass; the corner of each point and the pedal's fixed tone K from the Res-off takes
    (H = K (1 + A^4), K shared)."""
    bypass = raw("bypass-sweep")
    H = {res: np.array([raw(f"point-{p}-res-{res}") / bypass for p in POINTS]) for res in ("0700", "1200", "0500")}
    fc = np.array([85.0, 4670.0, 1678.0, 307.0, 1113.0])
    sel = (GRID > 50) & (GRID < 16000)
    for _ in range(6):
        chains = np.array([1 + allpass(GRID, c) ** 4 for c in fc])
        K = (H["0700"] * np.conj(chains)).sum(0) / (abs(chains) ** 2).sum(0)
        for i in range(5):
            cost = lambda c: np.sum(abs(H["0700"][i] - K * (1 + allpass(GRID, c) ** 4))[sel] ** 2 / GRID[sel])
            fc[i] = scipy.optimize.minimize_scalar(cost, bounds=(fc[i] * 0.9, fc[i] * 1.1), method="bounded",
                                                   options={"xatol": 1e-3}).x
    error = [np.sqrt(np.mean(abs(H["0700"][i] - K * (1 + allpass(GRID, fc[i]) ** 4))[sel] ** 2)
                     / np.mean(abs(H["0700"][i])[sel] ** 2)) for i in range(5)]
    return H, K, fc, error


def rational(Y, A, order, iterations=40):
    """Y = P(A) / Q(A), real coefficients, Q(0) = 1 (Sanathanan-Koerner)."""
    V = np.vander(A, order + 1, increasing=True)
    w, best = np.ones(len(A)), None
    for _ in range(iterations):
        M = np.hstack([V, -Y[:, None] * V[:, 1:]]) * w[:, None]
        c = np.linalg.lstsq(np.vstack([M.real, M.imag]), np.concatenate([(Y * w).real, (Y * w).imag]), rcond=None)[0]
        p, q = c[:order + 1], np.concatenate([[1], c[order + 1:]])
        e = np.sqrt(np.mean(abs((V @ p) / (V @ q) - Y) ** 2) / np.mean(abs(Y) ** 2))
        if best is None or e < best[0]:
            best = (e, p, q)
        w = 1 / abs(V @ q)
    return best


def corner_roots(c):
    """A polynomial in the stage A, its roots in units of the corner (A = (1 - s) / (1 + s))."""
    a = np.roots(c[::-1])
    s = (1 - a) / (1 + a)
    s = s[(s.imag >= -1e-9) & (abs(s) < 60)]
    return " ".join(f"{z.real:+.2f}{z.imag:+.2f}j" for z in s[np.argsort(abs(s))])


def main():
    H, K, fc, error = load()
    print("point   corner    Res-off fit")
    for i in range(5):
        print(f"  {i + 1}   {fc[i]:7.1f} Hz   {100 * error[i]:.2f} %")
    again = raw("point-1-res-0700-again") / raw("bypass-sweep")
    print(f"point 1 read again with Res off: {100 * np.sqrt(np.mean(abs(again - H['0700'][0]) ** 2) / np.mean(abs(again) ** 2)):.2f} % from the first")
    sel = (GRID > 400) & (GRID < 15000)
    print("\nPoles and zeros in units of the corner (order 7 in the stage, 400 Hz - 15 kHz):")
    for res in ("1200", "0500"):
        for i in (3, 4, 2, 1):
            e, p, q = rational(H[res][i][sel] / K[sel], allpass(GRID[sel], fc[i]), 7)
            print(f"  Res {int(res[:2])}:00 point {i + 1} ({fc[i]:.0f} Hz) {100 * e:.2f} %\n      zeros {corner_roots(p)}\n      poles {corner_roots(q)}")
    if "--plot" in sys.argv:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        sel = (GRID > 50) & (GRID < 16000)
        fig, ax = plt.subplots(3, 2, figsize=(16, 13))
        for row, (res, label) in enumerate([("0700", "Res 7:00"), ("1200", "Res 12:00"), ("0500", "Res 5:00")]):
            for i in range(5):
                x = np.tan(np.pi * GRID / FS) / np.tan(np.pi * fc[i] / FS)
                ax[row, 0].semilogx(x[sel], 20 * np.log10(abs(H[res][i] / K)[sel]), label=f"point {i + 1}: corner {fc[i]:.0f} Hz")
                ax[row, 1].semilogx(GRID[sel], 20 * np.log10(abs(H[res][i])[sel]))
            ax[row, 0].set(xlim=(0.02, 60), ylim=(-45, 20), title=f"{label}: level against frequency / corner (the fixed tone removed)")
            ax[row, 1].set(ylim=(-45, 20), title=f"{label}: level against frequency (Hz)")
            ax[row, 0].legend()
            for a in ax[row]:
                a.grid(True, which="both", alpha=0.3)
        plt.tight_layout()
        (SESSION / "fit").mkdir(exist_ok=True)
        plt.savefig(SESSION / "fit" / "res4-slide.png", dpi=70)
        print(f"\n{(SESSION / 'fit' / 'res4-slide.png').relative_to(REPOSITORY)}")


if __name__ == "__main__":
    main()
