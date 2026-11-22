#!/usr/bin/env python3
"""The CS-3 focused reamp against NAM-X's Compressor (the CS-3 model). Set RENDER_CS3_WAS to an
older render_cs3 that takes the same arguments to see it beside today's.

A compressor is a gain that moves: the effect alone is that gain over time. It is read two ways,
for the pedal and for each model alike (the model rendered from the same played signal):
  - on the guitar: the output's level over the dry's (the played signal through the bypass
    take's chain), 60 Hz smoothing, where the dry is within 45 dB of the reference;
  - on the probe (a 9 kHz sine far under the guitar): its line on the output over the played
    one's, where the guitar is quieter than that — the recovery between hits.
Per take and model: the gain's difference from the pedal's, mean and rms (dB) on each reading;
the waveform residual; the scorecard's bands. The model's Level is matched once for the session
(the pedal's Level sat at 12:00 throughout): the median gain difference over the takes at
Sustain 12:00, Attack 12:00.

  cs3/focused.py [SESSION] [--plot]   (default captures/boss/cs-3/2026-11-11-focused)
  --plot: SESSION/fit/gain-TAKE.png (the gain over time, pedal and models, and the differences)

Run from the repository root, with build-cs3/render_cs3 built (tools/capture/render_cs3.cpp)."""
import json, os, pathlib, subprocess, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
SESSION = pathlib.Path(ARGS[0] if ARGS else "captures/boss/cs-3/2026-11-11-focused")
PLOT = "--plot" in sys.argv
RENDER = os.environ.get("RENDER_CS3", "build-cs3/render_cs3")
RATE = scorecard.RATE
REF_NAMX_DBFS = -6.0
PROBE_HZ = 9000.0
# The pedal's knob positions as NAM-X's knobs (Cs3Compressor.h; Sustain 12:00 from the study's
# static curve: 18.5 dB more gain than 7:00).
THRESHOLD_DBFS = {"7:00": -20.26, "12:00": float(os.environ.get("CS3_T12", -38.76)), "5:00": float(os.environ.get("CS3_T5", -55.73))}
RELEASE_MS = {"7:00": 1152.0, "12:00": 300.0, "5:00": 99.5}  # the recovery's time scale
MODELS = {"namx": RENDER}
if "RENDER_CS3_WAS" in os.environ:
    MODELS["was"] = os.environ["RENDER_CS3_WAS"]
SMOOTH = scipy.signal.butter(2, 60, "lowpass", fs=RATE, output="sos")
LINE = scipy.signal.butter(4, 200, "lowpass", fs=RATE, output="sos")
NOTCH = scipy.signal.butter(4, 6000, "lowpass", fs=RATE, output="sos")


def power(x):
    """The level over time of x under 6 kHz (the probe out), 60 Hz smoothing."""
    return np.maximum(scipy.signal.sosfiltfilt(SMOOTH, scipy.signal.sosfiltfilt(NOTCH, x) ** 2), 1e-20)


def line(x):
    z = x * np.exp(-2j * np.pi * PROBE_HZ * np.arange(len(x)) / RATE)
    return 2 * np.abs(scipy.signal.sosfiltfilt(LINE, z.real) + 1j * scipy.signal.sosfiltfilt(LINE, z.imag))


