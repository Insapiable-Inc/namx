"""The CE-5's chorus tone refitted to the two-output session's standing sweep (OUTPUT A, the
chorus alone, Depth 7:00, E.Level 5:00): the low end (after the wet clip: a high-pass and the
bump over it) on the complex response, the top (before it: the wall at 10 kHz) on the level,
with the model's own losses there (its delay line's interpolation) counted in.

  ce5/tone.py     prints the sections for Ce5Chorus.h and the fit's error

Run from the repository root with build-ce5/render_ce5 built."""
import sys, pathlib
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from focused import *

SR = 48000.0


def biquad(kind, hz, q, gain_db, f):
    """An RBJ section's complex response at f (as BiquadChain.h designs it)."""
    w = 2 * np.pi * hz / SR
    c, alpha, a = np.cos(w), np.sin(w) / (2 * q), 10 ** (gain_db / 40)
    if kind == "lp":
        b, den = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "hp":
        b, den = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "peak":
        b, den = [1 + alpha * a, -2 * c, 1 - alpha * a], [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif kind == "hp1":
        k = np.tan(w / 2)
        b, den = [1, -1, 0], [1 + k, k - 1, 0]
    z = np.exp(-2j * np.pi * f / SR)
    return (b[0] + b[1] * z + b[2] * z * z) / (den[0] + den[1] * z + den[2] * z * z)


def chain(sections, f):
    h = np.ones(len(f), complex)
    for s in sections:
        h = h * biquad(*s, f)
    one = np.ones(1, complex)
    for s in sections:
        one = one * biquad(*s, np.array([1000.0]))
    return h / abs(one[0])


OLD = [("hp", 133.2, 0.714, 0), ("peak", 75.7, 0.879, 8.0), ("peak", 12000, 0.812, -8.0), ("lp", 16000, 0.4, 0),
       ("lp", 16000, 0.4, 0), ("lp", 9642.9, 1.12, 0), ("lp", 12021.1, 2.563, 0)]


def measured():
    """Pedal and model responses of the chorus (re the bypass chain) from the standing sweep,
    smoothed over a twelfth of an octave on a log grid."""
    hA, _ = chains(STEREO)
    meta, played, a, _, _ = load(STEREO, "static-sweep")
    s0 = meta["signal_start"]
    n = 1 << 20
    ref = np.fft.rfft(scorecard.through(hA, played)[s0:], n)
    wet, _ = render(played, meta)
    hp, hm = np.fft.rfft(a[s0:], n) / ref, np.fft.rfft(scorecard.through(hA, wet)[s0:], n) / ref
    f = np.fft.rfftfreq(n, 1 / RATE)
    grid = 30 * 2 ** (np.arange(0, 8.95 * 24) / 24)
    # Pedal re model, the pedal's extra delay (its dry path's latency, which the model leaves
    # out) taken off; smoothed as level (power) and phase (of the mean) per band.
    ratio = hp / hm * np.exp(2j * np.pi * f * 47.4 / RATE)
    out = []
    for q in grid:
        band = (f > q / 2 ** (1 / 48)) & (f < q * 2 ** (1 / 48))
        out.append(np.sqrt(np.mean(abs(ratio[band]) ** 2)) * np.exp(1j * np.angle(np.mean(ratio[band]))))
    return grid, np.array(out)


def main():
    f, ratio = measured()
    at1k = np.argmin(abs(f - 1000))
    # What the tone has to be for the model as a whole to match: the old tone times pedal / model.
    target = chain(OLD, f) * ratio
    target = target / abs(target[at1k])
    # Low end, complex (a delay left free): 30 Hz - 1.5 kHz.
    low = (f < 1500)

    SHAPES = {  # name: (sections from parameters (the last one: the delay left free, ms), start, low, high)
        "hp + hp1": (lambda p: [("hp", p[0], p[1], 0), ("hp1", p[2], 0.7, 0)],
                     [80, 1.0, 60, 0], [20, 0.3, 5, -1], [400, 5, 400, 1]),
        "hp + hp": (lambda p: [("hp", p[0], p[1], 0), ("hp", p[2], p[3], 0)],
                    [70, 1.0, 70, 0.6, 0], [20, 0.3, 10, 0.3, -1], [400, 5, 400, 5, 1]),
        "hp + hp1 + peak": (lambda p: [("hp", p[0], p[1], 0), ("hp1", p[2], 0.7, 0), ("peak", p[3], p[4], p[5])],
                            [80, 1.0, 60, 150, 1.0, 1.0, 0], [20, 0.3, 5, 40, 0.2, -6, -1], [400, 5, 400, 600, 5, 6, 1]),
    }
    old_top = chain(OLD[2:], f)
    keep, target = target, target / old_top
    best_low, best = None, np.inf
    for name, (sections, p0, lo, hi) in SHAPES.items():
        def low_error(p):
            h = chain(sections(p), f[low]) * np.exp(-2j * np.pi * f[low] * p[-1] * 1e-3)
            e = np.log(h / target[low])
            return np.r_[e.real * 8.686, e.imag * 8.686]
        r = scipy.optimize.least_squares(low_error, p0, bounds=(lo, hi))
        error = np.sqrt(np.mean(r.fun ** 2))
        print(f"low end, {name}: rms error {error:.2f} dB (level and phase as dB) -> {np.round(r.x, 3)}")
        if name != "hp + hp1 + peak" and error < best:
            best, best_low = error, sections(r.x)
    target = keep
    # Top, level only: 1.5 - 15 kHz, the weight falling above 12.5 kHz.
    top = (f > 1500) & (f < 15000)
    want = 20 * np.log10(abs(target[top] / chain(best_low, f)[top]))
    weight = np.where(f[top] < 12500, 1.0, 0.3)

    def top_sections(p):
        return [("lp", p[0], p[1], 0), ("lp", p[2], p[3], 0), ("lp", p[4], p[5], 0), ("lp", p[6], p[7], 0),
                ("peak", p[8], p[9], p[10])]

    def top_error(p):
        return (20 * np.log10(abs(chain(top_sections(p), f[top]))) - want) * weight

    p0 = [10500, 0.6, 11000, 1.0, 11500, 2.0, 12000, 5.0, 9000, 2.0, 1.0]
    r = scipy.optimize.least_squares(top_error, p0, bounds=([6000, 0.3] * 4 + [3000, 0.3, -6], [20000, 12] * 4 + [14000, 8, 6]))
    print(f"top: rms error {np.sqrt(np.mean(r.fun ** 2)):.2f} dB -> {np.round(r.x, 2)}")
    new = best_low + top_sections(r.x)
    total = chain(new, f)
    print("\n   f     target dB   new    old   (the tone alone, re 1 kHz)")
    for q in [40, 50, 63, 80, 100, 125, 160, 200, 315, 500, 1000, 2000, 4000, 5000, 6300, 8000, 9000, 10000, 11000, 12500, 14000]:
        i = np.argmin(abs(f - q))
        print(f"{q:6d} {20*np.log10(abs(target[i])):+8.2f} {20*np.log10(abs(total[i])):+7.2f} {20*np.log10(abs(chain(OLD, f)[i])):+7.2f}")
    print("\nsections:")
    for s in new:
        print(f"    {{FilterKind::{ {'hp': 'HIGH_PASS', 'lp': 'LOW_PASS', 'peak': 'PEAK', 'hp1': 'HIGH_PASS_1'}[s[0]] }, {s[1]:.1f}f, {s[2]:.3f}f, {s[3]:.2f}f}},")


if __name__ == "__main__":
    main()
