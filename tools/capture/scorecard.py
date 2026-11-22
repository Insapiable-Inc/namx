"""The scorecard: one way to compare a pedal's recording with a model's render of the same take,
the same for every pedal. Both sides go through one causal third-octave filter bank (80 Hz-10 kHz);
the level of each band in 50 ms windows is compared wherever either side is 12 dB over the rig's
floor in that band (the floor: the pedal channel before the marker, the pedal on; its power is
added to the model's side, so both read the floor where the signal sinks into it).

  d = model - pedal, dB, per band and window:
    rms      over every band and window seen                (the headline number)
    p95      the 95th percentile of |d|
    level    the median d of the whole band's level in the same windows (one gain too high or low)
    tone     the mean d per band                            (a filter off)
    residual the waveform's error re the pedal, dB, after the best sub-sample alignment and with
             the model's side through the bypass chain (time-invariant pedals only)

The same numbers between a take and its repeat are the pedal's difference from itself: the floor
under which a model's error can't be told from the pedal's own."""
import json, pathlib
import numpy as np, scipy.signal, soundfile

RATE = 48000
WINDOW = 2400                     # 50 ms: the 80 Hz band is 18 Hz wide
OVER_FLOOR_DB = 12.0
BANDS = [80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000,
         5000, 6300, 8000, 10000]
BANK = [scipy.signal.butter(3, [b / 2 ** (1 / 6), b * 2 ** (1 / 6)], "bandpass", fs=RATE, output="sos")
        for b in BANDS]
HIGH_PASS = scipy.signal.butter(4, 60, "highpass", fs=RATE, output="sos")


def db(power):
    return 10 * np.log10(power + 1e-30)


