"""Modes I and II's Drive: the session of 2026-11-21 (focused-m12; rotors parked with Slow at 7:00), the way Mode III's
was read (parked.py, drive3_*.py). RT2_MODE=1 or 2 picks the mode.

At 7:00 these modes take the Drive out of the path (clean to the guitar's peak), so the both-rotor 7:00 take's quiet
sweep is the parked rotors' exact response, and a recording divided by it is the Drive's own output. That output is
Mode III's stage with other gains and filters (src/dsp/Rt2Drive.h):

  gain in → y = F(x − c), Mode III's curve and bias (build-rt2/drive3/fit-5000.json) → a one-pole low-pass → gain out

  RT2_MODE=1 drive12.py           the Drive's output per take (build-rt2/drive<mode>/d-<drive>.npy, rows played, output);
                                  the low-pass, from the quiet sweeps with Drive on over the 7:00 one; the gain into and
                                  after the curve per position, on the ladders and the funk (gains.json)
  RT2_MODE=1 drive12.py check     the guitar at the pedal's outputs per third octave, model − pedal: the fitted Drive and
                                  the tanh clipper NAM-X had before; the gain on the noise ladder
  RT2_MODE=1 drive12.py engine    the C++ stage (build-rt2/bin/render_rt2_drive) against the model with the engine's law
  RT2_MODE=1 drive12.py listen    pedal / before / after WAVs in the session's ab/ (rotors parked)
  drive12.py turning              the reamps of 2026-11-07 with Drive up (Mode I, 3:00, rotors turning): the engine
                                  before (build-rt2/bin/render_rt2.before12) and after (render_rt2) against the pedal,
                                  and their WAVs in that session's ab/

Run from the repository root. Units: 1 = the guitar's peak × 10^(−36/20)."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.optimize, scipy.signal, soundfile
from parked import MODE, OUT, RATE, S, TAG, UNIT, at, load, parts, sweep_response
import drive3_fit

DRIVES = ("0900", "1200", "0300", "0500")
KNOB = {"0700": 0.0, "0900": 0.2, "1200": 0.5, "0300": 0.8, "0500": 1.0}
P = json.load(open("build-rt2/drive3/fit-5000.json"))                    # Mode III's curve and bias
LOW_HZ = 5522.0                                                           # the low-pass after the curve (main() refits it)
ENGINE_LAW = {"0900": (-1.67, 0.41), "1200": (8.38, -7.58), "0300": (13.03, -10.13), "0500": (14.52, -10.70)}   # Rt2Drive.h
BANDS = [100, 200, 400, 800, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500]
DC = ([1, -1], [1, -np.exp(-2 * np.pi * 10 / RATE)])                      # the engine's DC blocker
HIGH = scipy.signal.butter(2, 30, "high", fs=RATE, output="sos")          # the Drive's output is known from 40 Hz up
LOW = scipy.signal.butter(4, 10000, "low", fs=RATE, output="sos")         # and compared under 10 kHz
HALF = scipy.signal.firwin(63, 0.5)


def low_pass(fc):
    """The pedal's low-pass: a bilinear one-pole at 48 kHz."""
    k = np.tan(np.pi * fc / RATE)
    return [k / (1 + k), k / (1 + k)], [1, -(1 - k) / (1 + k)]


def rotors():
    return {ch: sweep_response(f"{TAG}-both-drive0700", ch) for ch in "ab"}


def through(y, ch, R):
    """A Drive output through the parked rotors (the one sample main() delayed the outputs by taken back)."""
    N = 1 << int(np.ceil(np.log2(len(y))))
    f = np.fft.rfftfreq(N, 1 / RATE)
    return np.fft.irfft(np.fft.rfft(y, N) * at(R[ch], f) * np.exp(2j * np.pi * f / RATE), N)[:len(y)]


def curve(x_units, pre_db, oversample):
    """The curve at the bias on a signal in units, before the low-pass and the gain out; at RATE × oversample in and out."""
    rate = RATE * oversample
    y = drive3_fit.core(x_units * 10 ** (pre_db / 20), drive3_fit.rest(P), P["c_far"], 1 - np.exp(-1000.0 / (P["tau_r"] * rate)),
                        P["k"] * 1000.0 / rate, P["theta"], P["cap"], P["a"], P["v1"], P["v3"], 1.0, False)
    return y - y[0]


def new_drive(xg, drive, law):
    """The fitted Drive on a signal re the guitar's peak, as the engine runs it (4× oversampled, its DC blocker)."""
    if drive == "0700":
        return xg
    y = scipy.signal.resample_poly(curve(scipy.signal.resample_poly(xg / UNIT, 4, 1), law[drive][0], 4), 1, 4)[:len(xg)]
    y = scipy.signal.lfilter(*low_pass(LOW_HZ), y) * 10 ** (law[drive][1] / 20) * UNIT
    return scipy.signal.lfilter(*DC, y)


