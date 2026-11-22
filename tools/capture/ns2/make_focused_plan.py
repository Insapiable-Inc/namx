"""Writes tools/capture/plans/ns-2-focused.json: the NS-2's gain read directly.

The NS-2's detector listens to INPUT only; its gain element sits between RETURN and OUTPUT (the schematic). So the
test signal goes to INPUT and a steady three-tone probe to RETURN (the plan's `loop_probe`: the interface's loopback
output, its cable moved to the pedal): the output is the probe times the gate's gain, at every instant and however
quiet the input.

Per setting, two takes. "tones", every level placed re the setting's threshold T (Ns2Gate.h's table, dB re the guitar's
peak): rising 2 dB steps through T at five frequencies (where it opens: the detector's frequency response); 1 kHz dropped
from T + 20 dB to a level under T and held, eight levels (what the gain does down there, and how fast); plucks decaying
35 dB/s at four pitches and 12 dB/s at two (a note's tail); a noise burst and then hiss around T (the stab-and-hiss
case). "guitar": the DI dataset's stabs, funk, notes and dynamics.

Run from the repository root."""
import json, pathlib

THRESHOLD = {"12:00": -66.7, "3:00": -49.4, "5:00": -27.3}      # T, dB re the guitar's peak
HOLD = {"7:00": (2, 1.0), "12:00": (5, 1.5), "5:00": (10, 3.0)}  # Decay: half-second steps held under T; seconds of tail


def tones(threshold, decay):
    T = THRESHOLD[threshold]
    held, tail = HOLD[decay]
    r = lambda v: round(v, 1)
    parts = [{"type": "silence", "seconds": 2, "tail": 0}]
    for freq in (82, 220, 660, 2000, 6000):
        parts.append({"type": "steps", "freq": freq, "levels_db": [r(T + d) for d in range(-8, 13, 2)], "step_seconds": 0.3, "tail": tail})
    under = [-2, -5, -8, -12, -16, -22, -30, None] if decay != "5:00" else [-2, -6, -12, -20, None]
    levels = []
    for d in under:
        levels += [r(min(T + 20, 0))] + [-120 if d is None else r(T + d)] * held
    levels += [0] + [r(T - 8)] * held                             # the same drop after the guitar's peak
    parts.append({"type": "steps", "freq": 1000, "levels_db": levels, "step_seconds": 0.5, "tail": 1})
    period = round((30 - T) / 35 + 0.3 + (1.5 if decay == "5:00" else 0), 1)
    parts.append({"type": "pluck-train", "freq": [110, 220, 440, 880], "onsets": [r(k * period) for k in range(4)],
                  "decay_seconds": 0.248, "seconds": r(4 * period), "repick": True, "tail": 0.5})
    period = round((13 - T) / 12 + 0.3, 1)
    parts.append({"type": "pluck-train", "freq": [110, 440], "onsets": [0, period], "decay_seconds": 0.724,
                  "seconds": r(2 * period), "repick": True, "level_db": -12, "tail": 0.5})
    for d in (6, 1, -4, -9):
        parts.append({"type": "noise", "seconds": 0.15, "seed": 1, "level_db": -6, "tail": 0})
        parts.append({"type": "noise", "seconds": tail, "seed": 2, "level_db": r(T + d), "tail": 0.6})
    return {"type": "sequence", "tail": 0, "parts": parts}


GUITAR = {"type": "sequence", "tail": 0, "parts": [
    {"type": "file", "path": "captures/di/stabs.wav", "seconds": 13, "tail": 1.5},
    {"type": "file", "path": "captures/di/funk.wav", "seconds": 12, "tail": 1.5},
    {"type": "file", "path": "captures/di/notes.wav", "start": 48, "seconds": 22, "tail": 1.5},
    {"type": "file", "path": "captures/di/dynamics.wav", "seconds": 15, "tail": 1.5},
]}

NOTES = {
    0: "The pedal on (its CHECK light lit). Mode REDUCTION. Threshold 12:00, Decay fully down.",
    1: "Decay to 12:00.",
    2: "Threshold fully up.",
    3: "Threshold back to 3:00 (pointer horizontal).",
    4: "Decay fully down.",
    5: "Decay fully up.",
}

plan = {
    "pedal": "Boss NS-2 Noise Suppressor — the gain read directly: the signal into INPUT, a probe into RETURN (mono: OUTPUT)",
    "comment": ("The NS-2's detector listens to INPUT; its gain element sits between RETURN and OUTPUT. The interface's loopback output "
                "plays a steady three-tone probe into RETURN (loop_probe), so OUTPUT is the probe times the gate's gain at every instant, "
                "however quiet the input. Cabling: interface out 3 to INPUT as always; the loopback cable's far end moves from interface "
                "input 5 to the pedal's RETURN (its near end stays in interface out 4); SEND empty; OUTPUT to interface input 2. Each "
                "setting has a take of test tones placed around its threshold and a take of guitar (tools/capture/ns2/make_focused_plan.py "
                "writes this file and says what is in them). Clock positions: 7:00 = fully counter-clockwise, 5:00 = fully clockwise, 3:00 "
                "pointer horizontal. Takes ordered so each step turns one knob."),
    "quiet_dbfs": -60,
    "marker_to_pedal": False,
    "loop_probe": {"freq": [1877, 2341, 3163], "level_db": -12},
    "knobs": ["Threshold", "Decay", "Mode"],
    "takes": [{
        "name": "bypass-probe", "bypass": True,
        "signal": {"type": "sequence", "tail": 0, "parts": [{"type": "silence", "seconds": 2, "tail": 0},
                                                           {"type": "sine", "freq": 1000, "seconds": 3, "tail": 1}]},
        "note": ("The pedal off (its CHECK light off): the probe's reference. Cabling for the whole session: interface out 3 to INPUT; "
                 "unplug the loopback cable from interface input 5 and plug that end into the pedal's RETURN (the other end stays in "
                 "interface out 4); nothing in SEND; OUTPUT to interface input 2; the 9 V adapter. You will hear a steady three-note "
                 "tone: that is the probe. With the pedal on it pumps as the gate works."),
    }],
}
for i, (threshold, decay) in enumerate([("12:00", "7:00"), ("12:00", "12:00"), ("5:00", "12:00"), ("3:00", "12:00"), ("3:00", "7:00"), ("3:00", "5:00")]):
    name = f"threshold-{threshold.replace(':', '').zfill(4)}-decay-{decay.replace(':', '').zfill(4)}"
    settings = {"Threshold": threshold, "Decay": decay, "Mode": "REDUCTION"}
    plan["takes"].append({"name": f"{name}-tones", "settings": settings, "signal": tones(threshold, decay), "note": NOTES[i]})
    plan["takes"].append({"name": f"{name}-guitar", "settings": settings, "signal": GUITAR,
                          "note": "Leave every control in place: the guitar at the same setting."})
path = pathlib.Path(__file__).resolve().parents[1] / "plans" / "ns-2-focused.json"
path.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
print(path)
