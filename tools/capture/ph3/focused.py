"""The PH-3 focused session (plans/ph-3-focused.json) against NAM-X's Phaser.

Tone takes: nine sines together; each one's level and phase on the output, against its own on
the played signal through the bypass chain, is the phaser's response at that frequency at
every moment. Folded on the sweep's rate that is the whole cycle at nine frequencies, for the
pedal and for the model (build-ph3/render_ph3) alike."""
import importlib.util, json, os, pathlib, subprocess, sys
import numpy as np, scipy.optimize, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard, signals

_spec = importlib.util.spec_from_file_location("ce5_focused", pathlib.Path(__file__).resolve().parents[1] / "ce5/focused.py")
ce5 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ce5)
slide, best_slide, body, band_map, no_probe = ce5.slide, ce5.best_slide, ce5.body, ce5.band_map, ce5.no_probe

RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
RENDER = os.environ.get("RENDER_PH3", "build-ph3/render_ph3")
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
SESSION = pathlib.Path("captures/boss/ph-3/2026-11-10-focused")
TYPES = {"4 Stage": 1, "8 Stage": 2, "10 Stage": 3, "12 Stage": 4, "Rise": 5, "Fall": 6, "Step": 7}
RATE_HZ = {"9:00": 0.225, "12:00": 0.654, "3:00": 4.64, "5:00": 21.5}
LATENCY = 26  # samples, about: the pedal's own latency (0.544 ms), which the model leaves out


def load(session, name):
    """A take as recorded: its sidecar, the played signal and the pedal's output aligned to it
    (its offset before the marker removed)."""
    session = pathlib.Path(session)
    meta = json.loads((session / f"{name}.json").read_text())
    recorded, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(session / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(recorded) - lat) - 16
    return meta, played[:n], recorded[lat:lat + n, 0] - recorded[:meta["marker_start"], 0].mean()


def chain(session):
    _, played, a = load(session, "bypass-sweep")
    return scorecard.chain_filter(played, a)


def render(fed, meta, rate_hz, pad=0, depth=None, res=None, binary=None):
    """The model's output for a take's settings, in the capture's scale; `pad` samples of
    silence before the signal place its sweep."""
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    args = [TYPES[s["Mode"]], rate_hz, POSITION[s["Depth"]] if depth is None else depth,
            POSITION[s["Res"]] if res is None else res]
    x = np.r_[np.zeros(pad), fed] * scale
    y = subprocess.run([binary or RENDER] + [f"{v:.6f}" for v in args], input=x.astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64)[pad:] / scale


STEP = 48  # the tone responses are kept every millisecond


def tone_responses(out, ref, meta, band_hz=30.0):
    """The response at each of the take's tones over its body (a quarter second off each end):
    complex, [tone, millisecond]. Also the sample the first column sits at."""
    freqs = meta["signal"]["freq"]
    low = scipy.signal.butter(4, band_hz, fs=RATE, output="sos")
    t = np.arange(len(out)) / RATE
    a, b = body(meta)
    a, b = a + RATE // 4, b - RATE // 4
    z = []
    for f0 in freqs:
        carrier = np.exp(-2j * np.pi * f0 * t)
        z.append((scipy.signal.sosfiltfilt(low, out * carrier)[a:b:STEP] / scipy.signal.sosfiltfilt(low, ref * carrier)[a:b:STEP]))
    return np.array(z), a


def sweep_rate(x, guess):
    """The sweep's rate from a real curve sampled every millisecond: the rate at which the curve,
    folded on it, is sharpest; refined on a finer grid."""
    t = np.arange(x.shape[-1]) * STEP / RATE
    bins = 100

    def spread(rate):
        idx = np.minimum(((rate * t) % 1.0 * bins).astype(int), bins - 1)
        count = np.maximum(np.bincount(idx, minlength=bins), 1)
        total = 0.0
        for row in np.atleast_2d(x):
            mean = np.bincount(idx, row, minlength=bins) / count
            total += np.mean((row - mean[idx]) ** 2)
        return total

    grid = guess * np.linspace(0.8, 1.2, 801)
    f0 = grid[int(np.argmin([spread(f) for f in grid]))]
    fine = f0 * np.linspace(0.9995, 1.0005, 201)
    return float(fine[int(np.argmin([spread(f) for f in fine]))])


def folded(z, rate, at, bins=240):
    """[tone, phase bin] means of z over the sweep's cycle, phase 0 at the played signal's sample 0."""
    ph = (rate * (np.arange(z.shape[1]) * STEP + at) / RATE) % 1.0
    idx = np.minimum((ph * bins).astype(int), bins - 1)
    count = np.maximum(np.bincount(idx, minlength=bins), 1)
    return np.array([(np.bincount(idx, row.real, minlength=bins) + 1j * np.bincount(idx, row.imag, minlength=bins)) / count
                     for row in z])


FLOOR_DB = -30.0


def level(z):
    return np.maximum(20 * np.log10(np.abs(z) + 1e-9), FLOOR_DB)


