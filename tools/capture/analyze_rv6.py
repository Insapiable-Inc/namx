#!/usr/bin/env python3
"""Measure the Boss RV-6 Reverb: Room, Hall, Plate, Spring, Modulate and Shimmer, stereo.

    python -I tools/capture/analyze_rv6.py SESSION [--audio-source WAV_DIRECTORY]
        [--namx-render BINARY]

SESSION supplies the sidecars and receives fit/ (rv-6.json and the figures); the audio
source is only read. --namx-render is tools/capture/render_reverb.cpp built against
NAM-X (the command is in its header): NAM-X's Plate, Hall and Room are then rendered at
the settings matched to the pedal and measured the same way. Needs numpy, scipy,
soundfile and matplotlib. Study: captures/boss/rv-6/README.md.

The WAVs hold three channels: Output A (input 2), the loopback (input 5), Output B
(input 1). Every response is a pedal channel over the loopback. The marker plays through
the reverb too, and at Time noon or 5:00 its tail runs into the signal: a burst take is
deconvolved across the whole take (the marker is most of its excitation), a sweep from its
start on (its own energy dwarfs the marker's tail, and two excitations leave ghosts). The
interface's own response and each take's latency rounding cancel in that ratio. The dry is the bypass path exactly (Level 7:00 equals the
bypass take to the noise floor), so the wet is the ratio over the bypass take's dry
response (its impulse response windowed to -2..+8 ms) minus one.
"""
import argparse
import functools
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "rv6-matplotlib"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import NullFormatter
import numpy as np
from scipy import fft, signal
from scipy.special import erfc
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402  (find_slips; JACK is only imported when recording)

FS = 48000
MODES = ["room", "hall", "plate", "spring", "modulate", "shimmer"]
LTI_MODES = ["room", "hall", "plate", "spring"]  # time-invariant: bursts deconvolve to IRs
OCTAVES = [125, 250, 500, 1000, 2000, 4000, 8000]
THIRDS = [63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000]
# A burst take is a 50 ms noise burst after the marker chirp (200 Hz-10 kHz): too little
# energy for a decay below 500 Hz or above 4 kHz once the tail is 40 dB down.
BURST_OCTAVES = [500, 1000, 2000, 4000]
SWEEP_BAND = (40.0, 16000.0)  # the sweeps' 20 Hz-20 kHz, inside the taper
# NAM-X settings matched to the pedal at noon (RT60 at 1 kHz; Damping from the 8 kHz/1 kHz
# ratio; a pre-delay equal to the pedal's onset minus NAM-X's own).
NAMX_MATCH = {"room": {"type": 2, "decay_pct": 47.5, "damping_pct": 0.0},
              "plate": {"type": 0, "decay_pct": 69.1, "damping_pct": 20.0},
              "hall": {"type": 1, "decay_pct": 74.6, "damping_pct": 0.0}}


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def taper(f, lo, hi):
    """1 inside lo..hi, raised-cosine to 0 over the octave below lo and the quarter above hi."""
    w = ((f >= lo) & (f <= hi)).astype(float)
    a = (f > lo / 2) & (f < lo)
    w[a] = 0.5 - 0.5 * np.cos(np.pi * np.log2(f[a] * 2 / lo))
    b = (f > hi) & (f < hi * 2 ** 0.25)
    w[b] = 0.5 + 0.5 * np.cos(np.pi * np.log2(f[b] / hi) * 4)
    return w


def bandlimit(v, lo=SWEEP_BAND[0], hi=SWEEP_BAND[1]):
    n = fft.next_fast_len(len(v))
    return fft.irfft(fft.rfft(v, n) * taper(fft.rfftfreq(n, 1 / FS), lo, hi), n)[:len(v)]


def narrowband(v, lo, hi):
    """Only lo..hi, brick-wall (bandlimit's taper reaches an octave below lo: a held tone
    an octave down leaks in at about −35 dB)."""
    n = fft.next_fast_len(len(v))
    f = fft.rfftfreq(n, 1 / FS)
    return fft.irfft(fft.rfft(v, n) * ((f >= lo) & (f <= hi)), n)[:len(v)]


def band(v, fc, per_octave=1):
    k = 2 ** (1 / (2 * per_octave))
    sos = signal.butter(4, [fc / k, min(fc * k, 23000)], "band", fs=FS, output="sos")
    return signal.sosfiltfilt(sos, v)


# The pedal's digital side whines at 2.9-3.05 kHz, ~50 dB under the wet, in surges every
# few seconds (in the bypass too); deconvolved, it lies all along an IR, and through the
# octave filters' skirts it reaches the 1, 2 and 4 kHz bands as false late "tail".
WHINE_HZ = (2850.0, 3100.0)


def notch(h, lo=WHINE_HZ[0], hi=WHINE_HZ[1]):
    n = fft.next_fast_len(len(h))
    H = fft.rfft(h, n)
    f = fft.rfftfreq(n, 1 / FS)
    H[(f >= lo) & (f <= hi)] = 0
    return fft.irfft(H, n)[:len(h)]


def rt60(h, fc, noise):
    """Schroeder decay of one octave band (the whine notched out first), noise subtracted and
    truncated where the 20 ms energy meets twice the floor; RT60 from -5 to -35 dB (T30),
    else -25 (T20)."""
    e = band(notch(h), fc) ** 2
    floor = np.mean(e[noise])
    smooth = np.convolve(e, np.ones(960) / 960, "same")
    above = np.nonzero(smooth[:noise.start] > 2 * floor)[0]
    end = above[-1] if len(above) else noise.start
    curve = np.cumsum(np.maximum(e[:end] - floor, 0)[::-1])[::-1]
    curve = 10 * np.log10(curve / curve[0] + 1e-30)

    def first(level):
        i = np.nonzero(curve < level)[0]
        return i[0] if len(i) else None

    a, z, kind = first(-5), first(-35), "T30"
    if z is None:
        z, kind = first(-25), "T20"
    if a is None or z is None or z < a + 10:
        return None, None
    slope = np.polyfit(np.arange(a, z) / FS, curve[a:z], 1)[0]
    return float(-60 / slope), kind


def envelope_ms(h, length):
    """1 ms RMS envelope."""
    return np.sqrt(np.convolve(h[:length] ** 2, np.ones(48) / 48, "same"))


def onset_ms(h, below_peak_db=20, seconds=0.8):
    e = envelope_ms(h, int(seconds * FS)) ** 2
    return float(np.argmax(e > e.max() * 10 ** (-below_peak_db / 10)) / FS * 1000), float(np.argmax(e) / FS * 1000)


def echo_density(h, window_ms=20, step_ms=5):
    """Abel & Huang's normalized echo density per window (1 = Gaussian, fully dense)."""
    w, step = int(window_ms * FS / 1000), int(step_ms * FS / 1000)
    values = []
    for a in range(0, len(h) - w, step):
        seg = h[a:a + w]
        s = np.std(seg)
        values.append(np.mean(np.abs(seg) > s) / erfc(1 / np.sqrt(2)) if s > 0 else 0.0)
    return np.arange(len(values)) * step / FS + window_ms / 2000, np.array(values)


def correlation(a, b):
    return float(np.sum(a * b) / np.sqrt(np.sum(a * a) * np.sum(b * b)))


