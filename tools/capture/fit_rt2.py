"""The Boss RT-2 Rotary Ensemble: the rotors' linear model, fitted per Mode, and NAM-X's tables
(src/dsp/Rt2RotaryData.h). The study is captures/boss/rt-2/README.md.

Data: the isolated session's takes (2026-11-07-isolated: the follow-up's recorded again clean — its
Drive was at 5:00) at Slow 9:00 (the horn at 0.25665 turns/s, the drum 1.0270 slower), white noise at
−12 dB re the guitar's peak:
  {mode}-horn-noise   Balance 5:00: the horn alone      {mode}-drum-noise   Balance 7:00: the drum alone
for mode m1, m2, m3 (Mode I, II, III).

  fit_rt2.py paths MODE       the paths: each output's direct path (its delay over a turn), the room's
                              reflections (shared by both outputs), the drum's path
                              → captures/boss/rt-2/2026-11-07-isolated/fit/paths-MODE.json
  fit_rt2.py filters MODE     the filters: least squares over the paths' angle components, ridge,
                              low rank → fit/filters-MODE.npz (~25 min a Mode)
  fit_rt2.py tone MODE        the horn's shelf per output (after the fit: what least squares leaves
                              out of its top), on the held-out part → fit/tone-MODE.json
  fit_rt2.py check MODE       the exported model's held-out error per output and band, and its tone
                              (third-octave power re the pedal's)
  fit_rt2.py --write          src/dsp/Rt2RotaryData.h from the fit/ files (and clang-format)

The model, per rotor and output: moving taps, each with a delay curve d(θ) over the rotor's angle θ
(turns): the direct path's a Fourier series fitted to its tracked arrival, a reflection's
T − S·cos 2π(θ − φ), the drum's a V (a source passing close by the microphone). Each tap's read
x(t − d(θ(t))) (4-point Lagrange, as the C++ engine) is split into angle components c(θ − φ): 1,
cos, sin … up to the tap's order; each component goes through its own FIR, the lags a window
around the read (negative lags are read ahead). A fixed FIR on the input takes what doesn't move.
The FIRs come from block-Toeplitz normal equations built from cross-correlations (FFT) over the
first 20 s of the take, solved with a ridge of 1 % of the mean diagonal; the rest of the take
scores them. NAM-X runs the components' filters in low rank (shared basis filters, a gain per
component): direct rank 4, reflections rank 10, the drum full rank.
"""
import argparse, json, pathlib, subprocess, sys

import numpy as np
import scipy.linalg, scipy.ndimage, scipy.optimize, scipy.signal, soundfile

import signals

FS = 48000
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
SESSION = REPOSITORY / "captures/boss/rt-2/2026-11-07-isolated"
FIT = SESSION / "fit"
HEADER = REPOSITORY / "src/dsp/Rt2RotaryData.h"

HORN_HZ = 0.25665                 # Slow 9:00 in this session (the noise takes' periodicity, every Mode)
DRUM_HZ = HORN_HZ / 1.0270        # the drum's measured ratio at slow
TRAIN_S = 20.0
RIDGE = 0.01
RANK = dict(direct=4, reflection=10, drum=7)
# Taps' filter windows: (taps, first lag) at 48 kHz; angle-component orders.
DIRECT = dict(order=3, L=128, lag0=-24)
REFLECTION = dict(order=1, L=48, lag0=-12)
DRUM = dict(order=3, L=384, lag0=-96)
STATIC = (600, 0)
# Scales for NAM-X: the rig (recorded / played through the bypassed pedal), and each rotor's take re
# Balance 12:00 (the both-rotor take is the power sum of the horn's −6.2 dB and the drum's −5.8 dB,
# within 0.3 dB per octave in every Mode).
CHAIN_DB = -8.89
HORN_REF_DB, DRUM_REF_DB = 6.2, 5.8
MODE_DB = {"m1": 0.0, "m2": 0.0, "m3": 0.0}


# --- data -----------------------------------------------------------------------------------------

