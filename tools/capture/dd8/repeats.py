"""The DD-8's repeats on guitar, one by one: for each stab of a take (a short chord, then silence),
the level of its 1st, 2nd and 3rd repeat in third-octave bands re the stab itself, on the pedal and
on NAM-X's model (rendered as in dd8/focused.py). The stab's own level is the dry's, the same on
both sides, so the difference model - pedal is the wet's alone: the first repeat's is the mode's
tone and level, each further one adds a pass round the loop.

  dd8/repeats.py [SESSION] [TAKE…]   (default: every stabs take of captures/boss/dd-8/2026-11-09-focused)

Bands where a repeat isn't 6 dB over the take's floor are left blank."""
import json, pathlib, sys
import numpy as np, scipy.signal
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scorecard, focused as F

BANDS = [100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300]
BANK = [scipy.signal.butter(3, [b / 2 ** (1 / 3), b * 2 ** (1 / 3)], "bandpass", fs=F.RATE, output="sos") for b in BANDS]
WINDOW = int(0.30 * F.RATE)  # a stab and its ring, or one repeat of it (the delay is longer)


def stab_onsets(played, meta):
    a = meta["signal_start"]
    body = played[a:a + int(meta["signal"]["seconds"] * F.RATE)]
    env = scipy.signal.sosfiltfilt(scipy.signal.butter(2, 30, fs=F.RATE, output="sos"), np.abs(body))
    loud = env > 0.25 * env.max()
    starts = np.flatnonzero(loud[1:] & ~loud[:-1])
    return [a + s - int(0.01 * F.RATE) for i, s in enumerate(starts) if i == 0 or s - starts[i - 1] > F.RATE]


def band_power(x, start):
    seg = x[start:start + WINDOW]
    return np.array([np.mean(scipy.signal.sosfilt(sos, np.r_[np.zeros(2400), seg])[2400:] ** 2) for sos in BANK])


def main():
    names = sys.argv[2:]
    _, played, pedal, _ = scorecard.load(F.SESSION, "bypass-stabs")
    h = scorecard.chain_filter(played, pedal)
    takes = sorted((json.loads(p.read_text()) for p in F.SESSION.glob("*stabs*.json")), key=lambda m: m["recorded_at"])
    print("repeat's level re the stab, dB: pedal, then model − pedal, at " + " ".join(f"{b:>5}" for b in BANDS))
    for meta in takes:
        if meta["bypass"] or (names and meta["take"] not in names):
            continue
        meta, played, pedal, pre = scorecard.load(F.SESSION, meta["take"])
        pedal = pedal - np.mean(pre)
        dry_at, _, first_at, _ = F.arrivals(pedal, played, meta)
        period = first_at - dry_at
        if period < WINDOW:
            continue  # the repeats overlap the stab's ring (Time 9:00)
        model = scorecard.through(h, F.render(played, meta, period / F.RATE * 1000))
        floor = band_power(pedal, meta["marker_start"] - WINDOW - 480) if meta["marker_start"] > WINDOW + 480 else \
            np.array([np.mean(scipy.signal.sosfilt(s, pre - np.mean(pre))[2400:] ** 2) for s in BANK])
        onsets = [o for o in stab_onsets(played, meta) if o + 4 * period + WINDOW < len(pedal)]
        # A stab counts only if the gap after it is clear of the next stab for the repeats read.
        print(f"{meta['take']}  (Level {meta['settings']['Level']}, Feedback {meta['settings']['Feedback']}, "
              f"{period / F.RATE * 1000:.0f} ms, {len(onsets)} stabs)")
        for k in (1, 2, 3):
            usable = [o for i, o in enumerate(onsets)
                      if i + 1 == len(onsets) or onsets[i + 1] - o > k * period + WINDOW + F.RATE // 20]
            if not usable:
                continue
            p_stab = np.mean([band_power(pedal, o) for o in usable], axis=0)
            p_rep = np.mean([band_power(pedal, o + k * period) for o in usable], axis=0)
            m_stab = np.mean([band_power(model, o) for o in usable], axis=0)
            m_rep = np.mean([band_power(model, o + k * period) for o in usable], axis=0)
            seen = p_rep > 4 * floor
            ped = F.db(p_rep / p_stab)
            diff = F.db(m_rep / m_stab) - ped
            print(f"   repeat {k}  pedal " + " ".join(f"{v:+5.1f}" if s else "     " for v, s in zip(ped, seen)))
            print(f"         model−pedal " + " ".join(f"{v:+5.1f}" if s else "     " for v, s in zip(diff, seen)))


if __name__ == "__main__":
    main()
