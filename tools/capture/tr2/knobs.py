"""Were the knobs where the plan says? For each setting of the TR-2 focused reamp, the Depth and Wave
positions (% of the travel) that bring NAM-X's model closest to the pedal's gain cycle on the
setting's probe-alone take. A small move that removes the error = the hand on the knob (or the
model's knob map between its measured positions), not the model's shape; what stays is the shape's.

  tr2/knobs.py [SESSION]   (default captures/boss/tr-2/2026-11-09-focused)"""
import json, sys, pathlib
import numpy as np, scipy.optimize
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scorecard, focused as F


def main():
    meta, played, pedal, _ = scorecard.load(F.SESSION, "bypass-chords")
    a, b = F.body(meta)
    unity = np.median(F.line(pedal)[a:b] / F.line(played)[a:b])
    print(f"{'take':40s} planned Depth Wave │ fitted Depth  Wave │ rms as planned → fitted (max)   units of full gain")
    for path in sorted(F.SESSION.glob("*-probe.json")):
        meta, played, pedal, _ = scorecard.load(F.SESSION, path.stem)
        a, b = F.body(meta)
        probe_in = F.line(played)[a:b]
        g = F.line(pedal)[a:b] / probe_in / unity
        period = F.period_of(g)
        cycle = F.fold(g, period)
        planned = np.array([F.PCT[meta["settings"]["Depth"]], F.PCT[meta["settings"]["Wave"]]])

        def error(knobs):
            F.PCT["fit-depth"], F.PCT["fit-wave"] = knobs
            m = dict(meta, settings={"Depth": "fit-depth", "Wave": "fit-wave"})
            gm = F.line(F.render(played, m, F.RATE / period))[a:b] / probe_in
            return F.align(cycle, F.fold(gm, period))[0] - cycle

        # Steps relative to the value: the unknowns are the positions plus 10.
        fit = scipy.optimize.least_squares(lambda u: error(u - 10), planned + 10, bounds=(10, 110), diff_step=0.01)
        before, after = error(planned), error(fit.x - 10)
        print(f"{path.stem:40s}        {planned[0]:5.0f} {planned[1]:4.0f} │      {fit.x[0] - 10:6.1f} {fit.x[1] - 10:5.1f} │ "
              f"{np.sqrt(np.mean(before ** 2)):.4f} → {np.sqrt(np.mean(after ** 2)):.4f} ({np.abs(after).max():.4f})")


if __name__ == "__main__":
    main()
