"""Reading capture sessions back: aligned takes, envelopes, LFO rate and cycle shape.

A take's signal starts at its sidecar's `signal_start` plus the latency measured on the
loopback, so the pedal channel lines up with what was played to the sample.
"""

import json
import pathlib

import numpy as np
import scipy.optimize
import scipy.signal
import soundfile

ENVELOPE_CUTOFF_HZ = 300.0
THIRD_OCTAVES_HZ = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


class Take:
    def __init__(self, session, name):
        session = pathlib.Path(session).expanduser()
        with open(session / f"{name}.json", encoding="utf-8") as file:
            self.meta = json.load(file)
        data, self.rate = soundfile.read(session / f"{name}.wav")
        self.pedal = data[:, 0]
        self.start = self.meta["signal_start"] + self.meta["latency_samples"]

    def signal(self, extra_seconds=0.0):
        """The pedal channel over the signal (plus some of the tail)."""
        spec = self.meta["signal"]
        seconds = spec["seconds"] if "seconds" in spec else spec["period"] * spec["count"]
        return self.pedal[self.start:self.start + int((seconds + extra_seconds) * self.rate)]


def envelope(x, rate):
    """Amplitude envelope of a sine carrier (Hilbert magnitude, low-passed)."""
    b, a = scipy.signal.butter(4, ENVELOPE_CUTOFF_HZ / (rate / 2))
    return scipy.signal.filtfilt(b, a, np.abs(scipy.signal.hilbert(x)))


def lfo_frequency(g, rate, low=0.3, high=40.0):
    """The modulation's fundamental (Hz, to ~0.01 %) and its phase at t = 0."""
    t = np.arange(len(g)) / rate
    centred = g - g.mean()
    padded = 16 * len(g)
    spectrum = np.abs(np.fft.rfft(centred * np.hanning(len(g)), padded))
    freqs = np.fft.rfftfreq(padded, 1 / rate)
    band = (freqs > low) & (freqs < high)
    guess = freqs[band][np.argmax(spectrum[band])]

    def projection(f):
        return np.sum(centred * np.exp(-2j * np.pi * f * t))

    best = scipy.optimize.minimize_scalar(lambda f: -abs(projection(f)),
                                          bounds=(guess * 0.995, guess * 1.005), method="bounded")
    return best.x, np.angle(projection(best.x))


def fold_cycle(g, rate, frequency, phase, bins):
    """One averaged LFO cycle (median per bin); phase 0 = the fundamental's peak."""
    t = np.arange(len(g)) / rate
    index = (np.mod(frequency * t + phase / (2 * np.pi), 1.0) * bins).astype(int)
    return np.array([np.median(g[index == k]) for k in range(bins)])


def third_octave_db(x, rate):
    """Energy per third-octave band at THIRD_OCTAVES_HZ, in dB."""
    power = np.abs(np.fft.rfft(x)) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    edges = 2 ** (1 / 6)
    return np.array([10 * np.log10(power[(freqs >= f / edges) & (freqs < f * edges)].sum())
                     for f in THIRD_OCTAVES_HZ])


def harmonics_db(x, rate, fundamental, count=5):
    """Levels of harmonics 2..count relative to the fundamental, in dB."""
    window = np.blackman(len(x))
    spectrum = np.abs(np.fft.rfft(x * window))
    freqs = np.fft.rfftfreq(len(x), 1 / rate)

    def level(f):
        return spectrum[(freqs > f - 3) & (freqs < f + 3)].max()

    return [20 * np.log10(level(k * fundamental) / level(fundamental)) for k in range(2, count + 1)]


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(np.square(x))) + 1e-20)
