#!/usr/bin/env python3
"""The AW-3 focused reamp against NAM-X's Wah (the AW-3 model). Set RENDER_AW3_WAS to an older
render_aw3 to see it beside today's.

A wah is a filter that moves: the effect alone is where the filter sits over time. Five quiet
tones run under the guitar (331 Hz to 2971 Hz, each 50 dB under its peak). Wherever the guitar
leaves a tone alone (the tone's line on the dry signal within 0.5 dB of its own level), the
output's line over the dry's is the filter's gain at that frequency, for the pedal and for the
model alike (the model rendered from the same played signal, through the bypass take's chain).
The five gains place the filter: the frequency at which the model's own static filter (Aw3Wah.h)
fits them best, and how well it fits (a bad fit = a different filter shape or level, not a
different place).

Per take and model: the share of the take the tones can be read on; the pedal's filter frequency
(median, lowest, highest); the model's frequency re the pedal's in octaves (mean, rms); the five
gains' difference (rms dB); the fit's own error for the pedal's gains (rms dB); the waveform
residual with the model delayed to the pedal's latency; and what is heard: the output's level per
octave band (125 Hz to 4 kHz) in 43 ms windows, model re pedal, over the windows either of them
fills (within 30 dB of the loudest): the rms difference, then each band's mean. A take that plays
several excerpts in a row (a sequence) gets a line per excerpt.

  aw3/focused.py [SESSION] [--plot]   (default captures/boss/aw-3/2026-11-11-focused)
  --plot: SESSION/fit/track-TAKE.png

Run from the repository root, with build-aw3/render_aw3 built (tools/capture/render_aw3.cpp)."""
import json, os, pathlib, re, subprocess, sys, warnings
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard, signals

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
SESSION = pathlib.Path(ARGS[0] if ARGS else "captures/boss/aw-3/2026-11-11-focused")
PLOT = "--plot" in sys.argv
RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
TONES = np.array([331.0, 577.0, 997.0, 1723.0, 2971.0])
KNOB = {"7:00": 0, "9:00": 20, "12:00": 50, "3:00": 80, "5:00": 100}
TYPE = {"Up": 1, "Down": 2, "Sharp": 3}
MODELS = {"namx": os.environ.get("RENDER_AW3", "build-aw3/render_aw3")}
if "RENDER_AW3_WAS" in os.environ:
    MODELS["was"] = os.environ["RENDER_AW3_WAS"]
LINE = scipy.signal.butter(4, 25, "lowpass", fs=RATE, output="sos")
STEP = 48  # the tracks' time step: 1 ms
GRID = np.geomspace(150, 6000, 600)


def constants():
    """Aw3Wah.h's tables and scalars by name."""
    text = pathlib.Path("src/dsp/Aw3Wah.h").read_text()
    out = {}
    for name, body in re.findall(r"constexpr float (\w+)\[[^\]]*\] = \{([^}]*)\}", text):
        out[name] = np.array([float(v.rstrip("f")) for v in body.replace("\n", " ").split(",") if v.strip()])
    for name, value in re.findall(r"constexpr float (\w+) = (-?[\d.]+)f;", text):
        out[name] = float(value)
    return out


def static_gains(c, sharp, freqs=TONES):
    """The model's filter at rest on each frequency of GRID: its gain at freqs, dB
    (GRID points, len(freqs))."""
    w = np.tan(np.pi * freqs[None, :] / RATE) / np.tan(np.pi * GRID[:, None] / RATE)
    s = 1j * w
    if sharp:
        d = s * s + s / c["SHARP_Q"] + 1
        h = (c["SHARP_ALPHA"] + c["SHARP_BETA"] * s + c["SHARP_GAMMA"] * s * s) * (1 + c["SHARP_DELTA"] * s) / d ** 2
    else:
        at = lambda name: np.interp(GRID, c["FILTER_HZ"], c[name])[:, None]
        k = 1 / at("FILTER_Q")
        h = (at("FILTER_LOW") + at("FILTER_GAIN") * k * s + at("FILTER_HIGH") * s * s) / (s * s + k * s + 1)
    return 20 * np.log10(np.abs(h))


def lines(x):
    """Each tone's line on x over time, every STEP samples: (5, n) complex."""
    t = np.arange(len(x)) / RATE
    out = []
    for f in TONES:
        z = x * np.exp(-2j * np.pi * f * t)
        out.append(2 * (scipy.signal.sosfiltfilt(LINE, z.real) + 1j * scipy.signal.sosfiltfilt(LINE, z.imag))[::STEP])
    return np.array(out)


BINS = np.arange(4, 107)  # of a 1024-point spectrum: 190 Hz to 5 kHz
BIN_HZ = BINS * RATE / 1024


