#!/usr/bin/env python3
"""The RV-6 focused session against NAM-X's Reverb, on the wet alone (rv6/takes.py: slips undone,
the dry taken out, the model rendered from each take's played signal through the same chain).
A tail can't be matched sample for sample (the tanks aren't the pedal's algorithm), so per take:

  level   the wet's energy, 80 Hz-10 kHz, model - pedal, dB: from the sweep's response (the sweep
          deconvolved, both outputs) and from the response to the loudest noise burst
  tone    the same per third octave with the level taken out: its rms over the bands, and the bands
  decay   RT60 per octave band from the loudest burst's tail (Schroeder's integral, a line from
          -5 to -25 dB, the pedal's floor taken out, both sides integrated over the same
          stretch): model / pedal. (From the sweep's response it reads the same in Hall, Plate and
          Room and nonsense in Spring, whose response is a chirp.)
  start   the sweep's response over its first 400 ms, 60 Hz-12 kHz: the waveform's error re the
          pedal, dB (`scorecard.residual_db`), output A
  a/b     the two outputs' correlation over the sweep's response, pedal and model
  guitar  the scorecard (third-octave bands, 50 ms windows) over the take's guitar parts: the
          level, and the rms over the bands of each band's mean difference (window by window two
          tails of different tanks differ by 4-6 dB whatever the fit: the bands' means are what
          a wrong filter or level moves)

Shimmer shifts pitch: a sweep's response means nothing there (read its bursts and its guitar).

  rv6/focused.py measure        every take -> build-rv6/focused-SESSION.json (minutes; parallel),
                                and without the plots' envelopes -> SESSION/fit/focused.json
  rv6/focused.py floor          the pedal against itself: the first block's sweeps against the last
                                block's (the same settings, two hours apart), the same numbers
  rv6/focused.py report         the tables from the JSON

Run from the repository root, with build-rv6/render_rv6 built (the command in
tools/capture/render_rv6.cpp's header)."""
import json, multiprocessing, pathlib, sys, warnings
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scorecard, signals, takes

RATE = takes.RATE
THIRDS = scorecard.BANDS
OCTAVES = [125, 250, 500, 1000, 2000, 4000, 8000]
OUT = takes.CACHE / f"focused-{takes.SESSION.name}.json"
SIZE = 1 << 21


def db(power):
    return 10 * np.log10(power + 1e-30)


def parts_of(meta):
    """[(part spec, first sample, samples of body, samples of tail)] of a take."""
    spec = meta["signal"]
    if spec["type"] != "sequence":
        spec = {"type": "sequence", "parts": [spec]}
    out = []
    for part, at in zip(spec["parts"], signals.part_starts(spec)):
        out.append((part, meta["signal_start"] + int(round(at * RATE)), int(round(signals.body_seconds(part) * RATE)),
                    int(round(part.get("tail", signals.DEFAULT_TAIL_SECONDS) * RATE))))
    return out


