#!/usr/bin/env python3
"""Measures the Boss GE-7 Equalizer from its three sessions (plans/ge-7.json, ge-7-level.json,
ge-7-retake.json). Findings: captures/boss/ge-7/README.md.

    analyze_ge7.py SESSION LEVEL_SESSION RETAKE_SESSION
        # writes SESSION/fit/ge-7.{json,svg}, ge-7-fit.svg

What it measures:
  tone       every sweep against its own session's bypass, per 1/12 octave (30 Hz-16 kHz),
             and one circuit fitted to all of them at once: the graphic EQ's single op-amp
             with seven slider pots between its two inputs, each wiper into a series RLC
             (a gyrator) to ground, the top band a series RC instead; then the Level stage,
             the same circuit with a resistor for the RLC, its gain rolling off at
             GBW / its noise gain
  clipping   the 1 kHz staircases: peaks and harmonics per step (the settled middle)
  noise      the silences: bypassed, flat, every slider up

SESSION's level-* takes moved the 6.4 kHz slider, not Level (the sweeps show it): they are
read as the 6.4 kHz slider at those positions. LEVEL_SESSION has the real Level takes.
SESSION's all-p15-sweep is left out: two sliders were short of their stops (RETAKE_SESSION's
two all-up takes agree with each other and sit 0.8-1 dB above it at 3.3 and 10-15 kHz).
"""

import argparse
import json
import pathlib
import sys

import numpy as np
import scipy.optimize

import analysis

RATE = 48000
BANDS = ["100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz"]
NOMINAL_HZ = [100, 200, 400, 800, 1600, 3200, 6400]
GRID_HZ = np.geomspace(30, 16000, 160)
STAIRS_DB = [-36, -30, -24, -18, -12, -6, -3, 0, 3]
GLOBALS = ["log_r", "half_cut", "half_boost", "log_level_r", "level_half", "log_level_gbw",
           "log_level_z", "offset_db", "level_offset_db", "retake_offset_db"]
PREFIXES = ["", "level/", "retake/"]  # per session, in take labels
LEFT_OUT = {(0, "all-p15-sweep")}


def label(take):
    return PREFIXES[take["session"]] + take["name"]


def rounded(values, digits=2):
    return [round(float(v), digits) for v in values]


def spectrum(session, name):
    t = analysis.Take(session, name)
    x = t.signal(1.5)
    return np.abs(np.fft.rfft(x, 2**20)), np.fft.rfftfreq(2**20, 1 / t.rate)


def response_db(session, name, bypass):
    """The take's level against the bypass take's, per 1/12 octave around each GRID_HZ."""
    x, f = spectrum(session, name)
    out = []
    for g in GRID_HZ:
        band = (f > g / 2 ** (1 / 24)) & (f < g * 2 ** (1 / 24))
        out.append(10 * np.log10(np.sum(x[band] ** 2) / np.sum(bypass[band] ** 2)))
    return np.array(out)


def sweeps(sessions):
    takes = []
    for index, s in enumerate(sessions):
        bypass, _ = spectrum(s, "bypass-sweep")
        for meta_path in sorted(s.glob("*sweep.json")):
            name = meta_path.stem
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
            if meta.get("bypass") or not meta.get("settings") or (index, name) in LEFT_OUT:
                continue
            settings = dict(meta["settings"])
            if index == 0 and name.startswith("level-"):
                settings["6.4 kHz"], settings["Level"] = settings["Level"], "0"
            takes.append({"session": index, "name": name, "settings": settings,
                          "db": response_db(s, name, bypass)})
    return takes


def slider(value, half_cut, half_boost):
    """The pot's wiper (0 = cut end, 1 = boost end) for a printed position."""
    x = {"-15": 0.0, "-7.5": 0.5 - half_cut, "0": 0.5, "+7.5": 0.5 + half_boost, "+15": 1.0}[value]
    return min(max(x, 1e-6), 1 - 1e-6)


