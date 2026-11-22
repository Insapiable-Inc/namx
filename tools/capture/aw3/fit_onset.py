#!/usr/bin/env python3
"""The AW-3 at low Sens: an onset path beside the fitted detector (Aw3Wah.h's ONSET_* constants).

The check session (2026-11-21-check, in no fit until here) showed the model's filter moving
0.6-0.8 octave too little on a guitar at Sens 9:00: on every pick the pedal's filter jumps to
about the same place — the Sens position's ceiling — and falls back within 0.2 s, under a chord
that holds its level; between hits the model was right. At Sens 12:00 the pedal does no such
thing (an onset path fitted there on the 2026-11-11 takes comes out at 7 % of this one and
improves nothing). So, at low Sens only:

  level'  the input through a one-pole high-pass at ONSET_HP_HZ (nearly its derivative), RMS per ms
  a       level' smoothed (ONSET_ATTACK_S); b: a smoothed again (ONSET_RELEASE_S)
  control = min(Sens · envelope + onset_gain · max(0, a − b), the Sens position's ceiling)

Fitted on the check session's Up take at Sens 9:00 (Manual 3:00, Decay 9:00: its Sens gain, rest
and release are its own, hand-set knobs) together with what October and the jumps session have at
Sens 9:00 on noise (the staircase, the jumps), everything else as in Aw3Wah.h. Checked on the
Sharp and Down takes at the same knobs (the detector is shared; Down's rest is its own) and on the
check session's other Up takes (Sens 3:00 and 5:00: no onset path, the model as it was).

Run from the repository root: tools/capture/aw3/fit_onset.py (caches in build-aw3/)."""
import json, pathlib, sys
import numpy as np, scipy.optimize, numba
sys.argv = sys.argv[:1]
sys.path.insert(0, str(pathlib.Path(__file__).parent)); sys.path.insert(0, "tools/capture")
import fit_jumps as FJ, focused as F, scorecard
M, M2 = FJ.M, FJ.M2
C = F.constants()
SESSION = pathlib.Path("captures/boss/aw-3/2026-11-21-check")
CACHE = pathlib.Path("build-aw3/check-tracks.npz")
FIT_ON = "up-m0300-s0900-d0900"
# Aw3Wah.h's detector, and the jumps session's own Sens gain and rest (hand-set knobs: fit_jumps.py).
Q = dict(hp1=C["DETECTOR_HP_HZ"][0], hp2=C["DETECTOR_HP_HZ"][1], limit=C["LEVEL_LIMIT"], attack=C["ATTACK_S"],
         fast=C["FAST_RELEASE_S"], share=C["FAST_SHARE"], power=C["POWER"], capf=C["CONTROL_CAP"],
         s9=C["SENS_GAIN"][2], sj9=212.32, mj=233.65)
DECAY = dict(zip(("7:00", "9:00", "12:00", "3:00", "5:00"), C["DECAY_S"]))
SENS = dict(zip(("7:00", "9:00", "12:00", "3:00", "5:00"), C["SENS_GAIN"][[0, 2, 5, 8, 10]]))
REST = dict(zip(("7:00", "9:00", "12:00", "3:00", "5:00"), C["UP_MANUAL_HZ"][[0, 2, 5, 8, 10]]))
REST["j12"] = Q["mj"]
SENS["j9"] = Q["sj9"]


