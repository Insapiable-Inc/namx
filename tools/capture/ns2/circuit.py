"""The NS-2's control circuit as a model, fitted to the gain read directly (focused.py).

From the schematic (the README's link), in volts above the 4.5 V reference:

  u   INPUT → two high-passes → the Threshold amplifier (gain A, its bandwidth falling as A rises)
  w   |u|, clamped (the amplifier's swing), smoothed by 0.1 ms: the rectifier's output
  v   the hold capacitor C19 (4.7 µF). Charged through a diode from w − a diode drop (fast: an op-amp drives it);
      discharged through the Decay pot back into the rectifier's output, so only while v > w
  g   the VCA is linear in its control voltage (its datasheet): g = v / SPAN, clamped to FLOOR … 1 (the 5.6 V clamp)

A diode is i = IS · (exp(V / ETA) − 1). The Threshold knob is A (and its bandwidth), Decay the pot's resistance.
Tried and left out (2026-11-21; each fitted to no effect): a diode's drop between the capacitor and the control, one
between the pot and the rectifier, a soft ceiling in the rectifier, a discharge that conducts less as it empties. The
pedal's release slows as it closes and holds longer while a signal carries on; this model does neither.

  circuit.py fit      fits the tones takes (a few minutes); writes build-ns2/focused/circuit.json
  circuit.py score    the fit and NAM-X's law before it (refit.baseline) on every take, the guitar takes (in no fit)
                      included
  circuit.py engine   NAM-X's C++ (build-ns2/render_ns2_gain) against the fitted model and the pedal
  circuit.py plot [TAKE…]   the pedal, the fit and the law before per take part (build-ns2/focused/fit-*.png)

Ns2Gate.h holds the fit's constants (the logs of A, the bandwidths and the pot; CHARGE_DROP = ETA · ln(1 + I10 / IS)).
A rerun lands on the same pot, high-pass and bandwidths; the voltages' scale is loose (A, the ceiling and SPAN move
together by a few per cent at the same error) and so is the bandwidth at 5:00 (over 20 kHz, the takes stop at 6).
Run from the repository root."""
import json, pathlib, sys
from concurrent.futures import ThreadPoolExecutor
import numpy as np, numba, scipy.signal, scipy.optimize

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused as F

RATE = F.RATE
C19 = 4.7e-6
I10 = 85e-6          # the current in the diode ahead of the capacitor's driver (4 V over 47 kΩ)
IMAX = 0.02          # the most the capacitor's driver gives, amperes (the pedal opens within 0.5 ms)
FLOOR = 0.012        # closed: −38.4 dB
PARAMS = F.OUT / "circuit.json"
# Where the fit starts: the schematic's values where it gives them, round numbers read off the takes elsewhere.
P0 = dict(a_1200=5000.0, a_0300=750.0, a_0500=80.0, fc_1200=900.0, fc_0300=5000.0, fc_0500=20000.0, hp=72.0, wmax=2.5, eta=0.08,
          ln_is=np.log(1e-7), r_0700=4700.0, r_1200=40000.0, r_0500=180000.0, span=0.85, noise_db=-106.0)
LOW = dict(a_1200=500, a_0300=80, a_0500=6, fc_1200=200, fc_0300=500, fc_0500=1000, hp=20, wmax=1.2, eta=0.015, ln_is=np.log(1e-11),
           r_0700=500, r_1200=5000, r_0500=30000, span=0.2, noise_db=-120)
HIGH = dict(a_1200=50000, a_0300=8000, a_0500=600, fc_1200=20000, fc_0300=40000, fc_0500=60000, hp=200, wmax=20.0, eta=0.1,
            ln_is=np.log(1e-4), r_0700=12000, r_1200=150000, r_0500=600000, span=3.0, noise_db=-70)
FREE = list(P0)
LOGGED = {"a_1200", "a_0300", "a_0500", "fc_1200", "fc_0300", "fc_0500", "hp", "r_0700", "r_1200", "r_0500"}


