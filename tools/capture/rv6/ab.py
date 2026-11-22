#!/usr/bin/env python3
"""Listening files for the RV-6's focused session: the pedal as recorded beside NAM-X's Reverb at
the take's settings, both outputs (stereo), for the guitar's stabs of a list of takes.

  SESSION/ab/<take>.pedal.wav   the recording (outputs A and B, slips undone)
  SESSION/ab/<take>.now.wav     NAM-X as built (build-rv6/render_rv6): the take's own dry as the
                                pedal's outputs carry it plus NAM-X's wet through the same chain
  SESSION/ab/<take>.was.wav     the same from another build (--was BINARY), for a before and after

24-bit, 48 kHz, git-ignored. A take's files start half a second before the stabs, run to the end of
their tail and share one gain (the loudest peak of the three at -1 dBFS), so levels compare within a
take. The wet is a different tank from the pedal's: what should match is the level, the colour, the
length and the feel of the tail, not the waveform.

  rv6/ab.py [--was BINARY] [TAKE ...]     (default: TAKES)

Run from the repository root."""
import pathlib, sys
import numpy as np, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import focused, takes

RATE = takes.RATE
# One per fix and a plain one per mode: E.Level 9:00, Tone 7:00 (Plate's bass, Modulate's without),
# Time 7:00 (Hall's swell), Shimmer at noon, at Time 3:00 and with the octave off.
TAKES = ["level-0900-plate", "level-0900-room", "tone-0700-plate", "tone-0700-hall", "tone-0700-modulate",
         "tone-0700-spring", "time-0700-hall", "time-0900-hall", "time-0700-room", "noon-guitar-hall",
         "noon-guitar-shimmer", "time-0300-shimmer", "time-0700-shimmer", "tone-0700-shimmer", "tone-0500-shimmer",
         "check-a-plate", "check-a-shimmer", "check-b-hall"]


def stabs_span(meta):
    for part, start, body, tail in focused.parts_of(meta):
        if part["type"] == "file" and "stabs" in part["path"]:
            return start - RATE // 2, start + body + tail
    raise ValueError("no stabs in this take")


def main():
    arguments = sys.argv[1:]
    was = None
    if "--was" in arguments:
        at = arguments.index("--was")
        was = arguments[at + 1]
        del arguments[at:at + 2]
    folder = takes.SESSION / "ab"
    folder.mkdir(exist_ok=True)
    for name in arguments or TAKES:
        meta, played, a, b, _ = takes.load(name)
        lo, hi = stabs_span(meta)
        dry = takes.dry_of(name)
        files = {"pedal": np.stack([a, b], axis=1)[lo:hi]}
        now = takes.model_wet(name)
        files["now"] = np.stack([dry[0] + now[0], dry[1] + now[1]], axis=1)[lo:hi]
        if was:
            takes.RENDER = was
            old = takes.model_wet(name)
            takes.RENDER = "build-rv6/render_rv6"
            files["was"] = np.stack([dry[0] + old[0], dry[1] + old[1]], axis=1)[lo:hi]
        files = {k: v - v.mean(axis=0) for k, v in files.items()}
        gain = 10 ** (-1 / 20) / max(np.max(np.abs(v)) for v in files.values())
        for kind, audio in files.items():
            soundfile.write(folder / f"{name}.{kind}.wav", gain * audio, RATE, subtype="PCM_24")
        s = meta["settings"]
        print(f"{name}: {s['Mode']}, E.Level {s['E.Level']}, Tone {s['Tone']}, Time {s['Time']}; {(hi - lo) / RATE:.1f} s")
    print(f"in {folder}/")


if __name__ == "__main__":
    main()
