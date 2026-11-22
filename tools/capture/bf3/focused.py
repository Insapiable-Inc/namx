"""The BF-3 focused session (plans/bf-3-focused.json) against NAM-X's Flanger.

Both outputs recorded: channel 0 = OUTPUT A (what the model is fitted to), channel 2 = OUTPUT B.
The model is NAM-X's own processor (build-bf3/render_bf3), fed the played signal, its output put
through the bypass take's chain like the pedal's."""
import json, os, pathlib, subprocess, sys
import numpy as np, scipy.optimize, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import importlib.util
import scorecard, signals

# The CE-5 session's helpers (loading with slips undone, chains, sub-sample slides, the probe).
_spec = importlib.util.spec_from_file_location("ce5_focused", pathlib.Path(__file__).resolve().parents[1] / "ce5/focused.py")
ce5 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ce5)
load, chains, slide, best_slide, body, probe_freq = ce5.load, ce5.chains, ce5.slide, ce5.best_slide, ce5.body, ce5.probe_freq
response, band_map, no_probe = ce5.response, ce5.band_map, ce5.no_probe

RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
RENDER = os.environ.get("RENDER_BF3", "build-bf3/render_bf3")
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
SESSION = pathlib.Path("captures/boss/bf-3/2026-11-10-focused")
TYPES = {"Standard": 1, "Ultra": 2, "refit": 1}  # the refit is Standard since 2026-11-10
RATE_HZ = {"7:00": 0.0553, "9:00": 0.1228, "12:00": 0.326, "3:00": 2.52, "5:00": 10.0}
KNOB_AT = [0.0, 0.2, 0.5, 0.8, 1.0]
MANUAL_MS = [14.673, 9.065, 2.005, 0.701, 0.447]
DRY_LATENCY = 26  # samples, about: the pedal's dry path (0.55 ms), which the model leaves out


def manual_ms(position):
    return float(np.exp(np.interp(position, KNOB_AT, np.log(MANUAL_MS))))


def render(fed, meta, kind=None, manual=None, rate_hz=None, pad=0, binary=None):
    """The model's output for a take's settings, in the capture's scale. `kind`: Standard,
    Ultra or refit (default: the take's Mode); `manual`: the Manual position (0-1) in place of
    the plan's (the knob is set by hand); `pad`: samples of silence before the signal (places
    the sweep, which starts at its bottom)."""
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    args = [TYPES[kind or s["Mode"]], POSITION[s["Manual"]] if manual is None else manual, POSITION[s["Res"]],
            POSITION[s["Depth"]], rate_hz or RATE_HZ[s["Rate"]]]
    x = np.r_[np.zeros(pad), fed] * scale
    y = subprocess.run([binary or RENDER] + [f"{v:.6f}" for v in args], input=x.astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64)[pad:] / scale


def probe_z(out, ref, meta, band_hz):
    """The probe tone on `out` re its own on `ref` (complex, per sample, over the take's body
    less a quarter second at each end): the dry plus the flanged copy, D + W e^(-jwd)."""
    f0 = probe_freq(meta)
    low = scipy.signal.butter(4, band_hz, fs=RATE, output="sos")
    t = np.arange(len(out)) / RATE
    tone = lambda y: scipy.signal.sosfiltfilt(low, y * np.exp(-2j * np.pi * f0 * t))
    a, b = body(meta)
    a, b = a + RATE // 4, b - RATE // 4
    return (tone(out)[a:b] / tone(ref)[a:b]), a


def circle(z):
    """Centre and radius of the circle z runs on (least squares, Kasa)."""
    x, y = z.real[::16], z.imag[::16]
    A = np.c_[2 * x, 2 * y, np.ones(len(x))]
    cx, cy, c = np.linalg.lstsq(A, x * x + y * y, rcond=None)[0]
    return cx + 1j * cy, float(np.sqrt(c + cx * cx + cy * cy))


def probe_delay(out, ref, meta, band_hz=400.0):
    """The flanged copy's delay (ms, modulo the probe's period: one constant per take is
    unknown) from the probe: the angle of z about the circle's centre (the dry). Also the
    centre, the radius, and the sample the curve starts at."""
    z, at = probe_z(out, ref, meta, band_hz)
    centre, radius = circle(z)
    d = -np.unwrap(np.angle(z - centre)) / (2 * np.pi * probe_freq(meta)) * 1000
    return d, centre, radius, at


