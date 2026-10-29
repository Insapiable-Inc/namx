#!/usr/bin/env python3
"""Measures the Boss PH-3 from its five 2026-10-28 sessions and draws the fitted models over the
measurements.

    analyze_ph3.py captures/boss/ph-3/2026-10-28 captures/boss/ph-3/2026-10-28-modes \\
        captures/boss/ph-3/2026-10-28-slow captures/boss/ph-3/2026-10-28-followup \\
        captures/boss/ph-3/2026-10-28-followup2            # writes 2026-10-28/fit/

What it measures itself (cheap and deterministic): every static sweep against its session's
bypass, the notches and their order (a simple or a double zero), the Res factor against the
all-pass chain's phase and the loop gain it implies, the 4 Stage sweep's scale per LFO phase,
Step's hold time per Rate, the LFO rate per Rate, the output level per Depth. The model
parameters — the all-pass sections and mix per mode, the 12 Stage twin passes, the Res loop,
the sweep laws — were fitted on 2026-10-28 with exploratory scripts that weren't kept; they are
the FINDINGS table below, the fitting procedures are described in captures/boss/ph-3/README.md,
and the figures draw those models over the measurements so the fit can be judged.
"""

import argparse
import json
import pathlib
import sys

import matplotlib
import numpy as np
import soundfile

import analysis
import analyze_dd8
import signals

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RATE = 48000
PEDAL_RATE = 44100.0        # the all-pass sections are designed at the pedal's own rate
RESPONSE_SAMPLES = 24000    # the impulse response's first half second
LATENCY_MS = 0.544

# Every fitted parameter (2026-10-28). Sections are (Hz, Q) of RBJ second-order all-passes at
# 44.1 kHz; a tone is (high-pass Hz, shelf Hz, shelf dB); a mix is (dry, wet).
FINDINGS = {
    "static": {   # Depth 7:00 (the sweep stops), Res 7:00, session 2026-10-28; fit = dB rms
        "4 Stage": {"tone": (63.7, 12508.0, 3.35), "mix": (0.677, 0.713),
                    "sections": [(209.1, 0.618), (659.7, 0.645)], "fit_db": 0.31},
        "8 Stage": {"tone": (240.5, 6632.0, 2.85), "mix": (0.745, 0.760),
                    "sections": [(259.1, 0.624), (642.4, 0.884), (1798.1, 0.643), (3784.6, 0.401)], "fit_db": 0.16},
        "10 Stage": {"tone": (63.3, 12778.0, 3.39), "mix": (0.678, 0.686),
                     "sections": [(541.1, 0.749), (1178.0, 0.224), (1265.4, 0.709), (3952.0, 0.420), (9497.5, 0.541)],
                     "fit_db": 0.19},
        "12 Stage": {"tone": (75.8, 500.0, -1.45), "mix": (0.676, 0.622), "passes": 2,   # a 6 Stage phaser twice
                     "sections": [(1152.7, 0.959), (1437.0, 0.270), (2999.1, 0.775)], "fit_db": 1.10},
    },
    "sweep": {
        # c = a + b·tri², the scale on every section frequency; tri = the LFO's triangle, 0..1
        "4 Stage depth": {"9:00": (0.912, 0.947), "12:00": (0.609, 4.257), "3:00": (0.316, 9.009), "5:00": (0.244, 11.043)},
        "4 Stage fast rate (Depth 5:00)": {"~4:00": (0.725, 9.65), "5:00": (1.42, 7.34)},
        "lfo_hz": {"8:00": 0.047, "9:00": 0.225, "10:30": 0.394, "12:00": 0.654, "1:30": 1.77, "3:00": 4.64,
                   "~4:00": 12.1, "5:00": 21.5},
        # the joint bases (frozen sweeps + the folded Rate 8:00 cycle) and the scale's range at Depth 5:00
        "8 Stage joint": {"sections": [(599.6, 0.655), (1024.5, 0.423), (1198.0, 0.358), (2696.9, 0.128)],
                          "mix": (0.865, 0.753), "c_range": (0.46, 5.16)},
        "10 Stage joint": {"sections": [(448.7, 0.670), (936.4, 1.104), (3145.9, 0.886), (3774.9, 0.077), (10081.4, 1.108)],
                           "mix": (0.800, 0.698), "c_range": (0.50, 5.31)},
        "12 Stage c_range": (0.32, 5.1),
    },
    "rise_fall": {"seconds_per_notch": {"8:00": 21.1, "9:00": 4.56, "12:00": 1.53, "3:00": 0.20, "5:00": 0.047},
                  "notch_spacing_octaves": 1.88, "depth_does_nothing": True},
    "step": {"hold_ms": {"8:00": 350, "9:00": 178, "10:30": 121, "12:00": 86, "1:30": 71, "3:00": 29},
             "note": "8:00 read off the notch track; 3:00 is the LFO's 8th harmonic (8 steps per cycle if real)"},
    "res": {
        # out = s · tone · (dry + wet·A) / (1 − k · z^-delay · HP · A · parked), the dry inside the loop
        "10 Stage": {"s": 0.877, "k": 0.681, "hp_hz": 54.9, "delay": 1, "parked": [], "fit_db": 0.285},
        "8 Stage": {"s": 0.750, "k": 0.698, "hp_hz": 201.7, "delay": 0, "parked": [(8366.0, 0.776)], "fit_db": 0.77},
        "8 Stage frozen pairs": {"s": 0.762, "k": 0.714, "hp_hz": 242.5, "delay": 0, "parked": [(8336.0, 0.602)],
                                 "fit_db": [1.39, 1.25, 1.95, 1.03]},
        "8 Stage k per Res": {"9:00": 0.245, "12:00": 0.443, "3:00": 0.634, "5:00": 0.698},
        # 12 Stage: each pass a phaser with the loop around its own three sections and the dry OUTSIDE it
        "12 Stage frozen point 4": {"s": 0.58, "k": 0.706, "hp_hz": 103.0, "delay": 1, "dry": "outside", "fit_db": 1.47},
        "4 Stage": "not solved: see the study",
    },
    "frozen": {   # Res-7:00 fits at the follow-up 2 frozen points (own tone per take): tone, mix, sections
        "4 Stage": [((54.8, 16000.0, -12.0), (0.919, 1.011), [(2421.8, 0.592), (7733.2, 0.750)]),
                    ((55.9, 2914.6, 0.60), (0.966, 0.975), [(305.5, 0.440), (324.3, 0.571)]),
                    ((55.8, 2941.3, 0.62), (0.959, 0.974), [(437.4, 0.583), (455.5, 0.431)])],
        "8 Stage": [((231.3, 16000.0, -12.0), (1.075, 1.088), [(1161.8, 0.447), (1257.5, 0.738), (2786.1, 0.890), (7480.8, 0.722)]),
                    ((230.0, 16000.0, -12.0), (1.070, 1.082), [(877.3, 0.481), (1049.5, 0.690), (2266.3, 0.842), (6774.4, 0.709)]),
                    ((213.6, 788.8, 0.70), (1.021, 1.022), [(258.9, 0.627), (337.7, 0.368), (477.0, 0.640), (4598.2, 0.483)])],
        "12 Stage": {   # twins; point 4 identical, points 1 and 3 with the second pass detuned (a delayed LFO)
            "point 4": ((61.3, 16000.0, -12.0), (0.743, 0.700), [(2863.6, 0.825), (3854.8, 0.279), (6786.1, 0.840)], (1.0, 1.0, 1.0)),
            "point 1": ((56.0, 12000.0, 3.4), (0.633, 0.709), [(184.1, 0.661), (488.8, 0.849), (1095.1, 0.563)], (1.179, 1.099, 1.000)),
            "point 3": ((56.0, 12000.0, 3.4), (0.741, 0.716), [(230.9, 0.661), (567.9, 0.853), (1159.8, 0.562)], (0.835, 0.910, 1.005)),
        },
    },
}


