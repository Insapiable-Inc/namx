#!/usr/bin/env python3
"""Check the offline Shimmer prototype against the pedal and the current C++ renderer.

Use after shimmer_prototype.py fit. The reference field for both models comes from the pedal.
All guitar recordings and the November 1 pitches are held out of the fit.
"""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from scipy import signal
import soundfile

import focused
import heard
import shimmer_prototype as model
import shimmer_structure as study
import takes

RATE = takes.RATE
LABELS = ("pedal", "current", "prototype")
SPECIAL = ("time-0700", "noon-guitar")


def db_ratio(a, b):
    return 10*np.log10(np.maximum(a, 1e-30)/np.maximum(b, 1e-30))


def knob_values(meta):
    return [takes.clock_fraction(meta["settings"][k]) for k in ("Time", "Tone", "E.Level")]


def synth_region(x, start, stop, knobs, bank, phase=0.):
    # Longest possible memory is under 13 s, including the octave reads and stereo response.
    at = max(0, start-14*RATE)
    y = model.render_wet(x[at:stop], *knobs, bank, phase + study.CLOCK*at/RATE)
    return y[start-at:]


def references(position, bank):
    name = ("noon" if position == "noon-guitar" else position) + "-shimmer"
    meta, x, *_ = takes.load(name)
    pedal = study.excerpts(meta, takes.wet(name))
    current = study.excerpts(meta, takes.model_wet(name))
    part, start, body, tail = next(r for r in focused.parts_of(meta) if r[0]["type"] == "sweep")
    fed = x.copy()
    fed[:meta["signal_start"]] = 0.
    generated = synth_region(fed, start, start+min(body+tail, 18*RATE), knob_values(meta), bank)
    return [study.tapered(y) for y in (pedal["sweep"], current["sweep"], generated, pedal["burst"])]


def stereo_row(y, own, pedal_sweep, pedal_burst, off):
    s = study.spectrum(y)
    return {
        "whole_vs_pedal_sweep_pct": study.stereo_error(study.spectrum(pedal_sweep), s),
        "release_vs_own_pct": study.release_test(own, y, off)["window_error_pct"][0],
        "release_vs_pedal_sweep_pct": study.release_test(pedal_sweep, y, off)["window_error_pct"][0],
        "release_vs_pedal_burst_pct": study.release_test(pedal_burst, y, off)["window_error_pct"][0],
        "tail_envelope_on_clock_pct": study.guitar_ripple(y, off),
    }