class Session:
    def __init__(self, path, audio):
        self.path, self.audio = path, audio
        self.meta = {p.stem: json.loads(p.read_text()) for p in sorted(path.glob("*.json")) if p.stem != "rig"}
        self._dry = {}

    @functools.lru_cache(maxsize=4)
    def load(self, name):
        """Played signal and the recording (A, loop, B), aligned, the whole take."""
        m = self.meta[name]
        x, rate = sf.read(self.audio / "played" / f"{name}.wav")
        y, recorded_rate = sf.read(self.audio / f"{name}.wav")
        if rate != FS or recorded_rate != FS or x.ndim != 1 or y.ndim != 2 or y.shape[1] != 3:
            raise ValueError(f"Unexpected format: {name}")
        lat = m["latency_samples"]
        n = min(len(x), len(y) - lat)
        return x[:n], y[lat:lat + n]

    def ratio(self, name, ch):
        """Pedal channel over loopback, t = 0 at the signal's start. A burst take of a
        time-invariant mode is deconvolved across the whole take: its own 50 ms burst is weak,
        the marker (0 dB, 0.55 s before) is most of the excitation. Everything else from the
        signal on only: a sweep's own energy dwarfs the marker's tail, and two excitations
        leave ghosts at multiples of the marker-to-sweep delay (6-7 s at 1-2 kHz), which a
        long tail runs into; Modulate's and Shimmer's marker tail is another instance of a
        moving reverb anyway."""
        _, y = self.load(name)
        n = fft.next_fast_len(2 * len(y))
        start = self.meta[name]["signal_start"]
        whole = self.meta[name]["signal"]["type"] == "noise-steps" and name.split("-")[0] not in ("modulate", "shimmer")
        if not whole:
            y = y.copy()
            y[:start] = 0
        shifted = np.roll(np.pad(y, ((0, n - len(y)), (0, 0))), -start, axis=0)
        P, L = fft.rfft(shifted[:, ch]), fft.rfft(shifted[:, 1])
        return n, P * L.conj() / (np.abs(L) ** 2 + 1e-6 * np.max(np.abs(L)) ** 2)

    def dry(self, ch):
        """The bypass path against the loopback, windowed to -2..+8 ms (index 0 = -2 ms)."""
        if ch not in self._dry:
            n, H = self.ratio("bypass-sweep", ch)
            h = fft.irfft(H, n)
            before, after, fade = int(0.002 * FS), int(0.008 * FS), int(0.001 * FS)
            piece = np.r_[h[-before:], h[:after]]
            ramp = np.hanning(2 * fade)
            piece[:fade] *= ramp[:fade]
            piece[-fade:] *= ramp[fade:]
            self._dry[ch] = piece
        return self._dry[ch]

    def dry_spectrum(self, ch, n):
        piece, before = self.dry(ch), int(0.002 * FS)
        h = np.zeros(n)
        h[:len(piece) - before] = piece[before:]
        h[-before:] = piece[:before]
        return fft.rfft(h)

    @functools.lru_cache(maxsize=24)
    def wet_ir(self, name, ch, lo=SWEEP_BAND[0], hi=SWEEP_BAND[1], seconds=None):
        """The wet impulse response re the input (dry = 1 removed), band-limited; t = 0 at the dry."""
        n, H = self.ratio(name, ch)
        f = fft.rfftfreq(n, 1 / FS)
        h = fft.irfft((H / self.dry_spectrum(ch, n) - 1) * taper(f, lo, hi), n)
        length = int((seconds or self.valid_seconds(name)) * FS)
        return h[:length].astype(np.float32).astype(float)

    def valid_seconds(self, name):
        s = self.meta[name]["signal"]
        return s["tail"] if s["type"] == "sweep" else s["tail"] + 0.05

    def dry_whole(self, name, ch):
        """The take's dry at that output: its loopback through the bypass path, the whole take."""
        _, y = self.load(name)
        piece, before = self.dry(ch), int(0.002 * FS)
        return signal.fftconvolve(y[:, 1], piece)[before:before + len(y)]

    def wet_signal(self, name, ch, lo=SWEEP_BAND[0], hi=SWEEP_BAND[1]):
        """The recording minus its dry (the loopback through the dry path), from the signal's start."""
        _, y = self.load(name)
        return bandlimit(y[:, ch] - self.dry_whole(name, ch), lo, hi)[self.meta[name]["signal_start"]:]

    def predict(self, sweep, take, ch, seconds=8.0):
        """`take` recorded and as predicted from its loopback through `sweep`'s response."""
        n, H = self.ratio(sweep, ch)
        h = fft.irfft(H, n)
        neg = int(0.05 * FS)
        kernel = np.r_[h[-neg:], h[:int(seconds * FS)]]
        _, y = self.load(take)
        predicted = signal.fftconvolve(y[:, 1], kernel)[neg:neg + len(y)]
        start = self.meta[take]["signal_start"]
        return bandlimit(y[:, ch])[start:], bandlimit(predicted)[start:]

    def noise_slice(self, name):
        end = int(self.valid_seconds(name) * FS)
        return slice(end - int(0.6 * FS), end - int(0.1 * FS))


def audit(session):
    rows = {}
    for name, m in session.meta.items():
        played, _ = sf.read(session.audio / "played" / f"{name}.wav", dtype="float32")
        x, y = session.load(name)
        slips = capture.find_slips(y[:, 1], x, 0, FS)
        rows[name] = {"played_hash_matches": hashlib.sha256(played.tobytes()).hexdigest() == m["played_sha256"],
                      "latency_samples": m["latency_samples"], "xruns": m["xruns"],
                      "loop_offsets_samples": sorted({int(o) for _, o in slips}),
                      "peaks_dbfs": m["peaks_dbfs"]}
    for name in (n for n in session.meta if n.startswith("bypass")):
        _, y = session.load(name)
        start = session.meta[name]["signal_start"]
        a, b = y[start:, 0], y[start:, 2]
        rows[name]["b_minus_a_db"] = float(db(rms(b - a)) - db(rms(a)))
    return rows


def dry_path(session):
    _, y0 = session.load("bypass-sweep")
    _, y1 = session.load("hall-level-0700")
    n = min(len(y0), len(y1))
    report = {}
    for ch, out in ((0, "A"), (2, "B")):
        n_fft, H = session.ratio("hall-level-0700", ch)
        f = fft.rfftfreq(n_fft, 1 / FS)
        ratio = H / session.dry_spectrum(ch, n_fft)
        ok = (f > 50) & (f < 15000)
        wet = session.wet_ir("hall-level-0700", ch)
        report[out] = {"level_0700_minus_bypass_db": float(db(rms(y1[:n, ch] - y0[:n, ch])) - db(rms(y0[:n, ch]))),
                       "level_0700_re_dry_db_p1_p99_50_15000": [float(np.percentile(db(ratio[ok]), 1)),
                                                                float(np.percentile(db(ratio[ok]), 99))],
                       "level_0700_wet_per_sample_dbfs_0.1_2s": float(db(rms(wet[int(0.1 * FS):int(2 * FS)])))}
    return report


def invariance(session):
    """Each burst take against its prediction from the same mode's sweep take."""
    report = {}
    for mode in MODES:
        rec, pred = session.predict(f"{mode}-sweep", f"{mode}-burst", 0)
        entry = {}
        for (a, b) in ((0.07, 0.4), (0.6, 1.5)):
            s = slice(int(a * FS), int(b * FS))
            per_band = [correlation(bandlimit(rec, fc / 2 ** (1 / 6), fc * 2 ** (1 / 6))[s],
                                    bandlimit(pred, fc / 2 ** (1 / 6), fc * 2 ** (1 / 6))[s]) for fc in THIRDS]
            entry[f"{a}-{b}s"] = {"residual_db": float(db(rms(rec[s] - pred[s])) - db(rms(rec[s]))),
                                  "correlation_per_third": dict(zip(map(str, THIRDS), per_band)),
                                  "recorded_dbfs": float(db(rms(rec[s])))}
        report[mode] = entry
    return report


def decays(session):
    """RT60 per octave, A/B mean: sweeps at noon (all bands), bursts and Tone takes (500 Hz-4 kHz)."""
    report = {}
    for name in sorted(session.meta):
        if name.startswith("bypass") or name.endswith("plucks") or name.startswith("hall-level"):
            continue
        sweep = session.meta[name]["signal"]["type"] == "sweep"
        bands = OCTAVES if sweep else BURST_OCTAVES
        row = {}
        moving = name.split("-")[0] in ("modulate", "shimmer") and not sweep
        for fc in bands:
            # A moving reverb's burst: the decay of the recorded wet itself (interrupted noise).
            sources = [session.wet_signal(name, ch)[:int(session.valid_seconds(name) * FS)] if moving
                       else session.wet_ir(name, ch) for ch in (0, 2)]
            values = [rt60(v, fc, session.noise_slice(name)) for v in sources]
            if all(v[0] is not None for v in values):
                row[str(fc)] = {"rt60_s": float(np.mean([v[0] for v in values])),
                                "fit": "T30" if all(v[1] == "T30" for v in values) else "T20"}
        report[name] = row
    return report


def early(session):
    report = {}
    for mode in MODES:
        entry = {}
        for ch, out in ((0, "A"), (2, "B")):
            h = session.wet_ir(f"{mode}-sweep", ch)
            entry[f"onset_ms_{out}"], entry[f"peak_ms_{out}"] = onset_ms(h)
        if mode in LTI_MODES:
            ref = session.wet_ir(f"{mode}-burst", 0, 400, 4000)
            for time in ("0700", "0500"):
                h = session.wet_ir(f"{mode}-time-{time}", 0, 400, 4000)
                entry[f"time_{time}_onset_ms_A"] = onset_ms(h)[0]
                windows = {}
                for a, b in ((0.05, 0.1), (0.1, 0.15), (0.15, 0.25), (0.25, 0.4)):
                    s = slice(int(a * FS), int(b * FS))
                    windows[f"{a}-{b}s"] = {"correlation": correlation(h[s], ref[s]),
                                            "gain_db": float(db(rms(h[s])) - db(rms(ref[s])))}
                entry[f"time_{time}_re_noon"] = windows
                entry[f"time_{time}_total_energy_re_noon_db"] = float(10 * np.log10(np.sum(h ** 2) / np.sum(ref ** 2)))
        report[mode] = entry
    return report


