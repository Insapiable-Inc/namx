"""The PH-3's Stage modes as chains of identical first-order all-pass stages (found 2026-11-10 on
the tone takes): the fit of one sweeping corner per moment of the cycle, per take.

  out = K(f) · (1 + ρ · A(f; fc)^n · Π A(f; parked))        4, 8, 10 Stage
  out = K(f) · (1 + ρ · A(f; fc)^6)^2                        12 Stage
  A: a first-order all-pass at the pedal's 44.1 kHz, 90 degrees at its corner

  ph3/stages.py            every sweeping tone take with Res off: the fit's error, the corner's
                           range, the mix, the tone K; saves fit/stages.npz and a plot of the
                           corner over the cycle"""
import pathlib, sys
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused as F
from focused import RATE, SESSION, scorecard, level

FS = 44100.0
STRUCTURE = {  # mode: (sweeping stages, parked corners Hz, run twice)
    "4 Stage": (4, [], False),
    "8 Stage": (6, [4345.0, 4345.0], False),
    "10 Stage": (6, [7650.0] * 4, False),
    "12 Stage": (6, [], True),
}


def allpass(f, fc):
    t = np.tan(np.pi * np.minimum(fc, 0.499 * FS) / FS)
    a = (t - 1) / (t + 1)
    z = np.exp(-2j * np.pi * f / FS)
    return (a + z) / (1 + a * z)


def wet(mode, f, fc):
    """The all-pass chain's response at corner fc (4/8/10 Stage: the wet; 12 Stage: one pass's)."""
    n, parked, _ = STRUCTURE[mode]
    A = allpass(f, fc) ** n
    for p in parked:
        A = A * allpass(f, p)
    return A


def response(mode, f, fc, rho=1.0, k=0.0):
    A = wet(mode, f, fc)
    one = 1 + rho * A
    return one * one if STRUCTURE[mode][2] else one


def cycle(name, h, bins=240):
    meta, played, a = F.load(SESSION, name)
    ref = scorecard.through(h, played)
    guess = F.RATE_HZ[meta["settings"]["Rate"]]
    z, at = F.tone_responses(a, ref, meta, 30.0 if guess < 2 else 80.0)
    rate = F.sweep_rate(level(z[3:8]), guess)
    return meta, np.array(meta["signal"]["freq"], float), F.folded(z, rate, at, bins), rate


def fit(mode, f, Z, model=response, passes=4):
    """One corner per phase bin; the tone K per tone and the mix ρ for the take."""
    bins = Z.shape[1]
    K = 0.8 * np.exp(-2j * np.pi * f * 0.000544)
    rho, corner = 1.0, np.zeros(bins)
    for it in range(passes):
        prev = None
        for b in range(bins):
            def err(p):
                e = K * model(mode, f, np.exp(p[0]), rho) - Z[:, b]
                return np.r_[e.real, e.imag]
            starts = ([prev] if prev is not None else []) + ([np.log(corner[b])] if it else []) + \
                     [np.log(s) for s in (40, 100, 250, 600, 1500, 4000, 9000)]
            best = min((scipy.optimize.least_squares(err, [s], method="lm") for s in starts), key=lambda r: r.cost)
            corner[b], prev = np.exp(best.x[0]), best.x[0]

        def total(r):
            M = np.array([model(mode, f, c, r) for c in corner]).T
            Kf = np.sum(Z * np.conj(M), axis=1) / np.sum(abs(M) ** 2, axis=1)
            return np.sum(abs(Kf[:, None] * M - Z) ** 2), M, Kf
        rho = scipy.optimize.minimize_scalar(lambda r: total(r)[0], bounds=(0.6, 1.6), method="bounded").x
        _, M, K = total(rho)
    E = K[:, None] * M - Z
    return dict(corner=corner, K=K, rho=rho, complex=float(np.sqrt(np.mean(abs(E) ** 2) / np.mean(abs(Z) ** 2))),
                level=float(np.sqrt(np.mean((level(K[:, None] * M) - level(Z)) ** 2))))


TAKES = ["4stage-1200-tones", "4stage-tones", "4stage-rate-0900-tones", "4stage-rate-0300-tones", "4stage-rate-0500-tones",
         "8stage-tones", "10stage-tones", "12stage-tones"]


def main():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    h = F.chain(SESSION)
    out = {}
    fig, ax = plt.subplots(figsize=(13, 7))
    for name in TAKES:
        meta, f, Z, rate = cycle(name, h)
        r = fit(meta["settings"]["Mode"], f, Z)
        c = r["corner"]
        c = np.roll(c, -int(np.argmin(c)))
        print(f"{name:26s} rate {rate:7.4f} Hz  complex error {r['complex']:.4f}  level {r['level']:.2f} dB  mix {r['rho']:.3f}  corner {c.min():6.0f}-{c.max():6.0f} Hz"
              f"  |K| dB {np.round(20 * np.log10(abs(r['K'])), 1)}", flush=True)
        out[name] = np.r_[rate, r["rho"], c]
        out[name + "/K"] = r["K"]
        ax.semilogy(np.arange(len(c)) / len(c), c, label=name)
    ax.grid(alpha=0.3, which="both")
    ax.legend(fontsize=8)
    ax.set_title("the sweeping stages' corner over the cycle (its bottom at 0), Hz")
    (SESSION / "fit").mkdir(exist_ok=True)
    plt.savefig(SESSION / "fit" / "stages-corner.png", dpi=60)
    np.savez(SESSION / "fit" / "stages.npz", **out)


if __name__ == "__main__":
    main()
