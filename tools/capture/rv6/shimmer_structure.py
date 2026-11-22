#!/usr/bin/env python3
"""Locate the RV-6 Shimmer's time variation using the two wet outputs of a mono reamp.

Run from the repository root. See the Shimmer routing study in captures/boss/rv-6/README.md.
The optional second renderer is the clock-only control described there. All scores use the
original recordings and full-take renders. No pedal or engine parameters are fitted to guitar.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from scipy import fft, optimize, signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analyze_rv6
import focused
import takes

RATE = takes.RATE
BASE = Path("captures/boss/rv-6")
CLOCK = 1.345816  # Hz on the capture clock; checked against a separate note and session
ENV_RATE = 200
FFT_SIZE = 1 << 20
BAND = slice(int(np.ceil(200 * FFT_SIZE / RATE)), int(6000 * FFT_SIZE / RATE) + 1)
POSITIONS = [
    "tone-0700", "noon", "tone-0500", "tone-0700-time-0700", "time-0700", "tone-0500-time-0700",
    "tone-0700-time-0500", "time-0500", "tone-0500-time-0500", "time-0300", "noon-guitar",
]
COMPARE = {"tone-0700", "noon", "tone-0500", "time-0700", "time-0300", "noon-guitar"}


def excerpts(meta, pair, burst_level=-12):
    result = {}
    for part, start, body, tail in focused.parts_of(meta):
        kind = part["type"]
        if kind == "noise-steps":
            index = part["levels_db"].index(burst_level)
            spacing = round((part["step_seconds"] + part.get("gap_seconds", 0)) * RATE)
            start += index * spacing
            length = min(6 * RATE, spacing + (tail if index + 1 == len(part["levels_db"]) else 0))
            kind = "burst"
        elif kind == "sweep":
            length = min(body + tail, 18 * RATE)
        elif kind == "sine":
            length = body + min(RATE, tail)
        elif kind == "file" and "stabs" in part["path"]:
            kind = "stabs"
            length = min(body + tail, FFT_SIZE)
            result["note_off"] = body
        else:
            continue
        result[kind] = np.column_stack([x[start:start + length] for x in pair])
    return result


def tapered(x):
    y = x.copy()
    n = RATE // 5
    y[-n:] *= (0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n)))[:, None]
    return y


def stereo_error(reference, test):
    """Fraction of test energy perpendicular to the sweep's complex stereo vector, per bin."""
    power = np.sum(abs(reference) ** 2, axis=1)
    cross = reference[:, 0] * test[:, 1] - reference[:, 1] * test[:, 0]
    return float(100 * np.sum(abs(cross) ** 2 / np.maximum(power, 1e-30)) / np.sum(abs(test) ** 2))


def spatial_scores(reference, test):
    ref_a, ref_b = reference.T
    a, b = test.T
    # A genuine one-way prediction from A only. Regularization limits gain in A's deep nulls.
    reg = 0.001 * (abs(ref_a) ** 2 + abs(ref_b) ** 2)
    transfer = ref_b * ref_a.conj() / np.maximum(abs(ref_a) ** 2 + reg, 1e-30)
    detuned = np.roll(reference, round(30 * FFT_SIZE / RATE), axis=0)
    return {
        "outside_pct": stereo_error(reference, test),
        "A_to_B_error_dB": float(10 * np.log10(np.sum(abs(transfer * a - b) ** 2) / np.sum(abs(b) ** 2))),
        "shifted_reference_pct": stereo_error(detuned, test),
    }


def spectrum(x):
    return fft.rfft(tapered(x), FFT_SIZE, axis=0)[BAND].copy()


