#!/usr/bin/env python3
"""Measure Boss AW-3 Up, and evaluate/refit a tied grey-box model.

    python -I tools/capture/analyze_aw3.py SESSION [--audio-source WAV_DIRECTORY] [--fit]

SESSION supplies sidecars and receives fit/; --audio-source is strictly read-only.
All figures and the JSON report are regenerated. --fit refits the static control law
and the detector (minutes); the default evaluates the published coefficients.
No capture/JACK code is imported. Dependencies: numpy, scipy, soundfile,
matplotlib, numba. See captures/boss/aw-3/README.md for limits and conventions.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

# Avoid writing a plotting cache outside the requested output/scratch areas.
os.environ.setdefault("MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "aw3-matplotlib"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from numba import njit
from scipy import fft, optimize, signal
from scipy.ndimage import gaussian_filter1d
import soundfile as sf

FS = 48000
POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
# One frequency-indexed resonator, shared by every knob setting and signal.
FILTER = {
    "hz": [194, 229, 524, 631, 1180, 1370, 2222],
    "q": [14.38, 13.11, 7.49, 6.48, 3.6, 3.2, 2.9],
    "resonant_gain": [5.165, 5.18, 5.21, 5.13, 5.05, 5.05, 5.08],
    "low_feed": [.030, .035, .069, .08, .14, .16, .18],
    "high_feed": [.0444, .0499, .103, .126, .27, .33, .47],
    "delay_samples": 26.79,
}
# f = floor + (Manual offset + Sens gain * envelope)^power, capped at top.
# The last five entries calibrate the steady noise law; the dynamic model below
# replaces nominal noise levels with the actual weighted, followed waveform.
CONTROL = [191.33824811792678, 3.2225561115194483, 2249.325286767076, -19.78743261933742,
 34.123144700839354, 93.21697170008322, 350.00136856203113, 659.2562056252598]
# Published joint fit. Parameters are documented in unpack_detector();
# --fit refits them and writes the new values to the generated report.
DETECTOR = [1168.3414192696073, 0.002815859005287871, 0.0166994036268528, 9046.78831053321,
 0.01788141249961396, -1.3303983102290406, -3.6989632050964065, -5.013171249937462,
 -0.8866549021885621, -0.43407074608865587, 3.5615652825960136, 5.083949850472449,
 -0.1606226871553359, 1.352409772983396, 5.828910588253567, 4.706177228109489,
 6.127447871566382, 6.834379446714373, 7.002051466995063, 6.511023571030592]


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def clock_fraction(position):
    """A clock string, not its tag interpreted as a number: 300 degree travel."""
    hour, minute = map(int, position.split(":"))
    fraction = ((hour - 7) % 12 + minute / 60) / 10
    if not 0 <= fraction <= 1:
        raise ValueError("Knob position is outside 7:00 through 5:00")
    return fraction


ALL_CLOCKS = ["7:00", "8:00", "9:00", "10:00", "11:00", "12:00", "1:00", "2:00", "3:00", "4:00", "5:00"]
FRACTIONS = np.array([clock_fraction(p) for p in POSITIONS])
SENS_FRACTIONS = np.array([clock_fraction(p) for p in ALL_CLOCKS])

def tag(position):
    return position.replace(":", "").zfill(4)


def interp_complex(f, frequencies, values):
    return np.interp(f, frequencies, values.real) + 1j * np.interp(f, frequencies, values.imag)


def circular_head(h, positive=24000, negative=2400):
    """Keep negative times in the end of the smaller circular response."""
    return np.r_[h[:positive], h[-negative:]]


class Session:
    def __init__(self, path, audio):
        self.path, self.audio = path, audio
        self.meta = {p.stem: json.loads(p.read_text()) for p in path.glob("*.json")
                     if p.stem != "rig"}
        self.cache = {}

    def load(self, name, whole=False):
        if name not in self.cache:
            m = self.meta[name]
            x, rate = sf.read(self.audio / "played" / f"{name}.wav")
            y, yr = sf.read(self.audio / f"{name}.wav")
            if rate != FS or yr != FS or x.ndim != 1 or y.ndim != 2 or y.shape[1] != 2:
                raise ValueError(f"Unexpected format: {name}")
            lat = m["latency_samples"]
            length = min(len(x), len(y) - lat)
            self.cache[name] = (x[:length], y[lat:lat + length])
        x, y = self.cache[name]
        start = 0 if whole else self.meta[name]["signal_start"]
        return self.meta[name], x[start:], y[start:]

    def response(self, name, source="played"):
        """Sweep deconvolution, including both sides of time zero."""
        _, x, y = self.load(name)
        if source == "loop":
            x = y[:, 1]
        n = fft.next_fast_len(2 * len(x))
        X, Y = fft.rfft(x, n), fft.rfft(y[:, 0], n)
        f = fft.rfftfreq(n, 1 / FS)
        H = Y * X.conj() / (abs(X)**2 + 1e-12)
        H[(f < 20) | (f > 20000)] = 0
        h = fft.irfft(H, n)
        head = circular_head(h)
        return fft.rfftfreq(len(head), 1 / FS), fft.rfft(head), h

    def audit(self):
        rows = {}
        for name, m in self.meta.items():
            _, x, y = self.load(name)
            entry = {"xruns": m["xruns"], "peak_dbfs": float(db(np.max(abs(y[:, 0]))))}
            # Hash the WAV samples, as capture.py does, not the RIFF container.
            played, _ = sf.read(self.audio / "played" / f"{name}.wav", dtype="float32")
            entry["played_hash_matches"] = hashlib.sha256(played.tobytes()).hexdigest() == m["played_sha256"]
            if m["signal"]["type"] == "noise-steps":
                changes = []
                duration = len(m["signal"]["levels_db"]) * m["signal"]["step_seconds"]
                for t in np.arange(.5, duration - .1, .5):
                    # Avoid the smoothed level boundaries (the 48 dB edge biases correlation).
                    if abs(t / m["signal"]["step_seconds"] - round(t / m["signal"]["step_seconds"])) < .01:
                        t += .1
                    a, length = int(t * FS), 4000
                    c = signal.correlate(y[a - 100:a + length + 100, 1], x[a:a + length], mode="valid")
                    lag = int(np.argmax(c) - 100)
                    if not changes or changes[-1][1] != lag:
                        changes.append([float(t), lag])
                entry["body_loop_lag_changes_samples"] = changes
            rows[name] = entry
        return rows


def resonator(f, fc, q, gain, low, high, delay=0):
    """Bilinear two-integrator resonator: low*LP + gain/Q*BP + high*HP."""
    r = np.tan(np.pi * f / FS) / np.tan(np.pi * fc / FS)
    return ((low + gain * 1j * r / q - high * r*r) / (1 - r*r + 1j*r/q)
            * np.exp(-2j * np.pi * f * delay / FS))


def transfer(f, fc, model=FILTER, delay=True):
    values = [np.interp(fc, model["hz"], model[k]) for k in
              ("q", "resonant_gain", "low_feed", "high_feed")]
    return resonator(f, fc, *values, model["delay_samples"] if delay else 0)


def fit_response(f, H):
    ok = (f > 45) & (f < 9000)
    ff, hh = f[ok], H[ok]
    band = (ff > 120) & (ff < 3500)
    peak = ff[band][np.argmax(abs(hh[band]))]

    def error(p):
        z = (resonator(ff, *p) - hh) / (abs(hh) + .08)
        return np.r_[z.real, z.imag]

    fit = optimize.least_squares(error, [peak, 7, 5, .04, .1, 27], x_scale="jac",
                                 bounds=([120, 1, 1, -2, -2, 20], [3500, 30, 10, 2, 2, 35]))
    return fit.x


def static_measurements(session):
    f, bypass, _ = session.response("bypass-sweep")
    report, curves, knots = {}, {}, []
    names = sorted((n for n in session.meta if n.startswith("rest-manual-")),
                   key=lambda n: clock_fraction(session.meta[n]["settings"]["Manual"]))
    for name in names:
        f, H, _ = session.response(name)
        H = H / bypass
        p = fit_response(f, H)
        fine = np.linspace(100, 800, 70001)
        mag = np.interp(fine, f, db(H))
        i = np.argmax(mag)
        left = np.interp(mag[i] - 3, mag[:i + 1], fine[:i + 1])
        right = np.interp(mag[i] - 3, mag[i:][::-1], fine[i:][::-1])
        pos = session.meta[name]["settings"]["Manual"]
        ok = (f > 60) & (f < 6000)
        report[pos] = {"fraction": clock_fraction(pos), "peak_hz": float(fine[i]),
                       "peak_db": float(mag[i]), "q_3db": float(fine[i] / (right - left)),
                       "pole_hz": p[0], "pole_q": p[1], "resonant_gain": p[2],
                       "low_feed": p[3], "high_feed": p[4], "delay_samples": p[5],
                       "shared_filter_rms_db_60_6000": rms(db(transfer(f[ok], p[0])) - db(H[ok]))}
        curves[name] = (f, H, p)
        knots.append([clock_fraction(pos), p[0]])
    return report, curves, np.array(knots)


class Tracker:
    """Local differential-equation identification, not a spectrogram peak picker.

    40 ms Hann windows, 10 ms hops; fit a two-pole, two-zero equation to
    simultaneous loop/output channels. A common measured frontend is removed
    from the equation. Poles are estimable below the FFT-bin spacing. Individual
    Q estimates in the -48 dB probe are ill-conditioned: do not report them.
    """
    def __init__(self, session, curves):
        self.session = session
        m, x, y = session.load("bypass-sweep")
        n = fft.next_fast_len(2 * len(x))
        L, Y = fft.rfft(y[:, 1], n), fft.rfft(y[:, 0], n)
        self.bf = fft.rfftfreq(n, 1 / FS)
        self.bypass_loop = Y * L.conj() / (abs(L)**2 + 1e-10)
        f, H, p = curves["rest-manual-1200"]
        self.cf = f
        self.common = H / resonator(f, *p)

    def xy(self, name):
        m, x, y = self.session.load(name)
        n = fft.next_fast_len(len(x) + FS)
        f = fft.rfftfreq(n, 1 / FS)
        b = interp_complex(f, self.bf, self.bypass_loop)
        c = interp_complex(f, self.cf, self.common)
        band = np.exp(-(f / 3500)**8)
        inp = fft.irfft(fft.rfft(y[:, 1], n) * b * c * band
                       * np.exp(-2j*np.pi*f*FILTER["delay_samples"]/FS), n)[:len(x):4]
        out = fft.irfft(fft.rfft(y[:, 0], n) * band, n)[:len(x):4]
        return inp, out

    def track(self, name, window=.04, hop=.01, start=None, stop=None):
        x, y = self.xy(name)
        fs = FS / 4
        c = 2 * fs / (2*np.pi*1000)
        y2 = c*c*(y[2:] - 2*y[1:-1] + y[:-2])
        y1 = c*(y[2:] - y[:-2])
        y0 = y[2:] + 2*y[1:-1] + y[:-2]
        x2 = c*c*(x[2:] - 2*x[1:-1] + x[:-2])
        x1 = c*(x[2:] - x[:-2])
        x0 = x[2:] + 2*x[1:-1] + x[:-2]
        A = np.array([-y0, -y1, x0, x1, x2]).T
        times = np.arange(window/2, len(y)/fs - window/2, hop)
        if start is not None:
            times = times[(times >= start) & (times <= stop)]
        rows = []
        for t in times:
            a = int((t-window/2)*fs)
            b = a + int(window*fs)
            aa, yy = A[a:b], y2[a:b]
            w = np.hanning(len(yy))
            coef = np.linalg.lstsq(aa*w[:, None], yy*w, rcond=1e-8)[0]
            omega = np.sqrt(max(coef[0], 1e-4))*1000
            fc = fs/np.pi*np.arctan(np.pi*omega/fs)
            rows.append([t, fc, float(db(rms(y[a:b])))])
        return np.array(rows)


def sweep_envelopes(session):
    # Complex lock-in against the exact chirp; avoids treating a moving filter as LTI.
    length = 10/np.log(1000)
    t = np.arange(10*FS)/FS
    phase = 2*np.pi*20*length*np.expm1(t/length)
    frequency = 20*np.exp(t/length)

    def demod(y):
        z = y[:len(t)] * np.exp(-1j*phase)
        return gaussian_filter1d(z.real, FS*.01) + 1j*gaussian_filter1d(z.imag, FS*.01)

    _, _, bypass = session.load("bypass-sweep")
    B = demod(bypass[:, 0])
    out = {}
    for name in session.meta:
        if name.startswith("rest-sens-"):
            _, _, y = session.load(name)
            H = demod(y[:, 0])/B
            out[name] = np.c_[t[::480], frequency[::480], H[::480].real, H[::480].imag]
    return out


def stairs_summary(session, tracks):
    out = {}
    for name, r in tracks.items():
        if not name.startswith("stairs-"):
            continue
        spec = session.meta[name]["signal"]
        rows = []
        for k, level in enumerate(spec["levels_db"]):
            a, b = (k+.6)*spec["step_seconds"], (k+.98)*spec["step_seconds"]
            values = r[(r[:, 0] > a) & (r[:, 0] < b), 1]
            rows.append([level, float(np.median(values)), float(np.std(values))])
        out[name] = rows
    return out


def fit_control(session, stairs, knots):
    def prediction(p, level, manual, sens):
        floor, power, cap, limit = p[:4]
        gain = np.interp(sens, FRACTIONS, [0, *p[4:]])
        base = max(np.interp(manual, knots[:, 0], knots[:, 1]) - floor, 0)**(1/power)
        return min(cap, floor + (base + gain*min(10**(level/20), 10**(limit/20)))**power)

    def residual(p):
        out = []
        for name, rows in stairs.items():
            settings = session.meta[name]["settings"]
            for level, f, _ in rows:
                if -42 <= level <= -6:
                    pred = prediction(p, level, clock_fraction(settings["Manual"]), clock_fraction(settings["Sens"]))
                    out.append(np.log(pred/f))
        return np.array(out)
    fit = optimize.least_squares(residual, CONTROL, x_scale="jac",
                                bounds=([100, 1, 2100, -35, 0, 0, 0, 0],
                                        [194, 5, 2500, 0, 1e5, 1e5, 1e5, 1e5]))
    return fit.x


@njit
def follow(level, attack, release, decay):
    """Two asymmetric one-pole followers, 1 ms steps, initialized before the marker."""
    a = 1 - np.exp(-.001/attack)
    r = 1 - np.exp(-.001/release)
    d = 1 - np.exp(-.001/decay)
    first = second = 0.
    out = np.empty(len(level))
    for i in range(len(level)):
        first += (level[i] - first) * (a if level[i] > first else r)
        second += (first - second) * (a if first > second else d)
        out[i] = second
    return out


def unpack_detector(p):
    return {"highpass_hz": [float(p[0]), float(p[3])], "attack_ms": float(1000*p[1]),
            "fixed_release_ms": float(1000*p[2]), "detector_clip_rms": float(p[4]),
            "decay_ms": (1000*np.cumsum(np.exp(p[5:10]))).tolist(),
            "sens_positions": ALL_CLOCKS, "sens_fractions": SENS_FRACTIONS.tolist(),
            "sens_gains": np.r_[0, np.cumsum(np.exp(p[10:20]))].tolist()}


class DetectorFit:
    def __init__(self, session, tracks, sweeps, knots, control):
        self.session, self.tracks, self.sweeps = session, tracks, sweeps
        self.knots, self.control = knots, np.asarray(control)
        self.pluck_training = {}
        self.inputs = {}
        for name in list(tracks) + list(sweeps) + [n for n in session.meta if n.startswith("plucks-")]:
            m, x, _ = session.load(name, True)
            self.inputs[name] = x[:len(x)//48*48] / 10**(m["ref_dbfs"]/20)

        for name in ["plucks-decay-0700", "plucks-decay-1200"]:
            m, _, y = session.load(name)
            _, _, bypass = session.load("bypass-plucks")
            rows = []
            for onset, freq in zip(m["signal"]["onsets"], m["signal"]["freq"]):
                a, b = int(onset*FS), int((onset+3)*FS)
                tt = np.arange(b-a)/FS
                lp = signal.butter(2, 30, fs=FS, output="sos")
                def amp(v):
                    return 2*abs(signal.sosfiltfilt(lp, v*np.exp(-2j*np.pi*freq*tt)))
                gain = db(amp(y[a:b, 0]))-db(amp(bypass[a:b, 0]))
                ok = (tt > .05) & (tt < 2.8)
                rows.extend(zip((tt+onset)[ok][::480], np.full(ok.sum(), freq)[::480], gain[ok][::480]))
            self.pluck_training[name] = np.array(rows)

    def frequency(self, p, name):
        m = self.session.meta[name]
        sos = np.vstack([signal.butter(1, hz, fs=FS, btype="highpass", output="sos") for hz in (p[0], p[3])])
        z = signal.sosfilt(sos, self.inputs[name])
        z = np.sqrt(np.mean(z.reshape(-1, 48)**2, axis=1))
        d = unpack_detector(p)
        settings = m["settings"]
        decay = np.interp(clock_fraction(settings["Decay"]), FRACTIONS, np.array(d["decay_ms"])/1000)
        gain = np.interp(clock_fraction(settings["Sens"]), d["sens_fractions"], d["sens_gains"])
        e = follow(np.minimum(z, p[4]), p[1], p[2], decay)
        floor, power, cap = self.control[:3]
        manual = np.interp(clock_fraction(settings["Manual"]), self.knots[:, 0], self.knots[:, 1])
        base = max(manual-floor, 0)**(1/power)
        fc = np.minimum(cap, floor + (base+gain*e)**power)
        return np.arange(len(fc))*.001 - m["signal_start"]/FS, fc

    def evaluate(self, p, predictions=False):
        residuals, out = [], {}
        for name in list(self.tracks) + list(self.sweeps) + list(self.pluck_training):
            t, fc = self.frequency(p, name)
            m = self.session.meta[name]
            if name in self.pluck_training:
                r = self.pluck_training[name]
                times, frequency, observed = r.T
                predicted = db(transfer(frequency, np.interp(times, t, fc)))
                error = predicted-observed
                residuals.append(error/np.sqrt(len(error))*.1)
                out[name] = (times, observed, predicted, np.ones(len(times), dtype=bool))
            elif name in self.tracks:
                r = self.tracks[name]
                times, observed = r[:, 0], r[:, 1]
                predicted = np.interp(times, t, fc)
                spec = m["signal"]
                body = spec["step_seconds"]*len(spec["levels_db"])
                ok = (times > .2) & (times < body-.05) & (observed > 180) & (observed < 2400) & (r[:, 2] > -88)
                if name.startswith("jump"):
                    ok &= ((times < 7.95) | (times > 8.13)) & ((times < 3.94) | (times > 4.06))
                else:
                    ok &= (times % 1.5 > .15) & (times < 13.5)
                error = np.log(predicted/observed)
                residuals.append(error[ok]/np.sqrt(ok.sum())*5)
                out[name] = (times, observed, predicted, ok)
            else:
                r = self.sweeps[name]
                times, frequency = r[:, 0], r[:, 1]
                H = r[:, 2] + 1j*r[:, 3]
                pred = transfer(frequency, np.interp(times, t, fc))
                ok = (frequency > 80) & (frequency < 12000) & ~((frequency > 160) & (frequency < 260))
                error = db(pred)-db(H)
                residuals.append(error[ok]/np.sqrt(ok.sum())*.05)
                out[name] = (frequency, db(H), db(pred), ok)
        return out if predictions else np.concatenate(residuals)

    def fit(self, p):
        lower = np.r_[30, .0002, .001, 500, .0001, np.log([.0003]*5), np.log([.01]*10)]
        upper = np.r_[8000, .05, 1.5, 22000, .5, np.log([3]*5), np.log([20000]*10)]
        result = optimize.least_squares(self.evaluate, p, bounds=(lower, upper), x_scale="jac",
                                        max_nfev=80, ftol=1e-5, diff_step=1e-3)
        return result.x, {"success": bool(result.success), "message": result.message,
                          "nfev": result.nfev, "cost": result.cost}


@njit
def render_filter(x, fc, hz, qs, gs, lows, highs):
    """Sample-by-sample TPT state-variable filter; control linearly interpolated in time."""
    out = np.empty(len(x))
    s1 = s2 = 0.
    for i in range(len(x)):
        f = fc[i]
        q = np.interp(f, hz, qs)
        gain = np.interp(f, hz, gs)
        lo = np.interp(f, hz, lows)
        hi = np.interp(f, hz, highs)
        g = np.tan(np.pi*f/48000)
        k = 1/q
        v1 = (s1 + g*(x[i]-s2))/(1+g*(g+k))
        v2 = s2 + g*v1
        s1, s2 = 2*v1-s1, 2*v2-s2
        hp = x[i]-k*v1-v2
        out[i] = lo*v2 + gain*k*v1 + hi*hp
    return out


def pluck_validation(session, tracker, detector, p):
    report, curves = {}, {}
    for name in sorted(n for n in session.meta if n.startswith("plucks-")):
        m, _, y = session.load(name, True)
        _, _, bypass = session.load("bypass-plucks", True)
        times, fc = detector.frequency(p, name)
        sample_t = np.arange(len(y))/FS - m["signal_start"]/FS
        pole = np.interp(sample_t, times, fc)
        # The recorded bypass twin preserves the rig and the pluck's exact amplitude.
        n = min(len(y), len(bypass))
        rendered = render_filter(bypass[:n, 0], pole[:n], *[np.asarray(FILTER[k]) for k in
                                 ("hz", "q", "resonant_gain", "low_feed", "high_feed")])
        # Output ceiling measured independently from the clipped pluck attacks.
        rendered = np.clip(rendered, -.330, .345)
        nf = fft.next_fast_len(n + FS)
        f = fft.rfftfreq(nf, 1/FS)
        common = interp_complex(f, tracker.cf, tracker.common)
        H = common*np.exp(-2j*np.pi*f*FILTER["delay_samples"]/FS)
        spectrum = fft.rfft(rendered, nf)*H
        start = m["signal_start"]
        observed = y[:n, 0]
        indices = np.arange(start+int(.04*FS), min(n, start+12*FS), 8)
        # One bounded nuisance clock offset, never an independent gain per take.
        def predict(shift):
            return fft.irfft(spectrum*np.exp(-2j*np.pi*f*shift/FS), nf)[:n]
        shift = optimize.minimize_scalar(lambda d: np.mean((predict(d)[indices]-observed[indices])**2),
                                         bounds=(-1, 1), method="bounded").x
        prediction = predict(shift)
        gain_rows, error = [], []
        for onset, frequency in zip(m["signal"]["onsets"], m["signal"]["freq"]):
            a, b = start+int(onset*FS), start+int((onset+3)*FS)
            tt = np.arange(b-a)/FS
            lp = signal.butter(2, 30, fs=FS, output="sos")
            def amp(v):
                return 2*abs(signal.sosfiltfilt(lp, v*np.exp(-2j*np.pi*frequency*tt)))
            base = amp(bypass[a:b, 0])
            output, model = amp(observed[a:b]), amp(prediction[a:b])
            measured_gain, predicted_gain = db(output)-db(base), db(model)-db(base)
            ok = (tt > .04) & (tt < 2.8) & (db(output) > -110)
            error.extend((predicted_gain-measured_gain)[ok][::480])
            gain_rows.append((tt[::480]+onset, measured_gain[::480], predicted_gain[::480]))
        body = slice(start, start+12*FS)
        report[name] = {"held_out": name.endswith("0500"), "fundamental_gain_rms_db": rms(np.array(error)),
                        "waveform_relative_error_db": float(db(rms(prediction[body]-observed[body])/rms(observed[body]))),
                        "clock_shift_samples": float(shift)}
        curves[name] = gain_rows
    return report, curves


def noise_and_headroom(session):
    report = {}
    for name in ["noise-sens-0500", "bypass-stairs", "bypass-plucks"]:
        _, _, y = session.load(name)
        a = FS if name.startswith("noise") else len(y)-int(.7*FS)
        report[name] = {"rms_dbfs": float(db(rms(y[a:-FS//10, 0])))}
    for name in sorted(n for n in session.meta if n.startswith("plucks-")):
        _, _, y = session.load(name)
        peak = int(np.argmax(abs(y[:, 0])))
        report[name] = {"peak_dbfs": float(db(np.max(abs(y[:, 0])))),
                        "max_sample": float(y[:, 0].max()), "min_sample": float(y[:, 0].min()),
                        "peak_at_seconds": peak/FS}
    return report


def jump_metrics(tracker, tracks, control, knots):
    out = {}
    floor, power = control[:2]
    base = max(np.interp(.5, knots[:, 0], knots[:, 1])-floor, 0)**(1/power)
    for pos in POSITIONS:
        name = "jump-decay-"+tag(pos)
        r = tracks[name]
        t, fc = r[:, 0], r[:, 1]
        low = float(np.median(fc[(t > 3) & (t < 3.9)]))
        high = float(np.median(fc[(t > 5) & (t < 7)]))
        fast = tracker.track(name, .008, .001, 3.98, 4.1)
        row = {"low_hz": low, "high_hz": high, "attack_window_ms": 8}
        for fraction in [.1, .5, .9]:
            crossed = fast[(fast[:, 0] >= 4) & (fast[:, 1] >= low+fraction*(high-low)), 0]
            row[f"attack_{round(fraction*100)}pct_ms"] = float((crossed[0]-4)*1000) if len(crossed) else None
        smoothed = signal.medfilt(fc, 7)
        for fraction in [.632, .9]:
            target = high-fraction*(high-low)
            good = (t > 8.13) & (t < 11.8) & (smoothed < target)
            # Require three adjacent windows, not one noise excursion.
            indices = np.flatnonzero(np.convolve(good.astype(int), np.ones(3, dtype=int), "valid") == 3)
            row[f"frequency_return_{round(fraction*100)}pct_ms"] = float((t[indices[0]]-8)*1000) if len(indices) else None
        e = np.maximum(fc-floor, 0)**(1/power)-base
        ok = (t > 8.15) & (t < 11.8) & (fc > 300) & (fc < 2000) & (e > 0) & (r[:, 2] > -88)
        coeff = np.polyfit(t[ok]-8, np.log(e[ok]), 1)
        row["effective_control_tail_tau_ms"] = float(-1000/coeff[0])
        row["tail_fit_log_rms"] = rms(np.log(e[ok])-np.polyval(coeff, t[ok]-8))
        out[pos] = row
    return out


def stationary_noise(session):
    """Independent noise check on moving Q/peak gain; skip the low-SNR steps."""
    _, _, b = session.load("bypass-stairs")
    a, e = 12*FS, 15*FS
    f, power = signal.welch(b[a:e, 1], FS, nperseg=16384)
    _, cross = signal.csd(b[a:e, 1], b[a:e, 0], FS, nperseg=16384)
    bypass = cross/power
    report = {}
    for name in sorted(n for n in session.meta if n.startswith("stairs-")):
        m, _, y = session.load(name)
        rows = []
        for k, level in enumerate(m["signal"]["levels_db"]):
            if not -36 <= level <= -6:
                continue
            a, e = int((k+.4)*1.5*FS), int((k+1)*1.5*FS)
            _, power = signal.welch(y[a:e, 1], FS, nperseg=16384)
            _, cross = signal.csd(y[a:e, 1], y[a:e, 0], FS, nperseg=16384)
            H = cross/power/bypass
            p = fit_response(f, H)
            band = (f > 80) & (f < 6000)
            error = rms(db(resonator(f[band], *p))-db(H[band]))
            rows.append({"level_db": level, "pole_hz": p[0], "q": p[1], "resonant_gain_db": float(db(p[2])),
                         "response_rms_db": error})
        report[name] = rows
    return report


def input_limit_check(session, tracker):
    """Fit one fixed filter to Sens 7:00's whole staircase, with/without input clip.

    A diagnostic proxy only. The residual improvement is too small to establish
    a precise input-clip circuit; the plucks establish the output clip separately.
    """
    m, x, y = session.load("stairs-sens-0700")
    n = fft.next_fast_len(len(x)+FS)
    f = fft.rfftfreq(n, 1/FS)
    bf, bypass, _ = session.response("bypass-sweep")
    base = interp_complex(f, bf, bypass)*interp_complex(f, tracker.cf, tracker.common)
    band = np.exp(-(f/7000)**12)
    observed = fft.irfft(fft.rfft(y[:, 0], n)*band, n)[:len(x)]
    reference = 10**(m["ref_dbfs"]/20)
    indices = np.concatenate([np.arange(int((k+.2)*1.5*FS), int((k+.95)*1.5*FS), 8) for k in range(5, 11)])
    def render(p):
        H = resonator(f, *p[:6])
        clipped = np.clip(x/reference, -p[6], p[6])*reference
        return fft.irfft(fft.rfft(clipped, n)*base*H*band, n)[:len(x)]
    fit = optimize.least_squares(lambda p: (render(p)-observed)[indices],
                                [229, 13, 5.18, .035, .05, 26.8, .6], x_scale="jac", diff_step=1e-3,
                                bounds=([220, 8, 3, 0, 0, 24, .1], [240, 18, 7, .2, .2, 30, 2]), max_nfev=50)
    predicted = render(fit.x)
    p = fit.x.copy()
    p[6] = 3
    linear = render(p)
    rows = []
    for k, level in enumerate(m["signal"]["levels_db"]):
        a, e = int((k+.3)*1.5*FS), int((k+.95)*1.5*FS)
        denominator = rms(observed[a:e])
        rows.append([level, float(db(rms(observed[a:e]-linear[a:e])/denominator)),
                     float(db(rms(observed[a:e]-predicted[a:e])/denominator))])
    return {"candidate_input_clip_db_re_peak": float(db(fit.x[6])),
            "level_linear_error_clip_error_db": rows,
            "note": "7 kHz band limit; a diagnostic proxy, not an identified clip threshold"}


def top_end(session):
    _, _, b = session.load("bypass-stairs")
    a, e = 12*FS, 15*FS
    f, px = signal.welch(b[a:e, 1], FS, nperseg=8192)
    _, pxy = signal.csd(b[a:e, 1], b[a:e, 0], FS, nperseg=8192)
    B = pxy/px
    out = {}
    for name in ["stairs-sens-0700", "stairs-sens-0900", "stairs-sens-1200"]:
        _, _, y = session.load(name)
        _, px = signal.welch(y[a:e, 1], FS, nperseg=8192)
        _, py = signal.welch(y[a:e, 0], FS, nperseg=8192)
        _, pxy = signal.csd(y[a:e, 1], y[a:e, 0], FS, nperseg=8192)
        out[name] = (f, pxy/px/B, abs(pxy)**2/(px*py))
    return out


def save_figure(fig, directory, name):
    fig.tight_layout()
    path = directory / f"{name}.svg"
    fig.savefig(path, metadata={"Date": None, "Creator": "NAM-X AW-3 analysis"})
    # Matplotlib leaves trailing spaces in SVG paths; newlines already separate coordinates.
    path.write_text("\n".join(line.rstrip() for line in path.read_text().splitlines())+"\n")
    plt.close(fig)


def plot(directory, session, static, curves, stairs, predictions, plucks, top, detector, p):
    plt.rcParams.update({"font.size": 9, "svg.hashsalt": "namx-aw3-up"})
    fig, axes = plt.subplots(2, 2, figsize=(13, 9))
    for name, (f, H, coeff) in curves.items():
        position = session.meta[name]["settings"]["Manual"]
        axes[0, 0].semilogx(f, db(H), label=position, lw=.8)
        axes[0, 1].plot(coeff[0], static[position]["q_3db"], "o")
    axes[0, 0].set(xlim=(30, 20000), ylim=(-40, 18), xlabel="Hz", ylabel="dB re bypass", title="Manual: quiet sweeps (Sens 7:00)")
    axes[0, 0].legend(ncol=4, fontsize=7)
    axes[0, 1].plot(FILTER["hz"], FILTER["q"], "k--", label="shared filter map")
    axes[0, 1].set(xlabel="pole frequency, Hz", ylabel="Q", title="Q decreases as the filter opens")
    axes[0, 1].legend()
    for name, (f, H, coherence) in top.items():
        axes[1, 0].plot(f, db(H), label=name.replace("stairs-sens-", "Sens "))
    axes[1, 0].set(xlim=(10000, 24000), ylim=(-75, 2), xlabel="Hz", ylabel="dB re bypass", title="Top-end wall: simultaneous noise transfer")
    axes[1, 0].legend(fontsize=7)
    f, H, coeff = curves["rest-manual-1200"]
    ok = (f > 1000) & (f < 20000)
    residual = H[ok]/resonator(f[ok], *coeff[:5], 0)
    phase = np.unwrap(np.angle(residual))
    delay = -np.gradient(phase, f[ok])/2/np.pi*1000
    axes[1, 1].plot(f[ok], signal.savgol_filter(delay, 101, 2))
    axes[1, 1].set(xlabel="Hz", ylabel="ms", ylim=(.4, .8), title="Delay after removing resonator phase")
    for ax in axes.flat:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "aw-3-filter")

    fig, axes = plt.subplots(2, 1, figsize=(12, 9))
    for name, rows in stairs.items():
        r = np.array(rows)
        line = axes[0].plot(r[:, 0], r[:, 1], "o-", ms=3, label=name.replace("stairs-", ""))
        t, fc = detector.frequency(p, name)
        pred = [np.median(fc[(t > (k+.6)*1.5) & (t < (k+.98)*1.5)]) for k in range(len(r))]
        axes[0].plot(r[:, 0], pred, "--", color=line[0].get_color(), lw=.8)
    axes[0].set(xlabel="nominal noise peak, dB re guitar peak", ylabel="pole Hz", title="Settled steps: solid measurement, dashed tied model")
    axes[0].legend(fontsize=8, ncol=3)
    for name in ["rest-sens-1200", "rest-sens-0200", "rest-sens-0300", "rest-sens-0500"]:
        x, obs, pred, ok = predictions[name]
        line = axes[1].semilogx(x, obs, label=name.replace("rest-sens-", "Sens "))
        axes[1].semilogx(x, pred, "--", color=line[0].get_color())
    axes[1].set(xlim=(50, 16000), ylim=(-35, 18), xlabel="sweep's instantaneous Hz", ylabel="dB re bypass", title="Moving sweeps: lock-in measurement and model")
    axes[1].legend(fontsize=8)
    for ax in axes:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "aw-3-control")

    fig, axes = plt.subplots(3, 2, figsize=(13, 11))
    for ax, name in zip(axes.flat, [f"jump-decay-{tag(pos)}" for pos in POSITIONS] + ["stairs-sens-1200"]):
        t, obs, pred, ok = predictions[name]
        ax.plot(t, np.where(ok, obs, np.nan), lw=.8, label="measured pole")
        ax.plot(t, pred, "--", lw=1, label="tied model")
        ax.set(title=name, xlabel="seconds from signal start", ylabel="Hz", ylim=(150, 2400))
        if name.startswith("jump"):
            ax.set_xlim(7.9, 11.8)
        ax.grid(alpha=.25)
        ax.legend(fontsize=7)
    save_figure(fig, directory, "aw-3-dynamics")

    fig, axes = plt.subplots(3, 1, figsize=(13, 10))
    for ax, (name, rows) in zip(axes, plucks.items()):
        for i, (t, obs, pred) in enumerate(rows):
            line = ax.plot(t, obs, label="pedal" if i == 0 else None)
            ax.plot(t, pred, "k--", lw=1, label="model" if i == 0 else None)
        ax.set(title=name + (" (held out)" if name.endswith("0500") else " (fit includes its gain envelope)"), xlabel="seconds", ylabel="fundamental gain, dB", ylim=(-22, 20))
        ax.legend()
        ax.grid(alpha=.25)
    save_figure(fig, directory, "aw-3-plucks")

    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    for name in ["plucks-decay-1200", "plucks-decay-0500"]:
        _, _, y = session.load(name)
        a, b = int(9.010*FS), int(9.022*FS)
        axes[0].plot(np.arange(a, b)/FS-9, y[a:b, 0], label=name[-4:])
    axes[0].set(xlabel="s after 329.63 Hz pick", ylabel="recorded sample", title="Output ceiling on the pick")
    axes[0].legend(title="Decay")
    for name in ["noise-sens-0500", "bypass-stairs"]:
        _, _, y = session.load(name)
        seg = y[FS:8*FS, 0] if name.startswith("noise") else y[-int(.7*FS):-FS//10, 0]
        f, power = signal.welch(seg, FS, nperseg=8192)
        axes[1].semilogx(f, 10*np.log10(np.maximum(power, 1e-20)), label=name)
    axes[1].set(xlim=(30, 24000), xlabel="Hz", ylabel="dBFS/Hz", title="Silence, after the marker has decayed")
    axes[1].legend()
    for ax in axes:
        ax.grid(alpha=.25)
    save_figure(fig, directory, "aw-3-headroom")


def json_default(value):
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    raise TypeError(type(value).__name__)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--audio-source", type=Path)
    parser.add_argument("--fit", action="store_true")
    args = parser.parse_args()
    session = Session(args.session, args.audio_source or args.session)
    directory = args.session / "fit"
    directory.mkdir(exist_ok=True)
    print("Auditing the WAVs and measuring static responses", flush=True)
    audit = session.audit()
    static, curves, knots = static_measurements(session)
    tracker = Tracker(session, curves)
    tracks = {n: tracker.track(n) for n in session.meta if n.startswith(("stairs-", "jump-"))}
    sweeps = sweep_envelopes(session)
    stairs = stairs_summary(session, tracks)
    control = fit_control(session, stairs, knots) if args.fit else np.array(CONTROL)
    detector = DetectorFit(session, tracks, sweeps, knots, control)
    p = np.array(DETECTOR)
    fit_info = {"method": "published coefficients; use --fit to refit"}
    if args.fit:
        print("Refitting the shared detector and knob maps", flush=True)
        p, fit_info = detector.fit(p)
    predictions = detector.evaluate(p, True)
    errors = {}
    for name, (_, obs, pred, ok) in predictions.items():
        key = "frequency_rms_semitones" if name in tracks else "response_rms_db"
        err = 12*np.log2(pred[ok]/obs[ok]) if name in tracks else pred[ok]-obs[ok]
        errors[name] = {key: rms(err), "points": int(ok.sum()), "held_out": False}
    print("Rendering plucks (Decay 5:00 held out)", flush=True)
    pluck_errors, plucks = pluck_validation(session, tracker, detector, p)
    top = top_end(session)
    report = {"pedal": "Boss AW-3, Up", "session": args.session.name, "rate": FS,
              "take_count": len(session.meta), "audit": audit, "rest_manual": static,
              "stairs_level_pole_hz_sd": stairs, "model": {"filter": FILTER, "control": control,
              "manual_fraction_pole_hz": knots, "detector": unpack_detector(p), "optimizer_vector": p,
              "fit": fit_info}, "errors": errors, "plucks": pluck_errors,
              "noise_headroom": noise_and_headroom(session), "input_limit_check": input_limit_check(session, tracker),
              "jumps": jump_metrics(tracker, tracks, control, knots),
              "stationary_noise": stationary_noise(session),
              "top_end_db": {name: {str(hz): float(np.median(db(H)[abs(f-hz) < 100]))
                                   for hz in [10000, 16000, 20000, 21000, 22000, 23000]}
                             for name, (f, H, _) in top.items()},
              "frequency_estimator": {"window_ms": 40, "hop_ms": 10,
                  "note": "local poles have about 1-3% calibration bias; quiet-tail Q is not identifiable"},
              "metric_masks": {"noise": "body >0.2 s and frame RMS >-88 dBFS (10 dB above idle noise); jumps exclude 3.94-4.06 and 7.95-8.13 s; stairs exclude first 0.15 s of steps and levels 0/+3 dB",
                               "sweeps": "80-12000 Hz, excluding 160-260 Hz moving-chirp peak",
                               "plucks": "0.04-2.8 s per note, measured output above -110 dBFS; Decay 5:00 held out; 7:00/12:00 gain envelopes fitted"}}
    (directory / "aw-3.json").write_text(json.dumps(report, indent=2, default=json_default, allow_nan=False)+"\n")
    plot(directory, session, static, curves, stairs, predictions, plucks, top, detector, p)
    print(json.dumps({"errors": errors, "plucks": pluck_errors, "detector": unpack_detector(p)}, indent=2))


if __name__ == "__main__":
    main()
