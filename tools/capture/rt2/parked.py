"""The RT-2's Drive read directly: the focused session of 2026-11-21 (Mode III, rotors parked with Slow at 7:00).

A parked rotor is a fixed filter, so each take is  recording = H · Drive(played). H comes from the take's quiet
sweep at Drive 7:00 (deconvolved, its harmonic products windowed out); dividing a recording by it leaves the
Drive's own output, in the scale where the quiet sweep's gain is 1:

  parked.py            the Drive's output at Drive 7:00 and 9:00 (the drum-alone take under ~1.1 kHz, the horn-alone
                       one above), then the both-rotor takes (Balance 12:00 as a mix of the two responses, read at
                       Drive 9:00): build-rt2/drive3/n-<drive>.npy and nb-<drive>.npy, rows (played, Drive output)

Run from the repository root. The other drive3_*.py scripts import this one. RT2_MODE=1 or 2 reads the same day's session
for Modes I and II instead (its m1- and m2- takes, build-rt2/drive1 and drive2): drive12.py uses it that way."""
import json, os, pathlib, sys
import numpy as np, soundfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import signals

MODE = int(os.environ.get("RT2_MODE", "3"))   # 1, 2: the session of the same day for Modes I and II (its m1-, m2- takes)
TAG = f"m{MODE}"
S = pathlib.Path("captures/boss/rt-2/2026-11-21-focused" + ("" if MODE == 3 else "-m12"))
OUT = pathlib.Path(f"build-rt2/drive{MODE}")
RATE = 48000
UNIT = 10 ** (-36 / 20)   # the ladder's quietest step re the guitar's peak: the fits' unit


def load(name):
    """meta, played, Output A, Output B, loopback: the recording aligned to the played signal."""
    meta = json.loads((S / f"{name}.json").read_text())
    rec, _ = soundfile.read(S / f"{name}.wav", dtype="float64")
    played, _ = soundfile.read(S / "played" / f"{name}.wav", dtype="float64")
    lat = meta["latency_samples"]
    n = min(len(played), len(rec) - lat)
    return meta, played[:n], rec[lat:lat + n, 0], rec[lat:lat + n, 2], rec[lat:lat + n, 1]


def parts(meta):
    """[(part's spec, its first sample)] of a sequence take."""
    spec = meta["signal"]
    return [(p, meta["signal_start"] + int(round(s * RATE))) for p, s in zip(spec["parts"], signals.part_starts(spec))]


def tone(x, f, k=1):
    """The complex amplitude of harmonic k of f in x."""
    t = np.arange(len(x)) / RATE
    w = np.hanning(len(x))
    return 2 * np.sum(x * w * np.exp(-2j * np.pi * k * f * t)) / np.sum(w)