def model(p, settings, session):
    """The pedal's response in dB on GRID_HZ; impedances are relative to the slider pot."""
    g = dict(zip(GLOBALS, p[:len(GLOBALS)]))
    s = 2j * np.pi * GRID_HZ
    y_plus = 0
    y_minus = 0
    for k, band in enumerate(BANDS):
        log_f, log_z, log_rs = p[len(GLOBALS) + 3 * k:len(GLOBALS) + 3 * k + 3]
        w0 = 2 * np.pi * np.exp(log_f)
        if k == len(BANDS) - 1:
            z = np.exp(log_rs) * (1 + w0 / s)  # series RC: log_f is its corner
        else:
            z = np.exp(log_z) * (s / w0 + w0 / s) + np.exp(log_rs)
        x = slider(settings[band], g["half_cut"], g["half_boost"])
        d = z + x * (1 - x)
        y_plus = y_plus + (1 - x) / d
        y_minus = y_minus + x / d
    r = np.exp(g["log_r"])
    eq = (1 + r * y_minus) / (1 + r * y_plus)
    x = slider(settings["Level"], g["level_half"], g["level_half"])
    d = np.exp(g["log_level_z"]) + x * (1 - x)
    noise_gain = 1 + np.exp(g["log_level_r"]) * x / d
    level = (noise_gain / (1 + np.exp(g["log_level_r"]) * (1 - x) / d)
             / (1 + noise_gain * s / np.exp(g["log_level_gbw"])))
    offset = g[["offset_db", "level_offset_db", "retake_offset_db"][session]]
    return 20 * np.log10(np.abs(eq * level)) + offset


def fit_tone(takes):
    p0 = [np.log(0.3), 0.4, 0.4, np.log(0.1), 0.45, np.log(2 * np.pi * 1e5), np.log(0.02), 0, 0, 0]
    lo = [np.log(0.005), 0.02, 0.02, np.log(0.005), 0.02, np.log(2 * np.pi * 1e4), np.log(1e-3),
          -3, -3, -3]
    hi = [np.log(20), 0.49, 0.49, np.log(20), 0.49, np.log(2 * np.pi * 1e8), np.log(10), 3, 3, 3]
    for k, f in enumerate(NOMINAL_HZ):
        top = k == len(NOMINAL_HZ) - 1
        p0 += [np.log(f * 0.6 if top else f), np.log(0.1), np.log(0.035)]
        lo += [np.log(300 if top else f / 2), np.log(0.005), np.log(1e-4)]
        hi += [np.log(1e5 if top else f * 2), np.log(20), np.log(2)]

    def residuals(p):
        return np.concatenate([model(p, t["settings"], t["session"]) - t["db"] for t in takes])

    fit = scipy.optimize.least_squares(residuals, p0, bounds=(lo, hi), x_scale="jac",
                                       max_nfev=40000)
    p = fit.x
    g = dict(zip(GLOBALS, p[:len(GLOBALS)]))
    bands = {}
    for k, band in enumerate(BANDS):
        log_f, log_z, log_rs = p[len(GLOBALS) + 3 * k:len(GLOBALS) + 3 * k + 3]
        if k == len(BANDS) - 1:
            bands[band] = {"rc_corner_hz": round(float(np.exp(log_f)), 1),
                           "rs": round(float(np.exp(log_rs)), 4)}
        else:
            bands[band] = {"f0_hz": round(float(np.exp(log_f)), 1),
                           "z0": round(float(np.exp(log_z)), 4),
                           "rs": round(float(np.exp(log_rs)), 4),
                           "q": round(float(np.exp(log_z - log_rs)), 2)}
    per_take = {label(t):
                round(float(np.sqrt(np.mean((model(p, t["settings"], t["session"]) - t["db"]) ** 2))), 2)
                for t in takes}
    return {
        "impedances": "relative to the slider pot",
        "r": round(float(np.exp(g["log_r"])), 4),
        "half_mark_wiper": {"-7.5": round(0.5 - float(g["half_cut"]), 3),
                            "+7.5": round(0.5 + float(g["half_boost"]), 3)},
        "bands": bands,
        "level": {"r": round(float(np.exp(g["log_level_r"])), 4),
                  "z": round(float(np.exp(g["log_level_z"])), 4),
                  "half_mark_wiper": round(0.5 + float(g["level_half"]), 3),
                  "gbw_hz": round(float(np.exp(g["log_level_gbw"]) / 2 / np.pi))},
        "offset_db": round(float(g["offset_db"]), 2),
        "level_session_offset_db": round(float(g["level_offset_db"]), 2),
        "retake_session_offset_db": round(float(g["retake_offset_db"]), 2),
        "rms_db": round(float(np.sqrt(np.mean(fit.fun ** 2))), 3),
        "rms_db_per_take": per_take,
        "parameters": rounded(p, 6),
    }