# --- the model -------------------------------------------------------------------------------

def z_inverse(f):
    return np.exp(-2j * np.pi * f / PEDAL_RATE)


def all_pass(f, f0, q):
    """An RBJ second-order all-pass at the pedal's rate."""
    z1 = z_inverse(f)
    w = 2 * np.pi * min(f0, 21900.0) / PEDAL_RATE
    alpha, c = np.sin(w) / (2 * q), np.cos(w)
    return ((1 - alpha) - 2 * c * z1 + (1 + alpha) * z1 ** 2) / ((1 + alpha) - 2 * c * z1 + (1 - alpha) * z1 ** 2)


def chain(f, sections, scale=1.0):
    a = np.ones(len(f), dtype=complex)
    for f0, q in sections:
        a = a * all_pass(f, f0 * scale, q)
    return a


def tone(f, hp_hz, shelf_hz, shelf_db):
    """A one-pole high-pass and a first-order high shelf (bilinear)."""
    z1 = z_inverse(f)
    k = np.tan(np.pi * hp_hz / PEDAL_RATE)
    high_pass = (1 - z1) / ((1 + k) - (1 - k) * z1)
    g = 10 ** (shelf_db / 20)
    ks = np.tan(np.pi * shelf_hz / PEDAL_RATE)
    shelf = ((g + ks) + (ks - g) * z1) / ((1 + ks) + (ks - 1) * z1)
    return high_pass * shelf


def high_pass1(f, fc):
    z1 = z_inverse(f)
    t = np.tan(np.pi * fc / PEDAL_RATE)
    return (1 - z1) / ((1 + t) + (t - 1) * z1)


def phaser(f, mix, sections, tone_params, scale=1.0, passes=1, detune=None):
    """dry + wet·A, run `passes` times; `detune` scales the second pass's sections."""
    out = tone(f, *tone_params) * np.exp(-2j * np.pi * f * LATENCY_MS * 1e-3)
    for p in range(passes):
        d = detune if (p == 1 and detune) else (1.0,) * len(sections)
        a = chain(f, [(f0 * di, q) for (f0, q), di in zip(sections, d)], scale)
        out = out * (mix[0] + mix[1] * a)
    return out


def res_loop(f, mix, sections, tone_params, s, k, hp_hz, delay, parked, dry_inside=True):
    """The Res loop around the chain (plus the parked sections), with the dry inside or outside it."""
    a = chain(f, sections)
    loop = k * z_inverse(f) ** delay * high_pass1(f, hp_hz) * a * chain(f, parked)
    t = tone(f, *tone_params) * np.exp(-2j * np.pi * f * LATENCY_MS * 1e-3)
    if dry_inside:
        return s * t * (mix[0] + mix[1] * a) / (1 - loop)
    return s * t * (mix[0] + mix[1] * a / (1 - loop))


