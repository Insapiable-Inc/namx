"""Mean-absolute detector for the NS-2 control candidate, alongside the existing peak.

Cache the peak/mean ratio in dB, averaged over the same 1 ms intervals as dataset.py.
The mean is causal, has a 20 ms time constant, and starts at zero for each complete
take, including its preamble. No measured output enters this detector.
"""
import json

import numba
import numpy as np
import soundfile

import dataset as D

MEAN_MS = 20.0
CACHE = D.ROOT / "build-ns2/rectified-mean-20ms.npz"
RATIOS = {}


def source(take):
    session, name = take["name"].split("/", 1)
    session = {"reamp": "2026-11-08-reamp", "history": "2026-10-30-history"}.get(session, session)
    folder = D.ROOT / "captures/boss/ns-2" / session
    return folder / "played" / f"{name}.wav", folder / f"{name}.json"


@numba.njit(cache=True)
def peak_mean_ratio(x, rate, mean_ms):
    peak = mean = 0.0
    peak_fall = np.exp(-1.0 / (rate * 0.05))
    mean_rise = -np.expm1(-1.0 / (rate * mean_ms * 0.001))
    out = np.empty(len(x))
    for i in range(len(x)):
        magnitude = abs(x[i])
        peak = max(magnitude, peak * peak_fall)
        mean += (magnitude - mean) * mean_rise
        out[i] = 20.0 * np.log10(max(peak, 1e-12) / max(mean, 1e-12))
    return out


def ratios(takes_by_set):
    if RATIOS:
        return RATIOS
    takes = [t for ts in takes_by_set.values() for t in ts]
    # Rebuild after a source file changes; caches are local, never a source of truth.
    signature = json.dumps({t["name"]: [source(t)[0].stat().st_size, source(t)[0].stat().st_mtime_ns,
                                       len(t["levels"])] for t in takes}, sort_keys=True)
    if CACHE.exists():
        with np.load(CACHE) as cached:
            if str(cached["signature"]) == signature:
                RATIOS.update({t["name"]: cached[t["name"]] for t in takes})
                return RATIOS
    for take in takes:
        wav, _ = source(take)
        played, rate = soundfile.read(wav, dtype="float64")
        if rate != D.RATE:
            raise ValueError(f"Unexpected capture rate: {wav.name}: {rate}")
        ratio = peak_mean_ratio(played, rate, MEAN_MS)
        n = len(ratio) // D.MS
        RATIOS[take["name"]] = ratio[:n * D.MS].reshape(n, D.MS).mean(axis=1)[:len(take["levels"])]
    CACHE.parent.mkdir(exist_ok=True)
    np.savez_compressed(CACHE, signature=signature, **RATIOS)
    return RATIOS