def character(session):
    """Stereo coherence, A-B balance, echo density, the wet's spectrum and its energy (noon sweeps)."""
    report = {}
    for mode in MODES:
        a, b = session.wet_ir(f"{mode}-sweep", 0), session.wet_ir(f"{mode}-sweep", 2)
        onset = onset_ms((a + b) / 2)[0] / 1000
        entry = {"onset_s": onset, "coherence": {}, "a_minus_b_db": {}}
        for (t0, t1) in ((0, 0.1), (0.1, 0.5), (0.5, 2.0)):
            s = slice(int((onset + t0) * FS), int((onset + t1) * FS))
            key = f"{t0}-{t1}s"
            entry["coherence"][key] = {str(fc): correlation(band(a, fc)[s], band(b, fc)[s]) for fc in OCTAVES}
            entry["a_minus_b_db"][key] = float(db(rms(a[s])) - db(rms(b[s])))
        t, ned = echo_density(bandlimit(a[:int(1.5 * FS)], 200, 8000))
        entry["echo_density_after_onset"] = {f"{int(d * 1000)}ms": float(np.interp(onset + d, t, ned))
                                             for d in (0.02, 0.05, 0.1, 0.2, 0.5)}
        s = slice(int(onset * FS), int((onset + 2) * FS))
        levels = {fc: 10 * np.log10(np.sum(band((a + b) / 2, fc, 3)[s] ** 2))
                  for fc in [63, 125, 250, 500, 1000, 2000, 4000, 8000, 12500]}
        entry["spectrum_re_1k_db"] = {str(fc): float(v - levels[1000]) for fc, v in levels.items()}
        entry["wet_energy_re_dry_db"] = float(10 * np.log10((np.sum(a ** 2) + np.sum(b ** 2)) / 2))
        report[mode] = entry
    return report


def knob_level(session):
    report = {}
    for ch, out in ((0, "A"), (2, "B")):
        energy = {pos: float(10 * np.log10(np.sum(session.wet_ir(name, ch, seconds=7.5) ** 2)))
                  for pos, name in (("7:00", "hall-level-0700"), ("12:00", "hall-sweep"), ("5:00", "hall-level-0500"))}
        noon, top = session.wet_ir("hall-sweep", ch, seconds=7), session.wet_ir("hall-level-0500", ch, seconds=7)
        report[out] = {"wet_energy_re_dry_db": energy,
                       "5:00_re_noon_per_octave_db": {str(fc): float(10 * np.log10(np.sum(band(top, fc) ** 2) /
                                                                                np.sum(band(noon, fc) ** 2)))
                                                      for fc in OCTAVES}}
    return report


def knob_tone(session):
    """Tail spectrum (0.2-1.5 s) at Tone 7:00 and 5:00 over noon's, 1/6 octave, A/B mean."""
    centres = 1000 * 2 ** (np.arange(-14, 18) / 6)  # 200 Hz .. 7.1 kHz
    curves = {}
    for mode in ("hall", "spring"):
        for time in ("0700", "0500"):
            values = []
            for ch in (0, 2):
                s = slice(int(0.2 * FS), int(1.5 * FS))
                ref = session.wet_ir(f"{mode}-burst", ch)[s]
                h = session.wet_ir(f"{mode}-tone-{time}", ch)[s]
                f, p_ref = signal.welch(ref, FS, nperseg=8192)
                _, p = signal.welch(h, FS, nperseg=8192)
                values.append([10 * np.log10(np.sum(p[(f >= c / 2 ** (1 / 12)) & (f < c * 2 ** (1 / 12))]) /
                                             np.sum(p_ref[(f >= c / 2 ** (1 / 12)) & (f < c * 2 ** (1 / 12))]))
                               for c in centres])
            curves[f"{mode}-tone-{time}"] = np.mean(values, axis=0)
    return centres, curves


def spring(session):
    report = {}
    for ch, out in ((0, "A"), (2, "B")):
        h = session.wet_ir("spring-sweep", ch)
        f, t, S = signal.spectrogram(h[:int(0.5 * FS)], FS, nperseg=256, noverlap=248, nfft=4096)
        w = (t > 0.065) & (t < 0.125)
        freqs = [200, 500, 1000, 1500, 2000, 2500, 3000, 3300, 3600, 3900, 4200, 4500, 5000, 6000]
        report[f"first_arrival_ms_{out}"] = {str(q): float(t[w][np.argmax(S[np.argmin(abs(f - q)), w])] * 1000)
                                             for q in freqs}
        peaks = {}
        for lo, hi in ((300, 1000), (1000, 2000)):
            e = np.convolve(bandlimit(h[:int(2 * FS)], lo, hi) ** 2, np.ones(96) / 96, "same")[int(0.07 * FS):int(1.5 * FS)]
            e = e - e.mean()
            ac = np.correlate(e, e, "full")[len(e) - 1:]
            lags = np.arange(len(ac)) / FS * 1000
            ok = (lags > 5) & (lags < 80)
            found, _ = signal.find_peaks(ac[ok])
            top = sorted(sorted(found, key=lambda i: -ac[ok][i])[:3])
            peaks[f"{lo}-{hi}"] = [float(lags[ok][i]) for i in top]
        report[f"envelope_repeat_ms_{out}"] = peaks
    return report


def shimmer(session, f0=329.63, onset=9.0, frames=60):
    """Wet level at the pluck's frequency and its octaves, 50 ms frames, re the fundamental's peak."""
    report = {}
    for ch, out in ((0, "A"), (2, "B")):
        w = session.wet_signal("shimmer-plucks", ch)
        rows = {}
        for k in (1, 2, 4, 8):
            v = narrowband(w, f0 * k / 2 ** (1 / 12), f0 * k * 2 ** (1 / 12))
            rows[k] = np.array([db(rms(v[int((onset + i * 0.05) * FS):int((onset + (i + 1) * 0.05) * FS)]))
                                for i in range(frames)])
        peak = rows[1].max()
        report[out] = {f"{k}x": (rows[k] - peak).tolist() for k in rows}
    return report


def pitch_wobble(session, f0=329.63):
    """The wet's frequency after the 9 s pluck (sd in cents): only a coarse check, since
    even a static reverb's own resonances beat in a decaying sine's tail."""
    report = {}
    for mode in MODES:
        v = bandlimit(session.wet_signal(f"{mode}-plucks", 0), 260, 400)[int(9.4 * FS):int(15 * FS)]
        z = signal.hilbert(v)
        inst = np.gradient(np.unwrap(np.angle(z))) * FS / (2 * np.pi)
        k = int(0.02 * FS)
        inst = np.convolve(inst, np.ones(k) / k, "same")[k:-k]
        amp = db(np.abs(z))[k:-k]
        ok = amp > amp.max() - 45
        cents = 1200 * np.log2(inst[ok] / f0)
        report[mode] = {"sd_cents": float(np.std(cents)), "p5_p95_cents": [float(np.percentile(cents, 5)),
                                                                           float(np.percentile(cents, 95))]}
    return report


def namx_compare(session, binary):
    import subprocess
    report = {}
    for mode, settings in NAMX_MATCH.items():
        x = np.zeros(14 * FS, np.float32)
        x[0] = 1.0
        out = subprocess.run([str(binary), str(settings["type"]), str(settings["decay_pct"]),
                              str(settings["damping_pct"]), "0", "0"], input=x.tobytes(),
                             capture_output=True, check=True).stdout
        y = np.frombuffer(out, np.float32).reshape(-1, 2).astype(float)
        a, b = bandlimit(y[:, 0]), bandlimit(y[:, 1])
        noise = slice(int(13 * FS), int(13.9 * FS))
        rts = {}
        for fc in OCTAVES:
            values = [rt60(v, fc, noise)[0] for v in (a, b)]
            rts[str(fc)] = float(np.mean(values))
        onset, peak = onset_ms((a + b) / 2)
        t, ned = echo_density(bandlimit(a[:int(1.5 * FS)], 200, 8000))
        s = slice(int(onset / 1000 * FS), int((onset / 1000 + 2) * FS))
        levels = {fc: 10 * np.log10(np.sum(band((a + b) / 2, fc, 3)[s] ** 2))
                  for fc in [63, 125, 250, 500, 1000, 2000, 4000, 8000, 12500]}
        report[mode] = {"settings": settings, "rt60_s": rts, "onset_ms": onset, "peak_ms": peak,
                        "echo_density_after_onset": {f"{int(d * 1000)}ms": float(np.interp(onset / 1000 + d, t, ned))
                                                     for d in (0.02, 0.05, 0.1, 0.2, 0.5)},
                        "spectrum_re_1k_db": {str(fc): float(v - levels[1000]) for fc, v in levels.items()},
                        "wet_energy_db_at_mix_100": float(10 * np.log10((np.sum(a ** 2) + np.sum(b ** 2)) / 2)),
                        "_ir": (a, b)}
    return report


