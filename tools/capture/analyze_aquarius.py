#!/usr/bin/env python3
"""The JOYO R-07 Aquarius in Low Bit mode (session 2026-11-02-low-bit, plans/aquarius-low-bit.json).

Measures, from the session's takes:

- the dry path and the repeat from the deconvolved sweeps (delay per Time position,
  level per Level position, tone, polarity);
- the hold rate from the held sine's mirror image (input f -> f and rate - f);
- the bit depth from the ladder (the repeat's fundamental and what's left beside it,
  per level, at Level 5:00 and 12:00), the click repeats (dropped or held);
- the feedback per pass (sweeps and bursts, Feedback 9:00 to 5:00);
- the noise with the pedal on and off;

then renders the model -- pre-emphasis, a sample-and-hold at the hold rate with no
anti-alias filter, the delay loop, de-emphasis -- over the Time 5:00 sweep (the repeat on
its own) and the low-E DI (dry + repeats at Feedback 12:00), and compares.

The plan sent the start marker to the loopback only (marker_to_pedal false), so the
pedal's input is the played file with everything before the signal silenced.

Writes SESSION/fit/aquarius-low-bit.json. Usage:
    tools/capture/analyze_aquarius.py [SESSION]
"""

import argparse
import json
import pathlib

import numpy as np
import scipy.signal
import soundfile

RATE = 48000
REF_DBFS = -13.5  # the rig's guitar peak on the pedal input (tools/capture/README.md)
DEFAULT_SESSION = pathlib.Path(__file__).resolve().parents[2] / "captures/joyo/aquarius/2026-11-02-low-bit"
LEAD = 2000  # deconvolved responses are rolled by this much: index LEAD = time 0
BANDS_HZ = [100, 200, 400, 800, 1600, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 16000]
KNOB_FRACTION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
LADDER_DB = [-60, -54, -48, -42, -36, -30, -24, -18, -12, -6, 0, 3]
LADDER_PERIOD = 3.0
LADDER_HZ = 211.0

# The model (fitted by this script's model section; see captures/joyo/aquarius/README.md).
MODEL = {
    "hold_hz": 5512.677,        # 44.1 kHz / 8, the pedal's clock 32 ppm fast against the interface
    "emphasis_hz": 1200.0,      # first-order pre-emphasis zero; the de-emphasis is its inverse
    "input_lowpass_hz": 11000.0,
    "output_lowpass_hz": 13000.0,
    "converter_samples": 45,    # dry latency at 48 kHz (ADC + DAC)
}


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-12))


class Session:
    def __init__(self, path):
        self.path = pathlib.Path(path)

    def load(self, name):
        """Sidecar, what reached the pedal, the pedal output aligned to it, the loopback."""
        meta = json.loads((self.path / f"{name}.json").read_text())
        data, rate = soundfile.read(self.path / f"{name}.wav")
        assert rate == RATE
        x, _ = soundfile.read(self.path / "played" / f"{name}.wav")
        if meta.get("marker_to_pedal", True) is False:
            x = x.copy()
            x[:meta["signal_start"]] = 0.0
        latency = meta["latency_samples"]
        y = np.concatenate([data[latency:, 0], np.zeros(latency)])
        return meta, x, y

    def response(self, name, eps=1e-6):
        """The deconvolved response, rolled so index LEAD is time 0."""
        meta, x, y = self.load(name)
        X, Y = np.fft.rfft(x), np.fft.rfft(y)
        h = np.fft.irfft(Y * np.conj(X) / (np.abs(X) ** 2 + eps * np.max(np.abs(X)) ** 2), len(x))
        return meta, np.roll(h, LEAD)


def band_db(h, start, stop, n=1 << 16):
    """Third-octave levels of h[start:stop] (dB)."""
    H = np.fft.rfft(h[start:stop], n)
    f = np.fft.rfftfreq(n, 1 / RATE)
    return np.array([10 * np.log10(np.mean(np.abs(H[(f > c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))]) ** 2))
                     for c in BANDS_HZ])


def sine_fit(seg, freq):
    t = np.arange(len(seg)) / RATE
    basis = np.column_stack([np.cos(2 * np.pi * freq * t), np.sin(2 * np.pi * freq * t)])
    coef, *_ = np.linalg.lstsq(basis, seg, rcond=None)
    return np.hypot(*coef), seg - basis @ coef


