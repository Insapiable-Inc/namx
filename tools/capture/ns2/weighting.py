"""The detector's attack (rectify, then a one-pole: attack ATTACK_MS, release 50 ms; 0 = NAM-X's peak follower)
and its frequency weighting: a Butterworth high-pass before NAM-X's peak follower, unity at 1 kHz
(the tone the Threshold map was measured with). Reamp levels per corner are precomputed into
build-ns2/reamp-levels-<order>-<corner>.npy; the older and history takes are pure tones, so for them the
weighting is a constant offset (offset_db)."""
import pathlib, sys
import numpy as np, scipy.signal, soundfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import dataset as D

GRID = [(1, 100), (1, 150), (1, 200), (1, 300), (1, 400), (1, 600), (2, 150), (2, 250), (2, 400), (2, 600)]


def sos(order, corner):
    return scipy.signal.butter(order, corner, "highpass", fs=D.RATE, output="sos")


def offset_db(order, corner, freq):
    if corner <= 0:
        return 0.0
    _, h = scipy.signal.sosfreqz(sos(order, corner), worN=[freq, 1000], fs=D.RATE)
    return float(20 * np.log10(abs(h[0]) / abs(h[1])))


def path(order, corner):
    return D.ROOT / f"build-ns2/reamp-levels-{order}-{corner}.npy"


def build():
    import json
    z = np.load(D.CACHE)
    for order, corner in GRID:
        if path(order, corner).exists():
            continue
        out = {}
        for name in z["names"]:
            meta = json.loads((D.REAMP / f"{name}.json").read_text())
            played, _ = soundfile.read(D.REAMP / "played" / f"{name}.wav", dtype="float64")
            x = scipy.signal.sosfilt(sos(order, corner), played / 10 ** (meta["ref_dbfs"] / 20)) / 10 ** (
                offset_db(order, corner, 1000) / 20)
            _, h = scipy.signal.sosfreqz(sos(order, corner), worN=[1000], fs=D.RATE)
            out[str(name)] = D.detector(x / abs(h[0]))[:len(z[f"{name}.levels"])]
        np.save(path(order, corner), out, allow_pickle=True)
        print("built", order, corner, flush=True)


def reamp_levels(order, corner):
    if corner <= 0:
        z = np.load(D.CACHE)
        return {str(n): z[f"{n}.levels"] for n in z["names"]}
    return np.load(path(order, corner), allow_pickle=True).item()




# ---- the detector's attack: an averaging rectifier instead of the instant peak --------------------------
ATTACKS_MS = [0.25, 0.5, 1.0, 2.0, 4.0]


def averaging(x, attack_ms, release_ms=50.0):
    import numba

    @numba.njit(cache=False)
    def follow(x, a, r):
        y = 0.0
        out = np.empty(len(x))
        for i in range(len(x)):
            v = abs(x[i])
            y += (v - y) * (a if v > y else r)
            out[i] = 20 * np.log10(y) if y > 1e-12 else -140.0
        return out

    a = -np.expm1(-1 / (D.RATE * attack_ms * 1e-3))
    r = -np.expm1(-1 / (D.RATE * release_ms * 1e-3))
    levels = follow(np.ascontiguousarray(x, dtype=np.float64), a, r)
    n = len(levels) // D.MS
    return levels[:n * D.MS].reshape(n, D.MS).mean(axis=1)


def tone_offset_db(attack_ms, freq):
    """What the averaging detector reads for a sine at freq, re its amplitude (the peak follower's reading)."""
    t = np.arange(D.RATE * 2) / D.RATE
    s = 0.1 * np.sin(2 * np.pi * freq * t)
    return float(averaging(s, attack_ms)[-1000:].mean() - 20 * np.log10(0.1))


def attack_path(attack_ms):
    return D.ROOT / f"build-ns2/reamp-levels-attack-{attack_ms}.npy"


def build_attacks():
    import json
    z = np.load(D.CACHE)
    for attack in ATTACKS_MS:
        if attack_path(attack).exists():
            continue
        cal = tone_offset_db(attack, 1000)
        out = {}
        for name in z["names"]:
            meta = json.loads((D.REAMP / f"{name}.json").read_text())
            played, _ = soundfile.read(D.REAMP / "played" / f"{name}.wav", dtype="float64")
            out[str(name)] = averaging(played / 10 ** (meta["ref_dbfs"] / 20), attack)[:len(z[f"{name}.levels"])] - cal
        np.save(attack_path(attack), out, allow_pickle=True)
        print("built attack", attack, flush=True)


if __name__ == "__main__":
    build()
    build_attacks()