def load(name):
    meta = json.loads((SESSION / f"{name}.json").read_text())
    data, rate = soundfile.read(SESSION / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(SESSION / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(data) - lat)
    y = data[lat:lat + n]
    s0 = meta["signal_start"]
    return dict(meta=meta, x=played[:n], A=y[:, 0], B=y[:, 2], s0=s0,
                s1=s0 + int(signals.body_seconds(meta["signal"]) * rate))


def angles(tk, f):
    return f * (np.arange(len(tk["x"])) - tk["s0"]) / FS


def split(tk):
    lo, hi = tk["s0"] + 4800, tk["s1"] - 2000
    return lo, lo + int(TRAIN_S * FS), hi


# --- the model -----------------------------------------------------------------------------------

def lagrange4(z, pos):
    n = np.floor(pos).astype(np.int64)
    f = pos - n
    return (-f * (f - 1) * (f - 2) / 6 * z[n - 1] + (f + 1) * (f - 1) * (f - 2) / 2 * z[n]
            - (f + 1) * f * (f - 2) / 2 * z[n + 1] + (f + 1) * f * (f - 1) / 6 * z[n + 2])


def fourier(c, theta):
    out = np.full_like(theta, c[0], dtype=float)
    for m in range(1, (len(c) - 1) // 2 + 1):
        out += c[2 * m - 1] * np.cos(2 * np.pi * m * theta) + c[2 * m] * np.sin(2 * np.pi * m * theta)
    return out


def fourier_fit(theta, y, w, order):
    cols = [np.ones_like(theta)] + [f(2 * np.pi * m * theta) for m in range(1, order + 1) for f in (np.cos, np.sin)]
    A = np.array(cols).T
    sw = np.sqrt(w)
    return np.linalg.lstsq(A * sw[:, None], y * sw, rcond=None)[0]


def curve_ms(tap, theta):
    if "coef" in tap:
        return fourier(np.asarray(tap["coef"]), theta)
    return tap["T"] - tap["S"] * np.cos(2 * np.pi * (theta - tap["phi"]))


def components(order):
    return ["1"] + [f"{f}{m}" for m in range(1, order + 1) for f in "cs"]


def component(name, alpha):
    if name == "1":
        return np.ones_like(alpha)
    m = int(name[1:])
    return np.cos(2 * np.pi * m * alpha) if name[0] == "c" else np.sin(2 * np.pi * m * alpha)


class Model:
    """Taps (dicts: a curve — coef, or T, S, phi —, phi for the components, order, L, lag0) and a static FIR."""

    def __init__(self, taps):
        self.taps = taps
        self.firs = None

    def layout(self):
        return [(t["L"], t["lag0"]) for t in self.taps for _ in components(t["order"])] + [STATIC]

    def margin(self):
        return max(L + abs(a) for L, a in self.layout()) + 8

    def inputs(self, x, theta, rng):
        out = []
        for t in self.taps:
            read = lagrange4(x, rng - curve_ms(t, theta[rng]) * FS / 1000.0)
            alpha = theta[rng] - t["phi"]
            out += [component(c, alpha) * read for c in components(t["order"])]
        return out + [x[rng]]

    def normal_equations(self, x, y, theta, lo, hi):
        lay = self.layout()
        offs = np.cumsum([0] + [L for L, _ in lay])
        n = offs[-1]
        R = np.zeros((n, n)); b = np.zeros(n)
        mg = self.margin()
        rng = np.arange(lo - mg, hi + mg)
        yy = np.zeros(len(rng)); yy[mg:mg + (hi - lo)] = y[lo:hi]
        N = 1 << int(np.ceil(np.log2(2 * len(rng))))
        U = [np.fft.rfft(u, N) for u in self.inputs(x, theta, rng)]
        Y = np.fft.rfft(yy, N)
        for j, (L, a) in enumerate(lay):
            b[offs[j]:offs[j + 1]] = np.fft.irfft(Y * np.conj(U[j]), N)[np.arange(a, a + L) % N]
        for j, (Lj, aj) in enumerate(lay):
            for k in range(j, len(lay)):
                Lk, ak = lay[k]
                c = np.fft.irfft(U[j] * np.conj(U[k]), N)
                block = c[(np.arange(ak, ak + Lk)[None, :] - np.arange(aj, aj + Lj)[:, None]) % N]
                R[offs[j]:offs[j + 1], offs[k]:offs[k + 1]] = block
                if k != j:
                    R[offs[k]:offs[k + 1], offs[j]:offs[j + 1]] = block.T
        return R, b, offs

    def solve(self, R, b, offs, ridge=RIDGE):
        n = R.shape[0]
        sol = scipy.linalg.solve(R + ridge * np.trace(R) / n * np.eye(n), b, assume_a="pos")
        self.firs = [sol[offs[j]:offs[j + 1]] for j in range(len(offs) - 1)]
        return self

    def render(self, x, theta, lo, hi):
        mg = self.margin()
        rng = np.arange(lo - mg, hi + mg)
        y = np.zeros(len(rng))
        for (L, a), u, h in zip(self.layout(), self.inputs(x, theta, rng), self.firs):
            conv = np.convolve(u, h)
            if a >= 0:
                y[a:] += conv[:len(rng) - a]
            else:
                y[:a] += conv[-a:len(rng)]
        return y[mg:mg + (hi - lo)]


def nmse_db(y, yhat, bands=((None, None), (None, 400), (400, 2000), (2000, 6000), (6000, None))):
    out = []
    for lo, hi in bands:
        a, b = y, yhat
        if lo or hi:
            kind = "band" if lo and hi else ("high" if lo else "low")
            edges = [lo / (FS / 2), hi / (FS / 2)] if lo and hi else (lo or hi) / (FS / 2)
            sos = scipy.signal.butter(4, edges, kind, output="sos")
            a, b = scipy.signal.sosfiltfilt(sos, a), scipy.signal.sosfiltfilt(sos, b)
        out.append(10 * np.log10(np.sum((a - b) ** 2) / np.sum(a ** 2)))
    return np.round(np.array(out), 1)


TONE_BANDS = (100, 250, 630, 1000, 1600, 2500, 4000, 6300, 10000)


def tone_db(y, yhat):
    """The model's power re the pedal's per third octave (dB): the tone the fit leaves."""
    f, p = scipy.signal.welch(y, FS, nperseg=8192)
    q = scipy.signal.welch(yhat, FS, nperseg=8192)[1]
    sel = [(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6)) for b in TONE_BANDS]
    return np.round(np.array([10 * np.log10(q[s].mean() / p[s].mean()) for s in sel]), 1)


# --- the paths -------------------------------------------------------------------------------------

def angle_irs(x, y, s0, lo, hi, f, bins, pre=48, post=768):
    """Per angle bin, the response by cross-correlation (white input): lags −pre … post−1."""
    t = np.arange(lo, hi)
    b = ((((t - s0) / FS) * f) % 1.0 * bins).astype(int)
    X = np.lib.stride_tricks.sliding_window_view(x, pre + post)
    H = np.zeros((bins, pre + post))
    for k in range(bins):
        tt = t[b == k]
        H[k] = (y[tt] @ X[tt - post + 1])[::-1] / np.dot(x[tt], x[tt])
    return np.arange(-pre, post) / FS * 1000, H


def envelope(H, lo=None, hi=None):
    sos = (scipy.signal.butter(4, lo / (FS / 2), "high", output="sos") if lo else
           scipy.signal.butter(4, hi / (FS / 2), "low", output="sos"))
    return np.abs(scipy.signal.hilbert(scipy.signal.sosfiltfilt(sos, H, axis=1), axis=1))


def direct_shape(p, theta):
    """The direct path's delay: a point on a circle passing a microphone (a V: pre + the distance, rmin
    nearest at φ, rmax opposite) plus a gentle correction (cos, sin up to the 3rd harmonic)."""
    pre, rmin, rmax, phi = p[:4]
    P, Q = (rmax ** 2 + rmin ** 2) / 2, (rmax ** 2 - rmin ** 2) / 2
    d = pre + np.sqrt(np.maximum(P - Q * np.cos(2 * np.pi * (theta - phi)), 0))
    for m in range(1, 4):
        d = d + p[2 + 2 * m] * np.cos(2 * np.pi * m * theta) + p[3 + 2 * m] * np.sin(2 * np.pi * m * theta)
    return d


def track_direct(env, tau, order=24):
    """The direct path's arrival per bin: the strongest early peak, then re-picked near the curve in
    narrowing windows, each time fitting direct_shape weighted by the peak's power (a free Fourier
    series rang around the V's tip, where the path is strongest, and wandered on the weak far side).
    Returned as a Fourier series of `order` (the C++ table samples it)."""
    bins = env.shape[0]
    theta = (np.arange(bins) + 0.5) / bins
    early = (tau > 0.4) & (tau < 2.8)
    pk = scipy.ndimage.median_filter(np.array([tau[early][np.argmax(env[b, early])] for b in range(bins)]), size=9, mode="wrap")
    w = np.array([env[b, early].max() for b in range(bins)])
    near = theta[np.argmax(w)]
    lo, hi = np.percentile(pk, 5), np.percentile(pk, 95)
    p = np.array([lo - 0.05, 0.05, hi - lo + 0.05, near] + [0.0] * 6)
    pts, ww = pk, w
    for win in (None, 0.25, 0.12, 0.06):
        if win is not None:
            curve = direct_shape(p, theta)
            pts, ww = [], []
            for b in range(bins):
                sel = np.where(np.abs(tau - curve[b]) < win)[0]
                k = sel[np.argmax(env[b, sel])]
                y0, y1, y2 = env[b, k - 1], env[b, k], env[b, k + 1]
                pts.append(tau[k] + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) * (tau[1] - tau[0])); ww.append(y1)
            pts, ww = np.array(pts), np.array(ww)
        sw = ww / ww.max()
        res = lambda q: np.r_[(direct_shape(q, theta) - pts) * sw, 0.3 * q[4:]]   # a light ridge on the correction
        p = scipy.optimize.least_squares(res, p, bounds=([0.0, 0.0, 0.1, -1.0] + [-0.5] * 6, [3.0, 1.0, 3.0, 2.0] + [0.5] * 6)).x
    th = (np.arange(1024) + 0.5) / 1024
    return fourier_fit(th, direct_shape(p, th), np.ones_like(th), order)


