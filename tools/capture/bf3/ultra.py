"""The BF-3's ULTRA mode: what it is, and NAM-X's model of it against the pedal.

  bf3/ultra.py structure   October's ULTRA sweeps (Res minimum, Manual 12:00 and 7:00): three free
                           taps against two flangers in series
  bf3/ultra.py loops       October's quiet Res sweeps (Manual 7:00): where the Res loop sits
  bf3/ultra.py guitar      the focused session's ULTRA takes against the model (build-bf3/render_bf3):
                           the delay standing still (Manual and Res matched), then the sweep moving
                           (its phase, Depth and Manual found on the band levels, then the waveform)

Run from the repository root."""
import pathlib, sys
import numpy as np, scipy.optimize, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused as F
from focused import RATE, SESSION, scorecard, load, body, best_slide, band_map, no_probe

SR = 48000.0
OCTOBER = pathlib.Path("captures/boss/bf-3/2026-10-27-ultra")
QUIET = pathlib.Path("captures/boss/bf-3/2026-10-27-res-quiet")


def biquad(kind, hz, q, f):
    w = 2 * np.pi * hz / SR
    c, alpha = np.cos(w), np.sin(w) / (2 * q)
    b = [(1 - c) / 2, 1 - c, (1 - c) / 2] if kind == "lp" else [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    z = np.exp(-2j * np.pi * f / SR)
    return (b[0] + b[1] * z + b[2] * z * z) / (1 + alpha - 2 * c * z + (1 - alpha) * z * z)


def tone(f):
    """STANDARD's wet tone (Bf3Flanger.h, REFIT_TONE and REFIT_TONE_DB)."""
    h = lambda x: biquad("hp", 148.322387, 0.544603, x) * biquad("lp", 7899.7738, 0.405237, x) * biquad("lp", 9995.4989, 1.013955, x)
    return h(f) / abs(h(np.array([1000.0]))[0]) * 10 ** (-0.24726 / 20)


def measured(session, name, bypass="bypass-sweep", taps=4096):
    """A sweep take's response re the bypass chain, through its impulse response's first `taps` samples."""
    _, played, a, _, _ = load(session, bypass)
    h = scorecard.chain_filter(played, a)
    meta, played, a, _, _ = load(session, name)
    s0, n = meta["signal_start"], 1 << 20
    f = np.fft.rfftfreq(n, 1 / RATE)
    ok = (f > 25) & (f < 20000)
    H = np.where(ok, np.fft.rfft(a[s0:], n) / np.where(ok, np.fft.rfft(scorecard.through(h, played)[s0:], n), 1), 0)
    ir = np.fft.irfft(H, n)
    seg = np.r_[ir[:taps - 96], ir[-96:]]
    fade = taps // 16
    w = np.ones(taps)
    w[taps - 96 - fade:taps - 96] = np.hanning(2 * fade)[fade:]
    return np.fft.rfftfreq(taps, 1 / RATE), np.fft.rfft(seg * w)


def stage(f, dry, wet, t, r=0.0):
    """One flanger: dry + wet * T z^-t / (1 - r T z^-t)."""
    z = np.exp(-2j * np.pi * f * t / 1000)
    return dry + wet * tone(f) * z / (1 - r * tone(f) * z)


def fit(model, H, f, p0, scale):
    sel = (f > 80) & (f < 8000)
    err = lambda p: np.r_[(model(p, f[sel]) - H[sel]).real, (model(p, f[sel]) - H[sel]).imag]
    r = scipy.optimize.least_squares(err, p0, x_scale=scale)
    return r.x, 10 * np.log10(2 * r.cost / len(f[sel]) / np.mean(abs(H[sel]) ** 2))


def structure():
    lead = lambda f, t0: np.exp(-2j * np.pi * f * t0 / 1000)
    taps = lambda p, f: lead(f, p[1]) * (p[0] + tone(f) * sum(p[2 + k] * np.exp(-2j * np.pi * f * p[5 + k] / 1000) for k in range(3)))
    series = lambda p, f: lead(f, p[4]) * stage(f, p[0], p[1], p[5]) * stage(f, p[2], p[3], p[6])
    for name, d in (("manual-1200", 2.0), ("manual-0700", 14.6)):
        f, H = measured(OCTOBER, name)
        a, ea = min((fit(taps, H, f, [0.89, t0, 0.87, 0.86, 0.83, d / 2, d, 1.5 * d], [1, .01, 1, 1, 1, .01, .01, .01]) for t0 in (0.5, 0.55, 0.6)), key=lambda r: r[1])
        b, eb = min((fit(series, H, f, [0.9, 1.0, 0.9, 1.0, t0, d / 2, d], [1, 1, 1, 1, .01, .01, .01]) for t0 in (0.5, 0.55, 0.6)), key=lambda r: r[1])
        print(f"{name}: three free taps (8 numbers) {ea:.1f} dB: at {np.round(a[5:8], 4)} ms, the third {1000 * (a[7] - 3 * a[5]):+.0f} µs late re 3 x the first"
              f" | two flangers in series (7 numbers) {eb:.1f} dB: delays {np.round(b[5:7], 4)} ms")


def loops():
    f, H0 = measured(QUIET, "manual-0700-24", "bypass-sweep-24", 16384)
    kinds = {
        "a loop in each": lambda p, f: stage(f, p[0], p[1], p[5], p[7]) * stage(f, p[2], p[3], p[6], p[7]),
        "on the d stage only": lambda p, f: stage(f, p[0], p[1], p[5]) * stage(f, p[2], p[3], p[6], p[7]),
        "on the d/2 stage only": lambda p, f: stage(f, p[0], p[1], p[5], p[7]) * stage(f, p[2], p[3], p[6]),
        "one round both": lambda p, f: (lambda fwd: fwd / (1 - p[7] * (fwd - p[0] * p[2])))(stage(f, p[0], p[1], p[5]) * stage(f, p[2], p[3], p[6])),
    }
    wrap = lambda kind: (lambda p, f: np.exp(-2j * np.pi * f * p[4] / 1000) * kinds[kind](p, f))
    scale = [1, 1, 1, 1, .01, .01, .01, .3]
    base, e0 = fit(wrap("a loop in each"), H0, f, [0.92, 1.0, 0.92, 1.0, 0.55, 7.29, 14.6, 0.0], scale)
    print(f"Res minimum: {e0:.1f} dB")
    for name in ("res-0900", "res-1200", "res-0300", "res-0500"):
        _, H = measured(QUIET, f"{name}-manual-0700-24", "bypass-sweep-24", 16384)
        row = f"{name}:"
        for kind in kinds:
            p, e = min((fit(wrap(kind), H, f, list(base[:7]) + [r0], scale) for r0 in (0.2, 0.5, 0.8)), key=lambda r: r[1])
            row += f"  {kind} {e:.1f} dB (gain {p[7]:.3f})"
        print(row, flush=True)


def guitar():
    h = F.chains(SESSION)[0]
    for name in ("ultra-static-chords-m12", "ultra-res-0300-static-chords-m12"):
        r = F.compare(SESSION, name, h, "Ultra")
        P, M = band_map(r["pedal"], 2400), band_map(r["model"], 2400)
        seen = (P > P.max() - 45) | (M > P.max() - 45)
        print(f"{name:34s} residual<5k {r['residual']:6.1f} dB  bands {np.sqrt(np.mean((M - P)[seen] ** 2)):.2f} dB rms  (Manual {r['manual']:.4f}, Res {r['res']:.3f})", flush=True)
    # The sweep moving: the probe's path isn't a circle here, so the rate comes from its level's
    # cycle and the sweep's phase, Depth and Manual from the guitar (the first 11 s).
    meta, played, a, _, _ = load(SESSION, "ultra-probe")
    z, _ = F.probe_z(a, scorecard.through(h, played), meta, 400.0)
    rate = F.sweep_rate(np.abs(z), 0.326)
    meta, played, a, _, _ = load(SESSION, "ultra-chords-m12")
    s0, _ = body(meta)
    n = s0 + 11 * RATE
    lo = lambda x: scipy.signal.sosfiltfilt(F.UNDER, no_probe(x, meta))
    pedal = lo(a[:n])[s0:]
    P = band_map(pedal, 1200)

    def run(pad, depth, manual):
        y = F._render_depth(played[:n], meta, "Ultra", manual, depth, rate, int(pad), None)
        return lo(np.roll(scorecard.through(h, y), F.DRY_LATENCY))[s0:]

    def bands(pad, depth, manual):
        M = band_map(run(pad, depth, manual), 1200)
        return float(np.sqrt(np.mean((M - P)[P > P.max() - 40] ** 2)))

    cycle = RATE / rate
    e, pad, depth = min((bands(k * cycle / 32, d, 0.5), k * cycle / 32, d) for d in (0.44, 0.48, 0.52) for k in range(32))
    simplex = lambda p, steps: [list(p)] + [[p[i] + (steps[i] if i == j else 0) for i in range(3)] for j in range(3)]
    r = scipy.optimize.minimize(lambda p: bands(p[0] % cycle, p[1], p[2]), [pad, depth, 0.5], method="Nelder-Mead",
                                options={"initial_simplex": simplex([pad, depth, 0.5], [cycle / 64, 0.02, 0.02]), "xatol": 5e-4, "fatol": 1e-3, "maxiter": 80})
    start = [r.x[0] % cycle, r.x[1], r.x[2]]
    r = scipy.optimize.minimize(lambda p: best_slide(run(*p), pedal, 3.0)[1], start, method="Nelder-Mead",
                                options={"initial_simplex": simplex(start, [96, 0.004, 0.004]), "xatol": 3e-4, "fatol": 0.02, "maxiter": 90})
    print(f"ultra-chords-m12, the sweep moving (rate {rate:.4f} Hz, first 11 s): residual<5k {r.fun:.1f} dB, bands {bands(*r.x):.2f} dB rms "
          f"(Depth {r.x[1]:.4f}, Manual {r.x[2]:.4f})")


if __name__ == "__main__":
    {"structure": structure, "loops": loops, "guitar": guitar}[sys.argv[1] if len(sys.argv) > 1 else "guitar"]()