def tracks():
    """The pedal's filter track on every wet take of the check session, read as focused.py does."""
    if CACHE.exists():
        return np.load(CACHE, allow_pickle=True)["data"].item()
    _, played, pedal, _ = scorecard.load(SESSION, "bypass-funk")
    h = scorecard.chain_filter(played, pedal)
    out = {}
    for path in sorted(SESSION.glob("*.json")):
        meta = json.loads(path.read_text())
        if path.name == "rig.json" or meta["bypass"]:
            continue
        meta, played, pedal, _ = scorecard.load(SESSION, meta["take"])
        dry = scorecard.through(h, played)
        dry_lines = np.abs(F.lines(dry))
        inside = np.zeros(dry_lines.shape[1], bool)
        clear = inside.copy()
        for _, first, end in F.segments(meta):
            part = np.zeros_like(inside)
            part[(first + F.RATE // 5) // F.STEP:(end - F.RATE // 5) // F.STEP] = True
            own = np.percentile(dry_lines[:, part], 20, axis=1)[:, None]
            clear |= part & np.all(np.abs(20 * np.log10(np.maximum(dry_lines, 1e-12) / own)) < 0.5, axis=0)
            inside |= part
        sharp = meta["settings"]["Mode"] == "Sharp"
        table, wide = F.static_gains(C, sharp), F.static_gains(C, sharp, F.BIN_HZ)
        dry_bins = F.spectra(dry)[:, :dry_lines.shape[1]]
        weights = np.where(dry_bins > 10 ** ((meta["ref_dbfs"] - 40) / 20) / 2, dry_bins ** 2, 0.0)
        loud = inside & ~clear & (np.count_nonzero(weights, axis=0) >= 6)
        gains = 20 * np.log10(np.maximum(np.abs(F.lines(pedal)), 1e-12) / dry_lines)
        f, _ = F.place(gains, table)
        wide_gains = 20 * np.log10(np.maximum(F.spectra(pedal)[:, :dry_lines.shape[1]], 1e-12) / np.maximum(dry_bins, 1e-12))
        out[meta["take"]] = dict(settings=meta["settings"], f=np.where(clear, f, F.place_weighted(wide_gains, weights, wide)),
                                 x=(played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32), clear=clear, loud=loud)
        print("track", meta["take"], file=sys.stderr)
    CACHE.parent.mkdir(exist_ok=True)
    np.savez_compressed(CACHE, data=np.array(out, dtype=object))
    return out


@numba.njit(cache=True)
def onset(level, attack, release):
    out = np.empty(len(level))
    a = 0.0
    b = 0.0
    for i in range(len(level)):
        a += (level[i] - a) * attack
        b += (a - b) * release
        out[i] = max(a - b, 0.0)
    return out


def predict(t, r, smooth=True):
    """The filter's frequency per ms. r: the onset path (hp, ta, tr, gt) and, for a take with
    hand-set knobs of its own, gain, rest and decay."""
    s = t["settings"]
    gain = r.get("gain", SENS[s["Sens"]])
    rest = r.get("rest", REST.get(s["Manual"], 0.0))
    down = s["Mode"] == "Down"
    off = max(rest - (C["DOWN_FLOOR_HZ"] if down else M.FLOOR), 0) ** (1 / Q["power"])
    top = (M.TOP - M.FLOOR) ** (1 / Q["power"])
    travel = off if down else top - off
    cap = min(Q["limit"], max(travel + (Q["capf"] - 1) * top, 0) / gain)
    level = np.minimum(np.sqrt(M.levels(t["x"], M.hpc(Q["hp1"]), M.hpc(Q["hp2"]), True)), cap)
    env = M2.follow2(level, M.coef(Q["attack"]), M.coef(Q["fast"]), M.coef(r.get("decay", DECAY[s["Decay"]])), Q["share"])
    control = gain * env
    if r["gt"] > 0:
        quick = np.sqrt(M.levels(t["x"], M.hpc(r["hp"]), 0.0, False))
        control = np.minimum(control + r["gt"] * onset(quick, M.coef(r["ta"]), M.coef(r["tr"])), gain * cap)
    f = C["DOWN_FLOOR_HZ"] + np.maximum(0.0, off - control) ** Q["power"] if down else \
        np.minimum(M.TOP, M.FLOOR + (off + control) ** Q["power"])
    return np.exp(np.convolve(np.log(f), M.HANN, "same")) if smooth else f


def errors(t, r):
    """Model re pedal, octaves, wherever the pedal's filter can be read; and which of those are
    while the guitar plays."""
    f = predict(t, r)
    n = min(len(f), len(t["f"]))
    m = (t["clear"] | t["loud"])[:n]
    return np.log2(f[:n] / t["f"][:n])[m], t["loud"][:n][m]


def line(name, t, r):
    e, loud = errors(t, r)
    rms = lambda v: np.sqrt(np.mean(v ** 2)) if len(v) else np.nan
    return f"  {name:26s} playing {e[loud].mean():+.2f} / {rms(e[loud]):.2f}   between hits {e[~loud].mean():+.2f} / {rms(e[~loud]):.2f}"


NAMES = ["hp", "ta", "tr", "gt", "gain", "rest"]
START = dict(hp=16000.0, ta=0.009, tr=0.065, gt=4000.0, gain=196.34, rest=320.0)

if __name__ == "__main__":
    T = tracks()
    stairs = M.stairs()["stairs-sens-0900"]
    jump = FJ.new_jumps()["sens-0900"]
    M.predict = lambda t, r: predict(t, r)  # fit_guitar's stair_errors and fit_jumps's track_errors
    FJ.predict = lambda t, r, smooth=True: predict(t, r, smooth)  # read the model through these

    def own(v):  # the check take: its hand-set gain and rest
        return dict(zip(NAMES, np.array([START[n] for n in NAMES]) * np.exp(v)))

    def shared(r):  # the takes at the header's Sens 9:00 (and the jumps session's)
        return {k: r[k] for k in ("hp", "ta", "tr", "gt")}

    def residuals(v):
        r = own(v)
        e, _ = errors(T[FIT_ON], r)
        return np.concatenate([e / np.sqrt(len(e)), M.stair_errors(stairs, shared(r)) * 0.4,
                               (lambda j: j / np.sqrt(len(j)) * 1.5)(FJ.track_errors(jump, shared(r)))])

    def report(r):
        print(line(FIT_ON, T[FIT_ON], r))
        print("  October's staircase at Sens 9:00, octaves per step: " + " ".join(f"{e:+.2f}" for e in M.stair_errors(stairs, shared(r))))
        j = FJ.track_errors(jump, shared(r))
        print(f"  the jumps session at Sens 9:00: mean {j.mean():+.2f} rms {np.sqrt(np.mean(j ** 2)):.2f}")

    print("as it was (no onset path)")
    report(dict(own(np.zeros(len(NAMES))), gt=0.0))
    fit = scipy.optimize.least_squares(residuals, np.zeros(len(NAMES)), loss="soft_l1", f_scale=0.3, diff_step=0.02)
    r = own(fit.x)
    print("fitted")
    report(r)
    print("  " + ", ".join(f"{k} {v:.5g}" for k, v in r.items()))
    print("the same knobs in the other modes (in no fit; Down's rest its own)")
    for name in ("sharp-m0300-s0900-d0900", "down-m0300-s0900-d0900"):
        rr = dict(r)
        if name.startswith("down"):
            rest = scipy.optimize.least_squares(lambda v: errors(T[name], dict(r, rest=3000.0 * np.exp(v[0])))[0],
                                                [0.0], loss="soft_l1", f_scale=0.3, diff_step=0.02)
            rr["rest"] = 3000.0 * float(np.exp(rest.x[0]))
            none = scipy.optimize.least_squares(lambda v: errors(T[name], dict(r, gt=0.0, rest=3000.0 * np.exp(v[0])))[0],
                                                [0.0], loss="soft_l1", f_scale=0.3, diff_step=0.02)
            print(line(name + " (was)", T[name], dict(r, gt=0.0, rest=3000.0 * float(np.exp(none.x[0])))))
        else:
            print(line(name + " (was)", T[name], dict(r, gt=0.0)))
        print(line(name, T[name], rr))
    print(f"\nconstexpr float ONSET_HP_HZ = {r['hp']:.1f}f;\nconstexpr float ONSET_ATTACK_S = {r['ta']:.5f}f;\n"
          f"constexpr float ONSET_RELEASE_S = {r['tr']:.5f}f;\nconstexpr float ONSET_GAIN = {r['gt']:.1f}f; // at Sens 9:00")
    json.dump(r, open("build-aw3/onset.json", "w"))