def stairs(session, name):
    t = analysis.Take(session, name)
    x = t.signal()
    step = int(1.5 * RATE)
    out = []
    for i in range(len(STAIRS_DB)):
        y = x[i * step + step // 3:(i + 1) * step - step // 6]
        out.append({"in_db": STAIRS_DB[i],
                    "peak_dbfs": rounded([20 * np.log10(y.max()), 20 * np.log10(-y.min())], 1),
                    "harmonics_db": rounded(analysis.harmonics_db(y, RATE, 1000, 5), 1)})
    return out


def clipping(session, level_session):
    out = {"bypass": stairs(session, "bypass-stairs"), "flat": stairs(session, "flat-stairs"),
           "all_up": stairs(session, "all-p15-stairs"),
           "all_up_level_up": stairs(level_session, "all-p15-level-p15-stairs")}
    reference = np.mean(out["bypass"][STAIRS_DB.index(0)]["peak_dbfs"])
    ceiling = np.mean([s["peak_dbfs"] for s in out["all_up"][-3:] + out["all_up_level_up"][-3:]])
    out["reference_peak_dbfs"] = round(float(reference), 1)
    out["ceiling_dbfs"] = round(float(ceiling), 1)
    out["ceiling_db_re_reference"] = round(float(ceiling - reference), 1)
    return out


def noise(session, level_session):
    out = {}
    for s, name in [(session, "bypass-silence"), (session, "flat-silence"),
                    (level_session, "all-p15-level-p15-silence")]:
        t = analysis.Take(session if s == session else level_session, name)
        y = t.signal()
        out[name] = {"rms_dbfs": round(analysis.rms_db(y), 1),
                     "third_octaves_dbfs": rounded(analysis.third_octave_db(y, RATE), 1)}
    return out


def plot(out_dir, result, takes, p):
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.hashsalt"] = "ge-7"  # the same SVG ids on every run
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 2, figsize=(14, 9.5))
    by_name = {label(t): t for t in takes}

    def show(axis, names):
        for name in names:
            t = by_name[name]
            line, = axis.semilogx(GRID_HZ, t["db"], lw=1.6, alpha=0.6, label=name)
            axis.semilogx(GRID_HZ, model(p, t["settings"], t["session"]), "--", lw=0.9,
                          color=line.get_color())

    show(axes[0, 0], [f"{tag}-p15-sweep" for tag in ["100Hz", "200Hz", "400Hz", "800Hz", "16kHz",
                                                     "32kHz", "64kHz"]]
         + [f"{tag}-m15-sweep" for tag in ["100Hz", "200Hz", "400Hz", "800Hz", "16kHz", "32kHz",
                                           "64kHz"]])
    axes[0, 0].set_title("Each slider at +15 and -15 (measured; dashed: the circuit fit)",
                         fontsize=10)
    axes[0, 0].legend(fontsize=6, ncol=2)
    show(axes[0, 1], ["retake/all-p15-sweep", "all-m15-sweep", "400-800-p15-sweep",
                      "800m15-16kp15-sweep", "800Hz-p7-sweep", "800Hz-m7-sweep",
                      "level-p7-sweep", "level-m7-sweep"])
    axes[0, 1].set_title("Bands together, and the half-way marks (level-* = the 6.4 kHz slider)",
                         fontsize=10)
    axes[0, 1].legend(fontsize=7)
    show(axes[1, 0], ["level/flat-sweep", "level/level-p15-sweep", "level/level-p7-sweep",
                      "level/level-m7-sweep", "level/level-m15-sweep"])
    axes[1, 0].set_title("Level (second session)", fontsize=10)
    axes[1, 0].legend(fontsize=7)
    for axis in (axes[0, 0], axes[0, 1], axes[1, 0]):
        axis.set_xlabel("Hz")
        axis.set_ylabel("dB re bypass")
    axis = axes[1, 1]
    c = result["clipping"]
    for key in ["bypass", "flat", "all_up", "all_up_level_up"]:
        axis.plot(STAIRS_DB, [max(s["peak_dbfs"]) for s in c[key]], "o-", label=key)
    axis.axhline(c["ceiling_dbfs"], color="k", lw=0.6)
    axis.set_xlabel("1 kHz input, dB re the guitar's peak")
    axis.set_ylabel("output peak, dBFS")
    axis.set_title("One ceiling, whatever the sliders and Level", fontsize=10)
    axis.legend(fontsize=8)
    for axis in axes.flat:
        axis.grid(alpha=0.3, which="both")
    figure.suptitle("Boss GE-7 Equalizer")
    figure.tight_layout()
    figure.savefig(out_dir / "ge-7.svg", metadata={"Date": None})
    plt.close(figure)