def save_figure(fig, directory, name):
    fig.tight_layout()
    path = directory / f"{name}.svg"
    fig.savefig(path, metadata={"Date": None, "Creator": "NAM-X RV-6 analysis"})
    path.write_text("\n".join(line.rstrip() for line in path.read_text().splitlines()) + "\n")
    plt.close(fig)


def plot(directory, session, results, namx):
    # 1. The first 400 ms of every mode at noon, A and B, and Time 7:00 / 5:00 (A, 400 Hz-4 kHz).
    fig, axes = plt.subplots(3, 2, figsize=(13, 11), sharex=True)
    length, step = int(0.4 * FS), 12  # envelopes drawn every 0.25 ms
    t = np.arange(length)[::step] / FS * 1000
    for ax, mode in zip(axes.T.flat, MODES):
        for ch, label in ((0, "A"), (2, "B")):
            ax.plot(t, db(envelope_ms(session.wet_ir(f"{mode}-sweep", ch), length)[::step]), lw=.8, label=f"noon {label}")
        if mode in LTI_MODES:
            for time, style in (("0700", ":"), ("0500", "--")):
                h = session.wet_ir(f"{mode}-time-{time}", 0, 400, 4000)
                ax.plot(t, db(envelope_ms(h, length)[::step]), style, lw=.8, color="k",
                        label=f"Time {time[:2].lstrip('0')}:00 A, 0.4-4 kHz")
        if mode in namx:
            a, b = namx[mode]["_ir"]
            pedal = session.wet_ir(f"{mode}-sweep", 0)
            scale = np.sqrt(np.sum(pedal[:4 * FS] ** 2) / np.sum(a[:4 * FS] ** 2))
            ax.plot(t, db(scale * envelope_ms((a + b) / 2, length)[::step]), lw=.6, color="tab:red", alpha=.7,
                    label="NAM-X matched (same wet energy)")
        ax.set(title=f"{mode}: wet impulse response, 1 ms RMS", ylim=(-115, -35), ylabel="dB re the dry's impulse")
        ax.grid(alpha=.25)
        ax.legend(fontsize=7)
    for ax in axes[-1]:
        ax.set_xlabel("ms after the dry")
    save_figure(fig, directory, "rv-6-early")

    # 2. RT60 per octave.
    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    d = results["decay"]
    for ax, mode in zip(axes.flat, MODES):
        for name, style, label in ((f"{mode}-sweep", "o-", "noon (sweep)"), (f"{mode}-time-0700", "s:", "Time 7:00"),
                                   (f"{mode}-burst", "^:", "noon (burst)"), (f"{mode}-time-0500", "v:", "Time 5:00")):
            row = d.get(name, {})
            fcs = [fc for fc in OCTAVES if str(fc) in row]
            ax.semilogx(fcs, [row[str(fc)]["rt60_s"] for fc in fcs], style, label=label)
        if mode in namx:
            ax.semilogx(OCTAVES, [namx[mode]["rt60_s"][str(fc)] for fc in OCTAVES], "x--", color="tab:red",
                        label="NAM-X matched")
        ax.set(title=mode + (" (time-variant: sweep IR is an average)" if mode in ("modulate", "shimmer") else ""),
               xlabel="octave band, Hz", ylabel="RT60, s", yscale="log", ylim=(0.2, 20))
        ax.set_xticks(OCTAVES, [str(fc) for fc in OCTAVES])
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.grid(alpha=.25, which="both")
        ax.legend(fontsize=7)
    save_figure(fig, directory, "rv-6-decay")

    # 3. Spring: spectrogram, dispersion, spectrum.
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6), gridspec_kw={"width_ratios": [2, 1, 1]})
    h = session.wet_ir("spring-sweep", 0)
    f, tt, S = signal.spectrogram(h[:int(0.6 * FS)], FS, nperseg=512, noverlap=448, nfft=2048)
    S = 10 * np.log10(S + 1e-30)
    keep = f <= 8000
    axes[0].imshow(S[keep][::2], origin="lower", aspect="auto", cmap="magma", vmin=S.max() - 70, vmax=S.max(),
                   extent=(tt[0] * 1000, tt[-1] * 1000, 0, f[keep][-1]), interpolation="nearest", rasterized=True)
    axes[0].set(title="spring, noon, A: each echo a chirp", xlabel="ms", ylabel="Hz")
    for out in ("A", "B"):
        arrival = results["spring"][f"first_arrival_ms_{out}"]
        axes[1].plot([float(q) for q in arrival], list(arrival.values()), "o-", label=out)
    axes[1].set(title="first arrival's peak time", xlabel="Hz", ylabel="ms")
    axes[1].legend()
    for mode in ("spring", "plate", "hall"):
        spec = results["character"][mode]["spectrum_re_1k_db"]
        axes[2].semilogx([float(k) for k in spec], list(spec.values()), "o-", label=mode)
    axes[2].set(title="wet spectrum (2 s), 1/3 octave", xlabel="Hz", ylabel="dB re 1 kHz", ylim=(-55, 15))
    axes[2].legend()
    for ax in axes[1:]:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "rv-6-spring")

    # 4. Knobs and invariance.
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6))
    centres, curves = results["_tone"]
    for name, curve in curves.items():
        top = 4500 if name.startswith("spring") else 7200  # the spring has nothing above ~4.5 kHz
        keep = centres <= top
        axes[0].semilogx(centres[keep], curve[keep], label=name.replace("-tone-", " Tone ").replace("0700", "7:00").replace("0500", "5:00"))
    axes[0].set(title="Tone: tail spectrum (0.2-1.5 s) re noon", xlabel="Hz", ylabel="dB", ylim=(-25, 15))
    axes[0].xaxis.set_minor_formatter(NullFormatter())
    axes[0].legend(fontsize=8)
    for out in ("A", "B"):
        row = results["level"][out]["wet_energy_re_dry_db"]
        axes[1].plot([0, 0.5, 1], list(row.values()), "o-", label=out)
    axes[1].set(title="Level: wet energy (Hall)", xlabel="knob (7:00 = 0, 5:00 = 1)", ylabel="dB re the dry")
    axes[1].legend()
    for mode in MODES:
        corr = results["invariance"][mode]["0.07-0.4s"]["correlation_per_third"]
        axes[2].semilogx([float(k) for k in corr], list(corr.values()), "o-", ms=3, label=mode)
    axes[2].set(title="burst take vs the sweep's prediction, 70-400 ms", xlabel="Hz", ylabel="correlation", ylim=(-0.4, 1.05))
    axes[2].xaxis.set_minor_formatter(NullFormatter())
    axes[2].legend(fontsize=7)
    for ax in axes:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "rv-6-knobs")

    # 5. Shimmer's octaves.
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.2), sharey=True)
    for ax, out in zip(axes, ("A", "B")):
        for k, values in results["shimmer"][out].items():
            ax.plot(np.arange(len(values)) * 50 + 25, values, "o-", ms=2, label=f"{k} (329.63 Hz × {k[:-1]})")
        ax.set(title=f"Shimmer, Output {out}: the wet after the 329.63 Hz pluck", xlabel="ms after the pluck",
               ylabel="dB re the fundamental's peak", ylim=(-70, 10))
        ax.grid(alpha=.25)
        ax.legend(fontsize=8)
    save_figure(fig, directory, "rv-6-shimmer")


# --- The follow-up session (rv-6-followup.json): Time, Tone and Level laws, held tones. ---

TIME_POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]


def clock_fraction(position):
    """A clock string on the knob's 300° turn: 7:00 = 0, 12:00 = 0.5, 5:00 = 1."""
    hour, minute = map(int, position.split(":"))
    return ((hour - 7) % 12 + minute / 60) / 10


def tag(position):
    return position.replace(":", "").zfill(4)


def predict_across(sweep_session, sweep, take_session, take, ch, seconds=6.0):
    """`take` (one session) recorded and as predicted through `sweep`'s response (another)."""
    n, H = sweep_session.ratio(sweep, ch)
    h = fft.irfft(H, n)
    neg = int(0.05 * FS)
    kernel = np.r_[h[-neg:], h[:int(seconds * FS)]]
    _, y = take_session.load(take)
    predicted = signal.fftconvolve(y[:, 1], kernel)[neg:neg + len(y)]
    start = take_session.meta[take]["signal_start"]
    return bandlimit(y[:, ch])[start:], bandlimit(predicted)[start:]


