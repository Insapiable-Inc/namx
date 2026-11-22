"""Mode III's Drive fitted (the model in src/dsp/Rt2Drive.h):

  gain in → one-pole low-pass (FC) → y = F(x − c) → the low-pass's inverse → gain out
  F: two parabolas with a continuous slope (drive3_curve.py): a·v² to v1, the slope falling to zero at v3, flat after
  c, the bias: dc/dt = (c_far − c)/tau_r + k·G(x − c);  G odd: sign · clamp(|u| − theta, 0, cap)

  drive3_fit.py [FC]            the bias's law, the curve and Drive 9:00's gains, on the tone ladders and the funk at
                                Drive 7:00 and 9:00 (the noise and the other positions stay out), both sides low-passed at
                                FC so no inverse is needed: build-rt2/drive3/fit-FC.json
  drive3_fit.py gains [FC]      then the gain into and after the curve at each of the five positions, from the
                                both-rotor takes' settled 441 Hz steps: build-rt2/drive3/gains-FC.json

Needs parked.py and drive3_curve.py run first. Units: 1 = the guitar's peak × 10^(−36/20); times in ms, k per ms."""
import json, sys
import numba, numpy as np, scipy.optimize, scipy.signal
from parked import OUT, RATE, S, UNIT, parts
from drive3_curve import curve

NAMES = ["c_far", "tau_r", "k", "theta", "cap", "pre9", "post9", "a", "v1", "v3"]
START = dict(c_far=9.5, tau_r=426.0, k=0.065, theta=4.03, cap=65.0, pre9=18.67, post9=-11.67)
LOW = dict(c_far=3, tau_r=10, k=1e-4, theta=0.0, cap=1, pre9=15, post9=-14, a=0.05, v1=2, v3=15)
HIGH = dict(c_far=400, tau_r=2000, k=50, theta=25, cap=1000, pre9=22, post9=-9, a=0.4, v1=10, v3=40)
SOS = scipy.signal.butter(2, 30, "high", fs=RATE, output="sos")   # the Drive's output is known from 40 Hz up


@numba.njit(cache=True)
def core(x, c0, c_far, leak, k, theta, cap, a, v1, v3, alpha, inverse):
    """x already scaled by the gain in. alpha: the low-pass's coefficient (1: none); inverse: undo it after the curve."""
    y = np.empty_like(x)
    c = c0
    low = 0.0
    before = 0.0
    m = 2.0 * a * v1
    b = m / (2.0 * (v3 - v1))
    top = a * v1 * v1 + m * (v3 - v1) / 2.0
    for i in range(len(x)):
        low += alpha * (x[i] - low)
        u = low - c
        size = abs(u)
        e = min(max(size - theta, 0.0), cap)
        c += (c_far - c) * leak + (k * e if u > 0 else -k * e)
        f = a * size * size if size < v1 else (top - b * (v3 - size) * (v3 - size) if size < v3 else top)
        if u < 0:
            f = -f
        if inverse:
            y[i] = (f - (1.0 - alpha) * before) / alpha
            before = f
        else:
            y[i] = f
    return y


def rest(P):
    """The bias with no signal: the leak and the pull in balance."""
    kt = P["k"] * P["tau_r"]
    return (P["c_far"] + kt * P["theta"]) / (1 + kt)


def run(x, P, pre_db, post_db, rate, alpha, inverse):
    y = core(x * 10 ** (pre_db / 20), rest(P), P["c_far"], 1 - np.exp(-1000.0 / (P["tau_r"] * rate)), P["k"] * 1000.0 / rate,
             P["theta"], P["cap"], P["a"], P["v1"], P["v3"], alpha, inverse)
    return y * 10 ** (post_db / 20)


def drive(x_units, P, pre_db, post_db, fc, oversample=4):
    """The whole stage on a signal in units, as the engine runs it: oversampled, the inverse filter included."""
    rate = RATE * oversample
    x = scipy.signal.resample_poly(x_units, oversample, 1)
    y = run(x, P, pre_db, post_db, rate, 1 - np.exp(-2 * np.pi * fc / rate), True)
    return scipy.signal.resample_poly(y, 1, oversample)[:len(x_units)]