def plot_fit(out_dir, takes, p):
    """Every sweep against the circuit fit, one panel each, the error underneath."""
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.hashsalt"] = "ge-7"  # the same SVG ids on every run
    import matplotlib.pyplot as plt

    columns = 6
    rows = -(-len(takes) // columns)
    figure = plt.figure(figsize=(3.3 * columns, 3.4 * rows), layout="constrained")
    cells = figure.add_gridspec(rows, columns)
    order = sorted(takes, key=lambda t: (t["session"], t["name"]))
    for i, t in enumerate(order):
        row, column = divmod(i, columns)
        pair = cells[row, column].subgridspec(2, 1, height_ratios=[3, 1], hspace=0.05)
        top = figure.add_subplot(pair[0])
        bottom = figure.add_subplot(pair[1], sharex=top)
        fitted = model(p, t["settings"], t["session"])
        top.semilogx(GRID_HZ, t["db"], color="C0", lw=1.8, label="measured")
        top.semilogx(GRID_HZ, fitted, color="C3", lw=1.0, ls="--", label="model")
        middle = (max(t["db"].max(), fitted.max()) + min(t["db"].min(), fitted.min())) / 2
        low, high = top.get_ylim()
        if high - low < 4:  # nearly flat takes: a 4 dB window, not a zoom on tenths
            top.set_ylim(middle - 2, middle + 2)
        error = fitted - t["db"]
        bottom.semilogx(GRID_HZ, error, color="k", lw=0.9)
        bottom.axhline(0, color="0.6", lw=0.5)
        bottom.set_ylim(-2, 2)
        name = label(t)
        rms = float(np.sqrt(np.mean(error ** 2)))
        top.set_title(f"{name}  ({rms:.2f} dB rms)", fontsize=8)
        top.tick_params(labelbottom=False, labelsize=7)
        bottom.tick_params(labelsize=7)
        top.grid(alpha=0.3, which="both")
        bottom.grid(alpha=0.3, which="both")
        if column == 0:
            top.set_ylabel("dB re bypass", fontsize=8)
            bottom.set_ylabel("error, dB", fontsize=8)
        if i == 0:
            top.legend(fontsize=7)
    figure.suptitle("Boss GE-7: every sweep against the circuit fit "
                    "(level-* in the first session = the 6.4 kHz slider)")
    figure.savefig(out_dir / "ge-7-fit.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session")
    parser.add_argument("level_session")
    parser.add_argument("retake_session")
    args = parser.parse_args()
    sessions = [pathlib.Path(a).expanduser()
                for a in (args.session, args.level_session, args.retake_session)]
    session, level_session = sessions[:2]
    for s in sessions:
        if not (s / "rig.json").exists():
            sys.exit(f"{s} is not a capture session")
    takes = sweeps(sessions)
    tone = fit_tone(takes)
    result = {"pedal": "Boss GE-7 Equalizer", "sessions": [s.name for s in sessions],
              "tone": tone, "clipping": clipping(session, level_session),
              "noise": noise(session, level_session)}
    out_dir = session / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "ge-7.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot(out_dir, result, takes, np.array(tone["parameters"]))
    plot_fit(out_dir, takes, np.array(tone["parameters"]))
    summary = {k: v for k, v in tone.items() if k not in ("parameters", "rms_db_per_take")}
    summary["ceiling_db_re_reference"] = result["clipping"]["ceiling_db_re_reference"]
    summary["noise_rms_dbfs"] = {k: v["rms_dbfs"] for k, v in result["noise"].items()}
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