def slips(loop, played, start, block=RATE // 4, search=6):
    """Where the recording slips against the played signal, from the loopback: a list of (sample,
    samples slipped from there on). PipeWire now and then drops or repeats a sample in the middle of
    a take without an xrun (one sample, about one take in six on 2026-11-09; capture.py flags the
    47-sample ones); both recorded channels slip together. Read under 3 kHz (a probe's sine matches
    itself a period away); blocks too quiet there to place are skipped."""
    low = scipy.signal.butter(4, 3000, "lowpass", fs=RATE, output="sos")
    loop, played = scipy.signal.sosfiltfilt(low, loop), scipy.signal.sosfiltfilt(low, played)
    lags, at = [], []
    for i in range(start, len(played) - block - search, block):
        x = played[i + search:i + block - search]
        if np.sqrt(np.mean(x ** 2)) < 1e-4:
            continue
        c = [np.dot(x, loop[i + search + k:i + block - search + k]) for k in range(-search, search + 1)]
        k = int(np.argmax(c))
        if 0 < k < 2 * search:
            lags.append(k - search + 0.5 * (c[k - 1] - c[k + 1]) / (c[k - 1] - 2 * c[k] + c[k + 1]))
            at.append(i)
    if len(lags) < 4:
        return []
    steps = np.round(np.array(lags) - np.median(lags[:4])).astype(int)
    found, current = [], 0
    for i, step in zip(at, steps):
        # A step counts once the next block agrees (one odd block is a bad match, not a slip).
        if step != current and np.sum(steps[np.array(at) >= i][:2] == step) == 2:
            found.append((i, int(step)))
            current = step
    return found


def load(session, name):
    """A take: its sidecar, the played signal, the pedal channel aligned to it — any slip in the
    recording undone (`slips`; the sidecar's "slips" lists them) — and the pedal channel before the
    marker."""
    session = pathlib.Path(session)
    meta = json.loads((session / f"{name}.json").read_text())
    recorded, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(session / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(recorded) - lat) - 16
    pedal = recorded[lat:lat + n, 0].copy()
    loop = recorded[lat:lat + n + 16, 1]
    # A loopback that never arrived (its cable out: 2026-11-21, the AW-3's check session) has no
    # slips to show; read anyway, its noise gives a few samples either way at random.
    meta["slips"] = slips(loop, played[:n + 16], meta["signal_start"]) if np.abs(loop).max() > 1e-3 else []
    for i, step in meta["slips"]:
        pedal[i:] = recorded[lat + i + step:lat + n + step, 0]
    return meta, played[:n], pedal, recorded[:meta["marker_start"], 0]


def chain_filter(played, pedal):
    """The played -> recorded response through the bypassed pedal: a 512-tap FIR, centred. Read only
    where the played signal has energy (within 60 dB of its strongest bin) and drawn straight between
    those bins: a guitar excerpt is nearly empty above 5 kHz."""
    pedal = pedal - pedal.mean()
    f, pxy = scipy.signal.csd(played, pedal, RATE, nperseg=8192)
    _, pxx = scipy.signal.welch(played, RATE, nperseg=8192)
    known = pxx > pxx.max() * 1e-6
    response = pxy[known] / pxx[known]
    magnitude = np.interp(f, f[known], np.abs(response))
    phase = np.interp(f, f[known], np.unwrap(np.angle(response)))
    h = np.fft.irfft(magnitude * np.exp(1j * phase))
    return np.roll(h, 256)[:512] * np.hanning(512)


def through(h, x):
    return np.convolve(x, h)[256:256 + len(x)]


SETTLE = RATE // 5                # the bank runs this long before the signal: its start-up has died


def band_levels(x, start):
    """Power per band and window from `start` on: (bands, windows). The input's own offset (the
    interface's) is taken out first: it is a step to the bank."""
    x = x[start - SETTLE:] - np.mean(x[start - SETTLE:start])
    n = (len(x) - SETTLE) // WINDOW
    return np.array([np.mean(scipy.signal.sosfilt(sos, x)[SETTLE:SETTLE + n * WINDOW].reshape(n, WINDOW) ** 2,
                             axis=1) for sos in BANK])


def shift(x, samples):
    """x delayed by a fraction of a sample (through the spectrum)."""
    f = np.fft.rfftfreq(len(x))
    return np.fft.irfft(np.fft.rfft(x) * np.exp(-2j * np.pi * f * samples), len(x))


def residual_db(pedal, model):
    """The waveform's error re the pedal after the best alignment within +-4 samples (60 Hz high-passed)."""
    a, b = (scipy.signal.sosfilt(HIGH_PASS, v) for v in (pedal, model))
    lags = np.arange(-4, 5)
    xc = np.array([np.dot(a[8:-8], np.roll(b, k)[8:-8]) for k in lags])
    k = int(np.argmax(xc))
    frac = 0.0
    if 0 < k < len(lags) - 1:
        y0, y1, y2 = xc[k - 1:k + 2]
        frac = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    b = shift(b, lags[k] + frac)
    return db(np.sum((a - b)[64:-64] ** 2) / np.sum(a[64:-64] ** 2))


def score(pedal, model, pre, start, time_invariant=True):
    """The numbers for one take: pedal and model aligned and whole (from the played signal's first
    sample), `pre` the pedal channel before the marker, `start` where the signal begins."""
    pre = pre - np.mean(pre)
    floor = np.mean(np.array([scipy.signal.sosfilt(sos, pre)[RATE // 10:] ** 2 for sos in BANK]), axis=1)[:, None]
    p = band_levels(pedal, start)
    m = band_levels(model, start) + floor
    seen = (np.maximum(p, m) > floor * 10 ** (OVER_FLOOR_DB / 10))
    d = db(m) - db(p)
    seen_any = seen.any(axis=0)
    level = db((m * seen).sum(axis=0)[seen_any]) - db((p * seen).sum(axis=0)[seen_any])
    tone = np.array([d[i][seen[i]].mean() if seen[i].sum() >= 10 else np.nan for i in range(len(BANDS))])
    return {
        "rms": float(np.sqrt(np.mean(d[seen] ** 2))),
        "p95": float(np.percentile(np.abs(d[seen]), 95)),
        "level": float(np.median(level)),
        "tone": tone,
        "residual": residual_db(pedal[start:], model[start:]) if time_invariant else float("nan"),
        "seen": float(seen.mean()),
    }


HEADER = f"{'take':28s}    rms    p95  level  residual   seen"


def row(name, s):
    return (f"{name:28s} {s['rms']:6.2f} {s['p95']:6.2f} {s['level']:+6.2f} {s['residual']:8.1f} "
            f"{s['seen']:6.0%}")


def tone_rows(rows, step=1):
    bands = BANDS[::step]
    out = [f"{'tone: model - pedal, dB, at':28s} " + " ".join(f"{b if b < 1000 else f'{b / 1000:g}k':>5}" for b in bands)]
    for name, s in rows:
        out.append(f"{name:28s} " + " ".join("     " if np.isnan(t) else f"{t:+5.1f}" for t in s["tone"][::step]))
    return "\n".join(out)
