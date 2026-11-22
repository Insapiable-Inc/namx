"""Every NS-2 recording a control-law fit can be scored on, in one form: the detector's level every 1 ms
(NAM-X's 50 ms peak follower on the pedal's input, dB re the guitar's peak, averaged per ms) and what the
pedal's gain did.

  older   the 48 takes of 2026-10-27 and -dynamics (staircases, jumps, plucks, phrases, soft picking): the
          gain at the test tone every 1 ms, with the research's validity mask
          (tools/research/ns2_control_regression_cache.npz, validate_ns2_control_candidates.py)
  history the 2026-10-30 probes at Threshold 3:00 (−56/−54/−44 dB after quiet and after loud, both Decay
          extremes): the gain every 1 ms over each 8 s probe (tools/research/fit_ns2_control_candidates.py)
  reamp   the guitar DI dataset through the pedal (2026-11-08-reamp, 18 playing takes): per ms the power
          the open path would give (the bypass take's chain on the played signal) and the pedal's, both
          high-passed at 60 Hz, forward only (each switch steps the pedal's DC level), and the rig's floor —
          scored in 20 ms windows with the floor added to the prediction (refit.reamp_windows)

  dataset.py   builds build-ns2/reamp-dataset.npz (the older and history sets are read where they are)
Run from the repository root."""
import json, pathlib, sys
import numpy as np, scipy.signal, soundfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
RESEARCH = ROOT / "tools/research"
REAMP = ROOT / "captures/boss/ns-2/2026-11-08-reamp"
CACHE = ROOT / "build-ns2/reamp-dataset.npz"
RATE = 48000
MS = 48
CLOCKS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
# Forward only (a two-way filter carries each switch's DC step back before the switch), 60 Hz: the step's
# swing leaves less above it. The same filter on both sides of every comparison, so the gain is unchanged.
HIGH_PASS = scipy.signal.butter(4, 60, "highpass", fs=RATE, output="sos")


def tag(clock):
    return clock.replace(":", "").zfill(4)


def detector(x):
    """NAM-X's input level: a peak follower falling in 50 ms, dB re the guitar's peak, averaged per ms."""
    import numba

    @numba.njit(cache=False)
    def follow(x, fall):
        out = np.empty(len(x))
        peak = 0.0
        for i in range(len(x)):
            peak = max(abs(x[i]), peak * fall)
            out[i] = 20 * np.log10(peak) if peak > 1e-7 * 10 ** (6 / 20) else -140.0
        return out

    levels = follow(x, np.exp(-1 / (RATE * 0.05)))
    n = len(levels) // MS
    return levels[:n * MS].reshape(n, MS).mean(axis=1)


def per_ms(power):
    n = len(power) // MS
    return power[:n * MS].reshape(n, MS).mean(axis=1)


def build_reamp():
    def load(name):
        meta = json.loads((REAMP / f"{name}.json").read_text())
        recorded, _ = soundfile.read(REAMP / f"{name}.wav", dtype="float64")
        played, _ = soundfile.read(REAMP / "played" / f"{name}.wav", dtype="float64")
        lat = meta["latency_samples"]
        n = min(len(played), len(recorded) - lat)
        pedal = scipy.signal.sosfilt(HIGH_PASS, recorded[:, 0])
        # The floor: the stretch before the marker, through the same filter once it has settled (100 ms).
        return meta, pedal[lat:lat + n], played[:n], pedal[RATE // 10:meta["marker_start"]], recorded[lat:lat + n, 0]

    # The chain from the unfiltered bypass recording: the high-pass is applied once, to every side, afterwards.
    _, _, played, _, raw = load("bypass-dynamics")
    pedal = raw - raw.mean()
    _, pxy = scipy.signal.csd(played, pedal, RATE, nperseg=8192)
    _, pxx = scipy.signal.welch(played, RATE, nperseg=8192)
    h = np.roll(np.fft.irfft(pxy / np.maximum(pxx, 1e-30 * pxx.max())), 256)[:512] * np.hanning(512)
    out = {}
    names = []
    for path in sorted(REAMP.glob("threshold-*.json"), key=lambda p: json.loads(p.read_text())["recorded_at"]):
        name = path.stem
        if name.endswith("-noise"):
            continue
        meta, pedal, played, pre, _ = load(name)
        open_path = scipy.signal.sosfilt(HIGH_PASS, np.convolve(played, h)[256:256 + len(played)])
        s = meta["settings"]
        out[f"{name}.levels"] = detector(played / 10 ** (meta["ref_dbfs"] / 20))
        out[f"{name}.open"] = per_ms(open_path ** 2)
        out[f"{name}.pedal"] = per_ms(pedal ** 2)
        out[f"{name}.floor"] = np.mean(pre ** 2)
        out[f"{name}.start"] = meta["signal_start"] // MS
        out[f"{name}.knobs"] = np.array([CLOCKS.index(s["Threshold"]), CLOCKS.index(s["Decay"])])
        names.append(name)
        print(name, flush=True)
    out["names"] = np.array(names)
    CACHE.parent.mkdir(exist_ok=True)
    np.savez_compressed(CACHE, **out)


def older():
    z = np.load(RESEARCH / "ns2_control_regression_cache.npz", allow_pickle=True)
    meta = json.loads(str(z["metadata_json"]))
    return [{"name": m["name"], "threshold": m["threshold_tag"], "decay": m["tag"], "gain0": m["gain0"], "freq": m["freq"],
             "levels": z[f"{i}_levels"], "target": z[f"{i}_target"], "valid": z[f"{i}_valid"]}
            for i, m in enumerate(meta)]


def history():
    sys.path.insert(0, str(RESEARCH))
    from fit_ns2_control_candidates import load_history
    takes, _ = load_history(ROOT)
    out = []
    for t in takes:
        valid = np.zeros(len(t["levels"]), bool)
        for g in t["groups"]:
            valid[g["indices"]] = True
        out.append({"name": "history/" + t["name"], "threshold": "0300", "decay": t["tag"], "gain0": t["gain0"], "freq": 1000,
                    "levels": t["levels"], "target": t["target"], "valid": valid, "groups": t["groups"]})
    return out


def reamp():
    z = np.load(CACHE)
    return [{"name": "reamp/" + n, "threshold": tag(CLOCKS[z[f"{n}.knobs"][0]]),
             "decay": tag(CLOCKS[z[f"{n}.knobs"][1]]), "levels": z[f"{n}.levels"], "open": z[f"{n}.open"],
             "pedal": z[f"{n}.pedal"], "floor": float(z[f"{n}.floor"]), "start": int(z[f"{n}.start"])}
            for n in z["names"]]


if __name__ == "__main__":
    build_reamp()
