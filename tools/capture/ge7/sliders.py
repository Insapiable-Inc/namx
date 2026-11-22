"""Were the sliders where the plan says? For each setting of the GE-7 focused reamp, the eight slider
positions (printed scale) that bring NAM-X's model closest to the pedal's tone on that setting's
quietest take (no clipping), then every take of the setting scored again with them. Small moves that
remove the error = the hand on the sliders, not the model; what stays is the model's.

  ge7/sliders.py [SESSION]   (default captures/boss/ge-7/2026-11-09-focused; ~5 min)"""
import json, sys, pathlib
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scorecard, focused

SESSION = focused.SESSION
FIT_ON = {"flat": "flat-chords", "level-p15": "level-p15-funk-m24", "level-m15": "level-m15-chords",
          "mid-boost": "mid-boost-chords", "scoop": "scoop-chords", "all-up": "all-up-chords-m24"}


def main():
    _, played, pedal, _ = scorecard.load(SESSION, "bypass-chords")
    h = scorecard.chain_filter(played, pedal)
    names = [json.loads(p.read_text()) for p in SESSION.glob("*.json") if p.name != "rig.json"]
    names = [m["take"] for m in sorted(names, key=lambda m: m["recorded_at"]) if not m["bypass"]]

    def scored(name, sliders=None):
        meta, played, pedal, pre = scorecard.load(SESSION, name)
        if sliders is not None:
            meta = dict(meta, settings=dict(zip(focused.SLIDERS, (f"{v:.4f}" for v in sliders))))
        return scorecard.score(pedal, scorecard.through(h, focused.render(played, meta)), pre, meta["signal_start"])

    print(f"{'take':28s}   as planned: rms residual │ sliders fitted: rms residual")
    for group, fit_take in FIT_ON.items():
        meta = json.loads((SESSION / f"{fit_take}.json").read_text())
        planned = np.array([float(meta["settings"][s]) for s in focused.SLIDERS])

        def error(sliders):
            tone = scored(fit_take, sliders)["tone"]
            return np.nan_to_num(tone)

        # least_squares steps relative to the value: a slider at 0 would never move, so the unknown is
        # the slider's position from the scale's bottom plus one (16 at the centre).
        fit = scipy.optimize.least_squares(lambda u: error(u - 16), planned + 16, bounds=(1, 31), diff_step=0.003,
                                           xtol=1e-4, ftol=1e-5)
        fit.x = fit.x - 16
        print(f"{group}: sliders planned " + " ".join(f"{v:+5.1f}" for v in planned))
        print(f"{'':{len(group)}}          fitted " + " ".join(f"{v:+5.1f}" for v in fit.x))
        for name in names:
            if name == group or name.startswith(group + "-"):
                a, b = scored(name), scored(name, fit.x)
                print(f"  {name:26s}        {a['rms']:6.2f} {a['residual']:7.1f}  │       {b['rms']:6.2f} {b['residual']:7.1f}")


if __name__ == "__main__":
    main()