def followup_audit(first, second):
    """The takes, the dry against the first session's, and the first session's bursts
    predicted from this session's sweeps at the same settings (four sweeps slipped)."""
    report = {"takes": audit(second), "dry_re_first_session_db": {}, "first_bursts_from_these_sweeps": {}}
    for ch, out in ((0, "A"), (2, "B")):
        a, b = first.dry(ch), second.dry(ch)
        report["dry_re_first_session_db"][out] = float(db(rms(b - a)) - db(rms(a)))
    pairs = [("room-sweep-time-0700", "room-time-0700"), ("hall-sweep-tone-0700", "hall-tone-0700"),
             ("hall-sweep-time-0700", "hall-time-0700"), ("plate-sweep-time-0700", "plate-time-0700"),
             ("hall-sweep-time-0500", "hall-time-0500"), ("plate-sweep-time-0500", "plate-time-0500"),
             ("spring-sweep-time-0500", "spring-time-0500"), ("room-sweep-time-0500", "room-time-0500")]
    s = slice(int(0.07 * FS), int(0.4 * FS))
    for sweep, burst in pairs:
        entry = {}
        for ch, out in ((0, "A"), (2, "B")):
            rec, pred = predict_across(second, sweep, first, burst, ch)
            for lo, hi in ((40, 16000), (2800, 5600), (5600, 16000)):
                r, p = bandlimit(rec, lo, hi)[s], bandlimit(pred, lo, hi)[s]
                entry[f"{out}_{lo}-{hi}_residual_db"] = float(db(rms(r - p)) - db(rms(r)))
        report["first_bursts_from_these_sweeps"][f"{sweep} -> {burst}"] = entry
    return report


def time_law(first, second):
    """Per time-invariant mode and Time position: RT60 per octave, the wet's total energy and
    its first 50-100 ms (400 Hz-4 kHz) against noon, the onset. Noon is the first session's."""
    report = {}
    for mode in LTI_MODES:
        noon = {ch: first.wet_ir(f"{mode}-sweep", ch) for ch in (0, 2)}
        noon_early = {ch: first.wet_ir(f"{mode}-sweep", ch, 400, 4000) for ch in (0, 2)}
        rows = {}
        for pos in TIME_POSITIONS:
            session, name = (first, f"{mode}-sweep") if pos == "12:00" else (second, f"{mode}-sweep-time-{tag(pos)}")
            entry = {"fraction": clock_fraction(pos), "rt60_s": {}}
            for fc in OCTAVES:
                values = [rt60(session.wet_ir(name, ch), fc, session.noise_slice(name)) for ch in (0, 2)]
                if all(v[0] is not None for v in values):
                    entry["rt60_s"][str(fc)] = float(np.mean([v[0] for v in values]))
            early = slice(int(0.05 * FS), int(0.1 * FS))
            entry["energy_re_noon_db"] = float(np.mean([10 * np.log10(np.sum(session.wet_ir(name, ch) ** 2) /
                                                                      np.sum(noon[ch] ** 2)) for ch in (0, 2)]))
            entry["early_50_100ms_re_noon_db"] = float(np.mean([db(rms(session.wet_ir(name, ch, 400, 4000)[early])) -
                                                                db(rms(noon_early[ch][early])) for ch in (0, 2)]))
            entry["onset_ms_A"] = onset_ms(session.wet_ir(name, 0, 400, 4000))[0]
            rows[pos] = entry
        report[mode] = rows
    for mode in ("modulate", "shimmer"):  # moving tails: the recorded wet's own decay, 500 Hz-4 kHz
        rows = {}
        for pos in TIME_POSITIONS:
            session, name = ((first, f"{mode}-burst") if pos == "12:00" else
                             (first, f"{mode}-time-{tag(pos)}") if pos in ("7:00", "5:00") else
                             (second, f"{mode}-time-{tag(pos)}"))
            entry = {"fraction": clock_fraction(pos), "rt60_s": {}}
            for fc in BURST_OCTAVES:
                values = [rt60(session.wet_signal(name, ch)[:int(session.valid_seconds(name) * FS)], fc,
                               session.noise_slice(name)) for ch in (0, 2)]
                if all(v[0] is not None for v in values):
                    entry["rt60_s"][str(fc)] = float(np.mean([v[0] for v in values]))
            rows[pos] = entry
        report[mode] = rows
    return report


SIXTHS = 1000 * 2 ** (np.arange(-20, 25) / 6)  # 99 Hz .. 16 kHz


def band_energy(h, centres=SIXTHS, per_octave=6):
    power = np.abs(np.fft.rfft(h)) ** 2
    f = np.fft.rfftfreq(len(h), 1 / FS)
    k = 2 ** (1 / (2 * per_octave))
    return np.array([np.sum(power[(f >= c / k) & (f < c * k)]) for c in centres])


def tone_law(first, second):
    """Hall's wet transfer at Tone 7:00/9:00/3:00/5:00 over noon's, 1/6 octave (first 4 s of the IR)."""
    curves = {}
    for pos in ("7:00", "9:00", "3:00", "5:00"):
        values = []
        for ch in (0, 2):
            noon = band_energy(first.wet_ir("hall-sweep", ch)[:4 * FS])
            h = band_energy(second.wet_ir(f"hall-sweep-tone-{tag(pos)}", ch)[:4 * FS])
            values.append(10 * np.log10(h / noon))
        curves[pos] = np.mean(values, axis=0)
    return {"centres_hz": SIXTHS, "re_noon_db": curves}


def level_law(first, second):
    takes = {"7:00": (first, "hall-level-0700"), "9:00": (second, "hall-sweep-level-0900"),
             "12:00": (first, "hall-sweep"), "3:00": (second, "hall-sweep-level-0300"),
             "5:00": (first, "hall-level-0500")}
    report = {}
    for pos, (session, name) in takes.items():
        energy = [10 * np.log10(np.sum(session.wet_ir(name, ch, seconds=7.5) ** 2)) for ch in (0, 2)]
        octaves = {}
        if pos != "7:00":
            for fc in (125, 1000, 8000):
                octaves[str(fc)] = float(np.mean([10 * np.log10(np.sum(band(session.wet_ir(name, ch, seconds=7), fc) ** 2) /
                                                                np.sum(band(first.wet_ir("hall-sweep", ch, seconds=7), fc) ** 2))
                                                  for ch in (0, 2)]))
        report[pos] = {"fraction": clock_fraction(pos), "wet_energy_re_dry_db": float(np.mean(energy)),
                       "re_noon_per_octave_db": octaves}
    return report


def line_spectrum(w, f0, lines=8, span=(3.0, 10.5)):
    """A held tone's wet as lines at f0 + k·fm: the modulation rate fm (searched 1.2-1.5 Hz),
    the lines' levels, and the RMS frequency deviation from them (Gabor's second moment)."""
    seg = w[int(span[0] * FS):int(span[1] * FS)]
    t = np.arange(len(seg)) / FS
    windowed = seg * np.blackman(len(seg))

    def amplitude(f):
        return abs(np.sum(windowed * np.exp(-2j * np.pi * f * t)))

    def side_energy(fm):
        return sum(amplitude(f0 + k * fm) ** 2 for k in range(-4, 5) if k)

    grid = np.arange(1.20, 1.50, 0.002)
    fm = grid[np.argmax([side_energy(x) for x in grid])]
    fm = optimize_scalar(lambda x: -side_energy(x), fm - 0.004, fm + 0.004)
    power = np.array([amplitude(f0 + k * fm) ** 2 for k in range(-lines, lines + 1)])
    k = np.arange(-lines, lines + 1)
    sigma = float(np.sqrt(np.sum(power * (k * fm) ** 2) / np.sum(power)))
    spectrum = np.abs(np.fft.rfft(windowed, 4 * len(seg))) ** 2
    f = np.fft.rfftfreq(4 * len(seg), 1 / FS)
    near = (f > f0 - (lines + 0.5) * fm) & (f < f0 + (lines + 0.5) * fm)
    on_lines = np.zeros_like(f, bool)
    for kk in k:
        on_lines |= np.abs(f - (f0 + kk * fm)) < 0.25
    return {"fm_hz": float(fm), "lines_db_re_total": dict(zip(map(int, k), (10 * np.log10(power / np.sum(power))).tolist())),
            "sigma_hz": sigma, "sigma_cents": float(1200 * np.log2(1 + sigma / f0)),
            "sinusoidal_peak_delay_ms": float(np.sqrt(2) * sigma / (2 * np.pi * fm * f0) * 1000),
            "energy_on_lines_db": float(10 * np.log10(np.sum(spectrum[near & on_lines]) / np.sum(spectrum[near]))),
            "wet_dbfs": float(db(rms(seg)))}


def optimize_scalar(function, lo, hi):
    from scipy import optimize
    return float(optimize.minimize_scalar(function, bounds=(lo, hi), method="bounded").x)