def search_ridges(env, tau, count, blank=0.22):
    """Greedy: the cosine curve T − S·cos 2π(θ − φ) with the most envelope along it, blanked, repeated."""
    bins = env.shape[0]
    theta = (np.arange(bins) + 0.5) / bins
    env = env.copy()
    dt = tau[1] - tau[0]
    T = np.arange(0.5, 15.5, 0.005)
    found = []
    for _ in range(count):
        best = (-1, None)
        for S in np.arange(0.45, 0.71, 0.05):
            for ph in np.arange(128) / 128:
                idx = (T[None, :] - S * np.cos(2 * np.pi * (theta[:, None] - ph)) - tau[0]) / dt
                i0 = np.clip(np.floor(idx).astype(int), 0, len(tau) - 2)
                fr = idx - i0
                rows = np.arange(bins)[:, None]
                v = (env[rows, i0] * (1 - fr) + env[rows, i0 + 1] * fr).mean(axis=0)
                k = np.argmax(v)
                if v[k] > best[0]:
                    best = (v[k], (T[k], S, ph))
        score, (t0, S, ph) = best
        found.append(dict(T=t0, S=S, phi=ph, score=score))
        d = t0 - S * np.cos(2 * np.pi * (theta - ph))
        for b in range(bins):
            env[b, np.abs(tau - d[b]) < blank] = 0
    return found


