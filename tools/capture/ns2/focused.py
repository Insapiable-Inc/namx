"""The NS-2's gain read directly (captures/boss/ns-2/2026-11-21-focused).

The test signal went into INPUT and a steady three-tone probe into RETURN, so the pedal's output is the probe times
the gate's gain at every instant. Per take this reads that gain:

  the probe as it leaves the open pedal: three sines, each one's amplitude and phase fitted where the take's output is
  loudest (the gate open); gain(t) = <output · probe> / <probe · probe> over a short window (2 ms), both band-passed
  round the probe's tones (the gate's switching thumps are below them)

and saves, per ms, the gain (dB re open) with the input played (1.0 = the guitar's peak) at the instant it acted:
build-ns2/focused/<take>.npz. `python3 tools/capture/ns2/focused.py` reads every take and prints a summary;
`plot` draws each take's input level and gain (build-ns2/focused/*.png).

Run from the repository root."""
import json, pathlib, sys
import numpy as np, scipy.signal, soundfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/capture"))
import signals  # noqa: E402

S = ROOT / "captures/boss/ns-2/2026-11-21-focused"
OUT = ROOT / "build-ns2/focused"
RATE = 48000
MS = 48
BAND = scipy.signal.firwin(193, [1300, 3900], pass_zero=False, fs=RATE, window=("kaiser", 7.0))  # 4 ms, linear phase
SMOOTH = np.hanning(2 * MS + 1)


def takes():
    return sorted(p.stem for p in S.glob("*.json") if p.stem != "rig")


def load(name):
    """The take's sidecar, the input played (guitar-peak units) and the probe played, both moved to the recording's
    time, and the recording."""
    meta = json.loads((S / f"{name}.json").read_text())
    rec = soundfile.read(S / f"{name}.wav")[0]
    rec = rec if rec.ndim == 1 else rec[:, 0]
    ref = 10 ** (meta["ref_dbfs"] / 20)

    def moved(path):
        x = soundfile.read(path)[0]
        x = x if x.ndim == 1 else x[:, 0]
        out = np.zeros(len(rec))
        n = min(len(x), len(rec) - meta["latency_samples"])
        out[meta["latency_samples"]:meta["latency_samples"] + n] = x[:n]
        return out

    played = moved(S / "played" / f"{name}.wav") / ref
    probe = moved(S / "played" / f"{name}.loop.wav")
    played[:meta["signal_start"] + meta["latency_samples"] - RATE // 100] = 0.0  # the marker is not the pedal's input
    return meta, played, probe, rec


def parts(meta):
    """[(part's spec, its first sample in the recording)] of a sequence take."""
    spec = meta["signal"]
    return [(p, meta["signal_start"] + meta["latency_samples"] + int(round(s * RATE))) for p, s in zip(spec["parts"], signals.part_starts(spec))]


def demodulate(x, freq, taps=385):
    """x's complex amplitude at freq over time (a Blackman window, 8 ms)."""
    n = np.arange(len(x))
    w = np.blackman(taps)
    return 2 * scipy.signal.fftconvolve(x * np.exp(-2j * np.pi * freq * n / RATE), w / w.sum(), mode="same")


def read(name):
    meta, played, probe, rec = load(name)
    freqs = meta["loop_probe"]["freq"]
    start = meta["signal_start"] + meta["latency_samples"]
    body = slice(start, len(rec) - RATE // 5)
    z = np.array([demodulate(rec, f) for f in freqs])            # the output's three tones over time
    zp = np.array([demodulate(probe, f) for f in freqs])          # the probe's as played
    size = np.abs(z).sum(axis=0)
    # The gate open: it is for over a third of every take, and its gain there is one value (the control clamps).
    top = np.percentile(size[body], 80)
    loud = np.zeros(len(rec), bool)
    loud[body] = np.abs(size[body] - top) < 0.01 * top
    each = [(z[k] / zp[k])[loud] for k in range(len(freqs))]
    ratio = np.array([np.median(v.real) + 1j * np.median(v.imag) for v in each])
    n = np.arange(len(rec))
    # The open pedal's probe: the played probe's tones, each with the pedal's gain and phase.
    open_probe = np.zeros(len(rec))
    for k, f in enumerate(freqs):
        open_probe += (zp[k] * ratio[k] * np.exp(2j * np.pi * f * n / RATE)).real
    a = scipy.signal.fftconvolve(rec, BAND, mode="same")
    b = scipy.signal.fftconvolve(open_probe, BAND, mode="same")
    num = scipy.signal.fftconvolve(a * b, SMOOTH, mode="same")
    den = scipy.signal.fftconvolve(b * b, SMOOTH, mode="same")
    gain = np.where(den > 1e-12, num / np.maximum(den, 1e-12), 0.0)
    return dict(meta=meta, played=played, rec=rec, gain=gain, ratio=ratio, tones=z / (zp * ratio[:, None] + 1e-30), loud=loud, start=start)


def per_ms(x):
    n = len(x) // MS
    return x[:n * MS].reshape(n, MS).mean(axis=1)


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-9))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bypass = None
    for name in ["bypass-probe"] + [t for t in takes() if t != "bypass-probe"]:
        r = read(name)
        if name == "bypass-probe":
            bypass = r["ratio"]
        g = r["gain"][r["start"]:]
        step = np.angle(r["ratio"] / bypass) / (2 * np.pi)
        print(f"{name:36s} latency {r['meta']['latency_samples']:4d}  open re bypass " + " ".join(f"{v:+.2f}" for v in db(r["ratio"] / bypass))
              + " dB, phase " + " ".join(f"{v:+.3f}" for v in step) + f" cycles · gain {db(np.percentile(g, 0.5)):6.1f} to {db(np.percentile(g, 99.5)):5.2f} dB"
              + f" · open {100 * r['loud'].mean():.0f} % of the take")
        np.savez(OUT / f"{name}.npz", gain=per_ms(r["gain"]), played=r["played"], ratio=r["ratio"], start=r["start"], latency=r["meta"]["latency_samples"])