def modulation(second):
    report = {}
    for name, f0 in (("hall-sine-1000", 1000), ("modulate-sine-1000", 1000), ("modulate-sine-250", 250),
                     ("shimmer-sine-1000", 1000)):
        report[name] = {out: line_spectrum(second.wet_signal(name, ch), f0) for ch, out in ((0, "A"), (2, "B"))}
    return report


def held_octaves(second, f0=1000.0, frames=40):
    """The wet at f0 × 0.5/1/2/4/8 for the held tone: 50 ms frames from its start, the steady
    level (3-10 s) and the decay after it stops, in Shimmer and in Hall (no octaves)."""
    report = {}
    for mode in ("shimmer", "hall"):
        name = f"{mode}-sine-1000"
        stop = second.meta[name]["signal"]["seconds"]
        for ch, out in ((0, "A"), (2, "B")):
            w = second.wet_signal(name, ch)
            entry = {}
            for k in (0.5, 1, 2, 4, 8):
                v = narrowband(w, f0 * k / 2 ** (1 / 12), f0 * k * 2 ** (1 / 12))
                build = [float(db(rms(v[int(i * 0.05 * FS):int((i + 1) * 0.05 * FS)]))) for i in range(frames)]
                after = [float(db(rms(v[int((stop + i * 0.25) * FS):int((stop + (i + 1) * 0.25) * FS)]))) for i in range(24)]
                entry[f"{k}x"] = {"build_dbfs_50ms": build, "steady_dbfs": float(db(rms(v[3 * FS:10 * FS]))),
                                  "after_stop_dbfs_250ms": after}
            report[f"{mode}_{out}"] = entry
    return report


# The Shimmer session (rv-6-shimmer.json): one held note per take from silence, the marker on
# the loopback only. Each line k·f0 is followed by complex demodulation, not a brick-wall band
# (whose ringing would reach before the onsets this session is for); levels are dB re the
# dry note at the same output, measured the same way.
SHIMMER_K = (0.5, 1, 2, 4, 8, 16)  # an octave below the note to four above
LINE_TOP_HZ = 12000.0  # past the pedal's band, and near the 16 kHz taper
HOP_S = 0.01
SHIMMER_PITCHES = ["0082", "0165", "0330", "0659", "1319", "2637"]
SHIMMER_330 = ["shimmer-tone-0330", "shimmer-tone-0330-quiet", "shimmer-tone-0330-loud",
               "shimmer-tone-0330-tone-0700", "shimmer-tone-0330-tone-0500",
               "shimmer-tone-0330-time-0700", "shimmer-tone-0330-time-0500",
               "shimmer-tone-0330-level-1200", "modulate-tone-0330"]


def line_window_s(f0):
    """Long enough that the Blackman-Harris main lobe (±4/T) stays within half the spacing
    to the nearest followed line (f0/2, from ½× to 1×); 20 ms at least."""
    return max(0.02, 10.0 / f0)


def line_level(v, f, window_s):
    """The line at f over time, dB of a sine's peak: complex demodulation through a
    Blackman-Harris window (sidelobes -92 dB) centred every HOP_S. The frame times are the
    windows' centres, in seconds from the start of v."""
    n = int(window_s * FS) | 1
    w = signal.windows.blackmanharris(n)
    t = np.arange(len(v)) / FS
    z = signal.oaconvolve(v * np.exp(-2j * np.pi * f * t), w / w.sum(), mode="same")
    index = np.arange(0, len(v), int(HOP_S * FS))
    return index / FS, db(2 * np.abs(z[index]))


def first_at(times, level, threshold, after):
    hits = np.nonzero((times >= after) & (level >= threshold))[0]
    return float(times[hits[0]]) if len(hits) else None


def take_parts(session, name, ch):
    """The take's wet and dry at one output, whole (t = 0 at the signal's start, given as an
    offset). With the marker on the loopback only, the pedal heard silence before the signal,
    so the dry there is zero (the loopback's marker through the dry path isn't the pedal's)."""
    m = session.meta[name]
    _, y = session.load(name)
    dry = session.dry_whole(name, ch)
    if not m.get("marker_to_pedal", True):
        dry[:m["signal_start"]] = 0
    return bandlimit(y[:, ch] - dry), bandlimit(dry), m["signal_start"] / FS


def held_note_lines(session, name, ch, f0):
    wet, dry, offset = take_parts(session, name, ch)
    return note_lines(wet, dry, offset, session.meta[name]["signal"]["seconds"], f0)


def note_lines(wet, dry, offset, seconds, f0):
    """Per line of a held note (wet and dry whole, the signal starting `offset` s in): the
    steady level (2.5 s to the note's end), its growth there, the noise floor (the silence
    before the signal), the onset and the -3 dB point re the steady level (from the note's
    own onset, measured the same way on the dry), the decay after the note stops and the
    trajectory in 100 ms steps. A render goes through here as the pedal does."""
    window = line_window_s(f0)
    times, note = line_level(dry, f0, window)
    times = times - offset
    steady = (times >= 2.5) & (times <= seconds - window / 2 - 0.02)
    lead = (times >= -offset + 0.05 + window / 2) & (times <= -window / 2 - 0.02)
    ref = float(np.median(note[steady]))
    note_onset = first_at(times, note - ref, -20, -0.2)
    lines = {}
    for k in SHIMMER_K:
        if not 30 <= k * f0 <= LINE_TOP_HZ:
            continue
        level = line_level(wet, k * f0, window)[1] - ref
        floor, held = float(np.median(level[lead])), float(np.median(level[steady]))
        entry = {"steady_db": held, "floor_db": floor,
                 "growth_db_per_s": float(np.polyfit(times[steady], level[steady], 1)[0])}
        if held > floor + 20:
            onset = first_at(times, level, held - 20, -0.2)
            rise = first_at(times, level, held - 3, -0.2)
            entry["onset_ms"] = None if onset is None else 1000 * (onset - note_onset)
            entry["rise_ms"] = None if rise is None else 1000 * (rise - note_onset)
            start = seconds + window / 2 + 0.1
            low = np.nonzero((times > start) & (level < floor + 10))[0]
            end = min(times[low[0]] if len(low) else times[-1], start + 8.0)
            span = (times > start) & (times < end)
            if end - start > 0.3 and np.ptp(level[span]) > 15:
                slope = float(np.polyfit(times[span], level[span], 1)[0])
                entry["decay_db_per_s"] = slope
                entry["t60_s"] = float(-60 / slope) if slope < 0 else None
        keep = (times >= -0.2) & (times <= seconds + 10)
        entry["trajectory_100ms"] = [round(float(v), 1) for v in level[keep][::10]]
        lines[f"{k:g}x"] = entry
    return {"note_onset_ms": 1000 * note_onset, "window_ms": 1000 * window,
            "trajectory_start_s": -0.2, "lines": lines}


def labels(f, notes, tolerance=0.004):
    """What a spectral line at f could be, from the notes played: octaves, fifths and
    harmonics of each, and second- and third-order products of pairs (a grain or nonlinear
    shifter's tell)."""
    found = []
    for i, a in enumerate(notes):
        for k in (0.5, 1, 1.5, 2, 3, 4, 5, 6, 8, 12, 16):
            if abs(f / (k * a) - 1) < tolerance:
                found.append(f"{k:g}x{a:g}")
        for b in notes[i + 1:]:
            for text, g in ((f"{a:g}+{b:g}", a + b), (f"|{a:g}-{b:g}|", abs(a - b)),
                            (f"2*{a:g}-{b:g}", abs(2 * a - b)), (f"2*{b:g}-{a:g}", abs(2 * b - a)),
                            (f"2*{a:g}+{b:g}", 2 * a + b), (f"2*{b:g}+{a:g}", 2 * b + a)):
                if g > 0 and abs(f / g - 1) < tolerance:
                    found.append(text)
    return found


def steady_spectrum(session, name, ch, span=None):
    """The wet's spectrum over the held part (Blackman-Harris, 8× zero-padded), dB re the dry
    notes' mean line amplitude; the peaks at least 10 dB above their surroundings, labelled."""
    wet, dry, offset = take_parts(session, name, ch)
    spec = session.meta[name]["signal"]
    notes = spec["freq"] if isinstance(spec["freq"], list) else [spec["freq"]]
    a, b = span or (2.5, spec["seconds"] - 0.1)
    w = signal.windows.blackmanharris(int((b - a) * FS))
    n = 8 * len(w)

    def amplitude(v):
        piece = v[int((offset + a) * FS):int((offset + a) * FS) + len(w)]
        return 2 * np.abs(fft.rfft(piece * w, n)) / w.sum()

    f = fft.rfftfreq(n, 1 / FS)
    dry_amplitude = amplitude(dry)
    dry_lines = [np.max(dry_amplitude[np.abs(f - x) < 2]) for x in notes]
    ref = db(np.mean(dry_lines))
    level = db(amplitude(wet)) - ref
    ok = (f > 30) & (f < LINE_TOP_HZ)
    peaks, props = signal.find_peaks(np.where(ok, level, -200), prominence=10, height=-80)
    order = np.argsort(props["peak_heights"])[::-1][:40]
    listed = [{"hz": round(float(f[p]), 2), "db": round(float(level[p]), 1), "is": labels(f[p], notes)}
              for p in peaks[order]]
    return {"notes_hz": notes, "span_s": [a, b], "peaks": listed, "_curve": (f[ok][::8], level[ok][::8])}


