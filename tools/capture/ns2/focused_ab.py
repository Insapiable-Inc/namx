"""The NS-2's guitar takes of 2026-11-21 recorded the plain way (INPUT to OUTPUT, no probe: the session's first run,
captures/boss/ns-2/2026-11-21-focused-plain) against NAM-X's Noise Gate before and after the circuit model:
both processors rendered from each take's played signal at the take's settings (build-ns2/render_ns2.before, the
build of the model before; build-ns2/render_ns2, the present one), level-matched to the pedal where it is open, and

  the gain over time (output re input, 20 ms windows, a 60 Hz high-pass on every side, the rig's floor added to
  the models) compared with the pedal's where the open path would be 12 dB over that floor, less the windows where
  the pedal is over the open path (its gate's thumps as it switches: not a gain)
  SESSION/ab/<take>.pedal.wav, .before.wav, .after.wav for listening, one gain for the three

Run from the repository root."""
import json, pathlib, re, subprocess
import numpy as np, scipy.signal, soundfile

S = pathlib.Path("captures/boss/ns-2/2026-11-21-focused-plain")
RATE, W = 48000, 960
HIGH_PASS = scipy.signal.butter(4, 60, "highpass", fs=RATE, output="sos")
HEADER = pathlib.Path("src/dsp/Ns2Gate.h").read_text()
CLOCKS = ["7:00", "9:00", "12:00", "3:00", "5:00"]


def table(name):
    return [float(v.rstrip("f")) for v in re.search(name + r"\{([^}]*)\}", HEADER).group(1).split(",")]


THRESHOLD_DB, DECAY_MS = dict(zip(CLOCKS, table("THRESHOLD_DB"))), dict(zip(CLOCKS, table("DECAY_MS")))
OPEN_DB = float(re.search(r"OPEN_DB = ([-\d.]+)f", HEADER).group(1))


def power(x):
    n = len(x) // W
    return np.mean(x[:n * W].reshape(n, W) ** 2, axis=1)


def main():
    (S / "ab").mkdir(exist_ok=True)
    print(f"{'take':36s} windows   gain error re the pedal, dB rms (worst 5 %):  before          after")
    for path in sorted(S.glob("threshold-*-guitar.json")):
        name = path.stem
        meta = json.loads(path.read_text())
        rec = soundfile.read(S / f"{name}.wav", dtype="float64")[0]
        rec = rec if rec.ndim == 1 else rec[:, 0]
        played = soundfile.read(S / "played" / f"{name}.wav", dtype="float64")[0]
        played = played if played.ndim == 1 else played[:, 0]
        lat, a = meta["latency_samples"], meta["signal_start"]
        n = min(len(played), len(rec) - lat)
        raw, played = rec[lat:lat + n], played[:n]
        pedal = scipy.signal.sosfilt(HIGH_PASS, raw)
        floor = np.mean(pedal[RATE // 10:meta["marker_start"]] ** 2)
        scale = 10 ** (-6.0 / 20) / 10 ** (meta["ref_dbfs"] / 20)
        s = meta["settings"]
        knobs = [f"{THRESHOLD_DB[s['Threshold']] + OPEN_DB - 6.0}", f"{DECAY_MS[s['Decay']]}"]
        x = (played * scale).astype(np.float32)
        p_in, p_pedal = power(scipy.signal.sosfilt(HIGH_PASS, played)[a:]), power(pedal[a:])
        gain_pedal = 10 * np.log10(p_pedal / np.maximum(p_in, 1e-30))
        loud = p_in > np.percentile(p_in, 70)
        top = np.median(gain_pedal[loud])                           # the open path: the rig's loss and the pedal's gain
        seen = (p_in * 10 ** (top / 10) > 16 * floor) & (gain_pedal < top + 3)
        cells, out = [], {}
        for label, binary in (("before", "build-ns2/render_ns2.before"), ("after", "build-ns2/render_ns2")):
            y = subprocess.run([binary] + knobs, input=x.tobytes(), capture_output=True, check=True).stdout
            y = np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale
            g = 10 * np.log10((power(scipy.signal.sosfilt(HIGH_PASS, y)[a:]) + floor * 10 ** (-top / 10)) / np.maximum(p_in, 1e-30))
            open_ = seen & (gain_pedal > top - 0.5)
            match = np.median(gain_pedal[open_]) - np.median(g[open_])     # the rig's loss and the open paths' difference
            d = (g + match - gain_pedal)[seen]
            cells.append(f"{np.sqrt(np.mean(d ** 2)):5.2f} ({np.percentile(np.abs(d), 95):5.1f})")
            out[label] = y * 10 ** (match / 20)
        level = 0.5 / max(np.abs(raw[a:]).max(), *(np.abs(v[a:]).max() for v in out.values()))
        soundfile.write(S / "ab" / f"{name}.pedal.wav", raw[a:] * level, RATE, subtype="FLOAT")
        for label, y in out.items():
            soundfile.write(S / "ab" / f"{name}.{label}.wav", y[a:] * level, RATE, subtype="FLOAT")
        print(f"{name:36s} {seen.sum():6d}                                               {cells[0]}   {cells[1]}")


if __name__ == "__main__":
    main()