def sweep_response(name, ch="a", pre_ms=2.0, post_ms=150.0):
    """The take's quiet sweep deconvolved: (freqs, H), the harmonic products (they land before the response) cut."""
    meta, played, a, b, loop = load(name)
    y = {"a": a, "b": b}[ch]
    part, start = parts(meta)[0]
    n = int((part["seconds"] + 0.4) * RATE)
    N = 1 << int(np.ceil(np.log2(n)))
    X = np.fft.rfft(played[start:start + n], N)
    Y = np.fft.rfft(y[start:start + n], N)
    f = np.fft.rfftfreq(N, 1 / RATE)
    raw = np.where((f > 30) & (f < 17000), Y * np.conj(X) / (np.abs(X) ** 2 + 1e-9 * np.max(np.abs(X) ** 2)), 0)
    ir = np.fft.irfft(raw, N)
    pre, post = int(pre_ms * RATE / 1000), int(post_ms * RATE / 1000)
    w = np.zeros(N)
    w[:post] = 1
    w[N - pre:] = 1
    fade = np.hanning(2 * (post // 4))[post // 4:]
    w[post - len(fade):post] = fade
    return f, np.fft.rfft(ir * w)


def responses():
    """Each parked rotor's response per output, from the Drive 7:00 sweeps."""
    return {(rotor, ch): sweep_response(f"{TAG}-{rotor}-drive0700", ch) for rotor in ("drum", "horn") for ch in "ab"}


def at(response, f):
    fh, h = response
    return np.interp(f, fh, h.real) + 1j * np.interp(f, fh, h.imag)


def through(n, mix, ch, R):
    """A Drive output through the parked rotors: mix = (horn gain, drum gain)."""
    N = 1 << int(np.ceil(np.log2(len(n))))
    f = np.fft.rfftfreq(N, 1 / RATE)
    H = sum(g * at(R[rotor, ch], f) for rotor, g in zip(("horn", "drum"), mix) if g)
    return np.fft.irfft(np.fft.rfft(n, N) * H, N)[:len(n)]


def invert(drive, R, split=(900.0, 1400.0)):
    """The Drive's output from the drum-alone and horn-alone takes at one Drive position."""
    est = {}
    for rotor in ("drum", "horn"):
        meta, played, a, b, loop = load(f"{TAG}-{rotor}-drive{drive}")
        n = len(played)
        N = 1 << int(np.ceil(np.log2(n)))
        f = np.fft.rfftfreq(N, 1 / RATE)
        num = den = 0
        for ch, y in (("a", a), ("b", b)):
            Hi = at(R[rotor, ch], f)
            num = num + np.fft.rfft(y, N) * np.conj(Hi)
            den = den + np.abs(Hi) ** 2
        est[rotor] = num / (den + 1e-4 * np.max(den))
    lo, hi = split
    w = np.sin(0.5 * np.pi * np.clip(np.log(np.maximum(f, 1) / lo) / np.log(hi / lo), 0, 1)) ** 2
    Nf = (1 - w) * est["drum"] + w * est["horn"]
    Nf[(f < 40) | (f > 15500)] = 0
    return played, np.fft.irfft(Nf, N)[:n]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    R = responses()
    for drive in ("0700", "0900"):
        played, n = invert(drive, R)
        np.save(OUT / f"n-{drive}.npy", np.stack([played, n]))
    # Balance 12:00: the mix of the two rotors, where the Drive's output is known
    meta, played, a, b, loop = load(f"{TAG}-both-drive0900")
    n9 = np.load(OUT / "n-0900.npy")[1][:len(played)]
    N = 1 << int(np.ceil(np.log2(len(played))))
    f = np.fft.rfftfreq(N, 1 / RATE)
    ok = (f > 60) & (f < 12000)
    N9 = np.fft.rfft(n9, N)
    mix = {}
    for ch, y in (("a", a), ("b", b)):
        Y = np.fft.rfft(y, N)[ok]
        A = np.stack([at(R["horn", ch], f)[ok] * N9[ok], at(R["drum", ch], f)[ok] * N9[ok]], 1)
        mix[ch], *_ = np.linalg.lstsq(np.concatenate([A.real, A.imag]), np.concatenate([Y.real, Y.imag]), rcond=None)
        miss = 10 * np.log10(np.sum(np.abs(A @ mix[ch] - Y) ** 2) / np.sum(np.abs(Y) ** 2))
        print(f"Balance 12:00, Output {ch.upper()}: horn {20 * np.log10(mix[ch][0]):+.2f} dB, drum {20 * np.log10(mix[ch][1]):+.2f} dB "
              f"re each alone; the both-rotor take explained to {miss:.1f} dB")
    json.dump({ch: list(map(float, v)) for ch, v in mix.items()}, open(OUT / "mix.json", "w"))
    for name in sorted(p.stem for p in S.glob(f"{TAG}-both-drive*.json")):
        meta, played, a, b, loop = load(name)
        N = 1 << int(np.ceil(np.log2(len(played))))
        f = np.fft.rfftfreq(N, 1 / RATE)
        num = den = 0
        for ch, y in (("a", a), ("b", b)):
            Hb = mix[ch][0] * at(R["horn", ch], f) + mix[ch][1] * at(R["drum", ch], f)
            num = num + np.fft.rfft(y, N) * np.conj(Hb)
            den = den + np.abs(Hb) ** 2
        Nf = num / (den + 1e-4 * np.max(den))
        Nf[(f < 40) | (f > 15500)] = 0
        out = np.fft.irfft(Nf, N)[:len(played)]
        np.save(OUT / f"nb-{name.split('drive')[1]}.npy", np.stack([played, out]))
        for drive in ("0700", "0900"):
            if name.endswith(drive):
                ref = np.load(OUT / f"n-{drive}.npy")[1]
                ref, out = ref[:min(len(ref), len(out))], out[:min(len(ref), len(out))]   # the takes' latencies differ
                print(f"Drive {drive}: the both-rotor take's Drive output against the two rotors' own: residual "
                      f"{10 * np.log10(np.mean((out - ref) ** 2) / np.mean(ref ** 2)):.1f} dB")


if __name__ == "__main__":
    main()