def common_reflections(found, direct_phis, gap=0.35):
    """Both outputs' ridges merged (within 0.35 ms and 0.06 turn), the direct paths' own ringing left out."""
    groups = []
    for p in sorted(found, key=lambda p: p["T"]):
        if p["T"] < 2.6 and any(abs((p["phi"] - d + 0.5) % 1 - 0.5) < 0.08 for d in direct_phis):
            continue
        for g in groups:
            if abs(p["T"] - g[0]["T"]) < gap and abs((p["phi"] - g[0]["phi"] + 0.5) % 1 - 0.5) < 0.06:
                g.append(p)
                break
        else:
            groups.append([p])
    return [dict(T=min(q["T"] for q in g), S=float(np.mean([q["S"] for q in g])), phi=max(g, key=lambda q: q["score"])["phi"])
            for g in groups]


def v_curve(S, phi, pre=2.0, ratio=0.1, n=17):
    """A V-shaped delay (a source passing close to the microphone) as a Fourier series."""
    theta = (np.arange(1024) + 0.5) / 1024
    rmin, rmax = ratio * S / (1 - ratio), S / (1 - ratio)
    P, Q = (rmax ** 2 + rmin ** 2) / 2, (rmax ** 2 - rmin ** 2) / 2
    return fourier_fit(theta, pre + np.sqrt(np.maximum(P - Q * np.cos(2 * np.pi * (theta - phi)), 0)), np.ones_like(theta), n)