def line_db(seg, freq, n=1 << 17):
    window = np.blackman(len(seg))
    spectrum = np.abs(np.fft.rfft(seg * window, n)) / (window.sum() / 2)
    f = np.fft.rfftfreq(n, 1 / RATE)
    return db(spectrum[(f > freq - 6) & (f < freq + 6)].max())


# --- the model ---------------------------------------------------------------------------

def first_order(x, corner_hz, kind):
    """A first-order zero or pole at corner_hz, applied in the frequency domain (whole takes)."""
    f = np.fft.rfftfreq(len(x), 1 / RATE)
    s = 1 + 1j * f / corner_hz
    gain = {"zero": s, "pole": 1 / s}[kind]
    return np.fft.irfft(np.fft.rfft(x) * gain, len(x))


def hold(x, phase=0.0, hold_hz=MODEL["hold_hz"], up=4):
    """Sample x at (phase + k) / hold_hz seconds with no filter in front, hold each value,
    and band-limit the steps to 21 kHz as the pedal's DAC does."""
    xu = scipy.signal.resample_poly(x, up, 1)
    rate_up = RATE * up
    k = np.floor(np.arange(len(xu)) / rate_up * hold_hz - phase)
    at = (k + phase) / hold_hz * rate_up
    i = np.clip(np.floor(at).astype(int), 0, len(xu) - 2)
    frac = at - np.floor(at)
    held = xu[i] * (1 - frac) + xu[i + 1] * frac
    fir = scipy.signal.firwin(255, 21000, fs=rate_up)
    return scipy.signal.filtfilt(fir, [1.0], held)[::up][:len(x)]


def render_wet(x, first_delay, loop, gain_per_pass, phase=0.0, passes=60):
    """The repeats alone at unit Level: emphasis, hold, the loop, de-emphasis."""
    emphasized = first_order(x, MODEL["emphasis_hz"], "zero")
    held = hold(first_order(emphasized, MODEL["input_lowpass_hz"], "pole"), phase)
    wet = np.zeros(len(x))
    for k in range(passes):
        delay = first_delay + k * loop
        whole = int(np.floor(delay))
        frac = delay - whole
        if whole + 1 >= len(x):
            break
        moved = held[:len(x) - whole - 1] * (1 - frac) + held[1:len(x) - whole] * frac
        wet[whole:whole + len(moved)] += gain_per_pass ** k * moved
    wet = first_order(wet, MODEL["emphasis_hz"], "pole")
    return first_order(wet, MODEL["output_lowpass_hz"], "pole")


def chunked_bands(x, chunk):
    """Third-octave energies (50 Hz-16 kHz) per `chunk` seconds, dB."""
    centres = 1000 * 2 ** (np.arange(-13, 13) / 3)
    f, t, Z = scipy.signal.stft(x, RATE, nperseg=4096, noverlap=3072)
    P = np.abs(Z) ** 2
    bands = np.array([P[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].sum(0) for c in centres])
    edges = np.arange(0, t[-1], chunk)
    out = np.array([bands[:, (t >= a) & (t < a + chunk)].sum(1) for a in edges]).T
    return centres, edges + chunk / 2, 10 * np.log10(out + 1e-20)


# --- the measurements --------------------------------------------------------------------

