#!/usr/bin/env python3
"""Capture-calibrated, causal 48 kHz RV-6 Shimmer listening model. Run from the repository root.

  python3 tools/capture/rv6/shimmer_prototype.py fit
  python3 tools/capture/rv6/shimmer_prototype.py render DI.wav out.wav --time 0 --tone .5

Calibration uses only sweeps, noise bursts and a 330 Hz sine. The model takes mono dry audio.
The stereo output includes the focused session's recording chain. See SHIMMER-ROUTING.md.
"""
import argparse
import functools
from pathlib import Path
import tempfile

import numpy as np
import soundfile
from scipy import fft, ndimage, optimize, signal, spatial

import focused
import shimmer_structure as study
import takes

RATE = takes.RATE
N = 1 << 20
HZ = fft.rfftfreq(N, 1 / RATE)
HOP = 1 / (5 * study.CLOCK)
PITCH_LOWPASS = signal.firwin(95, 9900, fs=RATE, window=("kaiser", 8.6))
BANDS = 125 * 2. ** (np.arange(14) / 2 - .5)
CACHE = Path("build-rv6/shimmer-model")
DARK = ("tone-0700-time-0700", "tone-0700", "tone-0700-time-0500")
BRIGHT = ("tone-0500-time-0700", "tone-0500", "tone-0500-time-0500")
POSITIONS = {
    "time-0700": (0., .5), "time-0900": (.2, .5), "noon": (.5, .5),
    "time-0300": (.8, .5), "time-0500": (1., .5), "tone-0300": (.5, .8),
    **{name: (i / 2, 0.) for i, name in enumerate(DARK)},
    **{name: (i / 2, 1.) for i, name in enumerate(BRIGHT)},
}


@functools.lru_cache(maxsize=6)
def probes(position, loudest_burst=False):
    name = position + "-shimmer"
    meta, x, *_ = takes.load(name)
    a, b = takes.wet(name)
    rows = {}
    for spec, start, body, tail in focused.parts_of(meta):
        kind = spec["type"]
        if kind == "noise-steps":
            spacing = round((spec["step_seconds"] + spec.get("gap_seconds", 0)) * RATE)
            db = max(spec["levels_db"]) if loudest_burst else -12
            start += spec["levels_db"].index(db) * spacing
            kind, length = "burst", 6 * RATE
        elif kind == "sweep":
            length = min(body + tail, 18 * RATE)
        elif kind == "sine":
            length = min(body + tail, 14 * RATE)
        else:
            continue
        sl = slice(start, start + length)
        rows[kind] = (x[sl].copy(), np.column_stack((a[sl], b[sl])),
                      np.column_stack((a[start-11000:start-2000], b[start-11000:start-2000])))
    takes.load.cache_clear()
    return rows


def spectrum(x):
    return fft.rfft(x, n=N, axis=0)


@functools.lru_cache(maxsize=6)
def sweep(position):
    x, y, _ = probes(position)["sweep"]
    return spectrum(x), spectrum(study.tapered(y))


def inverse_sweep(position):
    x, y = sweep(position)
    power = abs(x) ** 2
    return fft.irfft(y * (x.conj() / (power + 1e-9 * power.max()))[:, None], axis=0)


def project(h, direction, length, fade, passes):
    """Fit a causal FIR with the specified output direction. Only the design uses a two-sided FFT."""
    nfft = 2 * (len(direction) - 1)
    for _ in range(passes):
        response = fft.rfft(h, n=nfft, axis=0)
        response = direction * np.sum(response * direction.conj(), axis=1)[:, None]
        h = fft.irfft(response, n=nfft, axis=0)[:length].copy()
        h[-len(fade):] *= fade[:, None]
    return h