def sweep_rate(d, guess):
    """The sweep's rate (Hz) from a delay curve: the rate at which the curve, folded on it, is
    sharpest (the least spread within phase bins). The fundamental's peak alone is a few parts
    in a thousand off on a handful of sharp cycles, which is a visible drift over a take."""
    x = d[::96]
    t = np.arange(len(x)) * 96 / RATE
    bins = 100

    def spread(rate):
        idx = np.minimum(((rate * t) % 1.0 * bins).astype(int), bins - 1)
        count = np.bincount(idx, minlength=bins)
        mean = np.bincount(idx, x, minlength=bins) / np.maximum(count, 1)
        return float(np.mean((x - mean[idx]) ** 2))

    if guess > 1.0:  # many cycles in a take: the fundamental's peak is exact enough, and finer
        y = d[::48] - np.mean(d[::48])
        ts = np.arange(len(y)) / 1000.0
        power = lambda f: -abs(np.sum(y * np.exp(-2j * np.pi * f * ts)))
        coarse = guess * np.linspace(0.8, 1.2, 161)
        f0 = coarse[int(np.argmin([power(f) for f in coarse]))]
        return float(scipy.optimize.minimize_scalar(power, bounds=(f0 * 0.99, f0 * 1.01), method="bounded",
                                                    options={"xatol": 1e-7}).x)
    grid = guess * np.linspace(0.85, 1.15, 601)
    f0 = grid[int(np.argmin([spread(f) for f in grid]))]
    fine = f0 * np.linspace(0.9995, 1.0005, 201)
    return float(fine[int(np.argmin([spread(f) for f in fine]))])


def fold(d, rate, at=0):
    """Phase (0-1) of each sample of a delay curve at the sweep's rate, 0 at the curve's bottom."""
    t = (np.arange(len(d)) + at) / RATE
    ph = (rate * t) % 1.0
    # the bottom: the phase bin with the lowest mean
    bins = np.linspace(0, 1, 201)
    idx = np.digitize(ph, bins) - 1
    means = np.array([d[idx == k].mean() if np.any(idx == k) else np.inf for k in range(200)])
    return (ph - bins[int(np.argmin(means))]) % 1.0


UNDER = scipy.signal.butter(6, 5000, fs=RATE, output="sos")
DEPTH_AT_SWING = None


def cycle(d, rate, bins=400):
    """A delay curve folded on the sweep's rate: bin means, the bottom at bin 0, the minimum taken off."""
    ph = (rate * np.arange(len(d)) / RATE) % 1.0
    idx = np.minimum((ph * bins).astype(int), bins - 1)
    c = np.array([d[idx == k].mean() if np.any(idx == k) else np.nan for k in range(bins)])
    c = np.interp(np.arange(bins), np.flatnonzero(~np.isnan(c)), c[~np.isnan(c)])
    return np.roll(c, -int(np.argmin(c))) - c.min()


def bottom_time(d, rate):
    """Seconds (from the curve's first sample) of the sweep's first bottom, from the fundamental's phase
    of the folded curve (robust: uses the whole take)."""
    bins = 400
    ph = (rate * np.arange(len(d)) / RATE) % 1.0
    idx = np.minimum((ph * bins).astype(int), bins - 1)
    c = np.array([d[idx == k].mean() if np.any(idx == k) else np.nan for k in range(bins)])
    c = np.interp(np.arange(bins), np.flatnonzero(~np.isnan(c)), c[~np.isnan(c)])
    return float(np.argmin(c)) / bins / rate


