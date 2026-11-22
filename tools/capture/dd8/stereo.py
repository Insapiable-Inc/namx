"""The DD-8 with both outputs connected against NAM-X's Delay with Stereo on (R Time = twice Time,
the pedal's ping-pong): the marker chirp's repeats on each side, their level re the dry and their
arrival, pedal and model (render_dd8 with R_TIME_MS). The pairs must come out the same: the odd
repeats on A / left, the even ones on B / right, equal within a pair.

  dd8/stereo.py [SESSION]   (default captures/boss/dd-8/2026-11-10-stereo)"""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/dd-8/2026-11-10-stereo")
RATE = scorecard.RATE
TYPES = {"Standard": 1, "Analog": 2, "Tape": 3, "Warm": 4, "Mod": 5}
POSITION = {"7:00": 0.0, "9:00": 0.2, "12:00": 0.5, "3:00": 0.8, "5:00": 1.0}
MARKER = int(0.05 * RATE)


def load(name):
    meta = json.loads((SESSION / f"{name}.json").read_text())
    rec, _ = soundfile.read(SESSION / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(SESSION / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(rec) - lat)
    pre = rec[:meta["marker_start"]]
    return meta, played[:n], rec[lat:lat + n, 0] - pre[:, 0].mean(), rec[lat:lat + n, 2] - pre[:, 2].mean()


def train(x, played, meta, period, count=6):
    """The matched filter's peak near each multiple of the period after the dry: (ms, dB re the dry)."""
    m0 = meta["marker_start"]
    c = np.abs(scipy.signal.hilbert(scipy.signal.correlate(x[m0 - 240:], played[m0:m0 + MARKER], "valid")))
    dry = int(np.argmax(c[:480]))
    out = []
    for k in range(1, count + 1):
        centre = dry + int(round(k * period))
        if centre + 400 >= len(c):
            break
        i = centre - 400 + int(np.argmax(c[centre - 400:centre + 400]))
        out.append(((i - dry) / RATE * 1000, 20 * np.log10(c[i] / c[dry])))
    return out


def main():
    print("the marker's repeats, dB re the dry (a blank: under −45 dB): pedal A | B, then NAM-X left | right")
    for path in sorted(SESSION.glob("*-clicks.json")):
        meta, played, a, b = load(path.stem)
        s = meta["settings"]
        m0 = meta["marker_start"]
        c = np.abs(scipy.signal.hilbert(scipy.signal.correlate(a[m0 - 240:m0 + RATE], played[m0:m0 + MARKER], "valid")))
        dry = int(np.argmax(c[:480]))
        period = int(np.argmax(c[dry + 2400:])) + 2400  # the first repeat, on A
        scale = 10 ** ((-6.0 - meta["ref_dbfs"]) / 20)
        y = subprocess.run(["build-dd8/render_dd8", str(TYPES[s["Mode"]]), f"{period / RATE * 1000:.4f}",
                            str(POSITION[s["Level"]]), str(POSITION[s["Feedback"]]), f"{2 * period / RATE * 1000:.4f}"],
                           input=(played * scale).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
        model = np.frombuffer(y, dtype=np.float32).astype(np.float64).reshape(-1, 2) / scale
        print(f"{path.stem}  (Time {s['Time']}: {period / RATE * 1000:.1f} ms, Feedback {s['Feedback']})")
        for label, x in (("pedal A", a), ("pedal B", b), ("NAM-X L", model[:, 0]), ("NAM-X R", model[:, 1])):
            cells = ["      " if level < -45 else f"{level:+6.1f}" for _, level in train(x, played, meta, period)]
            print(f"   {label}: repeats 1-6  " + " ".join(cells))


if __name__ == "__main__":
    main()