def cmd_paths(mode):
    hk = load(f"{mode}-horn-noise")
    lo, cut, hi = split(hk)
    out = dict(direct={}, direct_phi={})
    found = []
    th512 = (np.arange(512) + 0.5) / 512
    for o in "AB":
        tau, H = angle_irs(hk["x"], hk[o], hk["s0"], hk["s0"] + 4800, hk["s1"] - 100, HORN_HZ, 128)
        env = envelope(H, lo=1500)
        c = track_direct(env, tau)
        out["direct"][o] = c.tolist()
        out["direct_phi"][o] = float(th512[np.argmin(fourier(c, th512))])
        dc = fourier(c, (np.arange(128) + 0.5) / 128)
        for b in range(128):
            env[b, np.abs(tau - dc[b]) < 0.2] = 0
            env[b, np.abs(tau - 0.85) < 0.12] = 0   # the horn's fixed path
        found += search_ridges(env, tau, 36)
        print(f"{mode} {o}: direct {fourier(c, th512).min():.2f}…{fourier(c, th512).max():.2f} ms, nearest at θ {out['direct_phi'][o]:.3f}", flush=True)
    out["reflections"] = common_reflections(found, list(out["direct_phi"].values()))
    print(f"{mode}: {len(out['reflections'])} reflections", flush=True)
    # The drum: a V, its phase and swing searched by the held-out error of a drum-only fit.
    dk = load(f"{mode}-drum-noise")
    theta = angles(dk, DRUM_HZ)
    lo, cut, hi = split(dk)
    best = None
    for S in (0.4, 0.6, 0.8, 1.0):
        for phi in np.arange(0, 1, 1 / 16):
            m = Model([dict(coef=v_curve(S, phi), phi=phi, **DRUM)])
            m.solve(*m.normal_equations(dk["x"], dk["A"], theta, lo, cut))
            e = nmse_db(dk["A"][cut:hi], m.render(dk["x"], theta, cut, hi))[0]
            if best is None or e < best[0]:
                best = (e, S, phi)
    out["drum"] = dict(coef=v_curve(best[1], best[2]).tolist(), phi=float(best[2]), swing_ms=best[1])
    print(f"{mode}: drum V, swing {best[1]} ms, nearest at θ {best[2]:.3f}, held-out {best[0]:.1f} dB", flush=True)
    FIT.mkdir(exist_ok=True)
    (FIT / f"paths-{mode}.json").write_text(json.dumps(out, indent=1) + "\n")


def low_rank(firs, rank):
    U, s, Vt = np.linalg.svd(np.array(firs), full_matrices=False)
    return U[:, :rank] * s[:rank], Vt[:rank]


def horn_model(paths, o):
    taps = [dict(coef=paths["direct"][o], phi=paths["direct_phi"][o], **DIRECT)]
    taps += [dict(T=p["T"], S=p["S"], phi=p["phi"], **REFLECTION) for p in paths["reflections"]]
    return Model(taps)


def drum_model(paths):
    return Model([dict(coef=paths["drum"]["coef"], phi=paths["drum"]["phi"], **DRUM)])


