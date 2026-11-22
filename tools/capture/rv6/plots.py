#!/usr/bin/env python3
"""The RV-6 focused session's plots, one page per mode, from rv6/focused.py's JSON
(build-rv6/focused-SESSION.json) -> SESSION/fit/focused-MODE.png. Pedal black, model red.

  row 1  the 1 kHz octave after the noise burst at Time 7:00, 9:00, 12:00, 3:00, 5:00
  row 2  at noon: the 250 Hz and 4 kHz octaves; RT60 per octave at every Time; the wet's
         third-octave spectrum at Tone 7:00, 12:00, 3:00, 5:00 re the pedal's at noon (pedal solid,
         model dashed; the 9:00 block was recorded at 12:00); model - pedal per third octave; the wet's level, model - pedal, against
         E.Level (bursts) and on guitar

  rv6/plots.py    (from the repository root)"""
import json, pathlib, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import focused, takes

POSITIONS = ["7:00", "9:00", "12:00", "3:00", "5:00"]
COLOURS = ["#1b4f9c", "#3f9bd6", "#555555", "#e08a2c", "#b5281c"]
tag = lambda position: position.replace(":", "").zfill(4)


def main():
    results = json.loads(focused.OUT.read_text())
    out = takes.SESSION / "fit"
    out.mkdir(exist_ok=True)
    thirds = np.array(focused.THIRDS)
    for mode in takes.MODES:
        m = mode.lower()
        take = lambda knob, position: results["noon-" + m if position == "12:00" else f"{knob}-{tag(position)}-{m}"]
        spectrum = "burst" if mode == "Shimmer" else "sweep"
        fig, axes = plt.subplots(2, 5, figsize=(24, 9))
        fig.suptitle(f"RV-6 {mode}: the pedal (black) and NAM-X's Reverb (red), the wet alone, {takes.SESSION.name}")
        for ax, position in zip(axes[0], POSITIONS):
            burst = take("time", position)["burst"]
            p, q = np.array(burst["envelope_pedal"]["1000"]), np.array(burst["envelope_model"]["1000"])
            t = np.arange(len(p)) * 0.02
            ax.plot(t, p, "k", lw=1)
            ax.plot(t, q, "r", lw=1, alpha=0.8)
            rt = burst["decay"]["1000"]
            ax.set_title(f"Time {position}: 1 kHz octave after the burst" + (f" (RT60 {rt[0]:.2f} / {rt[1]:.2f} s)" if all(rt) else ""), fontsize=9)
            ax.set_ylim(max(p.max(), q.max()) - 75, max(p.max(), q.max()) + 3)
            ax.set_xlabel("s")
            ax.set_ylabel("dBFS")
            ax.grid(alpha=0.3)
        ax = axes[1][0]
        burst = take("time", "12:00")["burst"]
        for band, style in (("250", "-"), ("4000", ":")):
            p, q = np.array(burst["envelope_pedal"][band]), np.array(burst["envelope_model"][band])
            t = np.arange(len(p)) * 0.02
            ax.plot(t, p, "k" + style, lw=1, label=f"pedal {band} Hz")
            ax.plot(t, q, "r" + style, lw=1, label=f"model {band} Hz")
        ax.set_ylim(np.max(burst["envelope_pedal"]["250"]) - 75, np.max(burst["envelope_pedal"]["250"]) + 6)
        ax.set_title("noon: 250 Hz and 4 kHz octaves after the burst", fontsize=9)
        ax.legend(fontsize=7)
        ax.grid(alpha=0.3)
        ax = axes[1][1]
        for position, colour in zip(POSITIONS, COLOURS):
            decay = take("time", position)["burst"]["decay"]
            bands = [b for b in decay if all(decay[b])]
            ax.plot([int(b) for b in bands], [decay[b][0] for b in bands], "-o", color=colour, ms=3, label=f"Time {position}")
            ax.plot([int(b) for b in bands], [decay[b][1] for b in bands], "--x", color=colour, ms=4)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_title("RT60 per octave (pedal solid, model dashed)", fontsize=9)
        ax.set_xlabel("Hz")
        ax.set_ylabel("s")
        ax.legend(fontsize=7)
        ax.grid(alpha=0.3, which="both")
        # Shimmer's noon take has other bursts (three levels, shorter gaps): its Tone curves are
        # drawn re the pedal's at 5:00, without noon. Tone 9:00 is left out everywhere: that
        # block was recorded with Tone at 12:00.
        shown = [(p, c) for p, c in zip(POSITIONS, COLOURS) if p != "9:00" and (mode != "Shimmer" or p != "12:00")]
        noon = np.array(take("tone", "5:00" if mode == "Shimmer" else "12:00")[spectrum]["pedal_thirds_db"])
        for position, colour in shown:
            r = take("tone", position)[spectrum]
            p, q = np.array(r["pedal_thirds_db"]), np.array(r["model_thirds_db"])
            axes[1][2].plot(thirds, p - noon, "-", color=colour, label=f"Tone {position}")
            axes[1][2].plot(thirds, q - noon, "--", color=colour)
            axes[1][3].plot(thirds, q - p, "-", color=colour, label=f"Tone {position}")
        for ax, title in ((axes[1][2], f"the wet's spectrum re the pedal's at {'Tone 5:00' if mode == 'Shimmer' else 'noon'} ({spectrum}; model dashed)"),
                          (axes[1][3], "model - pedal per third octave")):
            ax.set_xscale("log")
            ax.set_title(title, fontsize=9)
            ax.set_xlabel("Hz")
            ax.set_ylabel("dB")
            ax.legend(fontsize=7)
            ax.grid(alpha=0.3, which="both")
        axes[1][3].set_ylim(-12, 12)
        ax = axes[1][4]
        x = np.arange(1, 5)
        names = [f"level-0900-{m}", "noon-" + m, f"level-0300-{m}", f"level-0500-{m}"]
        ax.plot(x, [results[n]["burst"]["level"] for n in names], "-o", color="k", label="bursts")
        ax.plot(x, [results[n if "guitar" in results[n] else f"noon-guitar-{m}"]["guitar"]["level"] for n in names], "-s", color="#3f9bd6", label="guitar")
        ax.set_xticks(x, ["9:00", "12:00", "3:00", "5:00"])
        ax.axhline(0, color="gray", lw=0.5)
        ax.set_ylim(-6, 6)
        ax.set_title("the wet's level, model - pedal, against E.Level", fontsize=9)
        ax.set_ylabel("dB")
        ax.legend(fontsize=7)
        ax.grid(alpha=0.3)
        fig.tight_layout()
        fig.savefig(out / f"focused-{m}.png", dpi=80)
        plt.close(fig)
        print(out / f"focused-{m}.png")


if __name__ == "__main__":
    main()
