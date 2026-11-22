"""The AW-3 detector and control law, final fit: the two followers side by side (a fast share and
Decay's), fitted to the pedal's filter track on guitar (the focused session), October's noise
staircases and the noise jumps (October's five Decay positions and the blind test's). Builds on
fit_guitar.py (the first refit, one release: its helpers). Prints the errors before and after
and the fitted values (Aw3Wah.h holds them). Caches in build-aw3/.

Run from the repository root: tools/capture/aw3/fit_detector.py --fast=0.0005"""
import sys, json, pathlib, numpy as np, scipy.optimize, numba
ARGV = sys.argv[1:]; sys.argv = sys.argv[:1]
sys.path.insert(0, str(pathlib.Path(__file__).parent)); sys.path.insert(0, "tools/capture"); sys.path.insert(0, "tools/capture/aw3")
import fit_guitar as M, scorecard, focused as F
RATE = 48000
C = F.constants(); WIDE = F.static_gains(C, False, F.BIN_HZ)
CACHE = pathlib.Path("build-aw3/jumps.npz")

def noise_track(session, name, h, block=30):
    meta, played, pedal, _ = scorecard.load(session, name)
    dry = scorecard.through(h, played)
    db = F.spectra(dry); pb = F.spectra(pedal)[:, :db.shape[1]]
    g = 20 * np.log10(np.maximum(pb, 1e-12) / np.maximum(db, 1e-12))
    n = g.shape[1] // block
    gm = np.median(g[:, :n * block].reshape(len(g), n, block), axis=2)
    e = np.sqrt(np.mean((WIDE[:, :, None] - gm[None]) ** 2, axis=1))
    return meta, played, F.GRID[np.argmin(e, axis=0)]

def jumps():
    if CACHE.exists():
        return np.load(CACHE, allow_pickle=True)["data"].item()
    out = {}
    for session, names, by in (("2026-10-31-up", [f"jump-decay-{t}" for t in ("0700", "0900", "1200", "0300", "0500")], "bypass-jump"),
                               ("2026-11-11-blind", ["blind-jump"], "bypass-stairs")):
        S = pathlib.Path("captures/boss/aw-3") / session
        meta, played, pedal, _ = scorecard.load(S, by)
        h = scorecard.chain_filter(played, pedal)
        for name in names:
            meta, played, f = noise_track(S, name, h)
            st = dict(meta["settings"])
            if "blind" in name: st = {"Mode": "Up", "Manual": "blind", "Sens": "blind", "Decay": "10:30"}
            out[name] = dict(settings=st, x=(played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32), f=f,
                             edge=meta["signal_start"] + 8 * RATE)
    np.savez_compressed(CACHE, data=np.array(out, dtype=object))
    return out

@numba.njit(cache=True)
def follow2(level, attack, fast, decay, share):
    out = np.empty(len(level)); a = 0.0; b = 0.0
    for i in range(len(level)):
        l = level[i]
        a += (l - a) * (attack if l > a else fast)
        b += (l - b) * (attack if l > b else decay)
        out[i] = share * a + (1 - share) * b
    return out

NAMES = ["hp1", "hp2", "limit", "attack", "fast", "share", "d7", "d9", "d1030", "d12", "d3", "d5", "s9", "s12", "s3", "s5", "sb", "mb", "power"]
START = dict(hp1=428.1, hp2=7503.5, limit=0.017144, attack=0.002315, fast=0.06, share=0.5, d7=0.3, d9=0.3, d1030=0.3, d12=0.3,
             d3=0.8, d5=1.5, s9=252.69, s12=693.93, s3=2606.83, s5=4717.78, sb=693.93 * 1.24, mb=478.0, power=2.966657)
M.SENS.update({"blind": "sb"}); M.DEC.update({"9:00": "d9", "3:00": "d3", "10:30": "d1030"})

def predict(t, q):
    lv = np.minimum(np.sqrt(M.levels(t["x"], M.hpc(q["hp1"]), M.hpc(q["hp2"]), True)), q["limit"])
    s = t["settings"]
    env = follow2(lv, M.coef(q["attack"]), M.coef(q["fast"]), M.coef(q[M.DEC[s["Decay"]]]), q["share"])
    gain = q[M.SENS[s["Sens"]]] if M.SENS[s["Sens"]] else 0.0
    rest = q["mb"] if s["Manual"] == "blind" else M.MANUAL[s["Manual"]]
    off = max(rest - M.FLOOR, 0) ** (1 / q["power"])
    f = np.minimum(M.TOP, M.FLOOR + (off + gain * env) ** q["power"])
    return np.exp(np.convolve(np.log(f), M.HANN, "same"))
M.predict = predict

def jump_errors(t, q):
    f = predict(t, q)
    a = t["edge"] // 48
    n = int(3.5 * 1000) // 30
    pred = np.array([np.median(f[a + 30 * k:a + 30 * k + 30]) for k in range(n)])
    j0 = a // 30
    # measured blocks are 30 frames from frame 0; align by nearest block
    meas = np.array([t["f"][min((a + 30 * k) // 30, len(t["f"]) - 1)] for k in range(n)])
    predb = np.array([np.median(f[((a + 30 * k) // 30) * 30:((a + 30 * k) // 30) * 30 + 30]) for k in range(n)])
    return np.log2(predb / meas)

if __name__ == "__main__":
    T = M.tracks(); ST = M.stairs(); J = jumps()
    up = [n for n in T if T[n]["settings"]["Mode"] == "Up"]
    fit_on = [n for n in up if "lead" not in n and "repeat" not in n]
    fixed = dict(a.lstrip("-").split("=") for a in ARGV if "=" in a)
    free = [n for n in NAMES if n not in fixed]
    def q_of(v):
        q = dict(START); q.update({k: float(x) for k, x in fixed.items()})
        q.update({n: START[n] * np.exp(x) for n, x in zip(free, v)}); return q
    def res(v):
        q = q_of(v)
        g = [M.errors(T[n], q) for n in fit_on]
        return np.concatenate([e / np.sqrt(len(e)) for e in g] + [M.stair_errors(t, q) * 0.4 for t in ST.values()]
                              + [jump_errors(t, q) / np.sqrt(117) * 1.5 for t in J.values()])
    def report(q):
        for n in up:
            e = M.errors(T[n], q); print(f"  {n:24s} mean {e.mean():+.2f} rms {np.sqrt(np.mean(e**2)):.2f}")
        print("  stairs worst", max(np.abs(M.stair_errors(t, q)).max() for t in ST.values()).round(2))
        for n, t in J.items():
            e = jump_errors(t, q); print(f"  {n:24s} mean {e.mean():+.2f} rms {np.sqrt(np.mean(e**2)):.2f}")
    print("start"); report(q_of(np.zeros(len(free))))
    r = scipy.optimize.least_squares(res, np.zeros(len(free)), loss="soft_l1", f_scale=0.3, diff_step=0.02)
    q = q_of(r.x); print("fit"); report(q); print({k: round(float(v), 5) for k, v in q.items()})