def cyclic_shift(model, pedal):
    """The cyclic shift (bins) of the model's folded levels that matches the pedal's best, over all tones."""
    a, b = level(pedal), level(model)
    corr = np.sum(np.fft.ifft(np.fft.fft(a - a.mean(1, keepdims=True), axis=1) * np.conj(np.fft.fft(b - b.mean(1, keepdims=True), axis=1)), axis=1).real, axis=0)
    return int(np.argmax(corr))


TONE_TAKES = ["4stage-1200-tones", "4stage-tones", "4stage-rate-0900-tones", "8stage-tones", "10stage-tones",
              "12stage-tones", "4stage-res-tones", "8stage-res-tones", "10stage-res-tones", "12stage-res-tones"]
BASE = "build-ph3/render_ph3_base"  # the Phaser as it was before this session
PEDAL_LATENCY = 26.78  # samples (0.558 ms, from the standing sweeps)


def tone_cycle(session, name, h, binary=None, depth=None):
    """A tone take folded on its sweep: the pedal's responses and the model's ([tone, phase bin],
    the model's cycle turned to where the pedal's is), the rate and the tones."""
    meta, played, a = load(session, name)
    ref = scorecard.through(h, played)
    guess = RATE_HZ[meta["settings"]["Rate"]]
    band = 30.0 if guess < 2 else 80.0
    z, at = tone_responses(a, ref, meta, band)
    rate = sweep_rate(level(z[3:8]), guess)
    pedal = folded(z, rate, at)
    m = slide(scorecard.through(h, render(played, meta, rate, depth=depth, binary=binary)), PEDAL_LATENCY)
    zm, _ = tone_responses(m, ref, meta, band)
    model = folded(zm, rate, at)
    return meta, pedal, np.roll(model, cyclic_shift(model, pedal), axis=1), rate


def errors(model, pedal):
    """dB rms of the level error (floored at -30 dB) and the complex error re the pedal's response."""
    return (float(np.sqrt(np.mean((level(model) - level(pedal)) ** 2))),
            float(np.sqrt(np.mean(abs(model - pedal) ** 2) / np.mean(abs(pedal) ** 2))))


def fitted_depth(session, name, h, binary=None):
    """Depth is set by hand except at its stops: the position whose model matches the take best."""
    meta = json.loads((pathlib.Path(session) / f"{name}.json").read_text())
    start = POSITION[meta["settings"]["Depth"]]
    if start in (0.0, 1.0):
        return None
    cost = lambda d: errors(*tone_cycle(session, name, h, binary, d)[1:3][::-1])[0]
    grid = np.clip(start + np.linspace(-0.12, 0.12, 9), 0.02, 1)
    d0 = grid[int(np.argmin([cost(d) for d in grid]))]
    return float(scipy.optimize.minimize_scalar(cost, bounds=(d0 - 0.03, d0 + 0.03), method="bounded", options={"xatol": 2e-3}).x)


def main():
    """ph3/focused.py [TAKE...] [--plot]: the tone takes against the Phaser as it was
    (build-ph3/render_ph3_base) and as the source now has it (build-ph3/render_ph3): each tone's
    level over the sweep's cycle. --plot: SESSION/fit/tones-TAKE.png."""
    names = [a for a in sys.argv[1:] if not a.startswith("--")] or TONE_TAKES
    h = chain(SESSION)
    print(f"{'take':26s} rate Hz |  was: level dB rms  complex |  now: level dB rms  complex   (Depth as fitted)")
    for name in names:
        runs, depths = {}, {}
        for label, binary in (("was", BASE), ("now", None)):
            depths[label] = fitted_depth(SESSION, name, h, binary)
            meta, pedal, model, rate = tone_cycle(SESSION, name, h, binary, depths[label])
            runs[label] = (model, errors(model, pedal))
        print(f"{name:26s} {rate:7.4f} | {runs['was'][1][0]:17.2f} {runs['was'][1][1]:8.3f} | {runs['now'][1][0]:17.2f} {runs['now'][1][1]:8.3f}"
              + (f"   ({depths['was']:.3f}, {depths['now']:.3f})" if depths["now"] is not None else ""), flush=True)
        if "--plot" in sys.argv:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, axes = plt.subplots(3, 3, figsize=(17, 10.5))
            x = np.arange(pedal.shape[1]) / pedal.shape[1]
            for ax, f0, p, was, now in zip(axes.flat, meta["signal"]["freq"], pedal, runs["was"][0], runs["now"][0]):
                ax.plot(x, level(p), color="k", lw=1.6, label="pedal")
                ax.plot(x, level(was), lw=0.9, label="was")
                ax.plot(x, level(now), lw=0.9, label="now")
                ax.set_title(f"{f0} Hz: level over the sweep's cycle, dB re bypass", fontsize=9)
                ax.grid(alpha=0.3)
                ax.legend(fontsize=7)
            fig.suptitle(f"{name} ({meta['settings']['Mode']}, Depth {meta['settings']['Depth']}, Res {meta['settings']['Res']}): "
                         f"was {runs['was'][1][0]:.1f} dB rms, now {runs['now'][1][0]:.1f}")
            plt.tight_layout()
            (SESSION / "fit").mkdir(exist_ok=True)
            plt.savefig(SESSION / "fit" / f"tones-{name}.png", dpi=55)
            plt.close(fig)


if __name__ == "__main__":
    main()
