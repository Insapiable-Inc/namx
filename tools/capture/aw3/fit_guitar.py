#!/usr/bin/env python3
"""The AW-3 detector and control law refitted to the pedal's filter track on guitar (the focused
session, read as in focused.py) together with October's noise staircases (2026-10-31-up). Prints
the error per take before and after, then the fitted values (Aw3Wah.h holds them). Arguments
--NAME=VALUE fix a parameter. The tracks are cached in build-aw3/tracks.npz.

Run from the repository root: tools/capture/aw3/fit_guitar.py --p=1 --fast=0.0005 (superseded by fit_detector.py)"""
import json, pathlib, sys
import numpy as np, scipy.signal, scipy.optimize, numba
sys.argv = sys.argv[:1] + [a for a in sys.argv[1:] if a.startswith("--")]
sys.path.insert(0, "tools/capture"); sys.path.insert(0, "tools/capture/aw3")
import scorecard, focused as F
RATE, STEP = F.RATE, F.STEP
S = F.SESSION
C = F.constants()
CACHE = pathlib.Path("build-aw3/tracks.npz")

def tracks():
    if CACHE.exists():
        z = np.load(CACHE, allow_pickle=True)
        return z["data"].item()
    meta, played, pedal, _ = scorecard.load(S, "bypass-funk")
    h = scorecard.chain_filter(played, pedal)
    out = {}
    for p in sorted(S.glob("*.json")):
        if p.name == "rig.json": continue
        meta = json.loads(p.read_text())
        if meta["bypass"] or "probe" not in meta["signal"]: continue
        name = meta["take"]
        meta, played, pedal, pre = scorecard.load(S, name)
        a = meta["signal_start"]; b = a + int(meta["signal"]["seconds"] * RATE)
        dry = scorecard.through(h, played)
        dl = np.abs(F.lines(dry))
        inside = np.zeros(dl.shape[1], bool); inside[(a + RATE // 5) // STEP:(b - RATE // 5) // STEP] = True
        own = np.percentile(dl[:, inside], 20, axis=1)[:, None]
        clear = inside & np.all(np.abs(20 * np.log10(dl / own)) < 0.5, axis=0)
        sharp = meta["settings"]["Mode"] == "Sharp"
        table = F.static_gains(C, sharp); wide = F.static_gains(C, sharp, F.BIN_HZ)
        db = F.spectra(dry)[:, :dl.shape[1]]
        floor = 10 ** ((meta["ref_dbfs"] - 40) / 20) / 2
        w = np.where(db > floor, db ** 2, 0.0)
        loud = inside & ~clear & (np.count_nonzero(w, axis=0) >= 6)
        g = 20 * np.log10(np.maximum(np.abs(F.lines(pedal)), 1e-12) / dl)
        f, _ = F.place(g, table)
        wg = 20 * np.log10(np.maximum(F.spectra(pedal)[:, :dl.shape[1]], 1e-12) / np.maximum(db, 1e-12))
        f = np.where(clear, f, F.place_weighted(wg, w, wide))
        out[name] = dict(settings=meta["settings"], x=(played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32),
                         f=f, clear=clear, loud=loud)
        print("track", name, file=sys.stderr)
    np.savez_compressed(CACHE, data=np.array(out, dtype=object))
    return out

@numba.njit(cache=True)
def levels(x, c1, c2, order2):
    """Weighted mean square per 1 ms tick."""
    n = len(x) // 48
    out = np.empty(n)
    s1 = 0.0; s2 = 0.0; acc = 0.0
    for i in range(n * 48):
        h = x[i]
        v = (h - s1) * c1; lp = v + s1; s1 = lp + v; h -= lp
        if order2:
            v = (h - s2) * c2; lp = v + s2; s2 = lp + v; h -= lp
        acc += h * h
        if i % 48 == 47:
            out[i // 48] = acc / 48; acc = 0.0
    return out

@numba.njit(cache=True)
def follow(level, attack, fast, decay):
    out = np.empty(len(level)); f = 0.0; e = 0.0
    for i in range(len(level)):
        l = level[i]
        f += (l - f) * (attack if l > f else fast)
        e += (f - e) * (attack if f > e else decay)
        out[i] = e
    return out

HANN = np.hanning(23)[1:-1]; HANN /= HANN.sum()
def coef(tau): return 1 - np.exp(-0.001 / tau)
def hpc(hz): g = np.tan(np.pi * hz / RATE); return g / (1 + g)

NAMES = ["hp1", "hp2", "limit", "attack", "fast", "d7", "d12", "d5", "s9", "s12", "s3", "s5", "power", "p"]
START = dict(hp1=1168.34, hp2=9046.79, limit=0.0178814, attack=0.0028159, fast=0.0166994,
             d7=0.26437, d12=0.29577, d5=1.35567, s9=196.9, s12=541.335, s3=2039.48, s5=3810.88, power=3.222556, p=1.0)
FLOOR, TOP = C["FLOOR_HZ"], C["TOP_HZ"]
MANUAL = {"7:00": 193.74, "12:00": 228.91, "5:00": 523.62}
SENS = {"7:00": None, "9:00": "s9", "12:00": "s12", "3:00": "s3", "5:00": "s5"}
DEC = {"7:00": "d7", "12:00": "d12", "5:00": "d5"}

def predict(t, q):
    lv = np.sqrt(levels(t["x"], hpc(q["hp1"]), hpc(q["hp2"]), q["hp2"] < 20000))
    lv = np.minimum(lv, q["limit"]) ** q["p"]
    s = t["settings"]
    env = follow(lv, coef(q["attack"]), coef(q["fast"]), coef(q[DEC[s["Decay"]]]))
    gain = q[SENS[s["Sens"]]] if SENS[s["Sens"]] else 0.0
    off = max(MANUAL[s["Manual"]] - FLOOR, 0) ** (1 / q["power"])
    f = np.minimum(TOP, FLOOR + (off + gain * env) ** q["power"])
    return np.exp(np.convolve(np.log(f), HANN, "same"))

def errors(t, q):
    f = predict(t, q); n = min(len(f), len(t["f"]))
    m = (t["clear"] | t["loud"])[:n]
    return np.log2(f[:n] / t["f"][:n])[m]

def stairs():
    import soundfile
    O = pathlib.Path("captures/boss/aw-3/2026-10-31-up")
    measured = json.loads((O / "fit/aw-3.json").read_text())["stairs_level_pole_hz_sd"]
    out = {}
    for name, rows in measured.items():
        if name == "stairs-sens-0700": continue
        meta = json.loads((O / f"{name}.json").read_text())
        x, _ = soundfile.read(O / "played" / f"{name}.wav")
        x = x[:, 0] if x.ndim > 1 else x
        out[name] = dict(settings=meta["settings"], x=(x / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32),
                         start=meta["signal_start"], rows=[r for r in rows if -48 <= r[0] <= -6])
    return out

def stair_errors(t, q, show=False):
    f = predict(t, q); out = []
    for level, hz, _ in t["rows"]:
        k = round((level + 54) / 6)
        a = (t["start"] + int((k + .6) * 1.5 * RATE)) // 48; b = (t["start"] + int((k + .98) * 1.5 * RATE)) // 48
        out.append(np.log2(np.median(f[a:b]) / hz))
    return np.array(out)

if __name__ == "__main__":
    T = tracks()
    ST = stairs()
    up = [n for n in T if T[n]["settings"]["Mode"] == "Up"]
    fit_on = [n for n in up if "lead" not in n and "repeat" not in n]
    free = [n for n in NAMES if n not in sys.argv[1:] or True]
    fixed = dict(a.lstrip("-").split("=") for a in sys.argv[1:] if "=" in a)
    free = [n for n in NAMES if n not in fixed]
    def q_of(v):
        q = dict(START); q.update({k: float(x) for k, x in fixed.items()})
        q.update({n: START[n] * np.exp(x) for n, x in zip(free, v)}); return q
    def res(v):
        q = q_of(v)
        guitar = [errors(T[n], q) for n in fit_on]
        return np.concatenate([e / np.sqrt(len(e)) for e in guitar] + [stair_errors(t, q) * 0.4 for t in ST.values()])
    def report(q):
        for n in T:
            if T[n]["settings"]["Mode"] != "Up": continue
            e = errors(T[n], q)
            print(f"  {n:24s} mean {e.mean():+.2f} rms {np.sqrt(np.mean(e**2)):.2f} oct {'' if n in fit_on else '(held out)'}")
        for n, t in ST.items():
            print(f"  {n:24s} " + " ".join(f"{e:+.2f}" for e in stair_errors(t, q)))
    print("start"); report(q_of(np.zeros(len(free))))
    r = scipy.optimize.least_squares(res, np.zeros(len(free)), loss="soft_l1", f_scale=0.3, diff_step=0.02)
    q = q_of(r.x)
    print("fit"); report(q)
    print({k: round(v, 6) for k, v in q.items()})