def ensemble_check():
    """Learn a single stereo direction at each frequency from seven excitations, then freeze it."""
    training = {"noon-shimmer": ("sweep", -12, 0), "again-shimmer": ("sweep",),
                "level-0300-shimmer": (-12, 0), "level-0500-shimmer": (0,)}
    covariance = np.zeros((BAND.stop - BAND.start, 2, 2), dtype=complex)
    for name, probes in training.items():
        meta = takes.load(name)[0]
        wet = takes.wet(name)
        for probe in probes:
            if probe == "sweep":
                section = excerpts(meta, wet)["sweep"]
            else:
                section = excerpts(meta, wet, burst_level=probe)["burst"]
            s = spectrum(section)
            covariance += (s[:, :, None] * s[:, None, :].conj()) / np.mean(abs(s)**2)
        takes.load.cache_clear()
    eigenvalues, vectors = np.linalg.eigh(covariance)
    direction = vectors[:, :, -1]
    scores = {"minimum_training_pct": float(100 * eigenvalues[:, 0].sum() / eigenvalues.sum())}
    for name, probe in [("noon-guitar-shimmer", "stabs"), ("again-shimmer", "stabs"),
                        ("level-0500-shimmer", "stabs"), ("level-0900-shimmer", "burst")]:
        test = excerpts(takes.load(name)[0], takes.wet(name))[probe]
        scores[name + "/" + probe] = stereo_error(direction, spectrum(test))
        takes.load.cache_clear()
    return scores


