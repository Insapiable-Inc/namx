"""The DD-8 loop session (plans/dd-8-loop.json) against NAM-X's Delay: every take's played signal
through NAM-X's processor (render_dd8) at the take's mode, the delay measured on the take, and
Level and Feedback set to what the pedal did (the repeats' level under the signal, the fade
after it), then the repeats alone (output minus the bypass chain's dry) compared as waveforms.

  dd8/loop.py [SESSION] [TAKE…] [--plot]   (default captures/boss/dd-8/2026-11-10-loop, every take;
                                            --plot: SESSION/fit/loop-TAKE.png; --ab: the guitar takes as
                                            listening pairs in SESSION/ab/, Mod's sweep placed on the
                                            pedal's from the probe tone)

  residual   model − pedal re the pedal, repeats alone, dB (−20: the two subtract to a tenth), after
             sliding the model by the fraction of a sample the pedal's delay is off a whole one
  level      the model's repeats re the pedal's, dB rms, before the trims (knobs as read)
  trims      what the model's Level and Feedback were moved by, in degrees of the knob

Run from the repository root with build-dd8/render_dd8 built."""
import json, os, pathlib, subprocess, sys
import numpy as np, scipy.optimize, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard, signals

RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
RENDER = os.environ.get("RENDER_DD8", "build-dd8/render_dd8")
TYPES = {"Standard": 1, "Analog": 2, "Tape": 3, "Warm": 4, "Mod": 5}
MOD_PERIOD = 18508  # samples: the loop session's Time 12:00 in Mod (the bursts' arrivals)
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}


def load(session, name):
    """As scorecard.load, but a take with a steady tone in it (a probe, a sweep) keeps its
    samples as recorded: the slip finder reads a tone's period as slips."""
    meta = json.loads((pathlib.Path(session) / f"{name}.json").read_text())
    spec = meta["signal"]
    if spec["type"] == "sweep" or "probe" in spec:
        recorded, _ = soundfile.read(pathlib.Path(session) / f"{name}.wav", dtype="float64")
        played, _ = soundfile.read(pathlib.Path(session) / "played" / f"{name}.wav", dtype="float64")
        lat = meta["latency_samples"]
        n = min(len(played), len(recorded) - lat) - 16
        return meta, played[:n], recorded[lat:lat + n, 0].copy(), recorded[:meta["marker_start"], 0]
    return scorecard.load(session, name)


def to_pedal(meta, played):
    """What the pedal was fed: the played signal, without the marker when it went to the loopback only."""
    x = played.copy()
    if meta.get("marker_to_pedal", True) is False:
        x[:meta["signal_start"]] = 0.0
    return x


def delay_samples(wet, dry, low_ms=60, high_ms=900):
    """The first repeat's delay: the lag at which the repeats best match the dry."""
    n = 1 << int(np.ceil(np.log2(2 * len(dry))))
    c = np.fft.irfft(np.fft.rfft(wet, n) * np.conj(np.fft.rfft(dry, n)), n)
    lo, hi = int(low_ms * RATE / 1000), int(high_ms * RATE / 1000)
    return lo + int(np.argmax(np.abs(scipy.signal.hilbert(c[lo:hi]))))