def dataset(fc):
    D = {}
    b, a = scipy.signal.butter(1, fc, "low", fs=RATE)
    for name in ("0700", "0900"):
        meta = json.loads((S / f"m3-drum-drive{name}.json").read_text())
        played, n = np.load(OUT / f"n-{name}.npy")
        played, n = scipy.signal.lfilter(b, a, played), scipy.signal.lfilter(b, a, n)
        unit = 10 ** (meta["ref_dbfs"] / 20) * UNIT
        n = scipy.signal.sosfilt(SOS, n / unit)
        start = meta["signal_start"] + 5 * RATE   # after the sweep: it set the scale
        w = np.zeros(len(n))
        for part, at in parts(meta):               # each 25 ms weighs the same, whatever its level
            if part["type"] == "steps":
                end = at + int(len(part["levels_db"]) * (part["step_seconds"] + part["gap_seconds"]) * RATE)
            elif part["type"] == "file":
                end = at + int(part["seconds"] * RATE)
            else:
                continue
            for i in range(at, end - 1200, 1200):
                w[i:i + 1200] = 1.0 / (np.sqrt(np.mean(n[i:i + 1200] ** 2)) + 0.3)
        D[name] = dict(meta=meta, x=played / unit, n=n, w=w, start=start)
    return D


def simulate(d, P, name):
    pre, post = (P["pre9"], P["post9"]) if name == "0900" else (0.0, 0.0)
    return scipy.signal.sosfilt(SOS, run(d["x"], P, pre, post, RATE, 1.0, False))


def fit(fc):
    D = dataset(fc)
    a, v1, _, v3 = np.load(OUT / f"curve-{int(fc)}.npy")
    P0 = dict(START, a=float(a), v1=float(v1), v3=float(v3))

    def res(v, P):
        Q = dict(P, **dict(zip(NAMES, v)))
        return np.concatenate([((simulate(d, Q, name) - d["n"]) * d["w"])[d["start"]::2] for name, d in D.items()])

    best = None
    for start in ({}, {"theta": 8.0, "k": 0.2}):   # two starting points: they should agree
        P = dict(P0, **start)
        v0 = np.array([P[k] for k in NAMES])
        r = scipy.optimize.least_squares(res, v0, args=(P,), bounds=([LOW[k] for k in NAMES], [HIGH[k] for k in NAMES]),
                                         x_scale=np.maximum(np.abs(v0), 0.5) * 0.3, diff_step=1e-3, max_nfev=200)
        print(f"FC {fc:.0f} from {start or 'the first guess'}: rms error {100 * np.sqrt(np.mean(r.fun ** 2)):.2f} %",
              {k: round(float(v), 4) for k, v in zip(NAMES, r.x)}, flush=True)
        if best is None or r.cost < best.cost:
            best = r
    P = dict(zip(NAMES, map(float, best.x)))
    print(f"the bias at rest: {rest(P):.3f}")
    json.dump(P, open(OUT / f"fit-{int(fc)}.json", "w"), indent=1)


def gains(fc):
    P = json.load(open(OUT / f"fit-{int(fc)}.json"))
    b, a = scipy.signal.butter(1, fc, "low", fs=RATE)
    out = {}
    for name in ("0700", "0900", "1200", "0300", "0500"):
        meta = json.loads((S / f"m3-both-drive{name}.json").read_text())
        played, n = np.load(OUT / f"nb-{name}.npy")
        played, n = scipy.signal.lfilter(b, a, played), scipy.signal.lfilter(b, a, n)
        unit = 10 ** (meta["ref_dbfs"] / 20) * UNIT
        steps = []
        for part, start in parts(meta):
            if part["type"] != "steps" or part["freq"] != 441:
                continue
            period = part["step_seconds"] + part["gap_seconds"]
            for i in range(len(part["levels_db"])):
                s0, s1 = start + int((i * period + 0.35) * RATE), start + int((i * period + 0.55) * RATE)
                steps.append((played[s0:s1:3] / unit, n[s0:s1:3] / unit))

        def res(v):
            pre, post = np.exp(v[0]), np.exp(v[1])
            return np.concatenate([(post * (curve(pre * x - v[2 + 2 * k], P["a"], P["v1"], P["v1"], P["v3"]) - v[3 + 2 * k]) - y) / (np.std(y) + 0.05)
                                   for k, (x, y) in enumerate(steps)])

        r = min((scipy.optimize.least_squares(res, np.concatenate([[np.log(g), np.log(1 / g ** 0.6)], np.zeros(2 * len(steps))])) for g in (1, 8, 30, 60)),
                key=lambda r: r.cost)
        out[name] = (20 * np.log10(np.exp(r.x[0])), 20 * np.log10(np.exp(r.x[1])))
        print(f"Drive {name}: into the curve {out[name][0]:+6.2f} dB, after it {out[name][1]:+6.2f} dB (rms error {100 * np.sqrt(np.mean(r.fun ** 2)):.1f} %)")
    json.dump(out, open(OUT / f"gains-{int(fc)}.json", "w"), indent=1)


if __name__ == "__main__":
    args = sys.argv[1:]
    task = gains if args[:1] == ["gains"] else fit
    task(float(([a for a in args if a != "gains"] or ["5000"])[0]))