def seed_filter(dark, seed, duration, passes, whiten=False):
    length = round(RATE * duration)
    h = inverse_sweep(seed)[:length].copy()
    t = np.arange(length) / RATE
    h *= np.clip((t - .009) / .015, 0, 1)[:, None]
    _, y = sweep(dark)
    # The phase introduced by deconvolution is common to both ears.
    direction = y[::2] / np.maximum(np.linalg.norm(y[::2], axis=1, keepdims=True), 1e-20)
    fade = np.cos(np.linspace(0, np.pi / 2, round(.014 * RATE))) ** 2
    h = project(h, direction, length, fade, passes)
    if whiten:
        nfft = N // 2
        response = fft.rfft(h, n=nfft, axis=0)
        power = np.sum(abs(response) ** 2, axis=1)
        smooth = ndimage.gaussian_filter1d(power, 72)
        power = np.maximum.reduce((power, .025 * smooth, np.full_like(power, 1e-5 * smooth.max())))
        # Minimum-phase inverse of the total stereo magnitude retains the measured interaural phase.
        cepstrum = fft.irfft(.5 * np.log(power))
        cepstrum[1:nfft//2] *= 2
        cepstrum[nfft//2+1:] = 0
        h = fft.irfft(response * np.exp(-fft.rfft(cepstrum))[:, None], axis=0)[:length + round(.06 * RATE)]
    h[-1200:] *= np.cos(np.linspace(0, np.pi / 2, 1200))[:, None] ** 2
    return h


def triangle(x, intercept):
    t = np.arange(len(x)) / RATE
    lag = (intercept - t - .006) % HOP
    window = 1 - abs(2 * lag / HOP - 1)
    # Each read runs at double speed. Remove frequencies that would fold into the next octave.
    limited = signal.lfilter(PITCH_LOWPASS, [1.], x)
    return window * np.interp(t - lag - .006, t, limited, left=0.)


def sweep_phase(position, seed):
    """Read the free-running grain phase from the sweep's isolated first octave."""
    x, _, _ = probes(position)["sweep"]
    start_frequency, stop_frequency, seconds = 20., 20000., 10.
    octave_lead = seconds * np.log(2) / np.log(stop_frequency / start_frequency)
    t = np.arange(N) / RATE
    t[t > 8] -= N / RATE
    lag = t + octave_lead
    window = np.clip((lag - .005) / .025, 0, 1) * np.clip((.94 - lag) / .15, 0, 1)
    first = spectrum(inverse_sweep(position) * window[:, None]) * spectrum(x)[:, None]
    cross = np.sum(first * spectrum(seed).conj(), axis=1) * ((HZ >= 250) & (HZ <= 1200))
    offsets = np.arange(-round(RATE * HOP / 20), round(RATE * HOP / 20))
    peak, intercept = 0., 0.
    for initial in np.arange(10) * HOP / 10:
        corr = abs(fft.irfft(cross * spectrum(triangle(x, initial)).conj())[offsets])
        j = np.argmax(corr)
        if corr[j] > peak:
            # At read speed two, moving the output one sample moves the read intercept two.
            peak, intercept = corr[j], (initial + 2 * offsets[j] / RATE) % HOP
    return intercept


def order_responses(position, dark, bright):
    """Separate the first four octave orders of the sweep under a finite, causal support constraint."""
    time, tone = POSITIONS[position]
    if tone == 0:
        return [seed_filter(dark, position, .72 + .17 * time, 5)]
    x, y, _ = probes(position)["sweep"]
    intercept = sweep_phase(position, seed_filter(dark, bright, .82, 6))
    inputs = [x]
    for _ in range(3):
        inputs.append(triangle(inputs[-1], intercept))
    q = np.stack([spectrum(v) for v in inputs])
    power = abs(q) ** 2
    smooth = ndimage.gaussian_filter1d(power, 60, axis=1)
    inverse = q.conj() / (power + .12 * smooth + 1e-8 * smooth.max(axis=1, keepdims=True))
    length = round(RATE * (.74 + .17 * time))
    t = np.arange(length) / RATE
    taper = (np.sin(np.pi/2 * np.clip((length/RATE - t) / .16, 0, 1)) *
             np.sin(np.pi/2 * np.clip((t - .01) / .017, 0, 1))) ** 2
    band = (np.sin(np.pi/2 * np.clip((HZ-30)/22, 0, 1)) *
            np.sin(np.pi/2 * np.clip((19000-HZ)/3500, 0, 1))) ** 2
    _, d = sweep(dark)
    direction = d / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-20)
    responses = np.zeros((len(q), len(HZ), 2), complex)
    residual = spectrum(y)
    for _ in range(5):
        for j in range(len(q)):
            residual += q[j, :, None] * responses[j]
            h = fft.irfft(residual * (band * inverse[j])[:, None], axis=0)[:length] * taper[:, None]
            h = project(h, direction, length, taper, 1)
            responses[j] = spectrum(h)
            residual -= q[j, :, None] * responses[j]
    return [fft.irfft(v, axis=0)[:length] for v in responses]


def filters(position):
    """Factor the measured early orders and octave-off tail through a single stereo response."""
    time, tone = POSITIONS[position]
    index = int(np.argmin(abs(np.array([0., .5, 1.]) - time)))
    dark, bright = DARK[index], BRIGHT[index]
    room = seed_filter(dark, bright, .46 + .96 * time, 8, whiten=True)
    if tone > 0:
        covariance = 0.
        for name, weight in ((position, 1.), (dark, 1.), (bright, .5)):
            _, s = sweep(name)
            s = s[::2]
            norm = ndimage.gaussian_filter1d(np.sum(abs(s) ** 2, axis=1), 70) + 1e-15
            covariance += (weight/norm)[:, None, None] * s[:, :, None] * s[:, None, :].conj()
        direction = np.linalg.eigh(covariance)[1][:, :, -1]
        room = project(room, direction, len(room), np.linspace(1, 0, 1200), 4)
    # Allocate the first 15 ms of the measured predelay to the upstream mono filters.
    room = room[round(.015 * RATE):].copy()
    response = fft.rfft(room, n=N//2, axis=0)
    p = np.sum(abs(response) ** 2, axis=1)
    inverse = response.conj() / np.maximum(p + .025 * ndimage.gaussian_filter1d(p, 50), 1e-12)[:, None]
    length = round(RATE * (.43 + .24 * time))
    fade = np.cos(np.pi/2 * np.clip((np.arange(length)/length - .38) / .62, 0, 1)) ** 2
    early = []
    for h in order_responses(position, dark, bright):
        scalar = np.sum(inverse * fft.rfft(h, n=N//2, axis=0), axis=1)
        early.append(fft.irfft(scalar)[:length] * fade)

    response = spectrum(room)
    p = np.sum(abs(response) ** 2, axis=1)
    late = np.sum(spectrum(inverse_sweep(dark)) * response.conj(), axis=1)
    late /= p + .06 * ndimage.gaussian_filter1d(p, 100) + .003
    band = (np.sin(np.pi/2 * np.clip((HZ-45)/45, 0, 1)) *
            np.sin(np.pi/2 * np.clip((11500-HZ)/3500, 0, 1))) ** 2
    t = np.arange(round(RATE * (3.8 + 6.5*time))) / RATE
    late = fft.irfft(late * band)[:len(t)]
    late *= (np.sin(np.pi/2 * np.clip((t-.22)/.26, 0, 1)) *
             np.sin(np.pi/2 * np.clip((t[-1]-t)/.6, 0, 1))) ** 2
    late *= np.exp(-t * 1.55/(1+3*time) * min(1., 2*tone))
    return np.array(early), late, room


def convolve(x, h):
    if h.ndim == 2:
        x = x[:, None]
    return signal.oaconvolve(np.asarray(x, dtype=float), np.asarray(h, dtype=float), axes=0)[:len(x)]


def moving_read(x, phase):
    t = np.arange(len(x)) / RATE
    lag = .00125 * (1.1 + np.sin(2*np.pi * (study.CLOCK*t + phase)))
    return np.interp(t-lag, t, x, left=0.)


def sources(x, early, late, phase, pulse=.75):
    intercept = -phase / study.CLOCK
    shifted = x
    for j, h in enumerate(early):
        if j:
            shifted = triangle(shifted, intercept)
        yield convolve(shifted, h)
    tail = convolve(x, late)
    angle = 2*np.pi * (study.CLOCK * np.arange(len(x))/RATE + phase)
    tail = (.5*tail + .72*moving_read(tail, phase)) * (1 + pulse*np.cos(angle))
    yield tail
    if len(early) > 1:
        for _ in range(2):
            tail = triangle(tail, intercept)
            yield tail


def features(y):
    """Stereo power in half-octaves, averaged over adjacent 20 ms STFT frames."""
    size, hop = 4096, 960
    a = np.pad(y, ((size//2, size//2), (0, 0)))
    windows = np.lib.stride_tricks.sliding_window_view(a, size, axis=0)[::hop]
    p = np.sum(abs(fft.rfft(windows * signal.windows.hann(size, sym=False))) ** 2, axis=1)
    f = fft.rfftfreq(size, 1/RATE)
    bands = np.column_stack([p[:, (f>b*2**-.25) & (f<=b*2**.25)].sum(axis=1) for b in BANDS])
    return ndimage.gaussian_filter1d(bands, 5, axis=0, mode="nearest")[::5].copy()


def gain_filter(gain):
    h = signal.firwin2(1025, np.r_[0., BANDS, RATE/2], np.r_[gain[0], gain, gain[-1]], fs=RATE)
    return signal.minimum_phase(h, half=False, n_fft=4096)


def fit_levels(position, early, late, room):
    groups = []
    for kind, (x, y, pre) in probes(position).items():
        bank = np.mean([[features(convolve(v, room)) for v in sources(x, early, late, phase)]
                        for phase in (0., .23, .57)], axis=0)
        target = features(y)
        floor = np.clip(features(pre).mean(axis=0), target.max(axis=0)*1e-5, target.max(axis=0)*1e-2)
        sec = np.arange(len(target)) * .1
        windows = {"sweep": [(0, 14, 1.2)], "burst": [(0, .65, .8), (.65, 1.65, .7), (1.65, 4.2, .55)],
                   "sine": [(.35, 4.9, .38), (5.05, 8.5, .4)]}
        for start, stop, w in windows[kind]:
            use = (sec >= max(.1, start)) & (sec < stop)
            mask = target[use] > 5*floor
            count = mask.sum(axis=0)
            weight = w * np.sqrt(mask / np.maximum(count, 1)) * np.minimum(1., np.sqrt(count/5))
            groups.append((target[use], bank[:, use], floor, weight))

    def residual(values):
        v = values.reshape(len(BANDS), -1)
        gain = np.exp(v.T)[:, None, :]
        result = [(10*np.log10(((gain*b).sum(axis=0)+floor)/(y+floor))*w).ravel()
                  for y, b, floor, w in groups]
        result.extend((.46*np.diff(v, axis=0).ravel(), .67*np.diff(v, n=2, axis=0).ravel(), .13*values))
        return np.concatenate(result)

    v = optimize.least_squares(residual, np.zeros(len(BANDS)*len(bank)), bounds=(-16, 4), max_nfev=95).x
    gains = np.exp(.5*v.reshape(len(BANDS), -1)).T
    return np.array([gain_filter(g) for g in gains])


def note_lines(wet, pitch, order):
    centre = round((order-1) * pitch / study.CLOCK)
    offsets = np.arange(-27, 28)
    envelope = study.envelope(wet, pitch + centre*study.CLOCK)
    idx = np.arange(2*study.ENV_RATE, round(4.85*study.ENV_RATE))
    basis = np.exp(2j*np.pi*study.CLOCK * np.outer(idx/study.ENV_RATE, offsets))
    return np.linalg.lstsq(basis, envelope[idx], rcond=None)[0], (centre+offsets) % 5 == 0


def equalized_sources(x, early, late, eq, phase, pulse=.75):
    return [convolve(v, h) for v, h in zip(sources(x, early, late, phase, pulse), eq, strict=True)]


def fit_motion(position, early, late, room, eq, pulse=.75):
    x, y, _ = probes(position)["sine"]
    x, y = x[:round(5.5*RATE)], y[:round(5.5*RATE)]
    orders = 2**np.arange(len(early))
    notes = [note_lines(y, 329.63, k) for k in orders]
    targets = np.array([np.sum(abs(c[keep])**2)/np.sum(abs(c)**2) for c, keep in notes])
    states = []
    for phase in (0., .33):
        parts = equalized_sources(x, early, late, eq, phase, pulse)
        plain = convolve(sum(parts), room)
        delta = [convolve(moving_read(v, phase)-v, room) for v in parts[:len(early)]]
        states.append([(note_lines(plain, 329.63, k)[0],
                        np.stack([note_lines(v, 329.63, k)[0] for v in delta], axis=-1)) for k in orders])

    def residual(g):
        fractions = []
        for state in states:
            powers = [abs(c + d@g)**2 for c, d in state]
            fractions.append([p[keep].sum()/p.sum() for p, (_, keep) in zip(powers, notes)])
        return np.r_[np.mean(fractions, axis=0)-targets, .026*g, .025*np.diff(g)]

    return optimize.least_squares(residual, np.full(len(early), .25), bounds=(0., 1.)).x


def buses(x, bank, phase, climb=1.):
    parts = equalized_sources(x, bank["early"], bank["late"], bank["eq"], phase, bank["pulse"])
    for j, amount in enumerate(bank["motion"]):
        parts[j] += amount * (moving_read(parts[j], phase) - parts[j])
    split = len(bank["early"])
    if climb < 1.:
        # The forward octave follows the Tone interpolation. Each additional octave also
        # requires another pass through the pitched feedback return.
        for start, count in ((0, split), (split, len(parts)-split)):
            for order in range(2, count):
                parts[start+order] *= climb**(order-1)
    return sum(parts[:split]), sum(parts[split:])


def anchor_wet(x, bank, phase, climb=1.):
    early, late = buses(x, bank, phase, climb)
    return convolve(early+late, bank["room"])


def fit_release(position, bank):
    band = signal.butter(4, [1000, 4000], fs=RATE, btype="bandpass", output="sos")
    filtered = functools.partial(signal.sosfilt, band, axis=0)
    # A four-second clock estimate needs audible decay throughout the window.
    amplitudes = np.linspace(0., 1., 11)
    errors = []
    for kind, off in (("burst", .05), ("sine", 5.)):
        x, wet, pre = probes(position, loudest_burst=True)[kind]
        if np.mean(filtered(wet)[round((off+3.8)*RATE):round((off+4.3)*RATE)]**2) <= 7*np.mean(filtered(pre)**2):
            continue
        x = x[:round((off+4.35)*RATE)]
        target = np.array(study.guitar_ripple(wet, round(off*RATE)))
        profiles = []
        for phase in (0., .35):
            bank["pulse"] = 0.
            flat = anchor_wet(x, bank, phase)
            bank["pulse"] = 1.
            delta = anchor_wet(x, bank, phase) - flat
            profiles.append([study.guitar_ripple(flat+amount*delta, round(off*RATE)) for amount in amplitudes])
        errors.append(np.mean((np.mean(profiles, axis=0)-target)**2, axis=1))
    if errors:
        bank["pulse"] = amplitudes[np.argmin(np.mean(errors, axis=0))]
    # Change the late send only as far as both probes support. The early and late parts can
    # overlap in the scoring window, so include their cross term when changing the send.
    gains = []
    for kind, note_off in (("sine", 5.), ("burst", .05)):
        x, wet, pre = probes(position)[kind]
        sl = slice(round((note_off+.8)*RATE), round((note_off+1.6)*RATE))
        target = np.mean(filtered(wet)[sl]**2) - np.mean(filtered(pre)**2)
        if target <= 4*np.mean(filtered(pre)**2):
            continue
        coefficients = []
        for phase in (0., .25, .5, .75):
            a, b = [filtered(convolve(v, bank["room"]))[sl] for v in buses(x, bank, phase)]
            coefficients.append((np.mean(b*b), 2*np.mean(a*b), np.mean(a*a)))
        curve = np.mean(coefficients, axis=0)
        trial = np.geomspace(.5, 2., 121)
        gains.append(trial[np.argmin(abs(np.polyval(curve, trial)-target))])
    if len(gains) == 2:
        bank["late"] *= np.clip(1., min(gains), max(gains))
    bank["motion"] = fit_motion(position, bank["early"], bank["late"], bank["room"], bank["eq"], bank["pulse"])


def fit_decay_slope(position, bank):
    """Correct a fast return's slope when both probes resolve a 20 dB drop above the noise."""
    band = signal.butter(4, [1000, 4000], fs=RATE, btype="bandpass", output="sos")
    age = np.arange(len(bank["late"])) / RATE
    tilt = np.clip(age-.8, -.55, 1.35)
    fits = []
    for kind, off in (("burst", .05), ("sine", 5.)):
        x, wet, pre = probes(position)[kind]
        starts = np.arange(.4, 2.51, .2) + off

        def power(y):
            filtered = signal.sosfilt(band, y, axis=0)
            return np.array([np.mean(filtered[round(t*RATE):round((t+.2)*RATE)]**2) for t in starts])

        floor = np.mean(signal.sosfilt(band, pre, axis=0)**2)
        target = power(wet) - floor
        keep = target > 3*floor
        if keep.sum() < 4 or np.ptp(10*np.log10(target[keep])) < 20.:
            return
        x = x[:round((starts[-1]+.2)*RATE)]
        curves = []
        for slope in (0., 1.):
            trial = dict(bank, late=bank["late"]*np.exp(slope*tilt))
            curves.append(np.mean([power(anchor_wet(x, trial, ph)) for ph in (0., .25, .5, .75)], axis=0)[keep])
        wanted = 10*np.log10(target[keep]/curves[0])
        step = 10*np.log10(curves[1]/curves[0])
        step -= step.mean()
        fits.append(np.dot(step, wanted-wanted.mean()) / max(np.dot(step, step), 1e-12))
    # Use the smallest correction supported by both recordings. Limit the gain change beyond
    # the measured interval, where the original dark sweep can contain the recording floor.
    slope = np.clip(np.clip(0., min(fits), max(fits)), -1.5, 1.5)
    bank["late"] *= np.exp(slope*tilt)


def match_probe_levels(position, bank):
    # The early and late sources share some partials. Measure their actual coherent mix after
    # introducing the moving reads, which change the power in each band.
    corrections = []
    for kind in ("sweep", "burst"):
        x, wet, pre = probes(position)[kind]
        target = features(wet).mean(axis=0)
        noise = features(pre).mean(axis=0)
        predicted = np.mean([features(anchor_wet(x, bank, phase)).mean(axis=0)
                             for phase in (0., .25, .5, .75)], axis=0)
        audible = np.maximum(target - noise, target * .01)
        confidence = np.clip(1 - 3*noise/np.maximum(target, 1e-20), 0, 1)
        corrections.append((10*np.log10(audible/np.maximum(predicted, 1e-20)), confidence))
    numerator = sum(delta * confidence for delta, confidence in corrections)
    denominator = sum(confidence for _, confidence in corrections)
    correction = np.clip(numerator / np.maximum(denominator, 1.), -6., 6.)
    gain = 10**(ndimage.gaussian_filter1d(correction, .65, mode="nearest")/20)
    return signal.oaconvolve(bank["room"], gain_filter(gain)[:, None], axes=0)


def reference_spectrum(bank):
    # A common input measures cancellation between neighbouring settings at each frequency.
    rng = np.random.default_rng(706)
    noise = signal.sosfilt(signal.butter(2, 2300, fs=RATE, output="sos"), rng.normal(size=RATE))
    return fft.rfft(anchor_wet(np.r_[noise, np.zeros(2*RATE)], bank, 0.), axis=0)[::8].astype(np.complex64)


def calibrate(position, directory):
    early, late, room = filters(position)
    eq = fit_levels(position, early, late, room)
    bank = dict(early=early, late=late, room=room, eq=eq, motion=fit_motion(position, early, late, room, eq),
                pulse=np.array(.75))
    bank["room"] = match_probe_levels(position, bank)
    fit_release(position, bank)
    fit_decay_slope(position, bank)
    save_bank(position, bank, directory)


def save_bank(position, bank, directory):
    directory.mkdir(parents=True, exist_ok=True)
    arrays = {k: np.asarray(v, dtype=np.float32) for k, v in bank.items() if k != "reference"}
    with tempfile.NamedTemporaryFile(dir=directory, suffix=".npz", delete=False) as temporary:
        np.savez(temporary, reference=reference_spectrum(arrays), **arrays)
    Path(temporary.name).replace(directory / f"{position}.npz")


def anchors(time, tone):
    if not (0 <= time <= 1 and 0 <= tone <= 1):
        raise ValueError("Time and Tone must be in [0, 1]")
    # The measured forward octave is only 0.162/1.742 of noon at Tone 9:00 (SHIMMER.forward).
    tone = np.interp(tone, (0., .2, .5, 1.), (0., .5*.162/1.742, .5, 1.))
    names = list(POSITIONS)
    coords = np.array(list(POSITIONS.values()))
    matches = np.flatnonzero(np.max(abs(coords - (time, tone)), axis=1) < 1e-9)
    if len(matches):
        return [(names[matches[0]], 1.)]
    mesh = spatial.Delaunay(coords)
    simplex = mesh.find_simplex((time, tone))
    if simplex < 0:
        raise ValueError("Time and Tone must be in [0, 1]")
    transform = mesh.transform[simplex]
    uv = transform[:2] @ (np.array([time, tone]) - transform[2])
    return [(names[i], float(w)) for i, w in zip(mesh.simplices[simplex], np.r_[uv, 1-uv.sum()]) if w > 1e-9]


def interpolate_wet(x, time, tone, level, directory, phase):
    if not 0 <= level <= 1:
        raise ValueError("E.Level must be in [0, 1]")
    selected = anchors(time, tone)
    level_gain = (2*level)**(1.696 if level < .5 else 2.)
    if len(selected) == 1:
        with np.load(directory / f"{selected[0][0]}.npz") as bank:
            return level_gain * anchor_wet(x, bank, phase)
    result, reference, magnitude = 0., 0., 0.
    hz = fft.rfftfreq(3*RATE, 1/RATE)[::8]
    bands = [(hz > b*2**-.25) & (hz <= b*2**.25) for b in BANDS]
    climb = np.interp(tone, (0., .2, .5), (0., .0054/.417, 1.))
    for name, weight in selected:
        with np.load(directory / f"{name}.npz") as bank:
            result += weight * anchor_wet(x, bank, phase, climb)
            r = bank["reference"]
            reference += weight*r
            magnitude += weight*np.array([np.linalg.norm(r[m]) for m in bands])
    gain = magnitude / np.maximum([np.linalg.norm(reference[m]) for m in bands], 1e-20)
    return level_gain * signal.oaconvolve(result, gain_filter(gain)[:, None], axes=0)[:len(x)]


def fit_low_tone(directory):
    # The only genuine 9:00 Tone recording is check-a. Its burst supplies the spectral
    # correction between the octave-off and noon measurements. Both guitar parts stay held out.
    x, wet, pre = probes("check-a")["burst"]
    target = features(wet).mean(axis=0)
    noise = features(pre).mean(axis=0)
    power = np.mean([features(interpolate_wet(x, .8, .2, .8, directory, phase)).mean(axis=0)
                     for phase in (0., .25, .5, .75)], axis=0)
    correction = 10*np.log10(np.maximum(target-noise, target*.02) / np.maximum(power, 1e-20))
    correction *= np.clip(1-2*noise/np.maximum(target, 1e-20), 0., 1.)
    correction = ndimage.gaussian_filter1d(np.clip(correction, -12, 12), .65, mode="nearest")
    with tempfile.NamedTemporaryFile(dir=directory, suffix=".npy", delete=False) as temporary:
        np.save(temporary, correction)
    Path(temporary.name).replace(directory/"low-tone-dB.npy")


def save_dry(directory):
    a, b, _ = takes.dry_filters()
    with tempfile.NamedTemporaryFile(dir=directory, suffix=".npz", delete=False) as temporary:
        np.savez(temporary, a=a, b=b)
    Path(temporary.name).replace(directory/"dry.npz")


def render_wet(x, time, tone, level, directory=CACHE, phase=0.):
    wet = interpolate_wet(x, time, tone, level, directory, phase)
    if 0 < tone < .5:
        weight = np.interp(tone, (0., .2, .5), (0., 1., 0.))
        gain = 10**(weight*np.load(directory/"low-tone-dB.npy")/20)
        wet = signal.oaconvolve(wet, gain_filter(gain)[:, None], axes=0)[:len(x)]
    return wet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank", type=Path, default=CACHE)
    sub = parser.add_subparsers(dest="command", required=True)
    fit = sub.add_parser("fit")
    fit.add_argument("positions", nargs="*")
    sub.add_parser("fit-low-tone")
    render = sub.add_parser("render")
    render.add_argument("input", type=Path)
    render.add_argument("output", type=Path)
    for knob in ("time", "tone", "level"):
        render.add_argument("--"+knob, type=float, default=.5)
    render.add_argument("--tail", type=float, default=10.)
    render.add_argument("--wet-only", action="store_true")
    args = parser.parse_args()
    if args.command == "fit":
        for position in args.positions or POSITIONS:
            if position not in POSITIONS:
                parser.error(f"unknown calibration position {position}")
            calibrate(position, args.bank)
            print(position, flush=True)
        if not args.positions:
            fit_low_tone(args.bank)
        save_dry(args.bank)
        return
    if args.command == "fit-low-tone":
        fit_low_tone(args.bank)
        save_dry(args.bank)
        return
    x, rate = soundfile.read(args.input)
    if rate != RATE or x.ndim != 1 or not len(x) or args.tail < 0:
        parser.error("render requires a nonempty mono 48 kHz file and a nonnegative tail")
    x = np.pad(x, (0, round(args.tail*RATE)))
    y = render_wet(x, args.time, args.tone, args.level, args.bank)
    if not args.wet_only:
        with np.load(args.bank/"dry.npz") as dry:
            y += np.column_stack((takes.through(dry["a"], x), takes.through(dry["b"], x)))
    soundfile.write(args.output, y, RATE, subtype="FLOAT")


if __name__ == "__main__":
    main()
