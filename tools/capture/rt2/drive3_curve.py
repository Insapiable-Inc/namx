"""Mode III Drive's curve from the settled 441 Hz steps (parked.py's Drive outputs at 7:00 and 9:00).

Every settled step is the same odd curve F read at its own bias c (output = F(input − c), minus an offset: the
output is AC-coupled). F in four pieces with a continuous slope: a·v² to v1, a straight part to v2, a knee whose
slope falls linearly to zero at v3, flat after — the fit closes the straight part (v2 = v1): two parabolas.
Both sides go through a one-pole low-pass at FC first (the filter the pedal has ahead of the curve, its inverse after).

  drive3_curve.py [FC]     FC in Hz, default 5000 (0: no filter); writes build-rt2/drive3/curve-FC.npy

Units: 1 = the guitar's peak × 10^(−36/20)."""
import json, sys
import numpy as np, scipy.optimize, scipy.signal
from parked import OUT, RATE, S, UNIT, parts

PRE9, POST9 = 18.64, -11.39   # Drive 9:00 re 7:00, dB: first estimates; drive3_fit.py refits them


def curve(v, a, v1, v2, v3):
    x = np.abs(v)
    m = 2 * a * v1
    y = np.where(x < v1, a * x * x, a * v1 * v1 + m * (x - v1))
    k = np.clip(x - v2, 0, v3 - v2)
    y = y - np.where(x > v2, m * (x - v2) - (m * k - m * k * k / (2 * (v3 - v2))), 0)
    return np.sign(v) * y


def main():
    fc = float(sys.argv[1]) if len(sys.argv) > 1 else 5000.0
    pts = {}
    for drive, pre, post in (("0700", 0.0, 0.0), ("0900", PRE9, POST9)):
        meta = json.loads((S / f"m3-drum-drive{drive}.json").read_text())
        played, n = np.load(OUT / f"n-{drive}.npy")
        if fc:
            b, a = scipy.signal.butter(1, fc, "low", fs=RATE)
            played, n = scipy.signal.lfilter(b, a, played), scipy.signal.lfilter(b, a, n)
        unit = 10 ** (meta["ref_dbfs"] / 20) * UNIT
        for part, start in parts(meta):
            if part["type"] != "steps" or part["freq"] != 441:
                continue
            period = part["step_seconds"] + part["gap_seconds"]
            for i, level in enumerate(part["levels_db"]):
                s0, s1 = start + int((i * period + 0.40) * RATE), start + int((i * period + 0.56) * RATE)
                pts[drive, level] = (10 ** (pre / 20) * played[s0:s1:2] / unit, n[s0:s1:2] / unit / 10 ** (post / 20))
    keys = [("0700", lv) for lv in (-18, -12, -6, 0)] + [("0900", lv) for lv in (-36, -30, -24)] + [("0700", lv) for lv in (-36, -30, -24)]

    def res(p):
        out = []
        for k, key in enumerate(keys):
            x, y = pts[key]
            out.append((curve(x - p[4 + 2 * k], *p[:4]) - p[5 + 2 * k] - y) / (np.std(y) + 0.05))
        return np.concatenate(out)

    p0 = np.concatenate([[0.148, 3.9, 9.0, 22.0], np.zeros(14), [3.7, -2.0, 3.0, -1.5, 2.0, -1.4]])
    r = scipy.optimize.least_squares(res, p0)
    a, v1, v2, v3 = r.x[:4]
    print(f"FC {fc:.0f} Hz: a {a:.4f}, v1 {v1:.3f}, v2 {v2:.3f}, v3 {v3:.3f} (slope {2 * a * v1:.3f}, ceiling {curve(np.array([99.0]), a, v1, v2, v3)[0]:.3f}); "
          f"rms error {100 * np.sqrt(np.mean(r.fun ** 2)):.2f} % of each step's level")
    i = 0
    for k, key in enumerate(keys):
        e = r.fun[i:i + len(pts[key][0])]
        i += len(e)
        print(f"   Drive {key[0]} {key[1]:+4d} dB: bias {r.x[4 + 2 * k]:+6.2f}, error {100 * np.sqrt(np.mean(e ** 2)):.1f} %")
    np.save(OUT / f"curve-{int(fc)}.npy", r.x[:4])


if __name__ == "__main__":
    main()