def sweeps(session, findings):
    _, bypass = session.response("bypass-sweep")
    reference = band_db(bypass, LEAD - 200, LEAD + 1000)
    print("\nSweeps (dB re bypass per band):", " ".join(f"{c:>6}" for c in BANDS_HZ))
    findings["sweeps"] = {}
    for name in ["dry-sweep", "time-0700", "time-0900", "time-1200", "time-0300", "time-0500",
                 "level-0900", "level-1200", "level-0300"]:
        meta, h = session.response(name)
        mag = np.abs(h)
        dry_at = int(np.argmax(mag[LEAD - 200:LEAD + 200])) - 200
        dry = band_db(h, LEAD - 200, LEAD + 1000) - reference
        entry = {"settings": meta["settings"], "dry_db": dry.round(2).tolist(), "dry_peak_samples": dry_at,
                 "dry_polarity": int(np.sign(h[LEAD + dry_at]))}
        print(f"{name:11} dry " + " ".join(f"{v:6.1f}" for v in dry) + f"   peak {dry_at:+d} smp")
        if meta["settings"]["Level"] != "7:00":
            later = mag.copy()
            later[:LEAD + 150] = 0
            peak = int(np.argmax(later))
            onset = int(np.argmax(mag[LEAD + 150:] > 0.5 * mag[peak])) + 150
            wet = band_db(h, peak - 200, peak + 1000) - reference
            entry.update({"wet_db": wet.round(2).tolist(), "wet_onset_samples": onset,
                          "wet_onset_ms": round(onset / RATE * 1000, 2),
                          "wet_polarity": int(np.sign(h[peak]))})
            print(f"{'':11} wet " + " ".join(f"{v:6.1f}" for v in wet)
                  + f"   onset {onset} smp = {onset / RATE * 1000:.1f} ms, polarity {np.sign(h[peak]):+.0f}")
        findings["sweeps"][name] = entry
    times = {findings["sweeps"][n]["settings"]["Time"]: findings["sweeps"][n]["wet_onset_ms"]
             for n in ["time-0700", "time-0900", "time-1200", "time-0300", "time-0500"]}
    levels = {findings["sweeps"][n]["settings"]["Level"]: round(float(np.mean(findings["sweeps"][n]["wet_db"][:2])), 2)
              for n in ["level-0900", "level-1200", "level-0300", "time-1200"]}
    findings["time_ms"] = times
    findings["level_db"] = levels
    print("Time (first repeat, ms):", times)
    print("Level (wet at 100-200 Hz, dB re bypass):", levels)


def hold_rate(session, findings, first_delay):
    meta, x, y = session.load("hold-sine")
    s = meta["signal_start"]
    seg = y[s + first_delay + 4800: s + int(5.5 * RATE)]
    t = np.arange(len(seg)) / RATE
    found = []
    for guess in [1031.0, MODEL["hold_hz"] - 1031.0]:
        grid = np.linspace(guess - 1, guess + 1, 2001)
        power = [abs(np.sum(seg * np.exp(-2j * np.pi * f * t))) for f in grid]
        found.append(grid[int(np.argmax(power))])
    rate = found[0] + found[1]
    findings["hold_hz"] = round(rate, 3)
    print(f"\nHold rate: {found[0]:.3f} + {found[1]:.3f} = {rate:.3f} Hz "
          f"(44.1 kHz / 8 = 5512.5; the pedal's clock {1e6 * (rate / 5512.5 - 1):+.0f} ppm)")


def ladder(session, findings, first_delay):
    _, _, bypass = session.load("bypass-ladder")
    findings["ladder"] = {}
    for name in ["ladder", "ladder-level1200"]:
        meta, _, y = session.load(name)
        s = meta["signal_start"]
        rows = []
        print(f"\n{name} (Level {meta['settings']['Level']}): step, repeat re bypass, "
              "everything else re the repeat, 3rd/5th harmonic re the repeat, mirror image re the repeat")
        for i, level in enumerate(LADDER_DB):
            a = s + int(i * LADDER_PERIOD * RATE) + int(0.025 * RATE)
            b = a + int(0.2 * RATE)
            amp_in, _ = sine_fit(bypass[a:b], LADDER_HZ)
            rep = y[a + first_delay:b + first_delay]
            amp, rest = sine_fit(rep, LADDER_HZ)
            fund = line_db(rep, LADDER_HZ)
            row = {"step_db": level, "repeat_db": round(db(amp) - db(amp_in), 2),
                   "rest_db": round(db(np.std(rest)) - db(amp / np.sqrt(2)), 1),
                   "h3_db": round(line_db(rep, 3 * LADDER_HZ) - fund, 1),
                   "h5_db": round(line_db(rep, 5 * LADDER_HZ) - fund, 1),
                   "image_db": round(line_db(rep, findings["hold_hz"] - LADDER_HZ) - fund, 1)}
            rows.append(row)
            print(f"  {level:+4d}  {row['repeat_db']:+6.2f}  {row['rest_db']:6.1f}  "
                  f"{row['h3_db']:6.1f} {row['h5_db']:6.1f}  {row['image_db']:6.1f}")
        findings["ladder"][name] = rows


