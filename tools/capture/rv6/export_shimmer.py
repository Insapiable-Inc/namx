"""Pack the fitted 48 kHz Shimmer filters for the rack. Run after shimmer_prototype.py fit."""
import argparse
import struct
from pathlib import Path

import numpy as np
from scipy import fft, signal, spatial

import shimmer_prototype as model

GRID = 20  # 5% steps, including every calibration position
TAPS = 512
QUANT = 256  # an independent scale preserves the quiet end of each decay


def put_floats(out, values):
    out.write(np.asarray(values, dtype="<f4").tobytes())


def put_filter(out, h):
    out.write(struct.pack("<I", len(h)))
    for start in range(0, len(h), QUANT):
        part = h[start:start+QUANT]
        scale = np.max(np.abs(part)) / 32767.
        put_floats(out, scale)
        out.write((np.rint(part / scale) if scale else part).astype("<i2").tobytes())


def remove_rig(room, dry):
    # The recorded dry FIR is centred on sample 2048. Undo its complex response in the
    # recorded passband. Above the calibrated band, bound the inverse at the dry gain.
    n = fft.next_fast_len(len(room) + 2*len(dry))
    response = fft.rfft(np.roll(np.pad(dry, (0, n-len(dry))), -model.takes.HALF))
    nominal = np.median(abs(response)[(fft.rfftfreq(n, 1/model.RATE) > 100) &
                                      (fft.rfftfreq(n, 1/model.RATE) < 10000)])
    inv = np.conj(response) / np.maximum(abs(response)**2, (.75*nominal)**2)
    return fft.irfft(fft.rfft(room, n)*inv, n)[:len(room)]


def corrections(banks, directory):
    # Per-band Gram matrices reproduce the prototype's common-noise cancellation estimate
    # without storing or remixing the 12 stereo reference recordings for each knob setting.
    ref = np.stack([b['reference'] for b in banks])
    hz = fft.rfftfreq(3*model.RATE, 1/model.RATE)[::8]
    gram = []
    for f in model.BANDS:
        r = ref[:, (hz > f*2**-.25) & (hz <= f*2**.25)].reshape(len(banks), -1)
        gram.append((r @ r.conj().T).real.astype(float))
    gram = np.array(gram)
    low = np.load(directory/'low-tone-dB.npy')
    names = list(model.POSITIONS)
    for tone in np.linspace(0, 1, GRID+1):
        for time in np.linspace(0, 1, GRID+1):
            w = np.zeros(len(names))
            for name, amount in model.anchors(time, tone):
                w[names.index(name)] = amount
            h = np.ones(1)
            if np.count_nonzero(w) > 1:
                gain = (np.sqrt(gram.diagonal(axis1=1, axis2=2)) @ w /
                        np.sqrt(np.einsum('i,bij,j->b', w, gram, w)))
                h = model.gain_filter(gain)
            if 0 < tone < .5:
                weight = np.interp(tone, (0, .2, .5), (0, 1, 0))
                h = np.convolve(h, model.gain_filter(10**(weight*low/20)))
            yield np.pad(h[:TAPS], (0, max(0, TAPS-len(h))))


def export(directory, path):
    names = list(model.POSITIONS)
    coords = np.array(list(model.POSITIONS.values()))
    triangles = spatial.Delaunay(coords).simplices
    banks = [dict(np.load(directory/f'{name}.npz')) for name in names]
    with np.load(directory/'dry.npz') as dry, path.open('wb') as out:
        out.write(b'RV6S\x01\x00\x00\x00')
        out.write(struct.pack('<6I', model.RATE, len(names), len(triangles), GRID, TAPS, QUANT))
        put_floats(out, model.PITCH_LOWPASS)
        put_floats(out, coords)
        out.write(triangles.astype('u1').tobytes())
        for bank in banks:
            early = bank['early']
            out.write(struct.pack('<I', len(early)))
            put_floats(out, bank['pulse'])
            put_floats(out, bank['motion'])
            # Adjacent static filters commute. Motion and octave reads stay between their
            # original filters, so neither can be baked into a static impulse response.
            for h, eq in zip(early, bank['eq']):
                put_filter(out, signal.fftconvolve(h.astype(float), eq.astype(float)))
            put_filter(out, bank['late'])
            for eq in bank['eq'][len(early):]:
                put_filter(out, eq)
            for room, channel in zip(bank['room'].T, ('a', 'b'), strict=True):
                put_filter(out, remove_rig(room, dry[channel]))
        for h in corrections(banks, directory):
            put_floats(out, h)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank', type=Path, default=model.CACHE)
    parser.add_argument('--output', type=Path, default=Path('resources/dsp/rv6-shimmer.bin'))
    args = parser.parse_args()
    export(args.bank, args.output)
