"""The rotors' rates in the isolated session's noise takes (fit_rt2.py's HORN_HZ): the periodicity of the short-time
responses (cross-correlation over 50 ms windows, white input), refined over as many turns as the take allows. The horn
from its high band (> 1.5 kHz), the drum from its low band (< 400 Hz). Run from the repository root."""
import pathlib, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import fit_rt2 as F


def short_irs(x, y, start, stop, win, hop, L):
    """Short-time impulse responses: rows = windows, columns = lags 0 … L−1."""
    out = []
    for a in range(start, stop - win, hop):
        c = scipy.signal.correlate(y[a:a + win], x[a - L + 1:a + win], mode="valid")[::-1]
        out.append(c / np.dot(x[a:a + win], x[a:a + win]))
    return np.array(out)


def similarity(feat, lags):
    n = len(feat)
    return np.array([np.mean(np.sum(feat[l:] * feat[:n - l], axis=1) /
                             (np.linalg.norm(feat[l:], axis=1) * np.linalg.norm(feat[:n - l], axis=1) + 1e-12)) for l in lags])


def peak(sim, lags):
    k = int(np.clip(np.argmax(sim), 1, len(sim) - 2))
    y0, y1, y2 = sim[k - 1], sim[k], sim[k + 1]
    return lags[k] + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2), sim[k]


hi = scipy.signal.butter(4, 1500 / (F.FS / 2), "high", output="sos")
lo = scipy.signal.butter(4, 400 / (F.FS / 2), "low", output="sos")
for name, band in (("m1-horn-noise", hi), ("m1-drum-noise", lo), ("m2-horn-noise", hi), ("m2-drum-noise", lo),
                   ("m3-horn-noise", hi), ("m3-drum-noise", lo)):
    tk = F.load(name)
    win, hop = 2400, 600
    irs = scipy.signal.sosfiltfilt(band, short_irs(tk["x"], tk["A"], tk["s0"] + 4800, tk["s1"] - 100, win, hop, 480), axis=1)
    env = np.abs(scipy.signal.hilbert(irs, axis=1))
    feat = env - env.mean(axis=0)
    dt = hop / F.FS
    T, _ = peak(similarity(feat, np.arange(int(2.0 / dt), int(6.0 / dt))), np.arange(int(2.0 / dt), int(6.0 / dt)))
    T *= dt
    K = int((len(feat) * dt - 1.0) / T) - 1
    lags = np.arange(int((K - 0.2) * T / dt), int((K + 0.2) * T / dt))
    TK, s = peak(similarity(feat, lags), lags)
    print(f"{name:14s}: {K / (TK * dt):.5f} Hz over {K} turns (similarity {s:.2f})", flush=True)