def clicks(session, findings, first_delay):
    meta, _, y = session.load("clicks")
    s = meta["signal_start"]
    period = int(round(meta["signal"]["period"] * RATE))
    dry, rep = [], []
    for k in range(meta["signal"]["count"]):
        c = s + k * period
        dry.append(db(np.max(np.abs(y[c - 50:c + 300]))))
        rep.append(db(np.max(np.abs(y[c + first_delay - 300:c + first_delay + 600]))))
    findings["clicks"] = {"dry_peak_dbfs": np.round(dry, 1).tolist(), "repeat_peak_dbfs": np.round(rep, 1).tolist()}
    print(f"\nClicks: dry peaks {np.ptp(dry):.1f} dB apart, repeat peaks {np.ptp(rep):.1f} dB apart "
          f"(repeats {', '.join(f'{v:.1f}' for v in rep)} dBFS)")


def feedback(session, findings, first_delay):
    findings["feedback"] = {}
    print("\nFeedback per pass (dB), from the sweeps (100-2500 Hz bands) and the bursts:")
    _, bypass = session.response("bypass-sweep")
    reference = band_db(bypass, LEAD - 2000, LEAD + 2000)[:6]
    loop = None
    for name in ["feedback-0900", "feedback-1200", "feedback-0300"]:
        meta, h = session.response(name)
        levels, peaks = [], []
        for k in range(1, 6):
            c = LEAD + first_delay + (k - 1) * (first_delay - MODEL["converter_samples"])
            if c + 2000 > len(h):
                break
            levels.append(band_db(h, c - 2000, c + 2000)[:6] - reference)
            peaks.append(int(np.argmax(np.abs(h[c - 300:c + 300]))) - 300)
        steps = np.diff(np.array(levels), axis=0)
        per_pass = float(np.median(steps[:3]))
        spread = float(np.ptp(steps[:3].mean(0)))
        findings["feedback"][meta["settings"]["Feedback"]] = round(per_pass, 2)
        if name == "feedback-1200":
            loop = first_delay - MODEL["converter_samples"] + float(np.polyfit(np.arange(len(peaks)), peaks, 1)[0])
        print(f"  {meta['settings']['Feedback']:>5} (sweep): {per_pass:+.2f} dB per pass, "
              f"the bands within {spread:.1f} dB of each other")
    for name in ["feedback-0300-burst", "feedback-0300-quiet", "feedback-0500"]:
        meta, x, y = session.load(name)
        s = meta["signal_start"] + int(0.03 * RATE)
        n = int(0.14 * RATE)
        amps = []
        for k in range(12):
            a = s + first_delay + int(round(k * loop))
            if a + n > len(y):
                break
            amps.append(sine_fit(y[a:a + n], LADDER_HZ)[0])
        steps = np.diff(db(np.array(amps)))
        print(f"  {meta['settings']['Feedback']:>5} ({name}): {np.median(steps):+.2f} dB per pass over {len(amps)} passes")
        if name == "feedback-0500":
            findings["feedback"]["5:00"] = round(float(np.median(steps)), 3)
    findings["feedback"]["7:00"] = None
    findings["loop_samples"] = round(loop, 2)
    print(f"  the loop: {loop:.2f} samples = {loop / RATE * 1000:.2f} ms at Time 12:00 "
          f"(the first repeat minus the converters' {MODEL['converter_samples']} samples)")
    return loop


def noise(session, findings):
    findings["noise"] = {}
    for name in ["noise-floor", "bypass-silence"]:
        meta, _, y = session.load(name)
        s = meta["signal_start"]
        seg = y[s:s + int(meta["signal"]["seconds"] * RATE)]
        level = db(np.std(seg)) - REF_DBFS
        findings["noise"][name] = round(level, 1)
        print(f"\n{name}: {level:.1f} dB rms re the guitar's peak" if name == "noise-floor"
              else f"{name}: {level:.1f} dB rms re the guitar's peak")