def render(played, meta, model):
    s = meta["settings"]
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    y = subprocess.run([MODELS[model], str(THRESHOLD_DBFS[s["Sustain"]]),
                        str(RELEASE_MS[s["Attack"]]), "50"], input=(played * scale).astype(np.float32).tobytes(),
                       capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale


def main():
    takes = sorted((json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    meta, played, pedal, _ = scorecard.load(SESSION, "bypass-chords")
    h = scorecard.chain_filter(played, pedal)
    a = meta["signal_start"]
    probe_unity = np.median(line(pedal)[a + RATE:-2 * RATE] / line(played)[a + RATE:-2 * RATE])
    reference = 10 ** (meta["ref_dbfs"] / 20)  # the played level of the guitar's peak (a sine's peak)

    results = []
    for meta in takes:
        name = meta["take"]
        if meta["bypass"]:
            continue
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        a = meta["signal_start"]
        b = a + int(meta["signal"]["seconds"] * RATE)
        dry = scorecard.through(h, played)
        dry_power = power(dry)
        loud = np.zeros(len(played), bool)
        loud[a:b] = power(played)[a:b] > (reference * 10 ** (-45 / 20)) ** 2 / 2
        has_probe = meta["signal"]["type"] == "sine" or "probe" in meta["signal"]
        quiet = np.zeros(len(played), bool)
        if has_probe:
            quiet[a + RATE // 10:b - RATE // 10] = ~loud[a + RATE // 10:b - RATE // 10]
            probe_in = line(played) * probe_unity

        def gains(y):
            g = 10 * np.log10(power(y) / dry_power)
            p = 20 * np.log10(np.maximum(line(y), 1e-12) / probe_in) if has_probe else None
            return g, p

        pg, pp = gains(pedal)
        entry = {"name": name, "meta": meta, "pedal": (pg, pp), "loud": loud, "quiet": quiet, "models": {},
                 "a": a, "b": b}
        for model in MODELS:
            y = scorecard.through(h, render(played, meta, model))
            entry["models"][model] = (gains(y), y)
        entry["pedal_wave"], entry["pre"] = pedal, pre
        results.append(entry)

    main_takes = [e for e in results if e["meta"]["settings"]["Sustain"] == "12:00"
                  and e["meta"]["settings"]["Attack"] == "12:00" and e["loud"].any()]
    level = {m: np.median(np.concatenate([(e["pedal"][0] - e["models"][m][0][0])[e["loud"]] for e in main_takes]))
             for m in MODELS}
    print("the model's Level matched to the pedal's (Level 12:00): " +
          ", ".join(f"{m} {level[m]:+.2f} dB" for m in MODELS) + "\n")
    print(f"{'take':22s} {'pedal gain dB':>14s} │ " + " │ ".join(
        f"{m:>6s}: guitar mean  rms   probe mean  rms  resid" for m in MODELS))
    for e in results:
        pg, pp = e["pedal"]
        loud, quiet = e["loud"], e["quiet"]
        text = f"{e['name']:22s} {np.median(pg[loud]) if loud.any() else np.median(pp[quiet]):14.2f} │ "
        cells = []
        for m in MODELS:
            (mg, mp), y = e["models"][m]
            dg = (mg + level[m] - pg)[loud]
            dp = (mp + level[m] - pp)[quiet] if pp is not None and quiet.any() else np.array([np.nan])
            g_text = f"{dg.mean():+6.2f} {np.sqrt(np.mean(dg ** 2)):5.2f}" if loud.any() else "     —     —"
            resid = scorecard.residual_db(e["pedal_wave"][e["a"]:e["b"]], (y * 10 ** (level[m] / 20))[e["a"]:e["b"]])
            cells.append(f"{'':6s}        {g_text}       {np.nanmean(dp):+6.2f} {np.sqrt(np.nanmean(dp ** 2)):5.2f} {resid:6.1f}")
        print(text + " │ ".join(cells))
        if PLOT:
            plot(e, level)


def plot(e, level):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    a, b = e["a"], e["b"]
    t = (np.arange(a, b) - a) / RATE
    step = 48
    pg, pp = e["pedal"]
    fig, axes = plt.subplots(3, 1, figsize=(16, 10), sharex=True)
    axes[0].plot(t[::step], 20 * np.log10(np.abs(e["pedal_wave"][a:b]) + 1e-6)[::step], lw=0.4, color="0.6")
    axes[0].set_ylabel("pedal output, dBFS")
    axes[0].set_ylim(-90, -20)
    for ax, reading, mask, label in ((axes[1], 0, e["loud"], "gain on the guitar, dB"),
                                     (axes[2], 1, e["quiet"], "gain on the probe, dB")):
        ref = pg if reading == 0 else pp
        if ref is None:
            continue
        shown = np.where(mask, ref, np.nan)[a:b]
        ax.plot(t[::step], shown[::step], color="k", lw=1.2, label="pedal")
        for m, color in zip(MODELS, ("tab:blue", "tab:red")):
            g = e["models"][m][0][reading] + level[m]
            ax.plot(t[::step], np.where(mask, g, np.nan)[a:b][::step], color=color, lw=0.9, label=m)
        ax.set_ylabel(label)
        ax.grid(alpha=0.3)
        ax.legend(loc="upper right")
    axes[2].set_xlabel("s")
    axes[0].set_title(f"{e['name']}: {e['meta']['settings']}")
    out = SESSION / "fit"
    out.mkdir(exist_ok=True)
    fig.tight_layout()
    fig.savefig(out / f"gain-{e['name']}.png", dpi=80)
    plt.close(fig)


if __name__ == "__main__":
    main()