def cmd_filters(mode):
    paths = json.loads((FIT / f"paths-{mode}.json").read_text())
    R = len(paths["reflections"])
    out = {}
    hk = load(f"{mode}-horn-noise")
    theta = angles(hk, HORN_HZ)
    lo, cut, hi = split(hk)
    xh = rotor_input(hk["x"], mode, "horn")
    for o in "AB":
        m = horn_model(paths, o)
        m.solve(*m.normal_equations(xh, hk[o], theta, lo, cut))
        out[f"direct_gains_{o}"], out[f"direct_basis_{o}"] = low_rank(m.firs[:7], RANK["direct"])
        out[f"reflection_gains_{o}"], out[f"reflection_basis_{o}"] = low_rank(m.firs[7:7 + 3 * R], RANK["reflection"])
        out[f"horn_static_{o}"] = m.firs[-1]
        print(f"{mode} horn {o}: held-out {nmse_db(hk[o][cut:hi], m.render(xh, theta, cut, hi))}", flush=True)
    dk = load(f"{mode}-drum-noise")
    theta = angles(dk, DRUM_HZ)
    lo, cut, hi = split(dk)
    m = drum_model(paths)
    xd = rotor_input(dk["x"], mode, "drum")
    m.solve(*m.normal_equations(xd, dk["A"], theta, lo, cut), ridge=1e-6)
    out["drum_gains"], out["drum_basis"] = low_rank(m.firs[:7], RANK["drum"])
    out["drum_static"] = m.firs[-1]
    print(f"{mode} drum: held-out {nmse_db(dk['A'][cut:hi], m.render(xd, theta, cut, hi))}", flush=True)
    np.savez_compressed(FIT / f"filters-{mode}.npz", **out)


def exported(mode):
    """The model as NAM-X runs it (low-rank filters expanded), at the takes' scale."""
    paths = json.loads((FIT / f"paths-{mode}.json").read_text())
    f = np.load(FIT / f"filters-{mode}.npz")
    horn = {}
    for o in "AB":
        m = horn_model(paths, o)
        m.firs = (list(f[f"direct_gains_{o}"] @ f[f"direct_basis_{o}"]) + list(f[f"reflection_gains_{o}"] @ f[f"reflection_basis_{o}"])
                  + [f[f"horn_static_{o}"]])
        horn[o] = m
    drum = drum_model(paths)
    drum.firs = list(f["drum_gains"] @ f["drum_basis"]) + [f["drum_static"]]
    return horn, drum


def cmd_check(mode):
    horn, drum = exported(mode)
    for kind, model, f in (("horn", horn, HORN_HZ), ("drum", drum, DRUM_HZ)):
        tk = load(f"{mode}-{kind}-noise")
        theta = angles(tk, f)
        lo, cut, hi = split(tk)
        for o in "AB":
            m = model[o] if kind == "horn" else model
            yhat = m.render(rotor_input(tk["x"], mode, kind), theta, cut, hi)
            tone_path = FIT / f"tone-{mode}.json"
            if kind == "horn" and tone_path.exists():
                yhat = scipy.signal.sosfilt(shelf_sos(json.loads(tone_path.read_text())["horn"][o]), yhat)
            print(f"{mode} {kind} {o}: held-out [all, <400, 400-2k, 2k-6k, >6k] {nmse_db(tk[o][cut:hi], yhat)}")
            print(f"{mode} {kind} {o}: tone re the pedal at {', '.join(map(str, TONE_BANDS))} Hz: {tone_db(tk[o][cut:hi], yhat)}")


# --- the crossover and the horn's shelf ---------------------------------------------------------------
# Each rotor hears the input through the pedal's crossover: the horn has almost nothing under 300 Hz
# (−57 dB at 125 Hz re 1 kHz), the drum almost nothing over 3 kHz. The model puts a crossover ahead
# of each rotor, in the fit and in the engine (its phase in the band is the filters' to absorb): the
# horn's moving paths' short filters leaked −15 to −20 dB of low end without it, the drum's filters
# treble. A 4th-order Butterworth each, the corners fitted once as filters after the unfiltered
# model (the leak brought under the pedal's level, the band kept): the horn's high-pass the mean of
# its two outputs'. After the horn, a high shelf per output for what least squares leaves out of
# its top (fit_rt2.py tone). RBJ designs, as src/dsp/BiquadChain.h.

CROSSOVER = {"m1": (482.6, 2752.5), "m2": (532.5, 2513.1), "m3": (471.6, 2906.4)}   # horn high-pass, drum low-pass (Hz)
BUTTER4_Q = (0.54119610, 1.3065630)
SHELF_Q = 0.7071
TONE_FINE = (80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
             6300, 8000, 10000, 12500)