def old_drive(xg, drive):
    """NAM-X's Modes I and II until 2026-11-21: a level-compensated tanh, from the first session's bursts."""
    if drive == "0700":
        return xg
    G = 10 ** (np.interp(KNOB[drive], [0, .2, .5, .8, 1], [-60.0, 9.0, 16.7, 24.0, 26.4]) / 20)
    L = 10 ** (np.interp(KNOB[drive], [0, .2, .5, .8, 1], [0.0, -1.1, -1.6, 2.8, 4.5]) / 20)
    return np.tanh(G * np.clip(xg, -1.2942, 1.2942)) * L / G


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


def main():
    global LOW_HZ
    OUT.mkdir(parents=True, exist_ok=True)
    R = rotors()
    # the Drive's output per take: both outputs through the 7:00 sweep's response, weighted by it
    for name in sorted(p.stem for p in S.glob(f"{TAG}-both-drive*.json")):
        meta, played, a, b, loop = load(name)
        N = 1 << int(np.ceil(np.log2(len(played))))
        f = np.fft.rfftfreq(N, 1 / RATE)
        num = den = 0
        for ch, y in (("a", a), ("b", b)):
            H = at(R[ch], f)
            num = num + np.fft.rfft(y, N) * np.conj(H)
            den = den + np.abs(H) ** 2
        Nf = num / (den + 1e-4 * np.max(den))
        Nf[(f < 40) | (f > 15500)] = 0
        # one sample later: against the 7:00 take the Drive-on takes come a sample early for a causal low-pass
        np.save(OUT / f"d-{name.split('drive')[1]}.npy", np.stack([played, np.fft.irfft(Nf * np.exp(-2j * np.pi * f / RATE), N)[:len(played)]]))
    # the low-pass: the quiet sweeps with Drive on over the 7:00 one, 1–12 kHz re 1 kHz
    centres = np.array([1000, 1400, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12000])
    ratio = []
    for d in DRIVES:
        on, off = {ch: sweep_response(f"{TAG}-both-drive{d}", ch) for ch in "ab"}, R
        f = on["a"][0]
        row = []
        for c in centres:
            m = (f > c / 1.06) & (f < c * 1.06)
            row.append(abs(sum(np.vdot(off[ch][1][m], on[ch][1][m]) for ch in "ab") / sum(np.vdot(off[ch][1][m], off[ch][1][m]) for ch in "ab")))
        ratio.append(np.array(row) / row[0])
    target = 20 * np.log10(np.mean(ratio, 0))

    def shape(fc):
        b, a = low_pass(fc)
        h = np.abs(scipy.signal.freqz(b, a, centres, fs=RATE)[1])
        return 20 * np.log10(h / h[0])

    fit = scipy.optimize.least_squares(lambda p: shape(p[0]) - target, [5500.0])
    LOW_HZ = float(fit.x[0])
    print(f"Mode {'I' * MODE}: quiet sweeps with Drive on re 7:00, dB re 1 kHz, at " + " ".join(f"{c:>6}" for c in centres))
    print(" " * 51 + " ".join(f"{v:+6.2f}" for v in target))
    print(f"a one-pole low-pass (bilinear, 48 kHz) at {LOW_HZ:.0f} Hz: " + " ".join(f"{v:+6.2f}" for v in shape(LOW_HZ)))
    # the gains per position: the curve at 2× on the ladders and the funk, each 25 ms weighing the same
    gains = {}
    for d in DRIVES:
        meta = json.loads((S / f"{TAG}-both-drive{d}.json").read_text())
        played, n = np.load(OUT / f"d-{d}.npy")
        unit = 10 ** (meta["ref_dbfs"] / 20) * UNIT
        n = scipy.signal.sosfilt(LOW, scipy.signal.sosfilt(HIGH, n / unit))
        w = np.zeros(len(n))
        for part, start in parts(meta):
            if part["type"] == "steps":
                end = start + int(len(part["levels_db"]) * (part["step_seconds"] + part["gap_seconds"]) * RATE)
            elif part["type"] == "file":
                end = start + int(part["seconds"] * RATE)
            else:
                continue
            for i in range(start, end - 1200, 1200):
                w[i:i + 1200] = 1.0 / (np.sqrt(np.mean(n[i:i + 1200] ** 2)) + 0.3)
        xu = scipy.signal.resample_poly(played / unit, 2, 1)
        m = w > 0

        def model(pre):
            y = scipy.signal.upfirdn(HALF, curve(xu, pre, 2), 1, 2)[len(HALF) // 4:][:len(n)]
            y = scipy.signal.sosfilt(LOW, scipy.signal.sosfilt(HIGH, scipy.signal.lfilter(*low_pass(LOW_HZ), y)))
            g = np.sum(y[m] * n[m] * w[m] ** 2) / np.sum(y[m] ** 2 * w[m] ** 2)   # the gain out: linear, solved
            return g, np.sqrt(np.mean(((g * y - n) * w)[m] ** 2))

        coarse = min(np.arange(-6, 48.1, 3), key=lambda pre: model(pre)[1])
        pre = scipy.optimize.minimize_scalar(lambda pre: model(pre)[1], bounds=(coarse - 3, coarse + 3), method="bounded", options=dict(xatol=0.02)).x
        g, e = model(pre)
        gains[d] = (float(pre), float(20 * np.log10(g)))
        print(f"Drive {d}: into the curve {pre:+6.2f} dB, after it {gains[d][1]:+6.2f} dB (rms error {100 * e:.1f} % of each 25 ms's level)")
    json.dump(dict(low_hz=LOW_HZ, gains=gains), open(OUT / "gains.json", "w"), indent=1)


def check():
    R = rotors()
    law = {k: tuple(v) for k, v in json.load(open(OUT / "gains.json"))["gains"].items()}
    print(f"Mode {'I' * MODE}: the guitar at the pedal's outputs, model − pedal per third octave (dB), level, waveform residual")
    for which, lw in (("the mode's own fit", law), ("the engine's law (Modes I and II)", ENGINE_LAW)):
        print(f"{which}\n" + " " * 26 + " ".join(f"{c:>6}" for c in BANDS) + "   level  resid")
        rows = []
        for name in [f"{TAG}-both-drive{d}" for d in ("0900", "1200", "1200-lead", "0300", "0500")]:
            meta, played, a, b, loop = load(name)
            drive, ref = name.split("drive")[1][:4], 10 ** (meta["ref_dbfs"] / 20)
            s0, s1 = music(meta)
            for label, y in (("before", old_drive(played / ref, drive)), ("after", new_drive(played / ref, drive, lw))):
                if label == "before" and lw is ENGINE_LAW:
                    continue
                for ch, rec in (("a", a), ("b", b)):
                    m, p = through(y * ref, ch, R)[s0:s1], rec[s0:s1]
                    row = bands(m, p)
                    print(f"{name[8:]:<14} {ch.upper()} {label:<6} " + " ".join(f"{v:+6.1f}" for v in row)
                          + f"  {20 * np.log10(np.std(m) / np.std(p)):+6.2f} {20 * np.log10(np.std(m - p) / np.std(p)):+6.1f}")
                    if label == "after":
                        rows.append(row)
        print("after, rms per band:      " + " ".join(f"{v:6.2f}" for v in np.sqrt(np.mean(np.array(rows) ** 2, 0))))
    print("the noise ladder: gain at 250–4000 Hz re the rotors' own (dB) per step, pedal / after / before")
    for d in DRIVES:
        meta, played, a, b, loop = load(f"{TAG}-both-drive{d}")
        ref = 10 ** (meta["ref_dbfs"] / 20)
        outs = [a, through(new_drive(played / ref, d, ENGINE_LAW) * ref, "a", R), through(old_drive(played / ref, d) * ref, "a", R)]
        base = through(played, "a", R)
        line = []
        for part, start in parts(meta):
            if part["type"] != "noise-steps":
                continue
            for i, level in enumerate(part["levels_db"]):
                s0, s1 = start + int((i * part["step_seconds"] + 0.1) * RATE), start + int(((i + 1) * part["step_seconds"] - 0.02) * RATE)

                def power(y):
                    f, p = scipy.signal.welch(y[s0:s1], RATE, nperseg=4096)
                    return p[(f > 250) & (f < 4000)].sum()

                line.append(f"{level:+d}: " + "/".join(f"{10 * np.log10(power(y) / power(base)):+.1f}" for y in outs))
        print(f"  Drive {d}  " + "  ".join(line))


def engine():
    meta, played, a, b, loop = load(f"{TAG}-both-drive1200")
    xg = (played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32)
    start = meta["signal_start"]
    f = np.fft.rfftfreq(len(xg), 1 / RATE)
    for name, knob in KNOB.items():
        out = np.frombuffer(subprocess.run(["build-rt2/bin/render_rt2_drive", str(MODE - 1), str(knob)], input=xg.tobytes(), capture_output=True, check=True).stdout, np.float32).astype(float)
        ref = new_drive(xg.astype(float), name, ENGINE_LAW)
        ref = np.fft.irfft(np.fft.rfft(ref) * np.exp(-2j * np.pi * f * 22.5 / RATE), len(ref))   # the oversampler's delay
        row = []
        for band in ((30, 15000), (30, 2000), (2000, 6000), (6000, 15000)):
            sos = scipy.signal.butter(4, band, "band", fs=RATE, output="sos")
            o, r = scipy.signal.sosfilt(sos, out)[start:], scipy.signal.sosfilt(sos, ref)[start:]
            row.append(f"{band[0]}-{band[1]} Hz {20 * np.log10(np.std(o - r) / np.std(r)):.1f}")
        print(f"Mode {'I' * MODE}, Drive {name}: the C++ − the model, dB re the model: " + ", ".join(row)
              + f"; before the signal the C++ peaks at {20 * np.log10(np.abs(out[100:meta['marker_start'] - 100]).max() + 1e-12):.0f} dBFS")


def listen():
    R = rotors()
    out = S / "ab"
    out.mkdir(exist_ok=True)
    for name in [f"{TAG}-both-drive{d}" for d in ("0900", "1200", "1200-lead", "0300", "0500")]:
        meta, played, a, b, loop = load(name)
        drive, ref = name.split("drive")[1][:4], 10 ** (meta["ref_dbfs"] / 20)
        s0, s1 = music(meta)
        s1 += int(0.8 * RATE)
        takes = {"pedal": np.stack([a[s0:s1], b[s0:s1]], 1)}
        for label, y in (("before", old_drive(played / ref, drive)), ("after", new_drive(played / ref, drive, ENGINE_LAW))):
            takes[label] = np.stack([through(y * ref, "a", R)[s0:s1], through(y * ref, "b", R)[s0:s1]], 1)
        gain = 0.5 / max(np.abs(v).max() for v in takes.values())
        for label, v in takes.items():
            path = out / f"{TAG}-{name[8:]}{'' if 'lead' in name else '-funk'}.{label}.wav"
            soundfile.write(path, v * gain, RATE, subtype="FLOAT")
            print(path)


def turning():
    session = pathlib.Path("captures/boss/rt-2/2026-11-07-reamp3")
    ref_played, ref_namx, chain = 10 ** (-4.67 / 20), 10 ** (-6.0 / 20), 10 ** (-8.89 / 20)   # as in reamp_ab.py
    centres = [80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500]

    def third_octave(x):
        f, p = scipy.signal.welch(x, RATE, nperseg=8192)
        return np.array([10 * np.log10(p[(f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))].mean() + 1e-30) for c in centres])

    print("Mode I, Drive 3:00, Slow 12:00, rotors turning: model − pedal (dB), level then third octaves at " + " ".join(f"{c:>5}" for c in centres))
    for name in ("m1-slow-drive-0300-chords", "m1-slow-drive-0300-chugs"):
        meta = json.loads((session / f"{name}.json").read_text())
        data, _ = soundfile.read(session / f"{name}.wav", dtype="float64")
        played, _ = soundfile.read(session / "played" / f"{name}.wav", dtype="float32")
        lat, start = meta["latency_samples"], meta["signal_start"]
        n = min(len(played), len(data) - lat)
        takes = {"pedal": data[lat:lat + n][:, [0, 2]][start:]}
        for label, binary in (("before", "build-rt2/bin/render_rt2.before12"), ("after", "build-rt2/bin/render_rt2")):
            y = subprocess.run([binary, "0", "0.811", str(0.811 / 1.0270), "0.0", "0.25", "drive=0.8"],
                               input=(played * (ref_namx / ref_played)).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
            takes[label] = (np.frombuffer(y, np.float32).reshape(-1, 2).astype(float) * (ref_played / ref_namx) * chain)[:n][start:]
            for c, ch in enumerate("AB"):
                tone = third_octave(takes[label][:, c]) - third_octave(takes["pedal"][:, c])
                print(f"{name[3:]:<24} {ch} {label:<6} {10 * np.log10(np.mean(takes[label][:, c] ** 2) / np.mean(takes['pedal'][:, c] ** 2)):+5.1f}  " + " ".join(f"{t:+5.1f}" for t in tone))
        gain = 0.5 / max(np.abs(v).max() for v in takes.values())
        for label, v in takes.items():
            soundfile.write(session / "ab" / f"{name}.{label}.wav", v * gain, RATE, subtype="FLOAT")


if __name__ == "__main__":
    {"check": check, "engine": engine, "listen": listen, "turning": turning}.get(sys.argv[1] if len(sys.argv) > 1 else "", main)()
