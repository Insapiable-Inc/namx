"""The CE-5 focused sessions (plans/ce-5-focused.json) against NAM-X's Chorus.

  ce5/focused.py [stereo|mono] [TAKE...] [--plot] [--ab]     (see main)

Both outputs recorded (SESSION 2026-11-10-focused): channel 0 = OUTPUT A (the chorus alone),
channel 2 = OUTPUT B (the dry). Mono (2026-11-10-focused-mono): channel 0 = dry + chorus.
The model is NAM-X's own processor (build-ce5/render_ce5), fed the played signal, its two
outputs put through the bypass take's chain like the pedal's."""
import json, os, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard, signals

RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
RENDER = os.environ.get("RENDER_CE5", "build-ce5/render_ce5")
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
STEREO = pathlib.Path("captures/boss/ce-5/2026-11-10-focused")
MONO = pathlib.Path("captures/boss/ce-5/2026-11-10-focused-mono")


def load(session, name):
    """A take as recorded: its sidecar, the played signal, OUTPUT A and OUTPUT B (noise in the
    mono session) aligned to it, each with its own offset (read before the marker) removed."""
    session = pathlib.Path(session)
    meta = json.loads((session / f"{name}.json").read_text())
    recorded, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(session / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(recorded) - lat) - 16
    pre = recorded[:meta["marker_start"]]
    out = recorded[lat:lat + n + 16] - pre.mean(axis=0)
    # PipeWire's one-sample slips in the middle of a take (scorecard.slips), found on the
    # loopback with any steady tone (the probe) notched out of both sides, and undone.
    quiet = lambda x: scipy.signal.filtfilt(*scipy.signal.iirnotch(probe_freq(meta), 5, RATE), x)
    tone = meta["signal"]["type"] == "sine" or "probe" in meta["signal"]
    meta["slips"] = []
    if meta["signal"]["type"] == "file":
        loop, sent = (quiet(out[:, 1]), quiet(played[:n + 16])) if tone else (out[:, 1], played[:n + 16])
        meta["slips"] = scorecard.slips(loop, sent, meta["signal_start"])
    fixed = out[:n].copy()
    for at, step in meta["slips"]:
        fixed[at:] = out[at + step:n + step]
    b = fixed[:, 2].copy() if fixed.shape[1] > 2 else None  # October's sessions: OUTPUT A only
    return meta, played[:n], fixed[:, 0].copy(), b, fixed[:, 1].copy()


def chains(session):
    """The bypass chain to each output, from the bypass sweep."""
    _, played, a, b, _ = load(session, "bypass-sweep")
    return scorecard.chain_filter(played, a), scorecard.chain_filter(played, b)


