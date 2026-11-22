"""Listening pairs for the focused reamps: for every guitar take of a session, the pedal as recorded
and NAM-X's model of it (the take's played signal through NAM-X's own processor at the take's
settings, then through the bypass take's chain, as the pedal's recording went), written side by
side to SESSION/ab/<take>.pedal.wav and <take>.model.wav (24-bit mono, 48 kHz; git-ignored).

  ab.py [ge-7|tr-2|dd-8 …]     (default: all three)

Both files of a pair start 0.1 s before the guitar and share one gain (the louder of the two peaks
at -1 dBFS), so levels compare within a pair, not between pairs. The pedal's DC offset is removed.
TR-2: the model's LFO is set to the pedal's rate and phase as measured on the take (tr2/focused.py),
and the 9 kHz probe tone under the guitar is notched out of both. DD-8: the model's Time is the
delay measured on the take, and its Level and Feedback are trimmed until the first repeat's level and
the fade per repeat are the pedal's (its knobs were set by eye; the trims are printed), so a pair
compares the sound at the same setting, not two readings of a knob; the marker chirp went through the pedal in that session, so its repeats
are in both files' first second.

Run from the repository root with the renderers built (the commands in render_ge7.cpp,
render_tr2.cpp and render_dd8.cpp, into build-ge7/, build-tr2/ and build-dd8/)."""
import importlib.util, json, pathlib, subprocess, sys
import numpy as np, scipy.signal, soundfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import scorecard

RATE = scorecard.RATE
SESSIONS = {"ge-7": ("captures/boss/ge-7/2026-11-09-focused-psu", "bypass-chords"),
            "tr-2": ("captures/boss/tr-2/2026-11-09-focused", "bypass-chords"),
            "dd-8": ("captures/boss/dd-8/2026-11-09-focused", "bypass-stabs")}


def module(pedal):
    """The pedal's focused.py (its render and helpers), loaded without its command line."""
    spec = importlib.util.spec_from_file_location(f"{pedal}_focused", HERE / pedal.replace("-", "") / "focused.py")
    m = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, sys.argv[:1]
    spec.loader.exec_module(m)
    sys.argv = argv
    return m


def model_ge7(F, meta, played, pedal, pre, h):
    return F.render(played, meta)


def model_dd8(F, meta, played, pedal, pre, h):
    """The model set as the pedal was, not as its knobs were read: Time as measured on the take,
    Level trimmed until the repeats alone (the output minus the dry) are as loud as the pedal's
    over the guitar (not the marker: it is over the clip, the guitar mostly under), Feedback trimmed
    until the take fades at the pedal's dB per repeat after the guitar stops. The pedal's knobs
    were set by eye, and near 3:00 one degree of Feedback is 4 dB after 24 repeats."""
    dry_at, dry, first_at, first = F.arrivals(pedal, played, meta)
    s, period = meta["settings"], first_at - dry_at
    scale = 10 ** ((F.REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    body_end = meta["signal_start"] + int(meta["signal"]["seconds"] * RATE) + RATE // 20
    floor = F.db(np.mean((pre[RATE // 10:] - np.mean(pre)) ** 2))
    target, _ = F.decay_per_repeat(pedal, body_end, period, floor)

    def render(level, feedback):
        y = subprocess.run([F.RENDER, str(F.TYPES[s["Mode"]]), f"{period / RATE * 1000:.4f}", f"{level:.5f}",
                            f"{feedback:.5f}"], input=(played * scale).astype(np.float32).tobytes(),
                           capture_output=True, check=True).stdout
        return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale

    level, feedback = F.POSITION[s["Level"]], F.POSITION[s["Feedback"]]
    dry_path = scorecard.through(h, played)
    body = slice(meta["signal_start"], body_end)
    wet_power = np.mean((pedal - dry_path)[body] ** 2)
    y = render(level, feedback)
    for _ in range(3):
        off_db = 10 * np.log10(np.mean((scorecard.through(h, y) - dry_path)[body] ** 2) / wet_power)
        if level < 1.0 and abs(off_db) > 0.05:
            level -= off_db / 67.0  # the Level map's slope round 12:00, dB per whole turn
        decay, _ = F.decay_per_repeat(y, body_end, period, floor)
        if not np.isnan(target) and not np.isnan(decay):
            # the Feedback maps' slope: ~17 dB per whole turn up to 3:00, ~22 round it
            feedback = min(1.0, feedback + (target - decay) / (22.0 if feedback > 0.65 else 17.0))
        y = render(level, feedback)
    decay, _ = F.decay_per_repeat(y, body_end, period, floor)
    print(f"      Level {(level - F.POSITION[s['Level']]) * 300:+.1f} deg, Feedback "
          f"{(feedback - F.POSITION[s['Feedback']]) * 300:+.1f} deg; fade pedal {target:+.2f}, model {decay:+.2f} dB/repeat")
    return y


def model_tr2(F, meta, played, pedal, pre, h):
    a, b = F.body(meta)
    probe_in = F.line(played)[a:b]
    g = F.line(pedal)[a:b] / probe_in
    if g.std() < 0.01 * g.mean():  # Depth 7:00: no cycle
        return F.render(played, meta, 5.0)
    period = F.period_of(g)
    rate_hz = asked = RATE / period
    for _ in range(2):  # the model's rate is a hair off the one asked
        asked *= rate_hz / (RATE / F.period_of(F.line(F.render(played, meta, asked))[a:b] / probe_in))
    cycle = F.fold(g, period)
    _, turn = F.align(cycle, F.fold(F.line(F.render(played, meta, asked))[a:b] / probe_in, period))
    return F.render(played, meta, asked, int(round((1.0 - turn) * period)))


MODELS = {"ge-7": model_ge7, "tr-2": model_tr2, "dd-8": model_dd8}
NOTCH = scipy.signal.iirnotch(9000, 30, RATE)


def main():
    for name in sys.argv[1:] or list(SESSIONS):
        session, bypass = SESSIONS[name]
        session = pathlib.Path(session)
        F = module(name)
        _, played, pedal, _ = scorecard.load(session, bypass)
        h = scorecard.chain_filter(played, pedal)
        out = session / "ab"
        out.mkdir(exist_ok=True)
        takes = sorted((json.loads(p.read_text()) for p in session.glob("*.json") if p.name != "rig.json"),
                       key=lambda m: m["recorded_at"])
        for meta in takes:
            if meta["bypass"] or meta["signal"]["type"] != "file":
                continue
            meta, played, pedal, pre = scorecard.load(session, meta["take"])
            pedal = pedal - np.mean(pre)
            model = scorecard.through(h, MODELS[name](F, meta, played, pedal, pre, h))
            if name == "tr-2":
                pedal, model = (scipy.signal.filtfilt(*NOTCH, x) for x in (pedal, model))
            start = meta["signal_start"] - RATE // 10
            pedal, model = pedal[start:], model[start:]
            gain = 10 ** (-1 / 20) / max(np.abs(pedal).max(), np.abs(model).max())
            for kind, x in (("pedal", pedal), ("model", model)):
                soundfile.write(out / f"{meta['take']}.{kind}.wav", x * gain, RATE, subtype="PCM_24")
            level = 10 * np.log10(np.mean(model ** 2) / np.mean(pedal ** 2))
            print(f"{name}  {meta['take']:32s} {json.dumps(meta['settings'])}  model − pedal {level:+.2f} dB")
        print(f"  -> {out}/")


if __name__ == "__main__":
    main()