def spectra(x):
    """|x|'s short spectra (BINS, frames every STEP samples, centred), 21 ms windows."""
    _, _, z = scipy.signal.stft(x.astype(np.float32), fs=RATE, nperseg=1024, noverlap=1024 - STEP)
    return np.abs(z[BINS]).astype(np.float64)


def place_weighted(gains_db, weights, table):
    """As place, each bin of each frame weighted (the dry's power there)."""
    f = np.empty(gains_db.shape[1])
    for start in range(0, gains_db.shape[1], 4000):
        g, w = gains_db[:, start:start + 4000], weights[:, start:start + 4000]
        e = (table ** 2) @ w - 2 * table @ (w * g)
        f[start:start + 4000] = GRID[np.argmin(e, axis=0)]
    return f


def place(gains_db, table):
    """The frequency whose static gains fit each column of gains_db (5, n) best, and the fit's rms."""
    f = np.empty(gains_db.shape[1])
    err = np.empty(gains_db.shape[1])
    for start in range(0, gains_db.shape[1], 4000):
        g = gains_db[:, start:start + 4000]
        e = np.sqrt(np.mean((table[:, :, None] - g[None, :, :]) ** 2, axis=1))
        i = np.argmin(e, axis=0)
        f[start:start + 4000] = GRID[i]
        err[start:start + 4000] = e[i, np.arange(g.shape[1])]
    return f, err


BANDS = np.array([125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0])
HOP = 1024


def band_levels(x):
    """x's level per octave band of BANDS, dB, in 43 ms windows every HOP samples."""
    f, _, z = scipy.signal.stft(x, fs=RATE, nperseg=2 * HOP, noverlap=HOP)
    power = np.abs(z) ** 2
    return np.array([10 * np.log10(power[(f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)].sum(axis=0) + 1e-20) for c in BANDS])


def heard(pedal, model):
    """The model's band levels re the pedal's over the windows either fills: rms dB, each band's mean dB."""
    p, m = band_levels(pedal), band_levels(model)
    top = max(p.max(), m.max())
    p, m = np.maximum(p, top - 40), np.maximum(m, top - 40)
    counted = np.maximum(p, m) > top - 30  # the tones alone, through the resonance, stay under this
    d = np.where(counted, m - p, np.nan)
    with np.errstate(all="ignore"), warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)  # a band nothing fills: nan
        return np.sqrt(np.nanmean(d ** 2)), np.nanmean(d, axis=1)


def segments(meta):
    """A take's excerpts that carry the tones: (label, first sample, end). One, unlabelled, for a
    plain take; a sequence's parts by their file's name."""
    signal, start = meta["signal"], meta["signal_start"]
    if signal["type"] != "sequence":
        return [("", start, start + int(signal["seconds"] * RATE))] if "probe" in signal else []
    return [(pathlib.Path(part["path"]).stem, start + int(at * RATE), start + int((at + part["seconds"]) * RATE))
            for part, at in zip(signal["parts"], signals.part_starts(signal)) if "probe" in part]