def response(wet, played, start, body, tail):
    """The sweep deconvolved: the wet's impulse response, 40 Hz-16 kHz, `tail` long, and the stretch
    before time zero (noise and the sweep's distortion: the floor)."""
    n = body + tail
    p = np.fft.rfft(played[start:start + n], SIZE)
    f = np.fft.rfftfreq(SIZE, 1 / RATE)
    keep = np.clip(np.log(np.maximum(f, 1) / 30) / np.log(40 / 30), 0, 1) * np.clip(np.log(20000 / np.maximum(f, 1)) / np.log(20000 / 16000), 0, 1)
    keep = np.sin(0.5 * np.pi * keep) ** 2
    h = np.fft.irfft(np.fft.rfft(wet[start:start + n], SIZE) * np.conj(p) / (np.abs(p) ** 2 + 1e-8 * np.max(np.abs(p) ** 2)) * keep)
    return h[:tail], h[-RATE:-RATE // 5]


def third_energies(h, floor=None):
    """Energy per third octave of a response (its floor's share, scaled to its length, taken out)."""
    f = np.fft.rfftfreq(SIZE, 1 / RATE)
    spectrum = np.abs(np.fft.rfft(h, SIZE)) ** 2
    out = np.array([spectrum[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].sum() for b in THIRDS])
    if floor is not None:
        noise = np.abs(np.fft.rfft(floor, SIZE)) ** 2
        noise = np.array([noise[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].sum() for b in THIRDS])
        out = np.maximum(out - noise * len(h) / len(floor), 1e-3 * out)
    return out


def octave(x, centre):
    sos = scipy.signal.butter(3, [centre / 2 ** 0.5, centre * 2 ** 0.5], "bandpass", fs=RATE, output="sos")
    return scipy.signal.sosfilt(sos, x)


def decay_times(pedal, model, floor_signal, start, length):
    """RT60 per octave of the pedal's and the model's wet from `start` on (a burst has just ended):
    Schroeder's integral over the stretch the pedal stays 8 dB over its floor."""
    out = {}
    for centre in OCTAVES:
        floor = np.mean(octave(floor_signal - floor_signal.mean(), centre)[RATE // 10:] ** 2)
        p = octave(pedal[start - RATE // 2:start + length], centre)[RATE // 2:] ** 2
        m = octave(model[start - RATE // 2:start + length], centre)[RATE // 2:] ** 2
        window = RATE // 20
        level = np.convolve(p, np.ones(window) / window, "same")
        under = np.nonzero(level[window:] < floor * 10 ** 0.8)[0]
        end = (under[0] + window) if len(under) else length
        rts = []
        for energy, own_floor in ((p, floor), (m, 0.0)):
            curve = np.cumsum((energy[:end] - own_floor)[::-1])[::-1]
            curve = db(np.maximum(curve, 1e-30) / max(curve[0], 1e-30))
            first, last = np.argmax(curve < -5), np.argmax(curve < -25)
            if last <= first or last == 0:
                rts.append(None)
                continue
            slope = np.polyfit(np.arange(first, last) / RATE, curve[first:last], 1)[0]
            rts.append(float(-60 / slope))
        out[str(centre)] = rts
    return out


ENVELOPE_BANDS = [250, 1000, 4000]
ENVELOPE_STEP = RATE // 50        # 20 ms


def envelopes(x, start, length):
    """The level of three octave bands in 20 ms steps from `start` on, dB: {band: [...]}."""
    out = {}
    for centre in ENVELOPE_BANDS:
        y = octave(x[start - RATE // 2:start + length], centre)[RATE // 2:]
        n = len(y) // ENVELOPE_STEP
        out[str(centre)] = np.round(db(np.mean(y[:n * ENVELOPE_STEP].reshape(n, ENVELOPE_STEP) ** 2, axis=1)), 1).tolist()
    return out


def burst_energy(x, start, length):
    """Energy per third octave of the wet from a burst's start over `length`."""
    f = np.fft.rfftfreq(SIZE, 1 / RATE)
    spectrum = np.abs(np.fft.rfft(x[start:start + length], SIZE)) ** 2
    return np.array([spectrum[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].sum() for b in THIRDS])


def correlation(a, b):
    return float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30))


def compare(meta, played, pedal, model, pre):
    """The numbers for one take: pedal and model each (A, B) wet signals."""
    out = {"settings": meta.get("settings", {}), "slips": meta["slips"]}
    guitar = []
    for part, start, body, tail in parts_of(meta):
        if part["type"] == "sweep" and tail >= RATE:
            sides = {}
            for side, (a, b) in (("pedal", pedal), ("model", model)):
                ha, floor_a = response(a, played, start, body, tail)
                hb, floor_b = response(b, played, start, body, tail)
                sides[side] = (ha, hb, third_energies(ha, floor_a), third_energies(hb, floor_b))
            ea_p, ea_m = sides["pedal"][2], sides["model"][2]
            level = db(ea_m.sum()) - db(ea_p.sum())
            tone = db(ea_m) - db(ea_p) - level
            early = slice(0, min(int(0.4 * RATE), tail))
            band = scipy.signal.butter(4, [60, 12000], "bandpass", fs=RATE, output="sos")
            out["sweep"] = {
                "level_a": float(level), "level_b": float(db(sides["model"][3].sum()) - db(sides["pedal"][3].sum())),
                "tone": tone.tolist(), "tone_rms": float(np.sqrt(np.mean(tone ** 2))),
                "pedal_thirds_db": db(ea_p).tolist(), "model_thirds_db": db(ea_m).tolist(),
                "start_residual": scorecard.residual_db(scipy.signal.sosfilt(band, sides["pedal"][0])[early],
                                                        scipy.signal.sosfilt(band, sides["model"][0])[early]),
                "ab_pedal": correlation(sides["pedal"][0], sides["pedal"][1]),
                "ab_model": correlation(sides["model"][0], sides["model"][1]),
            }
        elif part["type"] == "noise-steps":
            step, gap = int(part["step_seconds"] * RATE), int(part.get("gap_seconds", 0) * RATE)
            count = len(part["levels_db"])
            loudest = int(np.argmax(part["levels_db"]))
            at = start + loudest * (step + gap)
            length = gap + (tail if loudest == count - 1 else 0)
            ep, em = burst_energy(pedal[0], at, length), burst_energy(model[0], at, length)
            level = db(em.sum()) - db(ep.sum())
            tone = db(em) - db(ep) - level
            burst_power = np.mean(played[at:at + step] ** 2)
            out["burst"] = {
                "level": float(level), "tone": tone.tolist(), "tone_rms": float(np.sqrt(np.mean(tone ** 2))),
                "pedal_thirds_db": db(ep).tolist(), "model_thirds_db": db(em).tolist(),
                "decay": decay_times(pedal[0], model[0], pre, at + step, length - step),
                "envelope_pedal": envelopes(pedal[0], at, length), "envelope_model": envelopes(model[0], at, length),
                # The wet's energy over the first second re the burst's own, per burst: is it linear?
                "ladder": [float(db(np.sum(pedal[0][start + k * (step + gap) + step:start + k * (step + gap) + step + min(RATE, max(gap, tail) - step)] ** 2))
                                 - db(np.sum(played[start + k * (step + gap):start + k * (step + gap) + step] ** 2)))
                           for k in range(count)],
                "levels_db": part["levels_db"],
            }
        elif part["type"] == "file":
            guitar.append((start, body + tail))
    if guitar:
        scores = []
        for start, length in guitar:
            lo = max(start - RATE, 0)
            s = scorecard.score(pedal[0][lo:start + length], model[0][lo:start + length], pre, start - lo, time_invariant=False)
            scores.append(s)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            tone = np.nanmean([s["tone"] for s in scores], axis=0)
        out["guitar"] = {"rms": float(np.mean([s["rms"] for s in scores])), "level": float(np.mean([s["level"] for s in scores])),
                         "tone": [None if np.isnan(t) else float(t) for t in tone],
                         "tone_rms": float(np.sqrt(np.nanmean((tone - np.nanmean(tone)) ** 2))),
                         "parts": [[s["rms"], s["level"]] for s in scores]}
    return out


def measure_one(name):
    meta, played, a, b, pre = takes.load(name)
    if not meta.get("settings") or meta["settings"]["E.Level"] == "7:00":
        return name, None
    return name, compare(meta, played, takes.wet(name), takes.model_wet(name), pre)


def floor_one(mode):
    """A noon take against the same setting two hours later: the pedal's own difference."""
    first, again = f"noon-{mode}", f"again-{mode}"
    meta, played, a, b, pre = takes.load(again)
    # The first take's wet laid over the last one's: their sweeps start the signal alike.
    w1, w2 = takes.wet(first), takes.wet(again)
    n = min(len(w1[0]), len(w2[0]))
    meta = dict(meta, signal={"type": "sequence", "parts": [meta["signal"]["parts"][0]]})
    return mode, compare(meta, played[:n], (w2[0][:n], w2[1][:n]), (w1[0][:n], w1[1][:n]), pre)


def cell(value, form="{:+5.1f}"):
    return "    -" if value is None or (isinstance(value, float) and np.isnan(value)) else form.format(value)


def report(results):
    order = {mode: i for i, mode in enumerate(takes.MODES)}
    rows = sorted(((n, r) for n, r in results.items() if r), key=lambda nr: (order[nr[1]["settings"]["Mode"]], nr[0]))
    print(f"{'take':30s} {'E.Lvl':>5} {'Tone':>5} {'Time':>5} | level: sweep burst | tone rms: sweep burst | RT model/pedal % at 250 500 1k 2k 4k | start  a/b ped mod | guitar: tone rms, level")
    last = None
    for name, r in rows:
        s = r["settings"]
        if s["Mode"] != last:
            print(f"--- {s['Mode']}")
            last = s["Mode"]
        sweep, burst, guitar = r.get("sweep"), r.get("burst"), r.get("guitar")
        ratios = []
        for centre in ("250", "500", "1000", "2000", "4000"):
            p, m = burst["decay"][centre] if burst else (None, None)
            ratios.append(cell(100 * m / p - 100 if p and m else None, "{:+5.0f}"))
        print(f"{name:30s} {s['E.Level']:>5} {s['Tone']:>5} {s['Time']:>5} |       "
              f"{cell(sweep and sweep['level_a'])} {cell(burst and burst['level'])} |          "
              f"{cell(sweep and sweep['tone_rms'], '{:5.1f}')} {cell(burst and burst['tone_rms'], '{:5.1f}')} |          "
              f"{' '.join(ratios)}       | {cell(sweep and sweep['start_residual'])}  "
              f"{cell(sweep and sweep['ab_pedal'], '{:+5.2f}')} {cell(sweep and sweep['ab_model'], '{:+5.2f}')} |   "
              f"{cell(guitar and guitar['tone_rms'], '{:5.1f}')} {cell(guitar and guitar['level'])}")


def save_summary(results):
    """The numbers without the envelopes (the plots' data), for the repository: SESSION/fit/focused.json."""
    def trimmed(value):
        if isinstance(value, dict):
            return {k: trimmed(v) for k, v in value.items() if not k.startswith("envelope")}
        return round(value, 3) if isinstance(value, float) else value
    fit = takes.SESSION / "fit"
    fit.mkdir(exist_ok=True)
    lines = [f" {json.dumps(n)}: {json.dumps(trimmed(r))}" for n, r in sorted(results.items()) if r]  # a take per line
    (fit / "focused.json").write_text("{\n" + ",\n".join(lines) + "\n}\n")


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "report"
    if command == "measure":
        takes.all_lags()
        takes.dry_filters()
        with multiprocessing.Pool(8) as pool:
            results = dict(pool.imap_unordered(measure_one, takes.names()))
        OUT.write_text(json.dumps(results, indent=1))
        save_summary(results)
        report(results)
    elif command == "floor":
        results = dict(floor_one(mode.lower()) for mode in takes.MODES)
        for mode, r in results.items():
            r["settings"] = dict(r["settings"], Mode=mode.capitalize())
        report({f"noon vs again: {mode}": r for mode, r in results.items()})
    else:
        report(json.loads(OUT.read_text()))


if __name__ == "__main__":
    main()