def envelope(wet, hz):
    # Keep complex phase. Comparing only magnitudes would allow an incorrectly tuned shifter to pass.
    t = np.arange(len(wet)) / RATE
    baseband = signal.resample_poly(wet * np.exp(-2j * np.pi * hz * t)[:, None], 1, RATE // ENV_RATE, axis=0)
    return signal.sosfiltfilt(signal.butter(5, 40, fs=ENV_RATE, output="sos"), baseband, axis=0)


def cycles(z, seconds, clock):
    end = int((seconds - 0.15) * ENV_RATE)
    at = np.arange(int(max(1.4, seconds - 2.8) * ENV_RATE), end - int(np.ceil(ENV_RATE / clock)))
    other = at + ENV_RATE / clock
    i = other.astype(int)
    w = (other - i)[:, None]
    return z[at], z[i] * (1 - w) + z[i + 1] * w


def estimate_clock(wet, pitch, seconds):
    # First estimate the repeat period from shape alone, then use the phase advance of the 4x line.
    z = envelope(wet, 4 * pitch)

    def loss(clock):
        a, b = cycles(z, seconds, clock)
        a = a - a.mean(axis=0)
        b = b - b.mean(axis=0)
        scale = np.sqrt(np.sum(abs(a)**2, axis=0) * np.sum(abs(b)**2, axis=0))
        coherence = abs(np.sum(a.conj() * b, axis=0)) / scale
        return -coherence.mean()

    rough = optimize.minimize_scalar(loss, bounds=(1.32, 1.38), method="bounded").x
    a, b = cycles(z, seconds, rough)
    offset = np.angle(np.sum(a.conj() * b, axis=0)) * rough / (2 * np.pi)
    lines = np.rint((3 * pitch + offset) / rough)
    return ((3 * pitch + offset) / lines).tolist()


def cycle_error(wet, pitch, order, seconds):
    a, b = cycles(envelope(wet, order * pitch), seconds, CLOCK)
    expected = a * np.exp(-2j * np.pi * (order - 1) * pitch / CLOCK)
    return (100 * np.sum(abs(b - expected)**2, axis=0) / np.sum(abs(b)**2, axis=0)).tolist()


def guitar_ripple(wet, note_off, after=0.3):
    """Periodic fraction of the detrended 1–4 kHz tail envelope in the handoff's four-second window."""
    at = note_off + round(after * RATE)
    segment = wet[at:at + 4 * RATE]
    band = signal.butter(4, [1000, 4000], fs=RATE, btype="bandpass", output="sos")
    amp = abs(signal.hilbert(signal.sosfiltfilt(band, segment, axis=0), axis=0))
    amp = signal.resample_poly(amp, 1, RATE // 100, axis=0)
    t = np.arange(len(amp))
    trend = np.stack([np.polyval(np.polyfit(t, np.log(v + 1e-12), 2), t) for v in amp.T], axis=1)
    ripple = (amp / np.exp(trend) - 1) * np.hanning(len(t))[:, None]
    nfft = 8 * len(t)
    f = fft.rfftfreq(nfft, 0.01)
    power = abs(fft.rfft(ripple, n=nfft, axis=0)) ** 2
    band = (f > 0.7) & (f < 20)
    on_clock = abs(f - np.round(f / CLOCK) * CLOCK) < 0.12
    return (100 * power[band & on_clock].sum(axis=0) / power[band].sum(axis=0)).tolist()


def release_test(sweep, guitar, note_off):
    """Restrict the sweep-derived stereo projection to +/-0.5 s, then measure only free decay."""
    nfft = 2 * FFT_SIZE
    s = fft.rfft(sweep, nfft, axis=0)
    f = fft.rfftfreq(nfft, 1 / RATE)
    # Unity at 1–4 kHz, tapered on either side.
    weight = (np.sin(np.pi / 2 * np.clip((f - 650) / 350, 0, 1)) *
              np.sin(np.pi / 2 * np.clip((5200 - f) / 1200, 0, 1))) ** 2
    projection = s[:, :, None] * s[:, None, :].conj()
    projection *= (weight / np.maximum(np.sum(abs(s)**2, axis=1), 1e-30))[:, None, None]
    h = fft.irfft(projection, nfft, axis=0)
    half = RATE // 2
    kernel = np.concatenate((h[-half:], h[:half + 1]))
    t = np.arange(-half, half + 1)
    kernel *= (np.sin(np.pi / 2 * np.clip((half - abs(t)) / (0.1 * RATE), 0, 1))**2)[:, None, None]
    predicted = np.zeros_like(guitar)
    for side in range(2):
        convolution = signal.oaconvolve(guitar[:, side, None], kernel[:, :, side], axes=0)
        predicted += convolution[half:half + len(guitar)]
    target = fft.irfft(fft.rfft(guitar, nfft, axis=0) * weight[:, None], nfft, axis=0)[:len(guitar)]
    error = []
    for start, stop in [(0.8, 1.6), (1.6, 2.6), (2.6, 3.6)]:
        window = slice(note_off + round(start * RATE), note_off + round(stop * RATE))
        error.append(float(100 * np.sum((predicted[window] - target[window])**2) / np.sum(target[window]**2)))
    return {"window_error_pct": error, "original_ripple_pct": guitar_ripple(target, note_off, 0.8),
            "projected_ripple_pct": guitar_ripple(predicted, note_off, 0.8)}


def line_locations(wet, sweep, pitch, order, seconds):
    # Fit all nearby clock lines together to avoid leakage from the carrier into its sidebands.
    centre = round((order - 1) * pitch / CLOCK)
    offsets = np.arange(-27, 28)
    bins = offsets + centre
    hz = pitch + centre * CLOCK
    z = envelope(wet, hz)
    idx = np.arange(round(max(1.1, seconds - 3.3) * ENV_RATE), round((seconds - 0.13) * ENV_RATE))
    basis = np.exp(2j * np.pi * CLOCK * np.outer(idx / ENV_RATE, offsets))
    lines = np.linalg.lstsq(basis, z[idx], rcond=None)[0]

    lo = pitch + bins[0] * CLOCK
    destination = signal.zoom_fft(sweep, [lo, lo + len(bins) * CLOCK], m=len(bins), fs=RATE, axis=0)
    t = np.arange(len(sweep)) / RATE
    source = np.exp(-2j * np.pi * pitch * t) @ sweep
    # For the unshifted note only its sidebands distinguish these two predictions.
    keep = bins != 0 if order == 1 else np.ones(len(bins), dtype=bool)
    energy = np.sum(abs(lines) ** 2, axis=1)
    return {
        "destination_pct": stereo_error(destination[keep], lines[keep]),
        "source_pct": stereo_error(np.broadcast_to(source, lines.shape)[keep], lines[keep]),
        "on_every_fifth_line_pct": float(100 * energy[bins % 5 == 0].sum() / energy.sum()),
    }


def earlier_sessions(reference):
    results = {}
    sessions = [("2026-11-01-shimmer", ["shimmer-tone-" + p for p in analyze_rv6.SHIMMER_PITCHES]),
                ("2026-10-31-followup", ["shimmer-sine-1000"])]
    for folder, names in sessions:
        session = analyze_rv6.Session(BASE / folder, BASE / folder)
        for name in names:
            meta = session.meta[name]["signal"]
            pitch, seconds = meta["freq"], meta["seconds"]
            wet = np.stack([session.wet_signal(name, ch)[:round((seconds + .5) * RATE)] for ch in (0, 2)], axis=1)
            results[name] = {
                "pitch_Hz": pitch, "first_octave_cycle_error_pct": cycle_error(wet, pitch, 2, seconds),
                "first_octave_stereo": line_locations(wet, reference, pitch, 2, seconds),
            }
        session.load.cache_clear()
    return results


def measure(renderers):
    results = {"focused": {}, "older": {}}
    dark_sweeps = {}
    noon_sweeps = {}
    for position in POSITIONS + ["noon-hall"]:
        name = position if position.endswith("hall") else position + "-shimmer"
        meta = takes.load(name)[0]
        time = meta["settings"]["Time"]
        sides = renderers if position in COMPARE else {"pedal": None}
        rows = results["focused"][name] = {}
        for label, renderer in sides.items():
            if renderer is None:
                wet = takes.wet(name)
            else:
                takes.RENDER = str(renderer)
                wet = takes.model_wet(name)
            parts = excerpts(meta, wet)
            del wet
            row = rows[label] = {}
            if "sweep" in parts:
                sweep = tapered(parts.pop("sweep"))
                if label == "pedal" and position.startswith("tone-0700"):
                    dark_sweeps[time] = sweep
                if position == "noon":
                    noon_sweeps[label] = sweep
            else:
                sweep = noon_sweeps[label]
            reference = fft.rfft(sweep, FFT_SIZE, axis=0)[BAND]
            for stimulus in ("burst", "stabs"):
                if stimulus in parts:
                    row[stimulus] = spatial_scores(reference, spectrum(parts[stimulus]))
            if "sine" in parts and not name.endswith("hall"):
                if label == "pedal" and position == "noon":
                    row["clock_estimate_Hz"] = estimate_clock(parts["sine"], 329.63, 5)
                orders = (1,) if position.startswith("tone-0700") else (1, 2, 4)
                row["cycle_error_pct"] = {str(m): cycle_error(parts["sine"], 329.63, m, 5) for m in orders}
                if label == "pedal" and time in dark_sweeps:
                    row["stereo_at_note"] = {str(m): line_locations(parts["sine"], dark_sweeps[time], 329.63, m, 5)
                                             for m in orders}
            if position in {"noon-guitar", "time-0300", "tone-0500"}:
                row["guitar_tail_ripple_pct"] = guitar_ripple(parts["stabs"], parts["note_off"])
            if position in {"noon-guitar", "time-0700"}:
                row["release"] = release_test(sweep, parts["stabs"], parts["note_off"])
        takes.load.cache_clear()
        print(name, flush=True, file=sys.stderr)
    results["older"] = earlier_sessions(dark_sweeps["12:00"])
    results["ensemble"] = ensemble_check()
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render", type=Path, default=Path(takes.RENDER))
    parser.add_argument("--clock-render", type=Path, help="optional clock-locked diagnostic renderer")
    parser.add_argument("--output", type=Path, help="write JSON here instead of stdout")
    args = parser.parse_args()
    renderers = {"pedal": None, "current": args.render}
    if args.clock_render:
        renderers["locked"] = args.clock_render
    report = json.dumps(measure(renderers), indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(report + "\n")
    else:
        print(report)


if __name__ == "__main__":
    main()