def model_checks(session, findings, first_delay, loop):
    # The Time 5:00 sweep: its repeat starts after the 2 s sweep, alone.
    meta, x, y = session.load("time-0500")
    s = meta["signal_start"]
    delay = findings["sweeps"]["time-0500"]["wet_onset_samples"]
    x_in = x[s - 4800:s + int(2.1 * RATE)]
    rec = y[s + delay - 4800:s + delay + int(2.1 * RATE)]
    mod = render_wet(x_in, 0.0, 1.0, 0.0, passes=1)
    centres, t, R = chunked_bands(rec, 0.1)
    _, _, M = chunked_bands(mod, 0.1)
    early = (t > 0.15) & (t < 0.9)
    M += np.median((R - M)[np.ix_(centres < 1000, early)])
    # Cells within 30 dB of the moment's strongest band: a sweep entering or leaving a band
    # mid-chunk leaves near-empty cells whose dB swing with a millisecond of timing.
    floor = np.maximum(R.max(0), M.max(0))[None, :] - 30
    err = np.maximum(R, floor) - np.maximum(M, floor)
    shown = (R > floor) | (M > floor)
    findings["model_sweep"] = {}
    for label, (a, b) in [("below_half_rate", (0.12, 1.38)), ("aliasing", (1.38, 2.0))]:
        cells = err[shown & ((t > a) & (t < b))[None, :]]
        findings["model_sweep"][label] = {"rms_db": round(float(np.sqrt(np.mean(cells ** 2))), 2),
                                          "median_abs_db": round(float(np.median(np.abs(cells))), 2)}
    print("\nModel vs the Time 5:00 repeat (third octaves per 0.1 s, within 30 dB of the strongest band): "
          + "; ".join(f"{label.replace('_', ' ')} {v['median_abs_db']:.2f} dB median, {v['rms_db']:.2f} rms"
                      for label, v in findings["model_sweep"].items()))

    # The low-E DI at Level 5:00, Feedback 12:00, Time 12:00: dry from the measured dry response.
    _, h_dry = session.response("dry-sweep")
    meta, x, y = session.load("reamp-lowe")
    s = meta["signal_start"]
    dry = scipy.signal.fftconvolve(x, h_dry[LEAD - 200:LEAD + 1000])[200:200 + len(x)]
    rec_wet = y - dry
    gain = 10 ** (findings["feedback"]["12:00"] / 20)
    best = None
    for phase in np.linspace(0, 1, 8, endpoint=False):
        wet = render_wet(x, first_delay, loop, gain, phase)
        corr = np.dot(rec_wet[s:], wet[s:]) / np.sqrt(np.dot(wet[s:], wet[s:]) * np.dot(rec_wet[s:], rec_wet[s:]))
        if best is None or abs(corr) > abs(best[0]):
            best = (corr, phase, wet)
    corr, phase, wet = best
    scale = np.dot(rec_wet[s:], wet[s:]) / np.dot(wet[s:], wet[s:])
    rest = rec_wet[s:] - scale * wet[s:]
    f = np.fft.rfftfreq(len(rest), 1 / RATE)
    octave = lambda v, c: 10 * np.log10((np.abs(np.fft.rfft(v)) ** 2)[(f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)].sum())
    centres_oct = [100, 200, 400, 800, 1600, 3200, 6400, 12800]
    diff = [octave(scale * wet[s:], c) - octave(rec_wet[s:], c) for c in centres_oct]
    _, _, R = chunked_bands(y[s:], 0.5)
    _, _, M = chunked_bands(dry[s:] + scale * wet[s:], 0.5)
    top = R.max()
    shown = (R > top - 50) | (M > top - 50)
    whole = float(np.sqrt(np.mean((np.maximum(R, top - 50) - np.maximum(M, top - 50))[shown] ** 2)))
    findings["model_reamp"] = {"correlation": round(float(corr), 4), "residual_db": round(db(np.std(rest)) - db(np.std(rec_wet[s:])), 1),
                               "octave_diff_db": dict(zip(map(str, centres_oct), np.round(diff, 2).tolist())),
                               "whole_output_rms_db": round(whole, 2), "hold_phase": float(phase)}
    print(f"Model vs the low-E DI (Level 5:00, Feedback 12:00, Time 12:00): the repeats correlate {corr:+.4f}, "
          f"residual {findings['model_reamp']['residual_db']:.1f} dB under them; model - pedal per octave "
          + ", ".join(f"{c}: {d:+.1f}" for c, d in zip(centres_oct, diff))
          + f" dB; dry + repeats per third octave every 0.5 s: {whole:.2f} dB rms")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("session", nargs="?", default=DEFAULT_SESSION)
    args = parser.parse_args()
    session = Session(args.session)
    findings = {"model": MODEL}
    sweeps(session, findings)
    first_delay = findings["sweeps"]["time-1200"]["wet_onset_samples"]
    hold_rate(session, findings, first_delay)
    ladder(session, findings, first_delay)
    clicks(session, findings, first_delay)
    loop = feedback(session, findings, first_delay)
    noise(session, findings)
    model_checks(session, findings, first_delay, loop)
    out = session.path / "fit" / "aquarius-low-bit.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(findings, indent=2) + "\n")
    print(f"\nWrote {out}")


if __name__ == "__main__":
    main()
