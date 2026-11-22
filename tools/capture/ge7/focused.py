"""The GE-7 focused reamp against NAM-X's Equalizer (the GE-7 model): NAM-X's own processor rendered
from each take's played signal at the take's sliders (render_ge7), through the bypass take's chain,
scored against the pedal with the scorecard (tools/capture/scorecard.py). The repeat row is the
pedal against itself.

  ge7/focused.py [SESSION]   (default captures/boss/ge-7/2026-11-09-focused)

Run from the repository root, with build-ge7/render_ge7 built (the command in
tools/capture/render_ge7.cpp's header)."""
import json, pathlib, subprocess, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import scorecard

SESSION = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "captures/boss/ge-7/2026-11-09-focused")
REF_NAMX_DBFS = -6.0
SLIDERS = ["100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz", "Level"]


def render(played, meta):
    scale = 10 ** ((REF_NAMX_DBFS - meta["ref_dbfs"]) / 20)
    y = subprocess.run(["build-ge7/render_ge7"] + [meta["settings"][s] for s in SLIDERS],
                       input=(played * scale).astype(np.float32).tobytes(), capture_output=True, check=True).stdout
    return np.frombuffer(y, dtype=np.float32).astype(np.float64) / scale


def main():
    _, played, pedal, _ = scorecard.load(SESSION, "bypass-chords")
    h = scorecard.chain_filter(played, pedal)
    takes = sorted((json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"),
                   key=lambda m: m["recorded_at"])
    rows = []
    for meta in takes:
        name = meta["take"]
        if meta["bypass"]:
            continue
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        model = scorecard.through(h, render(played, meta))
        rows.append((name, scorecard.score(pedal, model, pre, meta["signal_start"])))
    meta, _, first, pre = scorecard.load(SESSION, "mid-boost-funk")
    _, _, second, _ = scorecard.load(SESSION, "mid-boost-funk-repeat")
    n = min(len(first), len(second))
    repeat = ("pedal twice (the floor)", scorecard.score(first[:n], second[:n], pre, meta["signal_start"]))
    print(scorecard.HEADER)
    for name, s in rows + [repeat]:
        print(scorecard.row(name, s))
    print()
    print(scorecard.tone_rows(rows + [repeat], step=2))


if __name__ == "__main__":
    main()