def render(fed, meta, time_ms, level, feedback):
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    y = subprocess.run([RENDER, str(TYPES[meta["settings"]["Mode"]]), f"{time_ms:.4f}", f"{level:.5f}", f"{feedback:.5f}"],
                       input=(fed * scale).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale


def fade(x, start, period, floor_db, most=40):
    levels = []
    for i in range(start, len(x) - period, period):
        level = 10 * np.log10(np.mean(x[i:i + period] ** 2) + 1e-30)
        if level < floor_db + 10 or len(levels) >= most:
            break
        levels.append(level)
    return float(np.polyfit(np.arange(len(levels)), levels, 1)[0]) if len(levels) >= 3 else float("nan")


def probe_delay(x, dry, meta):
    """The delay's movement (ms, mean removed) read from the probe tone under the guitar: the
    phase of the tone on the repeats against the dry's."""
    f0 = meta["signal"]["probe"]["freq"]
    low = scipy.signal.butter(4, 150, fs=RATE, output="sos")
    t = np.arange(len(x)) / RATE
    tone = lambda y: scipy.signal.sosfiltfilt(low, y * np.exp(-2j * np.pi * f0 * t))
    a = meta["signal_start"] + RATE
    b = meta["signal_start"] + int(signals.body_seconds(meta["signal"]) * RATE) - RATE // 4
    phase = np.unwrap(np.angle((tone(x) * np.conj(tone(dry)))[a:b]))
    d = -phase / (2 * np.pi * f0) * 1000
    return d - d.mean()


def sweep_pad(fed, meta, dry, wet, period, level, feedback, h, rate_hz=1.514):
    """Samples of silence to put before the signal so the model's sweep (it starts with the first
    sample) is where the pedal's was: the pad whose delay movement, read from the probe as the
    pedal's is, follows the pedal's best."""
    target = probe_delay(wet, dry, meta)
    cycle = int(RATE / rate_hz)
    best = (np.inf, 0)
    for pad in range(0, cycle, cycle // 48):
        y = scorecard.through(h, render(np.r_[np.zeros(pad), fed], meta, period / RATE * 1000, level, feedback)[pad:]) - dry
        error = np.mean((probe_delay(y, dry, meta) - target) ** 2)
        best = min(best, (error, pad))
    return best[1]


def compare(session, name, h, trim=True):
    meta, played, pedal, pre = load(session, name)
    pedal = pedal - np.mean(pre)
    fed = to_pedal(meta, played)
    dry = scorecard.through(h, fed)
    wet = pedal - dry
    period = delay_samples(wet, dry)
    s = meta["settings"]
    level, feedback = POSITION[s["Level"]], POSITION[s["Feedback"]]
    body = slice(meta["signal_start"], meta["signal_start"] + int(signals.body_seconds(meta["signal"]) * RATE))
    floor = 10 * np.log10(np.mean((pre[RATE // 10:] - np.mean(pre)) ** 2) + 1e-30)
    target = fade(wet, body.stop + RATE // 20, period, floor)
    pad = 0
    if s["Mode"] == "Mod":
        period = MOD_PERIOD  # its delay moves: the session's Time, one sample under Standard's
        if "probe" in meta["signal"]:
            pad = sweep_pad(fed, meta, dry, wet, period, level, feedback, h)
    run = lambda: scorecard.through(h, render(np.r_[np.zeros(pad), fed], meta, period / RATE * 1000, level, feedback)[pad:]) - dry
    model = run()
    as_read = 10 * np.log10(np.mean(model[body] ** 2) / np.mean(wet[body] ** 2))
    if trim:
        for _ in range(3):
            off = 10 * np.log10(np.mean(model[body] ** 2) / np.mean(wet[body] ** 2))
            if level < 1.0:
                level -= off / 67.0
            got = fade(model, body.stop + RATE // 20, period, floor)
            if feedback < 1.0 and not np.isnan(target) and not np.isnan(got):
                feedback = min(1.0, feedback + (target - got) / (22.0 if feedback > 0.65 else 17.0))
            model = run()
    a = meta["signal_start"]
    # The pedal's delay isn't a whole number of samples and the model's is: slide the model's
    # repeats by the fraction of a sample (one shift for the whole take) that matches best.
    spectrum, k = np.fft.rfft(model), np.arange(len(model) // 2 + 1) / len(model)
    slid = lambda shift: np.fft.irfft(spectrum * np.exp(-2j * np.pi * k * shift), len(model))
    error = lambda shift: np.mean((slid(shift) - wet)[a:] ** 2)
    shift = scipy.optimize.minimize_scalar(error, bounds=(-1.5, 1.5), method="bounded").x
    model = slid(shift)
    residual = 10 * np.log10(np.mean((model - wet)[a:] ** 2) / np.mean(wet[a:] ** 2))
    return dict(meta=meta, wet=wet, model=model, dry=dry, period=period, residual=residual, as_read=as_read,
                level_deg=(level - POSITION[s["Level"]]) * 300, feedback_deg=(feedback - POSITION[s["Feedback"]]) * 300,
                fade_pedal=target, fade_model=fade(model, body.stop + RATE // 20, period, floor))


BANDS = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000]


def band_map(x, hop):
    out = []
    for c in BANDS:
        sos = scipy.signal.butter(3, [c / 2 ** (1 / 6), c * 2 ** (1 / 6)], "bandpass", fs=RATE, output="sos")
        y = scipy.signal.sosfilt(sos, x)
        n = len(y) // hop
        out.append(10 * np.log10(np.mean(y[:n * hop].reshape(n, hop) ** 2, axis=1) + 1e-16))
    return np.array(out)


def plot(session, r):
    """The repeats alone, pedal and model: envelopes, the level per third octave and 20 ms (the
    pedal's, and model − pedal), and the two waveforms at the first repeat and at a late one."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    meta, wet, model, T = r["meta"], r["wet"], r["model"], r["period"]
    a = meta["signal_start"] - RATE // 10
    w, m = wet[a:], model[a:]
    t = np.arange(len(w)) / RATE
    smooth = scipy.signal.butter(2, 40, fs=RATE, output="sos")
    env = lambda x: 20 * np.log10(np.maximum(scipy.signal.sosfiltfilt(smooth, np.abs(x)), 1e-7))
    fig = plt.figure(figsize=(17, 19))
    grid = fig.add_gridspec(5, 2)
    ax = fig.add_subplot(grid[0, :])
    ax.plot(t, env(w), lw=0.8, label="pedal")
    ax.plot(t, env(m), lw=0.8, label="model")
    ax.set_ylim(env(w).max() - 80, env(w).max() + 3)
    ax.grid(alpha=0.3)
    ax.legend()
    ax.set_title(f"{meta['take']}: the repeats alone (output − dry), envelope dB — residual {r['residual']:.1f} dB, "
                 f"Level {r['level_deg']:+.1f}°, Feedback {r['feedback_deg']:+.1f}°")
    hop = int(0.02 * RATE)
    P, M = band_map(w, hop), band_map(m, hop)
    tt = np.arange(P.shape[1]) * 0.02
    top = P.max()
    ax = fig.add_subplot(grid[1, :])
    im = ax.pcolormesh(tt, np.arange(len(BANDS)), P, vmin=top - 70, vmax=top, cmap="magma", shading="auto")
    ax.set_yticks(range(0, len(BANDS), 2))
    ax.set_yticklabels(BANDS[::2])
    ax.set_title("pedal: third octaves × 20 ms")
    plt.colorbar(im, ax=ax)
    d = M - P
    d[(P < top - 65) & (M < top - 65)] = np.nan
    ax = fig.add_subplot(grid[2, :])
    im = ax.pcolormesh(tt, np.arange(len(BANDS)), d, vmin=-8, vmax=8, cmap="RdBu_r", shading="auto")
    ax.set_yticks(range(0, len(BANDS), 2))
    ax.set_yticklabels(BANDS[::2])
    ax.set_title("model − pedal, dB (red: the model louder)")
    plt.colorbar(im, ax=ax)
    e = np.abs(scipy.signal.hilbert(w))
    first = int(np.argmax(e[:len(e) // 2] > 0.5 * e.max()))
    spots = [first, first + T, first + 4 * T, min(first + 20 * T, len(w) - RATE // 10 - 1)]
    for k, s0 in enumerate(spots):
        ax = fig.add_subplot(grid[3 + k // 2, k % 2])
        seg = slice(max(s0 - 480, 0), max(s0 - 480, 0) + 2400)
        ax.plot(t[seg], w[seg], lw=0.8, label="pedal")
        ax.plot(t[seg], m[seg], lw=0.8, label="model", alpha=0.85)
        ax.set_title(f"waveforms at {t[seg][0]:.2f} s")
        ax.legend()
    plt.tight_layout()
    out = pathlib.Path(session) / "fit"
    out.mkdir(exist_ok=True)
    plt.savefig(out / f"loop-{meta['take']}.png", dpi=62)
    plt.close(fig)


def write_pair(session, r):
    """The take as a listening pair in SESSION/ab/: the pedal's output and dry + the model's
    repeats, from 0.1 s before the signal, one gain for both; a probe tone notched out of both."""
    meta = r["meta"]
    a = meta["signal_start"] - RATE // 10
    pair = [(r["wet"] + r["dry"])[a:], (r["model"] + r["dry"])[a:]]
    if "probe" in meta["signal"]:
        notch = scipy.signal.iirnotch(meta["signal"]["probe"]["freq"], 30, RATE)
        pair = [scipy.signal.filtfilt(*notch, x) for x in pair]
    gain = 10 ** (-1 / 20) / max(np.abs(x).max() for x in pair)
    out = pathlib.Path(session) / "ab"
    out.mkdir(exist_ok=True)
    for kind, x in zip(("pedal", "model"), pair):
        soundfile.write(out / f"{meta['take']}.{kind}.wav", x * gain, RATE, subtype="PCM_24")


def main():
    session = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/dd-8/2026-11-10-loop")
    draw, pairs = "--plot" in sys.argv, "--ab" in sys.argv
    names = [a for a in sys.argv[2:] if not a.startswith("--")]
    _, played, pedal, _ = scorecard.load(session, "bypass-stabs")
    h = scorecard.chain_filter(played, pedal)
    takes = sorted((json.loads(p.read_text()) for p in session.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    print(f"{'take':34s} delay ms  residual dB  level as read  trims: Level, Feedback (deg)  fade dB/repeat: pedal model")
    for meta in takes:
        if meta["bypass"] or (names and meta["take"] not in names):
            continue
        r = compare(session, meta["take"], h)
        if draw:
            plot(session, r)
        if pairs and meta["signal"]["type"] == "file":
            write_pair(session, r)
        print(f"{meta['take']:34s} {r['period'] / RATE * 1000:7.2f}  {r['residual']:9.1f}  {r['as_read']:+11.2f}"
              f"     {r['level_deg']:+6.1f} {r['feedback_deg']:+6.1f}            {r['fade_pedal']:+7.2f} {r['fade_model']:+7.2f}")


if __name__ == "__main__":
    main()