# --- measurements ----------------------------------------------------------------------------

def response(session, name):
    """A sweep take's response against its session's bypass, on the analysis grid."""
    h = np.fft.rfft(analyze_dd8.impulse_response(session, name)[0][:RESPONSE_SAMPLES])
    b = np.fft.rfft(analyze_dd8.impulse_response(session, "bypass-sweep")[0][:RESPONSE_SAMPLES])
    f = np.fft.rfftfreq(RESPONSE_SAMPLES, 1 / RATE)
    return f, h / b


def db(x):
    return 20 * np.log10(np.abs(x) + 1e-12)


def notches(f, r, deeper_than=-10.0, lo=40.0, hi=19000.0):
    """Local minima of the response below `deeper_than` dB: (Hz, dB, slope below, slope above).

    The slopes are dB per halving of the distance to the notch: 6 = a simple zero, 12 = double."""
    k = np.flatnonzero((f > lo) & (f < hi))
    d = db(r[k])
    found = []
    for i in range(3, len(d) - 3):
        if d[i] < deeper_than and d[i] == d[max(0, i - 25):i + 25].min():
            f0 = f[k[i]]
            slopes = []
            for sel in ((f > f0 * 0.8) & (f < f0 * 0.97), (f > f0 * 1.03) & (f < f0 * 1.25)):
                slopes.append(float(np.polyfit(np.log2(np.abs(f[sel] - f0)), db(r[sel]), 1)[0]))
            found.append((round(float(f0), 1), round(float(d[i]), 1), round(slopes[0], 1), round(slopes[1], 1)))
    return found


def loop_gain_from_ratio(f, ratio, sections, phase_window=0.4):
    """The Res factor res/off against the chain's phase: its peaks near the even multiples of π
    (−2π, −4π, −6π) and dips near the odd ones (−π, −3π, −5π) give the loop gain,
    (1 + k)/(1 − k) = peak/dip, independent of the level. "Near": the loop's own phase (the
    high-pass, a parked section) shifts the peaks a little off the multiples, so each is the
    extreme within ±0.4π. The notch regions (ratio NaN) are left out."""
    phi = np.unwrap(np.angle(chain(f, sections))) / np.pi
    ok = (f > 50) & (f < 12000) & np.isfinite(ratio)
    peaks, dips = [], []
    for n in range(-1, -7, -1):
        sel = ok & (np.abs(phi - n) < phase_window)
        if sel.sum() < 5:
            continue
        values = db(ratio[sel])
        (peaks if n % 2 == 0 else dips).append(float(values.max() if n % 2 == 0 else values.min()))
    if not peaks or not dips:
        return None
    r = 10 ** ((np.median(peaks) - np.median(dips)) / 20)
    return {"peaks_db": [round(p, 1) for p in peaks], "dips_db": [round(d, 1) for d in dips],
            "k": round(float((r - 1) / (r + 1)), 3)}


def frames(session, name, n, hop):
    """Short-time responses of a noise take: times, frequencies, Y·X̄ and |X|² per frame, settings."""
    take = analysis.Take(session, name)
    played, _ = soundfile.read(pathlib.Path(session) / "played" / f"{name}.wav")
    length = int(signals.body_seconds(take.meta["signal"]) * RATE)
    x = played[take.meta["signal_start"]:take.meta["signal_start"] + length]
    y = take.pedal[take.start:take.start + length]
    w = np.hanning(n)
    starts = np.arange(0, length - n, hop)
    big_x = np.array([np.fft.rfft(w * x[s:s + n]) for s in starts])
    big_y = np.array([np.fft.rfft(w * y[s:s + n]) for s in starts])
    return (starts + n / 2) / RATE, np.fft.rfftfreq(n, 1 / RATE), big_x, big_y, take.meta.get("settings", {})


