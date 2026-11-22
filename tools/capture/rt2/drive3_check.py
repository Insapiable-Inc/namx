"""Mode III's Drive checked at the pedal's outputs (the session of 2026-11-21, rotors parked): a Drive model's output
through the parked rotors' measured responses against the recording.

  drive3_check.py [FC]          the funk (and the lead, in no fit) per third octave, model − pedal, the fitted Drive
                                and the tanh clipper NAM-X had before; the pedal's and the model's gain on the noise ladder
  drive3_check.py engine        the C++ stage (build-rt2/bin/render_rt2_drive) against the fitted model, per band
  drive3_check.py listen        pedal / before / after WAVs: captures/boss/rt-2/2026-11-21-focused/ab/ (parked) and,
                                with build-rt2/bin/render_rt2.before and render_rt2 (the engine before and after),
                                2026-11-07-reamp3/ab/m3-* (rotors turning)

Needs parked.py, drive3_curve.py and drive3_fit.py (both commands) run first."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile
from parked import OUT, RATE, S, UNIT, at, load, parts, responses, sweep_response, through, tone
import drive3_fit

FC = float(([a for a in sys.argv[1:] if a.replace(".", "").isdigit()] or ["5000"])[0])
P = json.load(open(OUT / f"fit-{int(FC)}.json"))
LAW = json.load(open(OUT / f"gains-{int(FC)}.json"))
MIX = dict(horn=(1.0, 0.0), drum=(0.0, 1.0), both=tuple(json.load(open(OUT / "mix.json"))["a"]))
KNOB = {"0700": 0.0, "0900": 0.2, "1200": 0.5, "0300": 0.8, "0500": 1.0}
BANDS = [100, 200, 400, 800, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500]
DC = ([1, -1], [1, -np.exp(-2 * np.pi * 10 / RATE)])   # the engine's DC blocker


def new_drive(xg, drive, extra_db=0.0):
    """The fitted Drive on a signal re the guitar's peak, in the Drive output's scale (the quiet sweep's gain = 1)."""
    y = drive3_fit.drive(xg / UNIT, P, LAW[drive][0], LAW[drive][1] + extra_db, FC) * UNIT
    return scipy.signal.lfilter(*DC, y - y[0])   # the curve's output at rest taken off, as the engine does


def old_drive(xg, drive, beta):
    """NAM-X's Mode III Drive until 2026-11-21: tanh with a bias, its gains per position."""
    into = np.interp(KNOB[drive], [0, .2, .5, .8, 1], [10.0, 30.3, 33.3, 32.9, 32.4])
    soft = np.interp(KNOB[drive], [0, .2, .5, .8, 1], [0.0, 7.8, 7.6, 6.9, 6.5])
    G, L, b = 10 ** (into / 20), 10 ** (soft / 20), 0.25
    return (np.tanh(G * np.clip(xg, -1.2942, 1.2942) + b) - np.tanh(b)) * L / (G * (1 - np.tanh(b) ** 2)) * beta


def noise_gain(y_of):
    """The gain at 250–1000 Hz on each step of the noise ladder (drum-alone take, Drive 7:00), dB."""
    meta, played, a, b, loop = load("m3-drum-drive0700")
    y = y_of(meta, played, a)
    out = {}
    for part, start in parts(meta):
        if part["type"] != "noise-steps":
            continue
        for i, level in enumerate(part["levels_db"]):
            s0, s1 = start + int((i * part["step_seconds"] + 0.1) * RATE), start + int(((i + 1) * part["step_seconds"] - 0.02) * RATE)
            f, pxy = scipy.signal.csd(played[s0:s1], y[s0:s1], RATE, nperseg=4096)
            _, pxx = scipy.signal.welch(played[s0:s1], RATE, nperseg=4096)
            m = (f > 250) & (f < 1000)
            out.setdefault(level, []).append((f[m], pxy[m] / pxx[m]))
    return out


def pedal_beta():
    """The pedal's own gain on the noise ladder re the quiet sweep's. At −12 dB it is what the rotors' filters, fitted
    on −12 dB noise at Drive 7:00, already hold (Rt2Drive.h's ROTORS_DB)."""
    H = sweep_response("m3-drum-drive0700", "a")
    return {lv: float(np.mean([10 * np.log10(np.sum(np.abs(t) ** 2) / np.sum(np.abs(at(H, f)) ** 2)) for f, t in v]))
            for lv, v in noise_gain(lambda meta, played, a: a).items()}


def bands(m, p):
    f = np.fft.rfftfreq(len(p), 1 / RATE)
    M, Pp = np.abs(np.fft.rfft(m)) ** 2, np.abs(np.fft.rfft(p)) ** 2
    return [10 * np.log10(M[(f > c / 1.12) & (f < c * 1.12)].sum() / Pp[(f > c / 1.12) & (f < c * 1.12)].sum()) for c in BANDS]


def music(meta):
    """The take's played guitar: (first sample, last)."""
    if meta["signal"]["type"] == "file":
        return meta["signal_start"], meta["signal_start"] + int(meta["signal"]["seconds"] * RATE)
    part, start = parts(meta)[-1]
    return start, start + int(part["seconds"] * RATE)


def check():
    R = responses()
    pb = pedal_beta()
    model = noise_gain(lambda meta, played, a: new_drive(played / 10 ** (meta["ref_dbfs"] / 20), "0700") * 10 ** (meta["ref_dbfs"] / 20))
    print("gain on the noise ladder re the quiet sweep's, dB, per step's level:")
    print("  the pedal ", {k: round(v, 2) for k, v in pb.items()})
    print("  the model ", {k: round(float(np.mean([10 * np.log10(np.mean(np.abs(t) ** 2)) for f, t in v])), 2) for k, v in model.items()})
    beta = 10 ** (pb[-12] / 20)
    print(f"\nthe guitar at the pedal's outputs, model − pedal per third octave (dB), level, waveform residual; filter at {FC:.0f} Hz")
    print(" " * 34 + " ".join(f"{c:>6}" for c in BANDS) + "   level  resid")
    rows = []
    for name, ch in (("m3-drum-drive0700", "a"), ("m3-horn-drive0700", "b"), ("m3-both-drive0700", "b"), ("m3-drum-drive0900", "a"),
                     ("m3-horn-drive0900", "b"), ("m3-both-drive0900", "b"), ("m3-both-drive1200", "b"), ("m3-both-drive1200-lead", "b"),
                     ("m3-both-drive0300", "b"), ("m3-both-drive0500", "b")):
        meta, played, a, b, loop = load(name)
        rec = {"a": a, "b": b}[ch]
        rotor, drive = name.split("-")[1], name.split("drive")[1][:4]
        ref = 10 ** (meta["ref_dbfs"] / 20)
        s0, s1 = music(meta)
        for label, y in (("before", old_drive(played / ref, drive, beta)), ("after", new_drive(played / ref, drive))):
            m = through(y * ref, MIX[rotor], ch, R)[s0:s1]
            row = bands(m, rec[s0:s1])
            print(f"{name[3:]:<20} {ch.upper()} {label:<6} " + " ".join(f"{v:+6.1f}" for v in row)
                  + f"  {20 * np.log10(np.std(m) / np.std(rec[s0:s1])):+6.2f} {20 * np.log10(np.std(m - rec[s0:s1]) / np.std(rec[s0:s1])):+6.1f}")
            if label == "after" and rotor != "drum":
                rows.append(row)
    print("after, the horn and both-rotor takes: rms per band " + " ".join(f"{v:6.2f}" for v in np.sqrt(np.mean(np.array(rows) ** 2, 0))))


def engine():
    meta, played, a, b, loop = load("m3-both-drive1200")
    xg = (played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32)
    start = meta["signal_start"]
    f = np.fft.rfftfreq(len(xg), 1 / RATE)
    for name, knob in KNOB.items():
        out = np.frombuffer(subprocess.run(["build-rt2/bin/render_rt2_drive", "2", str(knob)], input=xg.tobytes(), capture_output=True, check=True).stdout, np.float32).astype(float)
        ref = new_drive(xg.astype(float), name, 1.22 - 0.13)   # the engine's ROTORS_DB, and its gains after the curve (0.13 dB under the fit's)
        ref = np.fft.irfft(np.fft.rfft(ref) * np.exp(-2j * np.pi * f * 22.5 / RATE), len(ref))   # the oversampler's delay
        row = []
        for band in ((30, 15000), (30, 2000), (2000, 6000), (6000, 15000)):
            sos = scipy.signal.butter(4, band, "band", fs=RATE, output="sos")
            o, r = scipy.signal.sosfilt(sos, out)[start:], scipy.signal.sosfilt(sos, ref)[start:]
            row.append(f"{band[0]}-{band[1]} Hz {20 * np.log10(np.std(o - r) / np.std(r)):.1f}")
        print(f"Drive {name}: the C++ − the model, dB re the model: " + ", ".join(row)
              + f"; before the signal the C++ peaks at {20 * np.log10(np.abs(out[100:meta['marker_start'] - 100]).max() + 1e-12):.0f} dBFS")


def listen():
    R = responses()
    out = S / "ab"
    out.mkdir(exist_ok=True)
    meta, played, a, b, loop = load("bypass-reference")
    start = parts(meta)[1][1]
    b_gain = abs(tone(a[start + 9600:start + 24000], 110)) / abs(tone(b[start + 9600:start + 24000], 110))   # input 1 ran 6 dB hotter than input 2
    beta = 10 ** (pedal_beta()[-12] / 20)
    for name in sorted(p.stem for p in S.glob("m3-both-drive*.json") if "repeat" not in p.stem):
        meta, played, a, b, loop = load(name)
        drive = name.split("drive")[1][:4]
        ref = 10 ** (meta["ref_dbfs"] / 20)
        s0, s1 = music(meta)
        s1 += int(0.8 * RATE)
        takes = {"pedal": np.stack([a[s0:s1], b[s0:s1] * b_gain], 1)}
        for label, y in (("before", old_drive(played / ref, drive, beta)), ("after", new_drive(played / ref, drive))):
            takes[label] = np.stack([through(y * ref, MIX["both"], "a", R)[s0:s1], through(y * ref, MIX["both"], "b", R)[s0:s1] * b_gain], 1)
        gain = 0.5 / max(np.abs(v).max() for v in takes.values())
        for label, v in takes.items():
            path = out / f"{name[8:]}{'' if 'lead' in name else '-funk'}.{label}.wav"
            soundfile.write(path, v * gain, RATE, subtype="FLOAT")
            print(path)
    session = pathlib.Path("captures/boss/rt-2/2026-11-07-reamp3")
    ref_played, ref_namx, chain = 10 ** (-4.67 / 20), 10 ** (-6.0 / 20), 10 ** (-8.89 / 20)   # as in reamp_ab.py
    for name in ("m3-fast-chords", "m3-fast-chugs"):
        meta = json.loads((session / f"{name}.json").read_text())
        data, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
        played, _ = soundfile.read(session / "played" / f"{name}.wav", dtype="float32")
        lat, start = meta["latency_samples"], meta["signal_start"]
        n = min(len(played), len(data) - lat)
        takes = {"pedal": data[lat:lat + n][:, [0, 2]][start:]}
        for label, binary in (("before", "build-rt2/bin/render_rt2.before"), ("after", "build-rt2/bin/render_rt2")):
            y = subprocess.run([binary, "2", "6.845", str(6.845 / 1.0871), "0.0", "0.25", "drive=0.0", "fast=1"],
                               input=(played * (ref_namx / ref_played)).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
            takes[label] = (np.frombuffer(y, np.float32).reshape(-1, 2).astype(float) * (ref_played / ref_namx) * chain)[:n][start:]
        gain = 0.5 / max(np.abs(v).max() for v in takes.values())
        for label, v in takes.items():
            soundfile.write(session / "ab" / f"{name}.{label}.wav", v * gain, RATE, subtype="FLOAT")
            print(session / "ab" / f"{name}.{label}.wav")


if __name__ == "__main__":
    {"engine": engine, "listen": listen}.get(sys.argv[1] if len(sys.argv) > 1 else "", check)()