def listen(folder, name, clips, pre, off):
    out = folder / "listening"
    out.mkdir(exist_ok=True)
    gain = 10**(-1/20) / max(np.max(abs(c)) for c in clips)
    for label, c in zip(LABELS, clips):
        soundfile.write(out / f"{name}-{label}.wav", c*gain, RATE, subtype="PCM_24")
    if name == "time-0700-shimmer-stabs":
        # The same gain across the three copies. Last attack, then 2.4 s of release.
        sl = slice(pre+off-round(.4*RATE), pre+off+round(2.4*RATE))
        excerpt = [c[sl] for c in clips]
        gap = np.zeros((RATE//3, 2))
        audio = np.concatenate([excerpt[0], gap, excerpt[1], gap, excerpt[2]])
        soundfile.write(out/"short-release-pedal-current-prototype.wav", audio*gain, RATE, subtype="PCM_24")


def guitar(position, bank, output, write_audio):
    name = position+"-shimmer"
    refs = references(position, bank) if position in SPECIAL else None
    meta, x, _, _, noise = takes.load(name)
    knobs = knob_values(meta)
    dry = np.column_stack(takes.dry_of(name))
    pedal = np.column_stack(takes.wet(name))
    current = np.column_stack(takes.model_wet(name))
    floor = np.array([np.mean(focused.octave(noise-noise.mean(), f)[4800:]**2) for f in heard.BANDS])
    fed = x.copy()
    fed[:meta["signal_start"]] = 0.
    report = {}
    for spec, start, body, tail in focused.parts_of(meta):
        if spec["type"] != "file":
            continue
        part = Path(spec["path"]).stem
        pre = RATE//2
        left, right = start-pre, min(len(x), start+body+tail)
        trial = synth_region(fed, left, right, knobs, bank)
        wets = [pedal[left:right], current[left:right], trial]
        levels = np.array([[heard.levels(y.T, pre, len(y)-pre, f).mean() for f in heard.BANDS] for y in wets])
        rows = report[part] = {}
        for i, (label, y) in enumerate(zip(LABELS, wets)):
            row = rows[label] = {}
            error = db_ratio(levels[i]+(floor if i else 0.), levels[0])
            row["band_error_dB"] = error.tolist()
            row["band_rms_dB"] = float(np.mean(error**2)**.5)
        if refs is not None and part == "stabs":
            releases = []
            band = signal.butter(4, [1000, 4000], fs=RATE, btype="bandpass", output="sos")
            for i, (label, y) in enumerate(zip(LABELS, wets)):
                v = y[pre:pre+study.FFT_SIZE]
                rows[label].update(stereo_row(v, refs[i], refs[0], refs[3], body))
                filtered = signal.sosfilt(band, v, axis=0)
                intervals = ((.8, 1.6), (1.2, 1.6), (1.6, 2.2))
                releases.append([np.mean(filtered[body+round(lo*RATE):body+round(hi*RATE)]**2) for lo, hi in intervals])
                errors = db_ratio(releases[-1], releases[0])
                rows[label]["release_level_error_dB"] = float(errors[0])
                rows[label]["later_level_error_dB_1p2_1p6_and_1p6_2p2"] = errors[1:].tolist()
            # Other free-running phase starts. One sample origin per take for every metric.
            for phase in (.25, .5, .75):
                y = synth_region(fed, start, min(right, start+study.FFT_SIZE), knobs, bank, phase)
                other = stereo_row(y, refs[2], refs[0], refs[3], body)
                rows["prototype"].setdefault("other_phases", {})[str(phase)] = other
        if write_audio:
            listen(output, f"{name}-{part}", [dry[left:right]+v for v in wets], pre, body)
    takes.load.cache_clear()
    return report


def note_power(y, frequency, seconds):
    # Integrate the resolved octave cluster over the last 3 seconds of sustain.
    y = y[round((seconds-3.2)*RATE):round((seconds-.2)*RATE)]
    hz = np.fft.rfftfreq(len(y), 1/RATE)
    p = (abs(np.fft.rfft(y*np.hanning(len(y))[:, None], axis=0))**2).sum(axis=1)
    return np.array([p[abs(hz-frequency*k) < min(36., frequency*.32)].sum() for k in (1, 2, 4, 8)])


def earlier(bank):
    path = Path("captures/boss/rv-6/2026-11-01-shimmer")
    session = study.analyze_rv6.Session(path, path)
    rows = {}
    for hz in ("0082", "0165", "0330", "0659", "1319", "2637"):
        name = "shimmer-tone-"+hz
        meta = session.meta[name]
        freq, sec = meta["signal"]["freq"], meta["signal"]["seconds"]
        x = session.load(name)[0][meta["signal_start"]:][:round((sec+1)*RATE)]
        pedal = np.column_stack([session.wet_signal(name, c)[:len(x)] for c in (0, 2)])
        current = subprocess.run([takes.RENDER, "5", ".5", ".5", "1"], input=x.astype("f").tobytes(),
                                 capture_output=True, check=True).stdout
        raw = np.frombuffer(current, np.float32).reshape(-1, 2) - x[:, None]
        ha, hb, _ = takes.dry_filters()
        current = np.column_stack([takes.through(h, raw[:, i]) for i, h in enumerate((ha, hb))])
        trial = model.render_wet(x, .5, .5, 1., bank)
        values = np.array([note_power(y, freq, sec) for y in (pedal, current, trial)])
        rel = db_ratio(values[:, 1:], values[:, :1])
        rows[hz] = {"pedal_octaves_re_fundamental_dB": rel[0].tolist(),
                    "under_10k_and_above_minus35dB": ((freq*np.array([2,4,8]) < 10000) & (rel[0] > -35)).tolist(),
                    **{label+"_error_dB": (rel[i]-rel[0]).tolist() for i, label in [(1,"current"), (2,"prototype")]}}
    return rows


def grain_evidence():
    position = "tone-0500-time-0700"
    _, wet, _ = model.probes(position)["sine"]
    pitch = 329.63
    lines, every_fifth = model.note_lines(wet, pitch, 2)
    grid = round(pitch/study.CLOCK) + np.arange(-27, 28)
    bins = grid[every_fifth]
    hz = pitch + study.CLOCK*bins
    # Remove the unshifted, independently swept stereo response at each generated frequency.
    h = model.seed_filter(model.DARK[0], position, .75, 5)
    response = np.exp(-2j*np.pi*np.outer(hz, np.arange(len(h))/RATE)) @ h
    observed = abs(np.sum(lines[every_fifth]*response.conj(), axis=1)/np.sum(abs(response)**2, axis=1))
    detuning = pitch*model.HOP - bins/5
    peak = int(np.argmin(abs(detuning)))
    models = {"pedal": observed, "triangle": np.sinc(detuning/2)**2,
              "locked_two_head_Hann": abs(np.sinc(2*detuning) +
                     .5*(np.sinc(2*detuning-1) + np.sinc(2*detuning+1)))}
    return {k: [float((v[peak-j]+v[peak+j])/(2*v[peak])) for j in (1,2,3,4)]
            for k,v in models.items()}


def held_levels(bank):
    result = {}
    for name in ("time-0700", "noon", "time-0500", "tone-0500"):
        x, wet, _ = model.probes(name)["sine"]
        trial = model.render_wet(x[:6*RATE], *model.POSITIONS[name], .5, bank)
        result[name] = db_ratio(note_power(trial, 329.63, 5), note_power(wet, 329.63, 5)).tolist()
    return result


def bursts(bank):
    results = {}
    for pos in ("time-0700", "noon", "time-0500"):
        name = pos+"-shimmer"
        meta, x, *_ = takes.load(name)
        pedal = study.excerpts(meta, takes.wet(name))["burst"]
        current = study.excerpts(meta, takes.model_wet(name))["burst"]
        spec, start, *_ = next(v for v in focused.parts_of(meta) if v[0]["type"]=="noise-steps")
        start += spec["levels_db"].index(-12)*round(RATE*(spec["step_seconds"]+spec.get("gap_seconds",0)))
        fed = x.copy()
        fed[:meta["signal_start"]] = 0.
        trial = synth_region(fed, start, start+len(pedal), knob_values(meta), bank)
        rows = results[pos] = {}
        for label, wet in zip(LABELS, (pedal,current,trial)):
            row = {}
            for f in (1000,4000,8000):
                filtered = np.column_stack([focused.octave(v, f) for v in wet.T])
                step = RATE//20
                power = np.mean(filtered[:2*RATE].reshape(-1,step,2)**2, axis=(1,2))
                row[str(f)] = float((np.argmax(power)+.5)*step/RATE)
            rows[label] = row
        takes.load.cache_clear()
    return results


def publish_results(bank, output, destination):
    import matplotlib.pyplot as plt

    destination.mkdir(parents=True, exist_ok=True)
    results = {p.name.removesuffix("-scores.json"): json.loads(p.read_text())
               for p in sorted(output.glob("*-scores.json"))}
    measurements = json.loads((output/"grain-and-bursts.json").read_text())
    report = {"guitar": results, **measurements, "independent_pitches": json.loads((output/"older-notes.json").read_text())}
    (destination/"shimmer-prototype.json").write_text(json.dumps(report, indent=2)+"\n")

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), layout="constrained")
    colours = ("#182b39", "#bd5920", "#137c78")
    names = ("Pedal", "Current NAM-X", "Prototype")
    for key, name, colour in zip(("pedal", "locked_two_head_Hann", "triangle"),
                                  ("Pedal", "Two-head Hann, clock locked", "Single triangle"), colours):
        values = measurements["sideband_amplitudes"][key]
        axes[0].semilogy(range(1,5), values, 'o-', label=name, color=colour, lw=1.7)
    axes[0].set(xticks=[1,2,3,4], xlabel="Sideband order on either side of the octave",
                ylabel="Amplitude relative to the octave centre", title="Held E4, maximum Tone, minimum Time")

    meta, x, *_ = takes.load("time-0700-shimmer")
    _, start, body, _ = next(r for r in focused.parts_of(meta) if r[0]["type"]=="file")
    stop = start+body+round(2.4*RATE)
    fed = x.copy()
    fed[:meta["signal_start"]] = 0.
    clips = [np.column_stack(takes.wet("time-0700-shimmer"))[start:stop],
             np.column_stack(takes.model_wet("time-0700-shimmer"))[start:stop],
             synth_region(fed, start, stop, knob_values(meta), bank)]
    band = signal.butter(4, [1000,4000], fs=RATE, btype="bandpass", output="sos")
    ref = None
    for wet, colour, name in zip(clips, colours, names):
        step = RATE//20
        y = signal.sosfilt(band, wet, axis=0)
        power = np.mean(y[:len(y)//step*step].reshape(-1,step,2)**2, axis=(1,2))
        if ref is None:
            ref = power.max()
        t = (np.arange(len(power))+.5)*step/RATE-body/RATE
        axes[1].plot(t, db_ratio(power, ref), label=name, color=colour, lw=1.5)
    axes[1].axvspan(.8, 1.6, color="#c7d5db", alpha=.4)
    axes[1].set(xlim=(-.35, 2.2), ylim=(-65,5), xlabel="Seconds from the end of the short-Time stabs",
                ylabel="1–4 kHz wet level (dB re pedal peak)", title="Original guitar release, both outputs")
    for ax in axes:
        ax.grid(alpha=.22)
        ax.legend(frameon=False, fontsize=8)
    fig.savefig(destination/"shimmer-prototype.png", dpi=150)
    plt.close(fig)
    takes.load.cache_clear()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bank", type=Path, default=model.CACHE)
    p.add_argument("--output", type=Path, default=model.CACHE)
    p.add_argument("--current", default="build-rv6/render_rv6")
    p.add_argument("--audio", action="store_true")
    p.add_argument("--earlier", action="store_true")
    p.add_argument("--publish", type=Path, help="assemble the saved measurements and figure in this directory")
    p.add_argument("positions", nargs="*")
    args = p.parse_args()
    takes.RENDER = str(args.current)
    args.output.mkdir(parents=True, exist_ok=True)
    default_positions = [*SPECIAL, "check-b", "check-a", "again", *(p for p in model.POSITIONS if p not in ("time-0700","noon"))]
    for pos in args.positions or ([] if args.publish else default_positions):
        result = guitar(pos, args.bank, args.output, args.audio)
        (args.output/(pos+"-scores.json")).write_text(json.dumps(result, indent=2)+"\n")
        print(pos, {k: [round(v[label]["band_rms_dB"], 2) for label in LABELS[1:]] for k,v in result.items()}, flush=True)
    if args.earlier:
        (args.output/"older-notes.json").write_text(json.dumps(earlier(args.bank), indent=2)+"\n")
        (args.output/"grain-and-bursts.json").write_text(json.dumps(
            {"sideband_amplitudes": grain_evidence(), "burst_peak_seconds": bursts(args.bank),
             "focused_330_fundamental_2x_4x_8x_error_dB": held_levels(args.bank)}, indent=2)+"\n")
    if args.publish:
        publish_results(args.bank, args.output, args.publish)


if __name__ == "__main__":
    main()
