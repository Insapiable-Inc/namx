"""The C++ engine (render_rt2) against fit_rt2's exported model (with its tone) and the pedal, on the isolated
session's takes; Mode III's engine runs its Drive at 7:00, the Python model is linear.
  check_engine.py [GAIN] MODE …   (GAIN scales the input, to rule out the input clip)

Run from the repository root, with the engine built as build-rt2/bin/render_rt2 (the command in tools/capture/render_rt2.cpp's header, -o build-rt2/bin/render_rt2)."""
import json, pathlib, subprocess, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import fit_rt2 as F
CHAIN = 10 ** (F.CHAIN_DB / 20)
GAIN = float(sys.argv.pop(1)) if len(sys.argv) > 2 and sys.argv[1].replace(".", "").isdigit() else 1.0
for mode in sys.argv[1:]:
    horn, drum = F.exported(mode)
    tone = json.loads((F.FIT / f"tone-{mode}.json").read_text())
    for kind in ("horn", "drum"):
        tk = F.load(f"{mode}-{kind}-noise")
        fh, fd = F.HORN_HZ, F.DRUM_HZ
        h0 = fh * (0 - tk["s0"]) / F.FS - fh / F.FS
        d0 = fd * (0 - tk["s0"]) / F.FS - fd / F.FS
        bal = [f"horn={10 ** (F.HORN_REF_DB / 20)}", "drum=0"] if kind == "horn" else ["horn=0", f"drum={10 ** (F.DRUM_REF_DB / 20)}"]
        cmd = ["build-rt2/bin/render_rt2", str(int(mode[1]) - 1), str(fh), str(fd), repr(h0), repr(d0), *bal]
        y = np.frombuffer(subprocess.run(cmd, input=(tk["x"] * GAIN).astype(np.float32).tobytes(), capture_output=True, check=True).stdout,
                          dtype=np.float32).reshape(-1, 2).astype(np.float64) * CHAIN / GAIN
        lo, cut, hi = F.split(tk)
        theta = F.angles(tk, fh if kind == "horn" else fd)
        for i, o in enumerate("AB"):
            m = horn[o] if kind == "horn" else drum
            py = m.render(F.rotor_input(tk["x"], mode, kind), theta, cut - 48000, hi)
            if kind == "horn":   # the shelf's state from well before the scored part
                py = scipy.signal.sosfilt(F.shelf_sos(tone["horn"][o]), py)
            py = py[48000:]
            cpp, meas = y[cut:hi, i], tk[o][cut:hi]
            print(f"{mode} {kind} {o}: C++ vs Python {F.nmse_db(py, cpp)[0]:6.1f} dB; C++ vs pedal {F.nmse_db(meas, cpp)};"
                  f" tone C++ − pedal {np.round(F.bands_db(cpp, F.TONE_BANDS) - F.bands_db(meas, F.TONE_BANDS), 1)}", flush=True)