def rbj(kind, hz, fs, q, gain_db=0.0):
    """One biquad as BiquadChain.h designs it, as an sos row (b0, b1, b2, 1, a1, a2)."""
    w = 2 * np.pi * hz / fs
    c, alpha, a = np.cos(w), np.sin(w) / (2 * q), 10 ** (gain_db / 40)
    if kind == "lp":
        b, d = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif kind == "hp":
        b, d = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    else:  # high shelf
        r = 2 * np.sqrt(a) * alpha
        b = [a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - r)]
        d = [(a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - r]
    return np.r_[b, d] / d[0]


def crossover_sos(mode, kind, fs=FS):
    hz = CROSSOVER[mode][0 if kind == "horn" else 1]
    return np.array([rbj("hp" if kind == "horn" else "lp", hz, fs, q) for q in BUTTER4_Q])


def rotor_input(x, mode, kind):
    """The input as the rotor hears it: through its side of the crossover."""
    return scipy.signal.sosfilt(crossover_sos(mode, kind), x)


def shelf_sos(t, fs=FS):
    return np.array([rbj("shelf", t["shelf_hz"], fs, SHELF_Q, t["shelf_db"])])


def bands_db(y, bands=TONE_FINE):
    f, p = scipy.signal.welch(y, FS, nperseg=8192)
    return np.array([10 * np.log10(p[(f >= b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))].mean() + 1e-30) for b in bands])


def sos_db(sos, bands=TONE_FINE):
    return 20 * np.log10(np.abs(scipy.signal.sosfreqz(sos, worN=np.asarray(bands, float), fs=FS)[1]) + 1e-12)


def cmd_tone(mode):
    """The horn's shelf per output, on the horn take's held-out part, from 630 Hz up (under it the
    drum covers the horn at every Balance but 5:00) → fit/tone-MODE.json."""
    horn, _ = exported(mode)
    out = {"horn": {}}
    tk = load(f"{mode}-horn-noise")
    theta = angles(tk, HORN_HZ)
    lo, cut, hi = split(tk)
    f = np.asarray(TONE_FINE, float)
    x = rotor_input(tk["x"], mode, "horn")
    for o in "AB":
        yhat = horn[o].render(x, theta, cut, hi)
        target = bands_db(tk[o][cut:hi]) - bands_db(yhat)
        def res(p):
            return np.where(f >= 630, sos_db(shelf_sos(dict(shelf_hz=p[0], shelf_db=p[1]))) - target, 0.0)
        p = scipy.optimize.least_squares(res, [2000.0, 2.0], bounds=([400, -6], [8000, 9])).x
        t = dict(shelf_hz=round(float(p[0]), 1), shelf_db=round(float(p[1]), 2))
        out["horn"][o] = t
        after = bands_db(tk[o][cut:hi]) - bands_db(scipy.signal.sosfilt(shelf_sos(t), yhat))
        print(f"{mode} horn {o}: {t}")
        print(f"   model − pedal before {' '.join(f'{v:+.1f}' for v in -target)}")
        print(f"   model − pedal after  {' '.join(f'{-v:+.1f}' for v in after)}", flush=True)
    print(f"   bands {' '.join(map(str, TONE_FINE))}")
    (FIT / f"tone-{mode}.json").write_text(json.dumps(out, indent=1) + "\n")


# --- NAM-X's tables -------------------------------------------------------------------------------