def bypass_frames(session, n):
    _, _, x, y, _ = frames(session, "bypass-noise", n, n // 4)
    return (y * np.conj(x)).sum(0) / (np.abs(x) ** 2).sum(0)


def frame_db(fr, x, y):
    k = np.flatnonzero((fr > 80) & (fr < 12000))
    d = np.clip(db(y[:, k] / np.maximum(np.abs(x[:, k]), 1e-9)), -40, 20)
    return d - d.mean(0)


def explained(times, d, period, bins):
    ph = ((times / period) % 1.0 * bins).astype(int)
    counts = np.bincount(ph, minlength=bins)
    if counts.min() < 3:
        return 0.0
    means = np.zeros((bins, d.shape[1]))
    np.add.at(means, ph, d)
    means /= counts[:, None]
    return float(np.sum(means[ph] ** 2) / np.sum(d ** 2))


def lfo_period(times, fr, x, y, low, high, bins=24):
    """The LFO period: the fold that explains the most variance of the frames' dB, then the shortest
    submultiple that explains at least 90 % as much (a multiple folds just as well)."""
    d = frame_db(fr, x, y)
    candidates = np.geomspace(low, high, 600)
    scores = [explained(times, d, p, bins) for p in candidates]
    p = candidates[int(np.argmax(scores))]
    fine = np.linspace(p * 0.98, p * 1.02, 201)
    fine_scores = [explained(times, d, q, bins) for q in fine]
    best, score = fine[int(np.argmax(fine_scores))], max(fine_scores)
    for k in (5, 4, 3, 2):
        q0 = best / k
        if q0 < low:
            continue
        fine_k = np.linspace(q0 * 0.995, q0 * 1.005, 101)
        sk = [explained(times, d, q, bins) for q in fine_k]
        if max(sk) >= 0.9 * score:
            return float(fine_k[int(np.argmax(sk))]), float(max(sk))
    return float(best), float(score)


def fold(times, x, y, period, bins):
    ph = ((times / period) % 1.0 * bins).astype(int)
    sxy = np.zeros((bins, x.shape[1]), complex)
    sxx = np.zeros((bins, x.shape[1]))
    np.add.at(sxy, ph, y * np.conj(x))
    np.add.at(sxx, ph, np.abs(x) ** 2)
    return sxy / np.maximum(sxx, 1e-30)


def track_scale(session, name, static, bins=64, n=1024):
    """The 4 Stage sweep: the LFO period, then per LFO-phase bin the scale c on the static sections
    that best matches the folded response (a scan, the mix and tone fixed)."""
    times, fr, x, y, settings = frames(session, name, n, n // 8)
    y = y / bypass_frames(session, n)
    period, score = lfo_period(times, fr, x, y, 0.03, (times[-1] - times[0]) / 2.2)
    folded = fold(times, x, y, period, bins)
    band = (fr > 60) & (fr < 15000)
    grid = np.geomspace(0.1, 20, 160)
    models = [db(phaser(fr, static["mix"], static["sections"], static["tone"], scale=c))[band] for c in grid]
    scales, errors = [], []
    for h in folded:
        target = np.maximum(db(h)[band], -30)
        e = [np.sqrt(np.mean((np.maximum(m, -30) - target) ** 2)) for m in models]
        scales.append(float(grid[int(np.argmin(e))]))
        errors.append(float(min(e)))
    return {"settings": settings, "lfo_hz": round(1 / period, 4), "fold_explained": round(score, 2),
            "scale_per_bin": scales, "fit_db_per_bin": errors}


def triangle(bins):
    t = np.arange(bins) / bins
    return 1 - np.abs(2 * t - 1)


def align(model, measured):
    """Rolls the model's cycle to the measured one (the fold's phase is arbitrary)."""
    best = None
    for shift in range(len(model)):
        m = np.roll(model, shift)
        e = np.sum((np.log(m) - np.log(measured)) ** 2)
        if best is None or e < best[0]:
            best = (e, m)
    return best[1], np.sqrt(best[0] / len(model))


def step_hold(session, name, n=1024, hop=32):
    """Step's hold time: the strongest line in the spectrum of the frame-to-frame change, with the
    LFO's own harmonics (the triangle's turns) masked. Reliable from Rate 9:00 to 1:30."""
    times, fr, x, y, settings = frames(session, name, n, hop)
    period, _ = lfo_period(*frames(session, name, 1024, 128)[:4], 0.03, 20.0)
    lfo = 1 / period
    k = (fr > 250) & (fr < 7000)
    d = np.clip(db(y[:, k] / np.maximum(np.abs(x[:, k]), 1e-9)), -30, 15)
    change = np.mean(np.abs(d[2:] - d[:-2]), axis=1)
    change = change - change.mean()
    fs = RATE / hop
    spectrum = np.abs(np.fft.rfft(change * np.hanning(len(change)), 8 * len(change)))
    freqs = np.fft.rfftfreq(8 * len(change), 1 / fs)
    band = (freqs > 1.5) & (freqs < 80)
    for h in (1, 2, 3, 4):
        band &= np.abs(freqs - h * lfo) > 0.1 * h * lfo
    i = int(np.argmax(spectrum * band))
    harmonic = int(np.round(freqs[i] / lfo))
    strength = float(spectrum[i] / np.median(spectrum[band]))
    return {"settings": settings, "lfo_hz": round(lfo, 3), "hold_ms": round(1000 / freqs[i], 1),
            "line_strength": round(strength, 1), "steps_per_cycle": round(period * freqs[i], 1),
            "coincides_with_lfo_harmonic": harmonic if abs(freqs[i] / lfo - harmonic) < 0.03 else None,
            "reliable": strength >= 12}


def notch_track(session, name, seconds, n=1024, hop=32, lo=300.0, hi=4000.0):
    """Every notch below −10 dB per frame over the first `seconds`: (times, Hz) for the staircase plot."""
    times, fr, x, y, _ = frames(session, name, n, hop)
    h = y * np.conj(x) / np.maximum(np.abs(x) ** 2, 1e-12)
    h = (h[:-2] + h[1:-1] + h[2:]) / 3
    times = times[1:-1]
    d = db(h)
    k = np.flatnonzero((fr > lo) & (fr < hi))
    out_t, out_f = [], []
    for i in range(len(times)):
        if times[i] > seconds:
            break
        row = d[i, k]
        minima = np.flatnonzero((row[1:-1] < row[:-2]) & (row[1:-1] <= row[2:]) & (row[1:-1] < -10)) + 1
        for j in minima:
            around = row[max(0, j - 8):j + 9]
            if row[j] == around.min() and row[j] < around.max() - 8:   # a notch, not a dip in the noise
                out_t.append(times[i])
                out_f.append(fr[k[j]])
    return np.array(out_t), np.array(out_f)


def level_re_bypass(session, name):
    """Broadband rms of a noise take against the session's bypass-noise, dB."""
    def rms_ratio(take_name):
        take = analysis.Take(session, take_name)
        played, _ = soundfile.read(pathlib.Path(session) / "played" / f"{take_name}.wav")
        length = int(signals.body_seconds(take.meta["signal"]) * RATE)
        x = played[take.meta["signal_start"]:take.meta["signal_start"] + length]
        y = take.pedal[take.start:take.start + length]
        return 20 * np.log10(np.std(y) / np.std(x))
    return round(float(rms_ratio(name) - rms_ratio("bypass-noise")), 2)


# --- the run -----------------------------------------------------------------------------------

def measure(s):
    """Everything the script measures itself. `s` = the five sessions by role."""
    out = {"static": {}, "res": {}, "sweep": {}, "step": {}, "level_db_re_bypass": {}, "frozen_12": {}}
    for mode, take in [("4 Stage", "s4"), ("8 Stage", "s8"), ("10 Stage", "s10"), ("12 Stage", "s12")]:
        f, r = response(s["main"], f"{take}-depth-0700-sweep")
        out["static"][mode] = {"notches": notches(f, r)}
        if mode == "12 Stage":
            f2, r2 = response(s["main"], "s12-depth-0700-res-0500-sweep")
            out["static"][mode]["notches_res_0500"] = notches(f2, r2)
    for name in ("s12-frozen-1", "s12-frozen-2", "s12-frozen-3", "s12-frozen-4", "s12-frozen-4-res-0500"):
        f, r = response(s["followup2"], name)
        out["frozen_12"][name] = notches(f, r)
    # Res: the loop gain read off the Res factor; 8 Stage ladder against the session-1 Res 7:00 take
    for mode, take, ses, pairs in [
            ("8 Stage", "s8", s["main"], [("5:00", s["main"], "s8-depth-0700-sweep", "s8-depth-0700-res-0500-sweep")]
             + [(res, s["followup2"], None, f"s8-res-{res.replace(':', '').zfill(4)}-sweep") for res in ("9:00", "12:00", "3:00")]),
            ("10 Stage", "s10", s["main"], [("5:00", s["main"], "s10-depth-0700-sweep", "s10-depth-0700-res-0500-sweep")]),
            ("4 Stage", "s4", s["main"], [(res, s["main"], "s4-depth-0700-sweep", f"s4-res-{res.replace(':', '').zfill(4)}-sweep")
                                           for res in ("9:00", "12:00", "3:00", "5:00")])]:
        f, off = response(s["main"], f"{take}-depth-0700-sweep")
        out["res"][mode] = {}
        for res, ses_r, off_name, res_name in pairs:
            _, r = response(ses_r, res_name)
            ratio = r / off
            ratio[np.abs(off) < 0.08] = np.nan
            entry = loop_gain_from_ratio(f, ratio, FINDINGS["static"][mode]["sections"])
            entry["notches"] = notches(f, r)
            out["res"][mode][res] = entry
    # the frozen pairs: notches off and on, per point
    for mode, take in [("4 Stage", "s4"), ("8 Stage", "s8")]:
        out["res"][mode]["frozen pairs"] = []
        for i in (1, 2, 3):
            f, off = response(s["followup2"], f"{take}-frozen-{i}-res-0700")
            _, on = response(s["followup2"], f"{take}-frozen-{i}-res-0500")
            ratio = on / off
            ratio[np.abs(off) < 0.08] = np.nan
            sections = FINDINGS["frozen"][mode][i - 1][2]
            entry = loop_gain_from_ratio(f, ratio, sections)
            out["res"][mode]["frozen pairs"].append({"point": i, "notches_off": notches(f, off), "notches_on": notches(f, on),
                                                     "loop": entry})
    # the 4 Stage sweep per Depth, and the LFO rate per Rate
    static4 = FINDINGS["static"]["4 Stage"]
    out["sweep"]["4 Stage depth"] = {}
    for depth, name in [("9:00", "s4-depth-0900"), ("12:00", "s4-depth-1200"), ("3:00", "s4-depth-0300"), ("5:00", "s4-depth-0500")]:
        out["sweep"]["4 Stage depth"][depth] = track_scale(s["main"], name, static4)
    out["sweep"]["lfo_hz"] = {}
    for rate, ses, name in [("8:00", s["slow"], "s4-rate-0800"), ("9:00", s["main"], "s4-rate-0900"), ("12:00", s["main"], "s4-depth-0500"),
                            ("3:00", s["main"], "s4-rate-0300"), ("~4:00", s["followup"], "s4-rate-0400"), ("5:00", s["followup"], "s4-rate-0500")]:
        n = 4096 if rate == "8:00" else 256 if rate in ("3:00", "~4:00", "5:00") else 1024
        times, fr, x, y, _ = frames(ses, name, n, n // 8)
        period, score = lfo_period(times, fr, x, y, 0.01, (times[-1] - times[0]) / 2.2)
        out["sweep"]["lfo_hz"][rate] = {"hz": round(1 / period, 4), "fold_explained": round(score, 2)}
    # Step
    for rate, ses, name in [("8:00", s["slow"], "step-rate-0800"), ("9:00", s["followup2"], "step-rate-0900"), ("10:30", s["followup2"], "step-rate-1030"),
                            ("12:00", s["followup2"], "step-rate-1200"), ("1:30", s["followup2"], "step-rate-0130"), ("3:00", s["followup2"], "step-rate-0300"),
                            ("4:00", s["followup2"], "step-rate-0400"), ("Depth 7:00", s["followup2"], "step-depth-0700")]:
        out["step"][rate] = step_hold(ses, name)
    # level
    for name in ("s4-depth-0700-noise", "s4-depth-0900", "s4-depth-1200", "s4-depth-0300", "s4-depth-0500", "s4-res-1200", "s4-res-0500",
                 "s8-depth-0500", "s10-depth-0500", "s12-depth-0500"):
        out["level_db_re_bypass"][name] = level_re_bypass(s["main"], name)
    for name in ("rise-rate-1200", "rise-depth-0500-res-0500", "step-depth-0500"):
        out["level_db_re_bypass"][name] = level_re_bypass(s["modes"], name)
    return out


def plot_static_and_sweep(out_dir, s, measured):
    matplotlib.rcParams["svg.hashsalt"] = "ph-3"  # the same SVG ids on every run
    figure, axes = plt.subplots(2, 4, figsize=(20, 9), constrained_layout=True)
    for a, (mode, take) in zip(axes[0], [("4 Stage", "s4"), ("8 Stage", "s8"), ("10 Stage", "s10"), ("12 Stage", "s12")]):
        f, r = response(s["main"], f"{take}-depth-0700-sweep")
        st = FINDINGS["static"][mode]
        sel = (f > 35) & (f < 18000)
        a.semilogx(f[sel], db(r[sel]), "k", lw=1.2, label="pedal")
        model = phaser(f, st["mix"], st["sections"], st["tone"], passes=st.get("passes", 1))
        a.semilogx(f[sel], db(model[sel]), "--", color="C3", lw=1.2, label="model")
        secs = ", ".join(f"{f0:.0f} Q{q:.2f}" for f0, q in st["sections"])
        a.set_title(f"{mode}, Depth 7:00, Res 7:00: {len(st['sections'])} all-pass sections"
                    + (" run twice" if st.get("passes") == 2 else "") + f"\n{secs} Hz\n{st['fit_db']:.2f} dB rms", fontsize=8)
        a.set_ylim(-45, 12)
        a.set_xlabel("Hz")
        a.set_ylabel("dB re bypass")
        a.grid(alpha=0.3, which="both")
        a.legend(fontsize=8)
    bins = 64
    tri = triangle(bins)
    for a, depth in zip(axes[1], ["9:00", "12:00", "3:00", "5:00"]):
        track = measured["sweep"]["4 Stage depth"][depth]
        law = FINDINGS["sweep"]["4 Stage depth"][depth]
        model, err = align(law[0] + law[1] * tri ** 2, np.array(track["scale_per_bin"]))
        phase = np.arange(bins) / bins
        a.semilogy(phase, track["scale_per_bin"], "o", ms=3, color="k", label="pedal (scale per LFO phase)")
        a.semilogy(phase, model, "--", color="C3", label=f"{law[0]:.2f} + {law[1]:.2f}·tri²")
        a.set_title(f"4 Stage sweep, Depth {depth}, Rate 12:00 ({track['lfo_hz']:.3g} Hz)\n"
                    f"log rms {err:.3f}; per-bin spectrum fit median {np.median(track['fit_db_per_bin']):.1f} dB", fontsize=9)
        a.set_ylim(0.1, 20)
        a.set_xlabel("LFO cycle")
        a.set_ylabel("section frequency × static")
        a.grid(alpha=0.3, which="both")
        a.legend(fontsize=8)
    figure.suptitle("Boss PH-3 — the static filter per mode and the 4 Stage sweep against Depth", fontsize=13)
    figure.savefig(out_dir / "ph-3.svg", metadata={"Date": None})
    plt.close(figure)


def plot_res(out_dir, s, measured):
    matplotlib.rcParams["svg.hashsalt"] = "ph-3"
    figure, axes = plt.subplots(2, 3, figsize=(21, 11), constrained_layout=True)
    # the Res factor against the chain phase, 8 Stage ladder and 10 Stage, with the loop curve
    for a, mode, take, items in [(axes[0, 0], "8 Stage", "s8", [("9:00", s["followup2"], "s8-res-0900-sweep"), ("12:00", s["followup2"], "s8-res-1200-sweep"),
                                                                 ("3:00", s["followup2"], "s8-res-0300-sweep"), ("5:00", s["main"], "s8-depth-0700-res-0500-sweep")]),
                                 (axes[0, 1], "10 Stage", "s10", [("5:00", s["main"], "s10-depth-0700-res-0500-sweep")])]:
        f, off = response(s["main"], f"{take}-depth-0700-sweep")
        phi = np.unwrap(np.angle(chain(f, FINDINGS["static"][mode]["sections"]))) / np.pi
        for res, ses, name in items:
            _, r = response(ses, name)
            ok = (np.abs(off) > 0.08) & (f > 50) & (f < 12000)
            k = measured["res"][mode][res]["k"]
            a.plot(phi[ok], db(r[ok] / off[ok]), ".", ms=2, rasterized=True, label=f"Res {res}: k {k:.2f} from its peaks and dips")
        if mode == "10 Stage":
            loop = FINDINGS["res"]["10 Stage"]
            grid = np.linspace(-9, 0, 400)
            a.plot(grid, db(loop["s"] / (1 - loop["k"] * np.exp(1j * np.pi * grid))), "--", color="C3", lw=1,
                   label=f"fitted loop: {loop['s']:.2f} / (1 − {loop['k']:.2f}·e^jφ)")
        a.set_xlabel("all-pass chain phase / π")
        a.set_ylabel("|Res on / Res off| dB")
        a.set_title(f"{mode}, Depth 7:00: the Res factor against the chain's phase"
                    + ("\n(the 9:00–3:00 takes are from another session, 2 dB up)" if mode == "8 Stage" else "\n(peaks at the even multiples of π, dips at the odd ones)"), fontsize=9)
        a.set_xlim(-9, 0)
        a.set_ylim(-12, 14)
        a.grid(alpha=0.3)
        a.legend(fontsize=7)
    # 12 Stage pair at frozen point 4 with the per-pass loop, dry outside
    a = axes[0, 2]
    f, off = response(s["followup2"], "s12-frozen-4")
    _, on = response(s["followup2"], "s12-frozen-4-res-0500")
    t12, mix12, secs12, _ = FINDINGS["frozen"]["12 Stage"]["point 4"]
    r12 = FINDINGS["res"]["12 Stage frozen point 4"]
    model_off = phaser(f, mix12, secs12, t12, passes=2)
    model_on = res_loop(f, mix12, secs12, t12, 1.0, r12["k"], r12["hp_hz"], r12["delay"], [], dry_inside=False) ** 2 / (
        tone(f, *t12) * np.exp(-2j * np.pi * f * LATENCY_MS * 1e-3)) * r12["s"]
    sel = (f > 35) & (f < 18000)
    a.semilogx(f[sel], db(off[sel]), "k", lw=1, label="Res 7:00")
    a.semilogx(f[sel], db(model_off[sel]), "--", color="C3", lw=1, label="twins, 1.0 dB")
    a.semilogx(f[sel], db(on[sel]), color="C0", lw=1, label="Res 5:00")
    a.semilogx(f[sel], db(model_on[sel]), "--", color="C1", lw=1, label=f"per-pass loop, dry outside, k {r12['k']:.2f}: {r12['fit_db']:.1f} dB")
    a.set_title("12 Stage frozen (point 4): the twin passes, and Res 5:00 with a loop around each\npass's three sections and the dry outside it (the notches move and fill)", fontsize=9)
    a.set_ylim(-45, 20)
    a.set_xlabel("Hz")
    a.grid(alpha=0.3, which="both")
    a.legend(fontsize=7)
    # 8 Stage frozen pair 1 with the loop model (shared parameters)
    a = axes[1, 0]
    f, off = response(s["followup2"], "s8-frozen-1-res-0700")
    _, on = response(s["followup2"], "s8-frozen-1-res-0500")
    t8, mix8, secs8 = FINDINGS["frozen"]["8 Stage"][0]
    r8 = FINDINGS["res"]["8 Stage frozen pairs"]
    a.semilogx(f[sel], db(off[sel]), "k", lw=1, label="Res 7:00")
    a.semilogx(f[sel], db(phaser(f, mix8, secs8, t8)[sel]), "--", color="C3", lw=1, label="4 sections")
    a.semilogx(f[sel], db(on[sel]), color="C0", lw=1, label="Res 5:00")
    model_on = res_loop(f, mix8, secs8, t8, r8["s"], r8["k"], r8["hp_hz"], r8["delay"], r8["parked"])
    a.semilogx(f[sel], db(model_on[sel]), "--", color="C1", lw=1,
               label=f"loop k {r8['k']:.2f}, HP {r8['hp_hz']:.0f} Hz, parked section {r8['parked'][0][0]:.0f} Hz: {r8['fit_db'][0]:.1f} dB")
    a.set_title("8 Stage frozen (point 1): the loop around the four sections plus the parked fifth,\nthe dry inside it — one parameter set fits all three frozen points", fontsize=9)
    a.set_ylim(-45, 20)
    a.set_xlabel("Hz")
    a.grid(alpha=0.3, which="both")
    a.legend(fontsize=7)
    # 4 Stage pairs: what Res does (no model)
    a = axes[1, 1]
    for i, color in zip((1, 2, 3), ("C0", "C1", "C2")):
        f, off = response(s["followup2"], f"s4-frozen-{i}-res-0700")
        _, on = response(s["followup2"], f"s4-frozen-{i}-res-0500")
        a.semilogx(f[sel], db(off[sel]), color=color, lw=1, label=f"point {i} Res 7:00")
        a.semilogx(f[sel], db(on[sel]), "--", color=color, lw=1, label=f"point {i} Res 5:00")
    a.set_title("4 Stage frozen pairs: the loop's peaks and the deeper first notch, but the top notch\nfills to −20 dB and a 10 dB sag follows it — not solved (see the study)", fontsize=9)
    a.set_ylim(-45, 20)
    a.set_xlabel("Hz")
    a.grid(alpha=0.3, which="both")
    a.legend(fontsize=7)
    # the loop gain against the Res knob
    a = axes[1, 2]
    knob = {"9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
    for mode, marker in [("8 Stage", "o"), ("10 Stage", "s")]:
        pts = [(knob[res], measured["res"][mode][res]["k"]) for res in measured["res"][mode] if res in knob]
        a.plot([p[0] for p in pts], [p[1] for p in pts], marker, label=f"{mode}, from the Res factor's peaks and dips")
    fitted = FINDINGS["res"]["8 Stage k per Res"]
    a.plot([knob[r] for r in fitted], list(fitted.values()), "x", color="k", label="8 Stage, fitted loop")
    x = np.linspace(0.05, 1, 100)
    a.plot(x, 0.7 * x ** 0.6, "--", color="C3", lw=0.8, label="0.7 · fraction^0.6")
    a.set_xlabel("Res knob (fraction of its travel)")
    a.set_ylabel("loop gain k")
    a.set_title("The Res knob's law", fontsize=9)
    a.grid(alpha=0.3)
    a.legend(fontsize=7)
    figure.suptitle("Boss PH-3 — Res: a feedback loop around the whole engine", fontsize=13)
    figure.savefig(out_dir / "ph-3-res.svg", metadata={"Date": None})
    plt.close(figure)


def plot_step(out_dir, s, measured):
    matplotlib.rcParams["svg.hashsalt"] = "ph-3"
    figure, axes = plt.subplots(1, 4, figsize=(22, 5), constrained_layout=True)
    for a, (rate, ses, name, seconds) in zip(axes[:3], [("9:00", s["followup2"], "step-rate-0900", 3.0), ("12:00", s["followup2"], "step-rate-1200", 2.0),
                                                        ("1:30", s["followup2"], "step-rate-0130", 1.0)]):
        t, fz = notch_track(ses, name, seconds)
        a.semilogy(t, fz, ".", ms=3, color="k", rasterized=True)
        hold = measured["step"][rate]["hold_ms"]
        a.set_title(f"Step, Rate {rate}, Depth 5:00: the notches over time\nhold {hold:.0f} ms, LFO {measured['step'][rate]['lfo_hz']:.3g} Hz", fontsize=9)
        a.set_xlabel("s")
        a.set_ylabel("notch Hz")
        a.grid(alpha=0.3, which="both")
    a = axes[3]
    holds = FINDINGS["step"]["hold_ms"]
    lfo = FINDINGS["sweep"]["lfo_hz"]
    periods = np.array([1 / lfo[r] for r in holds])
    a.loglog(periods, [holds[r] for r in holds], "o", color="k", label="hold time (3:00 uncertain)")
    for r, p in zip(holds, periods):
        a.annotate(r, (p, holds[r]), textcoords="offset points", xytext=(4, 4), fontsize=8)
    grid = np.geomspace(0.1, 30, 100)
    a.loglog(grid, 77 * np.sqrt(grid), "--", color="C3", lw=0.8, label="77 ms · √period")
    a.loglog(grid, 1000 * grid / 8, ":", color="C0", lw=0.8, label="period / 8")
    a.set_xlabel("LFO period (s)")
    a.set_ylabel("hold (ms)")
    a.set_title("Step's hold time against the LFO period", fontsize=9)
    a.grid(alpha=0.3, which="both")
    a.legend(fontsize=8)
    figure.suptitle("Boss PH-3 — Step: the 8 Stage sweep sampled and held", fontsize=13)
    figure.savefig(out_dir / "ph-3-step.svg", metadata={"Date": None})
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("session", help="the Stage modes session (2026-10-28); fit/ goes here")
    parser.add_argument("modes_session")
    parser.add_argument("slow_session")
    parser.add_argument("followup_session")
    parser.add_argument("followup2_session")
    args = parser.parse_args()
    s = {role: pathlib.Path(p).expanduser() for role, p in [("main", args.session), ("modes", args.modes_session), ("slow", args.slow_session),
                                                              ("followup", args.followup_session), ("followup2", args.followup2_session)]}
    for p in s.values():
        if not (p / "rig.json").exists():
            sys.exit(f"{p} is not a capture session")
    measured = measure(s)
    result = {"pedal": "Boss PH-3 Phase Shifter", "sessions": [p.name for p in s.values()], "findings": FINDINGS, "measured": measured}
    out_dir = s["main"] / "fit"
    out_dir.mkdir(exist_ok=True)
    with open(out_dir / "ph-3.json", "w", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    plot_static_and_sweep(out_dir, s, measured)
    plot_res(out_dir, s, measured)
    plot_step(out_dir, s, measured)
    summary = {"notches": {m: [n[:2] for n in v["notches"]] for m, v in measured["static"].items()},
               "res k": {m: {r: v["k"] for r, v in d.items() if r != "frozen pairs"} for m, d in measured["res"].items()},
               "step hold ms": {r: v["hold_ms"] for r, v in measured["step"].items()},
               "lfo hz": {r: v["hz"] for r, v in measured["sweep"]["lfo_hz"].items()},
               "level db re bypass": measured["level_db_re_bypass"]}
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