def octave_pitch_cents(session, name, ch, f0, k=2):
    """The k× line's frequency over the held part, cents re exactly k·f0 (parabolic peak of
    the zero-padded spectrum)."""
    wet, _, offset = take_parts(session, name, ch)
    seconds = session.meta[name]["signal"]["seconds"]
    piece = wet[int((offset + 1.0) * FS):int((offset + seconds - 0.1) * FS)]
    n = 16 * len(piece)
    s = np.abs(fft.rfft(piece * signal.windows.blackmanharris(len(piece)), n))
    f = fft.rfftfreq(n, 1 / FS)
    near = np.nonzero(np.abs(f - k * f0) < 0.02 * k * f0)[0]
    i = near[np.argmax(s[near])]
    a, b, c = np.log(s[i - 1:i + 2])
    peak = f[i] + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])
    return float(1200 * np.log2(peak / (k * f0)))


def envelope_ripple(session, name, ch, f0, k=2):
    """The k× line's amplitude modulation over the held part: the strongest rate (0.5-45 Hz)
    and its depth (peak-to-mean, %). A grain shifter ripples at its grain rate; the tank's
    own modulation (Modulate's ~1.3 Hz) shows up too."""
    wet, _, offset = take_parts(session, name, ch)
    times, level = line_level(wet, k * f0, line_window_s(f0))
    seconds = session.meta[name]["signal"]["seconds"]
    keep = (times - offset >= 1.0) & (times - offset <= seconds - 0.1)
    a = 10 ** (level[keep] / 20)
    s = np.abs(np.fft.rfft((a - a.mean()) * np.hanning(len(a)))) / np.hanning(len(a)).sum() * 2
    rates = np.fft.rfftfreq(len(a), HOP_S)
    band = (rates >= 0.5) & (rates <= 45)
    i = np.argmax(np.where(band, s, 0))
    return {"rate_hz": float(rates[i]), "depth_pct": float(100 * s[i] / a.mean())}


def pluck_lines(session, name, ch, f0, onset_s, seconds=10.0):
    wet, dry, offset = take_parts(session, name, ch)
    return pluck_lines_of(wet, dry, offset, f0, onset_s, seconds)


def pluck_lines_of(wet, dry, offset, f0, onset_s, seconds=10.0):
    """A pluck's lines in 100 ms steps from its onset, dB re the dry pluck's peak at f0."""
    window = line_window_s(f0)
    times, note = line_level(dry, f0, window)
    times = times - offset - onset_s
    keep = (times >= -0.2) & (times <= seconds)
    ref = float(np.max(note[keep]))
    lines = {}
    for k in (1, 2, 4, 8):
        level = line_level(wet, k * f0, window)[1][keep] - ref
        lines[f"{k}x"] = {"peak_db": float(level.max()), "peak_s": float(times[keep][np.argmax(level)]),
                          "trajectory_100ms": [round(float(v), 1) for v in level[::10]]}
    return {"trajectory_start_s": -0.2, "lines": lines}


def shimmer_session(third):
    report = {"held": {}, "spectra": {}, "octave_pitch_cents": {}, "ripple_2x": {}, "plucks": {}}
    curves = {}
    for name, m in third.meta.items():
        spec = m["signal"]
        if spec["type"] != "sine":
            continue
        print(f"  {name}", flush=True)
        notes = spec["freq"] if isinstance(spec["freq"], list) else [spec["freq"]]
        for ch, out in ((0, "A"), (2, "B")):
            key = f"{name}_{out}"
            if len(notes) == 1:
                report["held"][key] = held_note_lines(third, name, ch, notes[0])
                if name.startswith("shimmer"):
                    report["octave_pitch_cents"][key] = octave_pitch_cents(third, name, ch, notes[0])
                    report["ripple_2x"][key] = envelope_ripple(third, name, ch, notes[0])
            spectrum = steady_spectrum(third, name, ch)
            curves[key] = spectrum.pop("_curve")
            report["spectra"][key] = spectrum
    for ch, out in ((0, "A"), (2, "B")):
        report["plucks"][f"82_{out}"] = pluck_lines(third, "shimmer-plucks", ch, 82.41, 0.0)
        report["plucks"][f"330_{out}"] = {**pluck_lines(third, "shimmer-plucks", ch, 329.63, 15.0),
                                          "note": "cut at 1 s, about -11 dB (the plan's 16 s body): the "
                                                  "step rings in the tail from 1 s on"}
    return report, curves


def plot_shimmer(directory, report, curves):
    colours = {"0.5x": "tab:gray", "1x": "black", "2x": "tab:blue", "4x": "tab:orange",
               "8x": "tab:green", "16x": "tab:red"}
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.5), sharey=True)
    for ax, pitch in zip(axes.flat, SHIMMER_PITCHES):
        for out, style in (("A", "-"), ("B", ":")):
            held = report["held"][f"shimmer-tone-{pitch}_{out}"]
            for k, entry in held["lines"].items():
                y = entry["trajectory_100ms"]
                ax.plot(-0.2 + 0.1 * np.arange(len(y)), y, style, color=colours[k], lw=1,
                        label=k if out == "A" else None)
        ax.axvspan(0, 4, color="0.92")
        ax.set_title(f"Shimmer, {int(pitch)} Hz held 4 s (A solid, B dotted)")
        ax.set_ylim(-90, 10)
        ax.set_xlabel("s")
        ax.grid(alpha=0.3)
    axes[0, 0].set_ylabel("dB re the dry note")
    axes[0, 0].legend(loc="lower right", fontsize=8)
    save_figure(fig, directory, "rv-6-shimmer-pitches")

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.8), sharey=True)
    for ax, k in zip(axes, ("1x", "2x", "4x")):
        for name in SHIMMER_330:
            entry = report["held"][f"{name}_A"]["lines"].get(k)
            if entry:
                y = entry["trajectory_100ms"]
                label = name.replace("shimmer-tone-0330", "noon").replace("-tone-0330", "").replace("noon-", "")
                ax.plot(-0.2 + 0.1 * np.arange(len(y)), y, "--" if name.startswith("modulate") else "-",
                        lw=1, label=label)
        ax.axvspan(0, 4, color="0.92")
        ax.set_title(f"330 Hz, line {k}, output A")
        ax.set_ylim(-90, 10)
        ax.set_xlabel("s")
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("dB re the dry note")
    axes[0].legend(fontsize=7)
    save_figure(fig, directory, "rv-6-shimmer-knobs")

    fig, axes = plt.subplots(2, 1, figsize=(13, 7.5))
    for ax, name in zip(axes, ("shimmer-fifth", "shimmer-chord")):
        f, level = curves[f"{name}_A"]
        ax.semilogx(f, level, lw=0.7, color="black")
        for peak in report["spectra"][f"{name}_A"]["peaks"][:20]:
            ax.annotate(", ".join(peak["is"][:2]) or "?", (peak["hz"], peak["db"]), fontsize=6,
                        rotation=60, xytext=(2, 4), textcoords="offset points")
        ax.set_xlim(30, LINE_TOP_HZ)
        ax.set_ylim(-110, 10)
        ax.set_title(f"{name}, output A, held part (2.5 s to the end), dB re the dry notes")
        ax.grid(alpha=0.3, which="both")
    save_figure(fig, directory, "rv-6-shimmer-chords")