def level_db(played):
    """The input's level per ms: the peak of |x| over each ms, dB re the guitar's peak."""
    n = len(played) // MS
    return db(np.abs(played[:n * MS]).reshape(n, MS).max(axis=1))


def plot(names):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    for name in names:
        d = np.load(OUT / f"{name}.npz")
        meta = json.loads((S / f"{name}.json").read_text())
        gain, level = db(d["gain"]), level_db(d["played"])
        P = parts(meta)
        groups = [P[i:i + 6] for i in range(0, len(P), 6)]
        for gi, group in enumerate(groups):
            fig, axes = plt.subplots(len(group), 1, figsize=(15, 3.1 * len(group)), squeeze=False)
            for ax, (part, start) in zip(axes[:, 0], group):
                seconds = signals.body_seconds(part) + part.get("tail", 1)
                a, b = start // MS - 100, min((start + int(seconds * RATE)) // MS + 100, len(gain), len(level))
                t = (np.arange(a, b) - start / MS) / 1000
                ax.plot(t, gain[a:b], "C3", lw=0.9)
                ax.set_ylim(-45, 3); ax.set_ylabel("gain, dB", color="C3"); ax.grid(alpha=0.3)
                ax2 = ax.twinx(); ax2.plot(t, level[a:b], "0.45", lw=0.6); ax2.set_ylim(-100, 5); ax2.set_ylabel("input peak, dB")
                ax.set_title(signals.describe(part)[:150], fontsize=8)
            fig.suptitle(f"{name} ({gi + 1}/{len(groups)})"); fig.tight_layout()
            fig.savefig(OUT / f"{name}-{gi + 1}.png", dpi=70); plt.close(fig)


if __name__ == "__main__":
    if sys.argv[1:2] == ["plot"]:
        plot(sys.argv[2:] or takes())
    else:
        main()