def compare(session, name, h, kind="refit", binary=None, fit_knobs=True):
    """A Standard-mode take against the model: Output A whole (dry + flanged copy), the model
    placed on the pedal's own sweep (rate and phase from the probe) and, with `fit_knobs`, its
    Depth and Manual matched to what the pedal did (both are set by hand): Depth by the sweep's
    width, Manual by the waveform. Returns both waveforms over the take's body (the model slid
    by the fraction of a sample that matches best), the delay curves and the numbers."""
    meta, played, a, _, _ = load(session, name)
    s = meta["settings"]
    ref = scorecard.through(h, played)
    s0, s1 = body(meta)
    moving = s["Depth"] != "7:00"
    guess = RATE_HZ[s["Rate"]]
    band = 400 if guess < 1 else 1500
    depth, manual, rate, pad = POSITION[s["Depth"]], POSITION[s["Manual"]], guess, 0
    lo = lambda x: scipy.signal.sosfiltfilt(UNDER, no_probe(x, meta))[s0:s1]
    pedal = lo(a)
    r = dict(meta=meta, start=s0)

    res = POSITION[s["Res"]]

    def run(depth, manual, pad, kind=kind):
        y = _render_depth(played, meta, kind, manual, depth, rate, pad, binary, res)
        return np.roll(scorecard.through(h, y), DRY_LATENCY)

    if moving:
        d, centre, radius, at = probe_delay(a, ref, meta, band)
        rate = sweep_rate(d, guess)
        cp = cycle(d, rate)
        if fit_knobs:
            def width(dp):
                dm, *_ = probe_delay(run(dp, manual, 0), ref, meta, band)
                return np.ptp(cycle(dm, rate)) - np.ptp(cp)
            lo_d, hi_d = max(0.02, depth - 0.12), min(1.0, depth + 0.12)
            if width(lo_d) * width(hi_d) < 0:
                depth = scipy.optimize.brentq(width, lo_d, hi_d, xtol=5e-4)
        dm, *_ = probe_delay(run(depth, manual, 0), ref, meta, band)
        # The model's bottom against the pedal's: the pad that brings them together.
        shift = (bottom_time(dm, rate) - bottom_time(d, rate)) % (1 / rate)
        pad = int(round(shift * RATE))
        r.update(d=d, rate=rate, at=at)
    if fit_knobs:
        def err(m):
            return best_slide(lo(run(depth, m, pad)), pedal, 3.0)[1]
        grid = np.clip(manual + np.linspace(-0.04, 0.04, 9), 0, 1)
        m0 = grid[int(np.argmin([err(m) for m in grid]))]
        manual = scipy.optimize.minimize_scalar(err, bounds=(max(0, m0 - 0.012), min(1, m0 + 0.012)), method="bounded",
                                                options={"xatol": 3e-4}).x
    if fit_knobs and 0.0 < res < 1.0:  # Res between its stops: set by hand too
        def err_res(value):
            nonlocal res
            res = value
            return best_slide(lo(run(depth, manual, pad)), pedal, 3.0)[1]
        grid = np.clip(POSITION[s["Res"]] + np.linspace(-0.08, 0.08, 9), 0, 1)
        r0 = grid[int(np.argmin([err_res(v) for v in grid]))]
        res = scipy.optimize.minimize_scalar(err_res, bounds=(r0 - 0.02, r0 + 0.02), method="bounded",
                                             options={"xatol": 2e-3}).x
    y = run(depth, manual, pad)
    r["slide"], r["residual"], r["gain"], _, model = best_slide(lo(y), pedal, 3.0)
    r["res"] = res
    if moving:
        r["dm"], *_ = probe_delay(slide(y, r["slide"]), ref, meta, band)
    r.update(pedal=pedal, model=model, depth=depth, manual=manual, pad=pad)
    return r


def _render_depth(fed, meta, kind, manual, depth, rate, pad, binary, res=None):
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    args = [TYPES[kind or s["Mode"]], manual, POSITION[s["Res"]] if res is None else res, depth, rate]
    x = np.r_[np.zeros(pad), fed] * scale
    y = subprocess.run([binary or RENDER] + [f"{v:.6f}" for v in args], input=x.astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64)[pad:] / scale


STANDARD_TAKES = ["static-chords-m12", "static-funk", "res-0300-static-chords-m12", "res-0500-static-chords-m12",
                  "manual-0700-static-chords-m12", "manual-0300-static-chords-m12", "1200-chords-m12", "1200-chords",
                  "1200-funk", "1200-funk-repeat", "1200-lead-m12", "res-0300-chords-m12", "res-0300-funk",
                  "res-0500-chords-m12", "depth-0500-chords-m12", "rate-0300-depth-0500-chords-m12",
                  "rate-0300-depth-0500-lead-m12", "rate-0500-depth-0500-chords-m12"]
BASE = "build-bf3/render_bf3_base"  # the Flanger as it was before this session