def render(played, meta, model):
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    y = subprocess.run([MODELS[model], str(TYPE[s["Mode"]]), str(KNOB[s["Manual"]]), str(KNOB[s["Sens"]]),
                        str(KNOB[s["Decay"]])], input=(played * scale).astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale


def lag(pedal, model, most=96):
    """The delay (whole samples) of pedal after model."""
    n = len(pedal)
    spectrum = np.fft.rfft(pedal, 2 * n) * np.conj(np.fft.rfft(model, 2 * n))
    corr = np.fft.irfft(spectrum)
    return int(np.argmax(np.concatenate([corr[-most:], corr[:most + 1]]))) - most


def main():
    c = constants()
    takes = sorted((json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    meta, played, pedal, _ = scorecard.load(SESSION, "bypass-funk")
    h = scorecard.chain_filter(played, pedal)
    print(f"{'take':24s} {'read':>5s} {'pedal Hz: median  low   high':>29s}  fit │ " + " │ ".join(
        f"{m}: tones oct mean rms gains  guitar oct mean rms  resid  lag  heard rms, per band" for m in MODELS))
    for meta in takes:
        name = meta["take"]
        if meta["bypass"] or not segments(meta):
            continue
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        parts = segments(meta)
        a, b = parts[0][1], parts[-1][2]
        dry = scorecard.through(h, played)
        dry_lines = np.abs(lines(dry))
        inside = np.zeros(dry_lines.shape[1], bool)
        clear = inside.copy()
        within = {}
        for label, first, end in parts:
            part = np.zeros_like(inside)
            part[(first + RATE // 5) // STEP:(end - RATE // 5) // STEP] = True
            own = np.percentile(dry_lines[:, part], 20, axis=1)[:, None]
            clear |= part & np.all(np.abs(20 * np.log10(np.maximum(dry_lines, 1e-12) / own)) < 0.5, axis=0)
            inside |= part
            within[label] = part
        sharp = meta["settings"]["Mode"] == "Sharp"
        table = static_gains(c, sharp)
        wide = static_gains(c, sharp, BIN_HZ)
        # On the guitar: the bins where the dry is within 40 dB of the reference, by its power.
        dry_bins = spectra(dry)[:, :dry_lines.shape[1]]
        floor = 10 ** ((meta["ref_dbfs"] - 40) / 20) / 2  # a sine's line in the spectrum: half its peak
        weights = np.where(dry_bins > floor, dry_bins ** 2, 0.0)
        loud = inside & ~clear & (np.count_nonzero(weights, axis=0) >= 6)

        def read(y):
            g = 20 * np.log10(np.maximum(np.abs(lines(y)), 1e-12) / dry_lines)
            f, err = place(g, table)
            wide_g = 20 * np.log10(np.maximum(spectra(y)[:, :dry_lines.shape[1]], 1e-12) / np.maximum(dry_bins, 1e-12))
            return g, np.where(clear, f, place_weighted(wide_g, weights, wide)), err

        pg, pf, pe = read(pedal)
        tracks = {}
        for model in MODELS:
            raw = scorecard.through(h, render(played, meta, model))
            delay = lag(pedal[a:b], raw[a:b])
            y = np.roll(raw, delay)
            mg, mf, _ = read(y)
            tracks[model] = (mf, y, mg, delay)
        for label, first, end in parts:
            still, playing_now = clear & within[label], loud & within[label]
            text = (f"{name + ('/' + label if label else ''):24s} {100 * still.sum() / within[label].sum():4.0f}% "
                    f"{np.median(pf[still]):16.0f} {pf[still].min():5.0f} {pf[still].max():6.0f} "
                    f"{np.sqrt(np.mean(pe[still] ** 2)):4.1f} │ ")
            cells = []
            for model, (mf, y, mg, delay) in tracks.items():
                octaves = np.log2(mf / pf)[still]
                playing = np.log2(mf / pf)[playing_now] if playing_now.any() else np.array([np.nan])
                gains = np.sqrt(np.mean((mg - pg)[:, still] ** 2))
                resid = scorecard.residual_db(pedal[first:end], y[first:end])
                rms, bands = heard(pedal[first:end], y[first:end])
                cells.append(f"{'':4s}{octaves.mean():+9.2f} {np.sqrt(np.mean(octaves ** 2)):5.2f} {gains:6.1f} "
                             f"{playing.mean():+6.2f} {np.sqrt(np.mean(playing ** 2)):5.2f} {resid:6.1f} {delay:4d} "
                             f"{rms:5.1f}  " + " ".join(f"{v:+5.1f}" for v in bands))
            print(text + " │ ".join(cells))
        if PLOT:
            plot(name, meta, a, b, clear, loud, pedal, pf, tracks)


def plot(name, meta, a, b, clear, loud, pedal, pf, tracks):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    span = slice(a // STEP, b // STEP)
    t = (np.arange(len(pf)) * STEP - a)[span] / RATE
    fig, axes = plt.subplots(2, 1, figsize=(16, 8), sharex=True)
    axes[0].plot(t, 20 * np.log10(np.abs(pedal[::STEP][span]) + 1e-6), lw=0.4, color="0.6", label="pedal")
    for (m, (_, y, _, _)), color in zip(tracks.items(), ("tab:blue", "tab:red")):
        axes[0].plot(t, 20 * np.log10(np.abs(y[::STEP][span]) + 1e-6), lw=0.4, color=color, alpha=0.5, label=m)
    axes[0].set_ylabel("output, dBFS")
    axes[0].set_ylim(-90, -10)
    axes[0].legend(loc="upper right")
    axes[1].plot(t, np.where(clear, pf, np.nan)[span], color="k", lw=1.4, label="pedal, tones")
    axes[1].plot(t, np.where(loud, pf, np.nan)[span], color="0.5", lw=1.0, label="pedal, guitar")
    for (m, (mf, _, _, _)), color, light in zip(tracks.items(), ("tab:blue", "tab:red"), ("lightskyblue", "salmon")):
        axes[1].plot(t, np.where(clear, mf, np.nan)[span], color=color, lw=1.0, label=m + ", tones")
        axes[1].plot(t, np.where(loud, mf, np.nan)[span], color=light, lw=0.8, label=m + ", guitar")
    axes[1].set_yscale("log")
    axes[1].set_ylabel("filter frequency, Hz")
    axes[1].set_xlabel("s")
    axes[1].grid(alpha=0.3, which="both")
    axes[1].legend(loc="upper right")
    axes[0].set_title(f"{name}: {meta['settings']}")
    out = SESSION / "fit"
    out.mkdir(exist_ok=True)
    fig.tight_layout()
    fig.savefig(out / f"track-{name}.png", dpi=80)
    plt.close(fig)


if __name__ == "__main__":
    main()