@numba.njit(cache=True, nogil=True)
def core(u, noise, wmax, eta, i_s, r, span, smooth, rate):
    """u: the Threshold amplifier's output, volts. Returns the gain per ms. Ns2Gate.h's Control::process, sample
    for sample."""
    dt = 1.0 / rate
    hold = np.exp(-dt * i_s / (C19 * eta))
    ramp = IMAX * dt / C19
    d10 = eta * np.log(1.0 + I10 / i_s)
    out = np.empty(len(u) // 48)
    w = 0.0
    v = 0.0
    acc = 0.0
    for n in range(len(out) * 48):
        w += smooth * (min(np.sqrt(u[n] * u[n] + noise * noise), wmax) - w)
        p = w - d10
        if p > v:
            # The diode into the capacitor, solved over the sample: dv/dt = IS/C · (exp((p − v)/ETA) − 1).
            z = 1.0 - (1.0 - np.exp((v - p) / eta)) * hold
            v = min(p + eta * np.log(z), v + ramp)
        if v > w:
            v -= (v - w) / r * dt / C19
        acc += min(max(v / span, FLOOR), 1.0)
        if n % 48 == 47:
            out[n // 48] = acc / 48.0
            acc = 0.0
    return out


def front(x, a, fc, hp):
    """INPUT to the Threshold amplifier's output: two first-order high-passes, the gain, a one-pole low-pass."""
    b, a_ = scipy.signal.butter(1, hp, "highpass", fs=RATE)
    y = scipy.signal.lfilter(b, a_, scipy.signal.lfilter(b, a_, x))
    pole = np.exp(-2 * np.pi * min(fc, 0.45 * RATE) / RATE)
    return a * scipy.signal.lfilter([1 - pole], [1, -pole], y)


def settings(name):
    t, d = name.split("-")[1], name.split("-")[3]
    return t, d


def simulate(x, P, threshold, decay):
    u = front(x, P[f"a_{threshold}"], P[f"fc_{threshold}"], P["hp"])
    smooth = 1 - np.exp(-2 * np.pi * 1592.0 / RATE)        # 100 kΩ · 1 nF across the rectifier
    return core(u, P[f"a_{threshold}"] * 10 ** (P["noise_db"] / 20), P["wmax"], P["eta"], np.exp(P["ln_is"]), P[f"r_{decay}"],
                P["span"], smooth, float(RATE))


def data(kind):
    out = []
    for name in F.takes():
        if not name.endswith(kind):
            continue
        d = np.load(F.OUT / f"{name}.npz")
        start = (int(d["start"]) - RATE // 2) // 48 * 48   # half a second of silence first
        x = d["played"][start:]
        g = d["gain"][start // 48:]
        n = min(len(x) // 48, len(g)) - 250
        out.append(dict(name=name, x=np.ascontiguousarray(x[:n * 48]), g=np.clip(g[:n], 0, 1), th=settings(name)))
    return out


def measure(g):
    """The scale errors are counted on: dB, eased at the bottom (−34 dB and under count little)."""
    return 20 * np.log10(g + 0.02)


POOL = ThreadPoolExecutor(12)


def run(P, takes):
    return list(POOL.map(lambda t: simulate(t["x"], P, *t["th"]), takes))


def unpack(q, P):
    Q = dict(P)
    for k, v in zip(FREE, q):
        Q[k] = float(np.exp(v)) if k in LOGGED else float(v)
    return Q


def pack(P, table=None):
    table = table or P
    return np.array([np.log(table[k]) if k in LOGGED else table[k] for k in FREE])


def fit():
    takes = data("tones")

    def residuals(q):
        return np.concatenate([(measure(s) - measure(t["g"]))[400:] for s, t in zip(run(unpack(q, P0), takes), takes)])

    q0 = pack(P0)
    r = scipy.optimize.least_squares(residuals, q0, bounds=(pack(P0, LOW), pack(P0, HIGH)), x_scale=np.maximum(np.abs(q0) * 0.05, 0.02),
                                     diff_step=1e-3, max_nfev=120)
    Q = unpack(r.x, P0)
    print(f"rms {np.sqrt(np.mean(r.fun ** 2)):.3f} dB", {k: round(Q[k], 4) for k in FREE}, flush=True)
    i_s = np.exp(Q["ln_is"])
    print("for Ns2Gate.h: LOG_GAIN", [round(np.log(Q[f"a_{t}"]), 4) for t in ("1200", "0300", "0500")], "LOG_BAND",
          [round(np.log(Q[f"fc_{t}"]), 4) for t in ("1200", "0300", "0500")], "LOG_OHMS (7:00, 12:00, 5:00)",
          [round(np.log(Q[f"r_{d}"]), 4) for d in ("0700", "1200", "0500")], f"CHARGE_IS {i_s:.4e} CHARGE_DROP {Q['eta'] * np.log(1 + I10 / i_s):.4f}",
          f"NOISE {10 ** (Q['noise_db'] / 20):.3e}")
    PARAMS.write_text(json.dumps(Q, indent=1))


def present(take):
    """NAM-X's present law (Ns2Gate.h, refit.baseline) on a take: the gain per ms, open = 1."""
    import refit as R
    import dataset as D
    levels = D.detector(take["x"])
    return 10 ** (R.baseline(levels, R.THRESHOLD_DB[take["th"][0]], R.DECAY_S[take["th"][1]], 0.0, R.BASELINE_P) / 20)


def engine(take):
    """NAM-X's control (ns2::Control through build-ns2/render_ns2_gain) on a take: the gain per ms."""
    import subprocess
    import refit as R
    opens_at = R.THRESHOLD_DB[take["th"][0]] + 1.29 - 6.0
    x = (take["x"] * 10 ** (-6.0 / 20)).astype(np.float32)
    y = subprocess.run(["build-ns2/render_ns2_gain", f"{opens_at}", f"{R.DECAY_S[take['th'][1]] * 1000}"], input=x.tobytes(),
                       capture_output=True, check=True).stdout
    g = np.frombuffer(y, dtype=np.float32).astype(np.float64)
    return g[:len(g) // 48 * 48].reshape(-1, 48).mean(axis=1)


def check_engine(path=PARAMS):
    P = json.loads(pathlib.Path(path).read_text())
    print(f"{'take':36s}  C++ vs the fitted model (rms, dB)   C++ vs the pedal   NAM-X before vs the pedal")
    for kind in ("tones", "guitar"):
        takes = data(kind)
        rows = []
        for t, s in zip(takes, run(P, takes)):
            e = engine(t)
            rows.append((rms(e, s), rms(e, t["g"]), rms(present(t), t["g"])))
            print(f"{t['name']:36s}  {rows[-1][0]:5.3f}   {rows[-1][1]:5.2f}   {rows[-1][2]:5.2f}")
        print(f"{kind + ', mean':36s}  {np.mean([r[0] for r in rows]):5.3f}   {np.mean([r[1] for r in rows]):5.2f}   {np.mean([r[2] for r in rows]):5.2f}")


def rms(a, b):
    n = min(len(a), len(b))
    return float(np.sqrt(np.mean((measure(a[:n]) - measure(b[:n]))[400:] ** 2)))


def score(path=PARAMS):
    P = json.loads(pathlib.Path(path).read_text())
    print(f"{'take':36s}  fitted  present   (rms error of the gain, dB; the scale eased under -34 dB)")
    for kind in ("tones", "guitar"):
        takes = data(kind)
        both = []
        for t, s in zip(takes, run(P, takes)):
            both.append((rms(s, t["g"]), rms(present(t), t["g"])))
            print(f"{t['name']:36s}  {both[-1][0]:5.2f}   {both[-1][1]:5.2f}")
        print(f"{kind + ', mean':36s}  {np.mean([b[0] for b in both]):5.2f}   {np.mean([b[1] for b in both]):5.2f}")


def plot(path=PARAMS, names=()):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import signals
    P = json.loads(pathlib.Path(path).read_text())
    for kind in ("tones", "guitar"):
        takes = [t for t in data(kind) if not names or t["name"] in names]
        for t, s in zip(takes, run(P, takes)):
            meta = json.loads((F.S / f"{t['name']}.json").read_text())
            first = (int(np.load(F.OUT / f"{t['name']}.npz")["start"]) - RATE // 2) // 48
            old = present(t)
            parts = F.parts(meta)
            groups = [parts[i:i + 6] for i in range(0, len(parts), 6)]
            for gi, group in enumerate(groups):
                fig, axes = plt.subplots(len(group), 1, figsize=(15, 3.1 * len(group)), squeeze=False)
                for ax, (part, start) in zip(axes[:, 0], group):
                    seconds = signals.body_seconds(part) + part.get("tail", 1)
                    a = max(start // 48 - first - 100, 0)
                    b = min(a + int(seconds * 1000) + 200, len(s), len(t["g"]))
                    sec = (np.arange(a, b) + first - start / 48) / 1000
                    ax.plot(sec, 20 * np.log10(np.maximum(old[a:b], 1e-3)), "C0", lw=0.7, label="NAM-X now")
                    ax.plot(sec, 20 * np.log10(np.maximum(t["g"][a:b], 1e-3)), "k", lw=1.3, label="pedal")
                    ax.plot(sec, 20 * np.log10(np.maximum(s[a:b], 1e-3)), "C3", lw=0.9, label="circuit model")
                    ax.set_ylim(-45, 3); ax.grid(alpha=0.3); ax.set_ylabel("gain, dB")
                    ax.set_title(signals.describe(part)[:150], fontsize=8)
                axes[0, 0].legend(loc="lower right", ncol=3)
                fig.suptitle(f"{t['name']} ({gi + 1}/{len(groups)})"); fig.tight_layout()
                fig.savefig(F.OUT / f"fit-{t['name']}-{gi + 1}.png", dpi=70); plt.close(fig)


if __name__ == "__main__":
    command = sys.argv[1]
    if command == "fit":
        fit()
    elif command == "score":
        score(*sys.argv[2:3])
    elif command == "engine":
        check_engine()
    else:
        plot(names=sys.argv[2:])