def cmd_write():
    def fmt(values, per_line=8):
        vals = [f"{v:.7g}f" if v != 0 else "0.0f" for v in np.asarray(values, dtype=np.float64).ravel()]
        return ",\n".join(", ".join(vals[i:i + per_line]) for i in range(0, len(vals), per_line))

    def arr(name, values):
        v = np.asarray(values).ravel()
        return f"inline constexpr std::array<float, {len(v)}> {name}{{{{\n{fmt(v)}}}}};\n"

    out = ["#pragma once\n\n", "#include <array>\n\n", '#include "Rt2RotaryTables.h"\n\n', "namespace namx::rt2\n{\n\n",
           "// BEGIN GENERATED (tools/capture/fit_rt2.py --write rewrites this file)\n",
           "// The Boss RT-2's measured paths and filters per Mode (captures/boss/rt-2/README.md), 48 kHz.\n",
           "// clang-format off\n"]
    rows = []
    th = np.arange(256) / 256
    for mode in ("m1", "m2", "m3"):
        paths = json.loads((FIT / f"paths-{mode}.json").read_text())
        f = np.load(FIT / f"filters-{mode}.npz")
        tone = json.loads((FIT / f"tone-{mode}.json").read_text())
        P, R = mode.upper(), len(paths["reflections"])
        g_h = 10 ** ((-CHAIN_DB - HORN_REF_DB + MODE_DB[mode]) / 20)
        g_d = 10 ** ((-CHAIN_DB - DRUM_REF_DB + MODE_DB[mode]) / 20)
        for o in "AB":
            out.append(arr(f"{P}_DIRECT_{o}_MS", fourier(np.asarray(paths["direct"][o]), th)))
        out.append(arr(f"{P}_DRUM_MS", fourier(np.asarray(paths["drum"]["coef"]), th)))
        out.append(f"inline constexpr std::array<Reflection, {R}> {P}_REFLECTIONS{{{{\n" +
                   ",\n".join(f"{{{p['T']:.5f}f, {p['S']:.4f}f, {p['phi']:.5f}f}}" for p in paths["reflections"]) + "}};\n")
        for o in "AB":
            out.append(arr(f"{P}_DIRECT_GAINS_{o}", f[f"direct_gains_{o}"] * g_h))
            out.append(arr(f"{P}_DIRECT_BASIS_{o}", f[f"direct_basis_{o}"]))
            out.append(arr(f"{P}_REFLECTION_GAINS_{o}", f[f"reflection_gains_{o}"] * g_h))
            out.append(arr(f"{P}_REFLECTION_BASIS_{o}", f[f"reflection_basis_{o}"]))
            out.append(arr(f"{P}_HORN_STATIC_{o}", f[f"horn_static_{o}"] * g_h))
        out.append(arr(f"{P}_DRUM_GAINS", f["drum_gains"] * g_d))
        out.append(arr(f"{P}_DRUM_BASIS", f["drum_basis"]))
        out.append(arr(f"{P}_DRUM_STATIC", f["drum_static"] * g_d))
        bank = lambda g, b, r: f"Bank{{{P}_{g}.data(), {P}_{b}.data(), {r}}}"
        rows.append(
            f"    ModeTables{{{{{P}_DIRECT_A_MS.data(), {P}_DIRECT_B_MS.data()}}, "
            f"{{{paths['direct_phi']['A']:.5f}f, {paths['direct_phi']['B']:.5f}f}}, {P}_REFLECTIONS.data(), {P}_REFLECTIONS.size(), "
            f"{P}_DRUM_MS.data(), {paths['drum']['phi']:.5f}f, "
            f"{{{bank('DIRECT_GAINS_A', 'DIRECT_BASIS_A', RANK['direct'])}, {bank('DIRECT_GAINS_B', 'DIRECT_BASIS_B', RANK['direct'])}}}, "
            f"{{{bank('REFLECTION_GAINS_A', 'REFLECTION_BASIS_A', RANK['reflection'])}, "
            f"{bank('REFLECTION_GAINS_B', 'REFLECTION_BASIS_B', RANK['reflection'])}}}, "
            f"{bank('DRUM_GAINS', 'DRUM_BASIS', RANK['drum'])}, "
            f"{{{P}_HORN_STATIC_A.data(), {P}_HORN_STATIC_B.data()}}, {P}_DRUM_STATIC.data(), "
            f"{CROSSOVER[mode][0]}f, {CROSSOVER[mode][1]}f, "
            f"{{{{" + ", ".join(f"{{{tone['horn'][o]['shelf_hz']}f, {tone['horn'][o]['shelf_db']}f}}" for o in "AB") + "}}}")
    out.append("// clang-format on\n\n")
    out.append("inline constexpr std::array<ModeTables, 3> MODES{{\n" + ",\n".join(rows) + "}};\n")
    out.append("// END GENERATED\n\n} // namespace namx::rt2\n")
    HEADER.write_text("".join(out))
    subprocess.run(["clang-format", "-i", str(HEADER)], check=True)
    print(f"wrote {HEADER.relative_to(REPOSITORY)}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("command", nargs="?", choices=("paths", "filters", "tone", "check"))
    ap.add_argument("mode", nargs="?", choices=("m1", "m2", "m3"))
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    if args.write:
        cmd_write()
    elif args.command and args.mode:
        {"paths": cmd_paths, "filters": cmd_filters, "tone": cmd_tone, "check": cmd_check}[args.command](args.mode)
    else:
        ap.print_help()
