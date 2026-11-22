#!/usr/bin/env python3
"""The AW-3 detector and control law, the fit in Aw3Wah.h: fit_detector.py's model plus a cap on the
control just over the top, fitted to everything — the guitar tracks, October's staircases and
jumps, the first blind jump and the jumps session (run jumps.py first). The second blind session
is validation: in no fit. --nocap fits without the cap.

Run from the repository root: tools/capture/aw3/fit_jumps.py --fast=0.0005"""
import sys, json, pathlib, numpy as np, scipy.optimize
ARGV = sys.argv[1:]; sys.argv = sys.argv[:1]
sys.path.insert(0, str(pathlib.Path(__file__).parent)); sys.path.insert(0, "tools/capture"); sys.path.insert(0, "tools/capture/aw3")
import fit_detector as M2
M = M2.M
NAMES = M2.NAMES + ["capf", "sj9", "sj12", "sj3", "mj"]
START = dict(M2.START, hp1=819.78, hp2=7274.77, limit=0.02173, attack=0.00503, fast=0.0005, share=0.11, d7=0.256, d9=0.298, d1030=0.2945,
             d12=0.2636, d3=0.708, d5=1.339, s9=257.45, s12=816.48, s3=3065.18, s5=5596.52, sb=1180.4, mb=466.3, power=2.72546,
             capf=1.15, sj9=257.45, sj12=816.48, sj3=3065.18, mj=234.0)
M.SENS.update({"j9": "sj9", "j12": "sj12", "j3": "sj3"})
NOCAP = "--nocap" in ARGV

def predict(t, q, smooth=True):
    lv = np.minimum(np.sqrt(M.levels(t["x"], M.hpc(q["hp1"]), M.hpc(q["hp2"]), True)), q["limit"])
    s = t["settings"]
    gain = q[M.SENS[s["Sens"]]] if M.SENS[s["Sens"]] else 0.0
    rest = q["mb"] if s["Manual"] == "blind" else q["mj"] if s["Manual"] == "j12" else M.MANUAL[s["Manual"]]
    off = max(rest - M.FLOOR, 0) ** (1 / q["power"])
    if gain > 0 and not NOCAP:
        top = (M.TOP - M.FLOOR) ** (1 / q["power"])
        lv = np.minimum(lv, max(q["capf"] * top - off, 0) / gain)
    env = M2.follow2(lv, M.coef(q["attack"]), M.coef(q["fast"]), M.coef(q[M.DEC[s["Decay"]]]), q["share"])
    f = np.minimum(M.TOP, M.FLOOR + (off + gain * env) ** q["power"])
    return np.exp(np.convolve(np.log(f), M.HANN, "same")) if smooth else f
M.predict = predict; M2.predict = predict

def new_jumps():
    d = np.load("build-aw3/jumps2.npz", allow_pickle=True)["data"].item()
    for n, t in d.items():
        s = t["settings"]
        s["Sens"] = {"9:00": "j9", "12:00": "j12", "3:00": "j3", "5:00": "5:00"}[s["Sens"]]
        if s["Manual"] == "12:00": s["Manual"] = "j12"
    return d

def track_errors(t, q):
    f = predict(t, q, smooth=False)
    a, b = (t["start"] + 48000) // 480, (t["start"] + int(24.4 * 48000)) // 480
    b = min(b, len(t["f"]), len(f) // 10)
    pred = np.median(f[:b * 10].reshape(b, 10), axis=1)
    return np.log2(pred[a:b] / t["f"][a:b])

if __name__ == "__main__":
    T = M.tracks(); ST = M.stairs(); J = M2.jumps(); N = new_jumps()
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
        nj = [track_errors(t, q) for t in N.values()]
        return np.concatenate([e / np.sqrt(len(e)) for e in g] + [M.stair_errors(t, q) * 0.4 for t in ST.values()]
                              + [M2.jump_errors(t, q) / np.sqrt(117) * 1.5 for t in J.values()] + [e / np.sqrt(len(e)) * 1.5 for e in nj])
    def report(q):
        print("  guitar: " + " ".join(f"{np.sqrt(np.mean(M.errors(T[n], q)**2)):.2f}" for n in up))
        print("  stairs worst", max(np.abs(M.stair_errors(t, q)).max() for t in ST.values()).round(2))
        print("  old jumps: " + " ".join(f"{np.sqrt(np.mean(M2.jump_errors(t, q)**2)):.2f}" for t in J.values()))
        for n, t in N.items():
            e = track_errors(t, q); print(f"  {n:24s} mean {e.mean():+.2f} rms {np.sqrt(np.mean(e**2)):.2f}")
    print("start"); report(q_of(np.zeros(len(free))))
    r = scipy.optimize.least_squares(res, np.zeros(len(free)), loss="soft_l1", f_scale=0.3, diff_step=0.02)
    q = q_of(r.x); print("fit"); report(q); print({k: round(float(v), 5) for k, v in q.items()})
    json.dump({k: float(v) for k, v in q.items()}, open("build-aw3/fit3" + ("-nocap" if NOCAP else "") + ".json", "w"))