def render(fed, meta, rate_hz=0.858, pad=0, settings=None, binary=None):
    """The model's two outputs (chorus, dry) for a take's settings, in the capture's scale.
    `pad`: samples of silence before the signal (places the sweep, which starts at its top)."""
    s = settings or meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    args = [POSITION[s["E.Level"]], rate_hz, POSITION[s["Depth"]], POSITION[s["Low"]], POSITION[s["High"]]]
    x = np.r_[np.zeros(pad), fed] * scale
    y = subprocess.run([binary or RENDER] + [f"{v:.6f}" for v in args], input=x.astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    y = np.frombuffer(y, dtype=np.float32).astype(np.float64).reshape(-1, 2)[pad:] / scale
    return y[:, 0], y[:, 1]


def response(x, y, nperseg=16384):
    """y re x: frequencies, complex response (where x has energy)."""
    f, pxy = scipy.signal.csd(x, y, RATE, nperseg=nperseg)
    _, pxx = scipy.signal.welch(x, RATE, nperseg=nperseg)
    return f, pxy / pxx


def slide(x, samples):
    return scorecard.shift(x, samples)


def best_slide(model, pedal, span=3.0):
    """The delay (samples, fractional) of `model` that matches `pedal` best, and the residual
    (model − pedal re the pedal, dB) there and with the best single gain as well."""
    import scipy.optimize
    spectrum, k = np.fft.rfft(model), np.fft.rfftfreq(len(model))
    slid = lambda s: np.fft.irfft(spectrum * np.exp(-2j * np.pi * k * s), len(model))
    err = lambda s: np.mean((slid(s) - pedal) ** 2)
    grid = np.arange(-span, span + 0.01, 0.5)
    s0 = grid[int(np.argmin([err(s) for s in grid]))]
    s = scipy.optimize.minimize_scalar(err, bounds=(s0 - 0.5, s0 + 0.5), method="bounded").x
    m = slid(s)
    g = np.dot(m, pedal) / np.dot(m, m)
    db = lambda e: 10 * np.log10(np.mean(e ** 2) / np.mean(pedal ** 2))
    return s, db(m - pedal), 20 * np.log10(abs(g)), db(g * m - pedal), m


def body(meta):
    a = meta["signal_start"]
    return a, a + int(signals.body_seconds(meta["signal"]) * RATE)


def probe_freq(meta):
    spec = meta["signal"]
    return spec["probe"]["freq"] if "probe" in spec else spec["freq"]


def probe_delay(out, ref, meta, band_hz=150.0):
    """The chorus's delay re `ref` (the played signal through the bypass chain), ms, modulo the
    probe's period (0.125 ms at 8 kHz), over the take's body: the phase of the probe tone on
    `out` against its phase on `ref`. Also the tone's level on `out` re `ref`, dB."""
    f0 = probe_freq(meta)
    low = scipy.signal.butter(4, band_hz, fs=RATE, output="sos")
    t = np.arange(len(out)) / RATE
    tone = lambda y: scipy.signal.sosfiltfilt(low, y * np.exp(-2j * np.pi * f0 * t))
    a, b = body(meta)
    a, b = a + RATE // 4, b - RATE // 4
    z = (tone(out) * np.conj(tone(ref)))[a:b] / np.abs(tone(ref)[a:b]) ** 2
    return -np.unwrap(np.angle(z)) / (2 * np.pi * f0) * 1000, 20 * np.log10(np.abs(z)), a


def tri_power(t, rate, phase, low, swing, power):
    """The model's sweep: low + swing * u^power, u a triangle, 1 at phase 0."""
    p = (rate * t + phase) % 1.0
    u = 1.0 - 2.0 * np.minimum(p, 1.0 - p)
    return low + swing * u ** power


def fit_sweep(d, rate_guess):
    """Rate (Hz), phase at the first sample (0-1, the top of the delay at 0), low (ms), swing
    (ms), power and the rms misfit (ms) of the bowed triangle on a delay curve d (ms, at RATE)."""
    import scipy.optimize
    step = 48
    y, t = d[::step], np.arange(0, len(d), step) / RATE
    best = None
    for rate in rate_guess * np.linspace(0.9, 1.1, 21):
        for phase in np.arange(0, 1, 1 / 24):
            p0 = [rate, phase, y.min(), np.ptp(y), 1.2]
            e = np.mean((tri_power(t, *p0) - y) ** 2)
            if best is None or e < best[0]:
                best = (e, p0)
    lo = [best[1][0] * 0.9, best[1][1] - 0.2, -np.inf, 0, 0.5]
    hi = [best[1][0] * 1.1, best[1][1] + 0.2, np.inf, np.inf, 3.0]
    p, _ = scipy.optimize.curve_fit(tri_power, t, y, best[1], bounds=(lo, hi))
    return dict(rate=p[0], phase=p[1] % 1.0, low=p[2], swing=p[3], power=p[4],
                misfit=float(np.sqrt(np.mean((tri_power(t, *p) - y) ** 2))))


RATE_GUESS = {"7:00": 0.25, "9:00": 0.375, "12:00": 0.858, "3:00": 2.515, "5:00": 5.934}
DRY_LATENCY = 47  # samples: the pedal's dry path (0.98 ms), which the model leaves out
UNDER = scipy.signal.butter(6, 5000, fs=RATE, output="sos")


def compare(session, name, h, binary=None, mono=False, trim=False):
    """A take against the model placed on the pedal's own sweep (rate and phase read from the
    probe). The chorus alone: OUTPUT A, or in the mono session the output minus the model's dry
    (the dry is right to -35 dB). Returns the pedal's and the model's chorus over the whole take
    (the model slid by the fraction of a sample that matches best; with `trim`, its level set to
    the pedal's: a knob set by hand), the dry, their delay curves and the residuals."""
    meta, played, a, b, _ = load(session, name)
    ref = scorecard.through(h, played)
    guess = RATE_GUESS[meta["settings"]["Rate"]]
    band = 150 if guess < 3 else 500
    _, dry = render(played, meta, binary=binary)
    dry = slide(scorecard.through(h, dry), DRY_LATENCY + 0.268)
    out = a
    if mono:
        a = a - dry
    d, level, at = probe_delay(a, ref, meta, band)
    fit = fit_law(d, guess)
    cycle = RATE / fit["rate"]
    # The model's output is slid DRY_LATENCY later below: its sweep starts that much earlier.
    pad = int(round((fit["phase"] * cycle - at + DRY_LATENCY) % cycle))
    wet, _ = render(played, meta, fit["rate"], pad, binary=binary)
    model = np.roll(scorecard.through(h, wet), DRY_LATENCY)
    s0, s1 = body(meta)
    r = dict(meta=meta, fit=fit, pedal=a, dry=dry, out=out, d=d, at=at, ref=ref, start=s0, stop=s1)
    r["slide"], r["residual"], r["gain"], _, _ = best_slide(model[s0:s1], a[s0:s1], 8.0)
    model = slide(model, r["slide"])
    lo = lambda x: scipy.signal.sosfiltfilt(UNDER, x)
    pl, ml = lo(a[s0:s1]), lo(model[s0:s1])
    r["gain_low"] = 10 * np.log10(np.mean(ml ** 2) / np.mean(pl ** 2))
    if trim:
        model = model * 10 ** (-r["gain_low"] / 20)
        ml = ml * 10 ** (-r["gain_low"] / 20)
    r["residual_low"] = 10 * np.log10(np.mean((ml - pl) ** 2) / np.mean(pl ** 2))
    r["dm"], _, _ = probe_delay(model, ref, meta, band)
    r["model"] = model
    return r


ROUND = 0.25  # the share of a half sine in each side of the sweep (the corners' rounding)


def sweep_law(t, rate, phase, duty, low, swing, bend):
    """The pedal's sweep as read from the probe: a triangle (1 at phase 0, down to 0 at `duty`,
    back up), each side rounded a little (ROUND of a half sine), through an exponential:
    delay = low + swing * (e^(bend v) - 1) / (e^bend - 1)."""
    p = (rate * t + phase) % 1.0
    u = np.where(p < duty, 1 - p / duty, (p - duty) / (1 - duty))
    v = (1 - ROUND) * u + ROUND * (0.5 - 0.5 * np.cos(np.pi * u))
    return low + swing * (np.expm1(bend * v) / np.expm1(bend) if abs(bend) > 1e-6 else v)


def fit_law(d, rate_guess):
    """sweep_law fitted to a delay curve d (ms, at RATE): its parameters and the rms misfit (ms)."""
    import scipy.optimize
    first = fit_sweep(d, rate_guess)
    step = 24
    y, t = d[::step], np.arange(0, len(d), step) / RATE
    p, _ = scipy.optimize.curve_fit(sweep_law, t, y, [first["rate"], first["phase"], 0.52, y.min(), np.ptp(y), 0.5],
                                    maxfev=40000)
    r = sweep_law(t, *p) - y
    return dict(rate=p[0], phase=p[1] % 1.0, duty=p[2], low=p[3], swing=p[4], bend=p[5],
                misfit=float(np.sqrt(np.mean(r ** 2))), worst=float(np.abs(r).max()))


GUITAR = ["static-chords", "static-funk-m12", "static-funk", "high-0700-chords", "filters-1200-chords", "low-0700-chords",
          "1200-chords", "1200-funk-m12", "1200-funk", "1200-funk-repeat", "1200-lead", "depth-0500-chords",
          "rate-0500-depth-0500-chords", "rate-0500-depth-0500-lead", "rate-0700-depth-0500-chords"]
BASE = "build-ce5/render_ce5_base"  # the model as it was before this session, for the comparison


def band_map(x, hop):
    out = []
    for sos in scorecard.BANK:
        y = scipy.signal.sosfilt(sos, x)
        n = len(y) // hop
        out.append(10 * np.log10(np.mean(y[:n * hop].reshape(n, hop) ** 2, axis=1) + 1e-16))
    return np.array(out)


def no_probe(x, meta):
    return scipy.signal.filtfilt(*scipy.signal.iirnotch(probe_freq(meta), 30, RATE), x)


def plot(session, name, runs, mono):
    """One take, the chorus alone: the sweep's error (model - pedal delay), the level per third
    octave and 50 ms (model - pedal) for each model, and the waveforms at two moments."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    first = next(iter(runs.values()))
    meta, s0, s1 = first["meta"], first["start"], first["stop"]
    fig = plt.figure(figsize=(16, 4 + 3.2 * len(runs) + 6))
    grid = fig.add_gridspec(2 + len(runs) + 2, 2)
    ax = fig.add_subplot(grid[0, :])
    t = np.arange(len(first["d"])) / RATE
    ax.plot(t, first["d"] - first["d"].mean(), lw=0.7, color="k", label="pedal")
    for label, r in runs.items():
        ax.plot(t, r["dm"] - r["dm"].mean(), lw=0.7, label=label)
    ax.set_title(f"{name} ({'mono output, dry taken off' if mono else 'OUTPUT A, the chorus alone'}): the delay from the probe, ms about its mean")
    ax.legend(loc="upper right", fontsize=8)
    if RATE_GUESS[meta["settings"]["Rate"]] > 3:
        ax.set_xlim(2, 4)
    ax = fig.add_subplot(grid[1, :])
    for label, r in runs.items():
        e = (r["dm"] - r["d"]) * 1000
        ax.plot(t, e - np.median(e), lw=0.7, label=f"{label}: {np.std(e):.1f} µs rms")
    ax.set_title("model − pedal delay, µs")
    ax.grid(alpha=0.3)
    ax.legend(loc="upper right", fontsize=8)
    if RATE_GUESS[meta["settings"]["Rate"]] > 3:
        ax.set_xlim(2, 4)
    hop = 2400
    pedal = no_probe(first["pedal"], meta)[s0:s1]
    P = band_map(pedal, hop)
    tt = np.arange(P.shape[1]) * hop / RATE
    for k, (label, r) in enumerate(runs.items()):
        M = band_map(no_probe(r["model"], meta)[s0:s1], hop)
        d = M - P
        d[(P < P.max() - 55) & (M < P.max() - 55)] = np.nan
        ax = fig.add_subplot(grid[2 + k, :])
        im = ax.pcolormesh(tt, np.arange(len(scorecard.BANDS)), d, vmin=-4, vmax=4, cmap="RdBu_r", shading="auto")
        ax.set_yticks(range(0, len(scorecard.BANDS), 3))
        ax.set_yticklabels(scorecard.BANDS[::3])
        ax.set_title(f"{label} − pedal, dB per third octave and 50 ms (red: the model louder) — waveform residual under 5 kHz "
                     f"{r['residual_low']:.1f} dB")
        plt.colorbar(im, ax=ax)
    env = np.abs(scipy.signal.hilbert(pedal))
    loud = int(np.argmax(env[RATE:len(env) // 2])) + RATE
    for k, spot in enumerate((loud, loud + 3 * RATE)):
        spot = min(spot, len(pedal) - 1500)
        seg = slice(spot - 200, spot + 1000)
        ax = fig.add_subplot(grid[2 + len(runs), k])
        ax.plot(pedal[seg], lw=1.0, color="k", label="pedal")
        for label, r in runs.items():
            ax.plot(no_probe(r["model"], meta)[s0:s1][seg], lw=0.8, label=label, alpha=0.85)
        ax.set_title(f"waveforms at {spot / RATE:.2f} s into the take (25 ms)")
        ax.legend(fontsize=7)
        ax = fig.add_subplot(grid[3 + len(runs), k])
        for label, r in runs.items():
            ax.plot((no_probe(r["model"], meta)[s0:s1] - pedal)[seg], lw=0.8, label=f"{label} − pedal", alpha=0.85)
        ax.set_ylim(*np.array([-1, 1]) * np.abs(pedal[seg]).max())
        ax.set_title("the difference, on the scale above")
        ax.legend(fontsize=7)
    plt.tight_layout()
    out = pathlib.Path(session) / "fit"
    out.mkdir(exist_ok=True)
    plt.savefig(out / f"focused-{name}.png", dpi=60)
    plt.close(fig)


def write_trio(session, name, runs):
    """The take as heard, for listening: SESSION/ab/TAKE.pedal.wav and one file per model (the
    model's chorus, its level set to the pedal's, plus the dry), the probe notched out, one gain."""
    first = next(iter(runs.values()))
    meta, a = first["meta"], first["start"] - RATE // 10
    files = {"pedal": first["pedal"] + first["dry"]}
    files.update({label: r["model"] + r["dry"] for label, r in runs.items()})
    files = {k: no_probe(v, meta)[a:first["stop"] + RATE // 2] for k, v in files.items()}
    gain = 10 ** (-1 / 20) / max(np.abs(v).max() for v in files.values())
    out = pathlib.Path(session) / "ab"
    out.mkdir(exist_ok=True)
    for label, x in files.items():
        soundfile.write(out / f"{name}.{label}.wav", x * gain, RATE, subtype="PCM_24")


def main():
    """ce5/focused.py [stereo|mono] [TAKE...] [--plot] [--ab]: every guitar take of the session
    against the model as it was (build-ce5/render_ce5_base) and as the source now has it
    (build-ce5/render_ce5). --plot: SESSION/fit/focused-TAKE.png; --ab: SESSION/ab/ listening files."""
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mono = bool(args) and args[0] == "mono"
    session = MONO if mono else STEREO
    names = [a for a in args if a not in ("mono", "stereo")] or [n for n in GUITAR if (session / f"{n}.json").exists()]
    h = chains(session)[0]
    print(f"{session}: the chorus alone, model against pedal; the model's level set to the pedal's (E.Level is set by hand)")
    print(f"{'take':30s} | {'was':>28s} | {'now':>28s}")
    print(f"{'':30s} | resid<5k  bands  sweep  level | resid<5k  bands  sweep  level   (dB, dB rms, µs rms, dB as read)")
    for name in names:
        runs = {"was": compare(session, name, h, BASE, mono, True), "now": compare(session, name, h, None, mono, True)}
        row = f"{name:30s}"
        for r in runs.values():
            meta = r["meta"]
            recorded, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
            pre = recorded[:meta["marker_start"], 0]
            sc = scorecard.score(no_probe(r["pedal"], meta), no_probe(r["model"], meta), pre, r["start"], False)
            e = (r["dm"] - r["d"]) * 1000
            row += f" | {r['residual_low']:8.1f} {sc['rms']:6.2f} {np.std(e):6.1f} {r['gain_low']:+6.2f}"
        print(row, flush=True)
        if "--plot" in sys.argv:
            plot(session, name, runs, mono)
        if "--ab" in sys.argv:
            write_trio(session, name, runs)


if __name__ == "__main__":
    main()