def plot_followup(directory, results):
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.5))
    law = results["time_law"]
    for ax, mode in zip(axes.flat, MODES):
        rows = law[mode]
        x = [rows[p]["fraction"] for p in TIME_POSITIONS]
        for fc in (OCTAVES if mode in LTI_MODES else BURST_OCTAVES):
            y = [rows[p]["rt60_s"].get(str(fc), np.nan) for p in TIME_POSITIONS]
            ax.plot(x, y, "o-", ms=3, label=f"{fc} Hz")
        ax.set(yscale="log", ylim=(0.2, 30), title=f"{mode}: RT60 against Time" +
               ("" if mode in LTI_MODES else " (bursts, 0.5-4 kHz)"), xlabel="Time (7:00 = 0, 12:00 = 0.5, 5:00 = 1)",
               ylabel="RT60, s")
        ax.grid(alpha=.25, which="both")
        ax.legend(fontsize=7, ncol=2)
    save_figure(fig, directory, "rv-6-time")

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6))
    tone = results["tone_law"]
    for pos, curve in tone["re_noon_db"].items():
        axes[0].semilogx(tone["centres_hz"], curve, label=f"Tone {pos}")
    axes[0].set(title="Tone: Hall's wet re noon (sweeps)", xlabel="Hz", ylabel="dB", ylim=(-30, 15))
    axes[0].xaxis.set_minor_formatter(NullFormatter())
    axes[0].legend(fontsize=8)
    level = results["level_law"]
    axes[1].plot([v["fraction"] for v in level.values()], [v["wet_energy_re_dry_db"] for v in level.values()], "o-")
    axes[1].set(title="Level: Hall's wet energy", xlabel="Level (7:00 = 0, 5:00 = 1)", ylabel="dB re the dry")
    for mode in LTI_MODES:
        rows = law[mode]
        x = [rows[p]["fraction"] for p in TIME_POSITIONS]
        line = axes[2].plot(x, [rows[p]["energy_re_noon_db"] for p in TIME_POSITIONS], "o-", label=f"{mode}: whole wet")
        axes[2].plot(x, [rows[p]["early_50_100ms_re_noon_db"] for p in TIME_POSITIONS], "s--",
                     color=line[0].get_color(), label=f"{mode}: 50-100 ms")
    axes[2].set(title="Time: the wet's energy and its start, re noon", xlabel="Time (7:00 = 0, 5:00 = 1)", ylabel="dB")
    axes[2].legend(fontsize=7, ncol=2)
    for ax in axes:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "rv-6-laws")

    fig, axes = plt.subplots(1, 4, figsize=(16, 4), sharey=True)
    for ax, (name, by_out) in zip(axes, results["modulation"].items()):
        for out, entry in by_out.items():
            lines = entry["lines_db_re_total"]
            ks = np.array([int(k) for k in lines])
            ax.stem(ks + (-0.12 if out == "A" else 0.12), [max(v, -40) + 40 for v in lines.values()],
                    linefmt=("C0-" if out == "A" else "C1-"), markerfmt=("C0o" if out == "A" else "C1o"), basefmt=" ",
                    bottom=0, label=f"{out}: σ {entry['sigma_cents']:.1f} cents")
        static = by_out["A"]["sigma_hz"] < 0.05
        ax.set(title=f"{name}: " + ("one line (static)" if static else f"lines at f0 + k·{by_out['A']['fm_hz']:.3f} Hz"),
               xlabel="k", ylabel="dB re total + 40")
        ax.grid(alpha=.25)
        ax.legend(fontsize=8)
    save_figure(fig, directory, "rv-6-modulation")

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.2), sharey=True)
    held = results["held_octaves"]
    for ax, out in zip(axes, ("A", "B")):
        for k, entry in held[f"shimmer_{out}"].items():
            ax.plot(np.arange(len(entry["build_dbfs_50ms"])) * 50 + 25, entry["build_dbfs_50ms"], "o-", ms=2,
                    label=f"{k} ({float(k[:-1]) * 1000:.0f} Hz)")
        ax.set(title=f"Shimmer, Output {out}: a held 1 kHz tone (falling lines: the marker's tail)", xlabel="ms after the tone starts",
               ylabel="wet, dBFS", ylim=(-110, -30))
        ax.grid(alpha=.25)
        ax.legend(fontsize=8)
    save_figure(fig, directory, "rv-6-shimmer-tone")


def json_default(value):
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    raise TypeError(type(value).__name__)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("session", type=Path, nargs="?", help="the first session (rv-6.json)")
    parser.add_argument("--audio-source", type=Path)
    parser.add_argument("--namx-render", type=Path)
    parser.add_argument("--followup", type=Path, help="the follow-up session (rv-6-followup.json)")
    parser.add_argument("--followup-audio", type=Path, help="its WAVs, if not in --followup")
    parser.add_argument("--followup-only", action="store_true", help="skip the first session's own report")
    parser.add_argument("--shimmer", type=Path, help="the Shimmer session (rv-6-shimmer.json), measured alone")
    parser.add_argument("--shimmer-audio", type=Path, help="its WAVs, if not in --shimmer")
    args = parser.parse_args()
    if args.shimmer:
        third = Session(args.shimmer, args.shimmer_audio or args.shimmer)
        directory = args.shimmer / "fit"
        directory.mkdir(exist_ok=True)
        print("Measuring (Shimmer session)", flush=True)
        measured, curves = shimmer_session(third)
        report = {"pedal": "Boss RV-6 Reverb, stereo", "session": args.shimmer.name,
                  "take_count": len(third.meta), **measured,
                  "method": {"lines": "complex demodulation at k·f0, Blackman-Harris window of max(20 ms, 10/f0), "
                                      "10 ms hop; dB re the dry note (the loopback through the bypass path) "
                                      "measured the same way",
                             "onset_ms": "first frame within 20 dB of the steady level (2.5 s to the note's end), "
                                         "re the dry note's own onset; rise_ms: within 3 dB",
                             "decay": "a line fitted from 0.1 s after the note stops to 10 dB over the floor "
                                      "(the silence before the signal), at most 8 s",
                             "spectra": "the held part's wet, Blackman-Harris, peaks 10 dB above their "
                                        "surroundings, labelled by what they could be"}}
        (directory / "rv-6-shimmer.json").write_text(json.dumps(report, indent=1, default=json_default,
                                                                allow_nan=False) + "\n")
        plot_shimmer(directory, measured, curves)
        print(f"Wrote {directory}")
        if not args.session:
            return
    if not args.session:
        parser.error("give the first session, or --shimmer")
    session = Session(args.session, args.audio_source or args.session)
    if args.followup:
        second = Session(args.followup, args.followup_audio or args.followup)
        directory = args.followup / "fit"
        directory.mkdir(exist_ok=True)
        followup = {}
        for key, step in (("audit", lambda: followup_audit(session, second)),
                          ("time_law", lambda: time_law(session, second)),
                          ("tone_law", lambda: tone_law(session, second)),
                          ("level_law", lambda: level_law(session, second)),
                          ("modulation", lambda: modulation(second)),
                          ("held_octaves", lambda: held_octaves(second))):
            print(f"Measuring (follow-up): {key}", flush=True)
            followup[key] = step()
        report = {"pedal": "Boss RV-6 Reverb, stereo", "session": args.followup.name,
                  "first_session": args.session.name, "take_count": len(second.meta), **followup}
        (directory / "rv-6-followup.json").write_text(json.dumps(report, indent=1, default=json_default,
                                                                 allow_nan=False) + "\n")
        plot_followup(directory, followup)
        print(f"Wrote {directory}")
        if args.followup_only:
            return
    directory = args.session / "fit"
    directory.mkdir(exist_ok=True)
    steps = [("audit", audit), ("dry", dry_path), ("invariance", invariance), ("decay", decays),
             ("early", early), ("character", character), ("level", knob_level), ("spring", spring),
             ("shimmer", shimmer), ("pitch_wobble", pitch_wobble)]
    results = {}
    for key, step in steps:
        print(f"Measuring: {key}", flush=True)
        results[key] = step(session)
    results["_tone"] = knob_tone(session)
    namx = namx_compare(session, args.namx_render) if args.namx_render else {}
    centres, curves = results["_tone"]
    report = {"pedal": "Boss RV-6 Reverb, stereo (Input A -> Outputs A and B)", "session": args.session.name,
              "rate": FS, "take_count": len(session.meta),
              **{k: v for k, v in results.items() if not k.startswith("_")},
              "tone_re_noon_db": {"centres_hz": centres, **curves},
              "namx_matched": {m: {k: v for k, v in r.items() if k != "_ir"} for m, r in namx.items()},
              "method": {"response": "pedal channel / loopback: a burst take (time-invariant modes) over the whole "
                                     "take, marker included; sweeps and the moving modes from the signal on",
                         "wet": "response / (bypass dry IR windowed -2..+8 ms) - 1, tapered to 40 Hz-16 kHz",
                         "rt60": "2.85-3.1 kHz notched (the pedal's whine), octave bands (4th-order Butterworth, "
                                 "zero-phase), Schroeder with the noise floor subtracted, T30 (T20 where marked); "
                                 "bursts only 500 Hz-4 kHz",
                         "burst_irs": "Room, Hall, Plate and Spring are time-invariant, so the burst takes "
                                      "(marker + burst) deconvolve to impulse responses"}}
    (directory / "rv-6.json").write_text(json.dumps(report, indent=1, default=json_default, allow_nan=False) + "\n")
    plot(directory, session, results, namx)
    print(f"Wrote {directory}")


if __name__ == "__main__":
    main()