def plot(session, name, runs):
    """One take, Output A whole: the sweep (pedal and models) and its error, the level per third
    octave and 50 ms (model - pedal) for each model, the waveforms at two moments."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    first = next(iter(runs.values()))
    moving = "d" in first
    rows = (2 if moving else 0) + len(runs) + 2
    fig = plt.figure(figsize=(16, 3.0 * rows))
    grid = fig.add_gridspec(rows, 2)
    k0 = 0
    if moving:
        t = np.arange(len(first["d"])) / RATE
        ax = fig.add_subplot(grid[0, :])
        ax.plot(t, first["d"] - first["d"].min(), lw=0.7, color="k", label="pedal")
        for label, r in runs.items():
            ax.plot(t, r["dm"] - r["dm"].min(), lw=0.7, label=label)
        ax.set_title(f"{name} (Output A): the flanged copy's delay from the probe, ms above its minimum")
        ax.legend(loc="upper right", fontsize=8)
        ax2 = fig.add_subplot(grid[1, :])
        for label, r in runs.items():
            e = (r["dm"] - r["d"]) * 1000
            e = e - 125 * np.round(np.median(e) / 125)
            ax2.plot(t, e, lw=0.7, label=f"{label}: {np.std(e):.0f} µs rms")
        ax2.set_title("model − pedal delay, µs")
        ax2.grid(alpha=0.3)
        ax2.legend(loc="upper right", fontsize=8)
        if first["rate"] > 1:
            ax.set_xlim(2, 2 + 6 / first["rate"])
            ax2.set_xlim(2, 2 + 6 / first["rate"])
        k0 = 2
    hop = 2400
    P = band_map(first["pedal"], hop)
    tt = np.arange(P.shape[1]) * hop / RATE
    for k, (label, r) in enumerate(runs.items()):
        M = band_map(r["model"], hop)
        d = M - P
        d[(P < P.max() - 50) & (M < P.max() - 50)] = np.nan
        ax = fig.add_subplot(grid[k0 + k, :])
        im = ax.pcolormesh(tt, np.arange(len(scorecard.BANDS)), d, vmin=-6, vmax=6, cmap="RdBu_r", shading="auto")
        ax.set_yticks(range(0, len(scorecard.BANDS), 3))
        ax.set_yticklabels(scorecard.BANDS[::3])
        ax.set_title(f"{label} − pedal, dB per third octave and 50 ms (red: the model louder) — waveform residual under 5 kHz "
                     f"{r['residual']:.1f} dB")
        plt.colorbar(im, ax=ax)
    pedal = first["pedal"]
    env = np.abs(scipy.signal.hilbert(pedal))
    loud = int(np.argmax(env[RATE:len(env) // 2])) + RATE
    for k, spot in enumerate((loud, loud + 3 * RATE)):
        spot = min(spot, len(pedal) - 1500)
        seg = slice(spot - 200, spot + 1000)
        ax = fig.add_subplot(grid[k0 + len(runs), k])
        ax.plot(pedal[seg], lw=1.0, color="k", label="pedal")
        for label, r in runs.items():
            ax.plot(r["model"][seg], lw=0.8, label=label, alpha=0.85)
        ax.set_title(f"waveforms at {spot / RATE:.2f} s into the take (25 ms)")
        ax.legend(fontsize=7)
        ax = fig.add_subplot(grid[k0 + len(runs) + 1, k])
        for label, r in runs.items():
            ax.plot((r["model"] - pedal)[seg], lw=0.8, label=f"{label} − pedal", alpha=0.85)
        ax.set_ylim(*np.array([-1, 1]) * np.abs(pedal[seg]).max())
        ax.set_title("the difference, on the scale above")
        ax.legend(fontsize=7)
    plt.tight_layout()
    out = pathlib.Path(session) / "fit"
    out.mkdir(exist_ok=True)
    plt.savefig(out / f"focused-{name}.png", dpi=58)
    plt.close(fig)


def main():
    """bf3/focused.py [TAKE...] [--plot]: the Standard-mode guitar takes, Output A whole, against the
    Flanger as it was (Standard, build-bf3/render_bf3_base) and the candidate (the refit's tone
    and loop on the source's sweep, build-bf3/render_bf3), each with Manual, Depth and Res matched
    to what the pedal did. --plot: SESSION/fit/focused-TAKE.png."""
    names = [a for a in sys.argv[1:] if not a.startswith("--")] or STANDARD_TAKES
    h = chains(SESSION)[0]
    print(f"{'take':32s} |  was: resid<5k  bands  sweep |  now: resid<5k  bands  sweep   (dB, dB rms, µs rms)   fitted Depth Manual Res")
    for name in names:
        runs = {"was": compare(SESSION, name, h, "Standard", BASE), "now": compare(SESSION, name, h, "refit")}
        row = f"{name:32s}"
        for r in runs.values():
            P, M = band_map(r["pedal"], 2400), band_map(r["model"], 2400)
            seen = (P > P.max() - 45) | (M > P.max() - 45)
            e = (r["dm"] - r["d"]) * 1000 if "dm" in r else np.zeros(2)
            r["bands"] = float(np.sqrt(np.mean((M - P)[seen] ** 2)))
            row += f" | {r['residual']:13.1f} {r['bands']:6.2f} {np.std(e):6.1f}"
        now = runs["now"]
        print(row + f"     {now['depth']:.3f} {now['manual']:.4f} {now['res']:.3f}", flush=True)
        if "--plot" in sys.argv:
            plot(SESSION, name, runs)


if __name__ == "__main__":
    main()
