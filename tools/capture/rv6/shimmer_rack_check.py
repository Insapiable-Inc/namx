"""Compare the shipping ReverbProcessor with the 20 guitar excerpts and the pedal's stereo sweep."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile

import focused
import heard
import shimmer_structure as study
import takes

RATE = takes.RATE
FIT = takes.SESSION / "fit/shimmer-prototype.json"


def render(binary, x, start, stop, knobs, offset, phase=0.):
    # Keep more than the longest complete path before the excerpt. Pre-roll silence
    # sets the free-running clock to the take's start phase within half a native sample.
    at = max(0, start - 14*RATE)
    pad = round(((phase + study.CLOCK*at/RATE) % 1) * RATE / study.CLOCK)
    fed = np.pad(x[at:stop].astype(np.float32), (pad, 0))
    raw = subprocess.run([str(binary), "5", *map(str, knobs)], input=fed.tobytes(),
                         stdout=subprocess.PIPE, check=True).stdout
    wet = np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).astype(float) - fed[:, None]
    dry_a, dry_b, dry_offset = takes.dry_filters()
    wet = np.column_stack([takes.scorecard.shift(takes.through(h, ch), offset - dry_offset)
                           for h, ch in zip((dry_a, dry_b), wet.T, strict=True)])
    return wet[pad+start-at:]


def check(binary, output, positions):
    previous = json.loads(FIT.read_text())['guitar']
    report = {}
    for position in positions or previous:
        name = position + '-shimmer'
        meta, x, _, _, noise = takes.load(name)
        knobs = [takes.clock_fraction(meta['settings'][key]) for key in ('Time', 'Tone', 'E.Level')]
        x = x.copy()
        x[:meta['signal_start']] = 0
        pedal = takes.wet(name)
        special = position in ('time-0700', 'noon-guitar')
        if special:
            if position == 'noon-guitar':
                ref = study.excerpts(takes.load('noon-shimmer')[0], takes.wet('noon-shimmer'))
            else:
                ref = study.excerpts(meta, pedal)
            sweep = study.tapered(ref['sweep'])
        floor = np.array([np.mean(focused.octave(noise-noise.mean(), f)[4800:]**2) for f in heard.BANDS])
        for part, start, body, tail in focused.parts_of(meta):
            if part['type'] != 'file':
                continue
            clip = Path(part['path']).stem
            pre = RATE//2
            left, right = start-pre, min(len(x), start+body+tail)
            wet = render(binary, x, left, right, knobs, meta['offset'])
            target = np.column_stack([side[left:right] for side in pedal])
            powers = np.array([[heard.levels(v.T, pre, len(v)-pre, f).mean() for f in heard.BANDS]
                               for v in (target, wet)])
            errors = 10*np.log10((powers[1]+floor) / np.maximum(powers[0], 1e-30))
            row = report[f'{position}/{clip}'] = {
                'old_band_rms_dB': previous[position][clip]['current']['band_rms_dB'],
                'prototype_band_rms_dB': previous[position][clip]['prototype']['band_rms_dB'],
                'rack_band_rms_dB': float(np.mean(errors**2)**.5),
            }
            if special and clip == 'stabs':
                release = []
                for phase in (0., .25, .5, .75):
                    trial = wet[pre:pre+study.FFT_SIZE] if phase == 0 else render(
                        binary, x, start, min(right, start+study.FFT_SIZE), knobs, meta['offset'], phase)
                    release.append(study.release_test(sweep, trial, body)['window_error_pct'][0])
                row['release_vs_pedal_sweep_pct'] = release
                row['pedal_release_vs_pedal_sweep_pct'] = previous[position][clip]['pedal']['release_vs_pedal_sweep_pct']
                dry = np.column_stack([ch[left:right] for ch in takes.dry_of(name)])
                excerpt = slice(pre+body-round(.4*RATE), pre+body+round(2.4*RATE))
                demo = np.vstack([(dry+target)[excerpt], np.zeros((RATE//3, 2)), (dry+wet)[excerpt]])
                soundfile.write(output/f'{position}-pedal-rack.wav', demo*(.89/abs(demo).max()),
                                RATE, subtype='PCM_24')
            print(position, clip, round(row['rack_band_rms_dB'], 3), flush=True)
        takes.load.cache_clear()
    (output/'rack-scores.json').write_text(json.dumps(report, indent=2)+'\n')
    for name in ('old_band_rms_dB', 'prototype_band_rms_dB', 'rack_band_rms_dB'):
        print(name, np.median([row[name] for row in report.values()]))
    for name, row in report.items():
        if 'release_vs_pedal_sweep_pct' in row:
            print(name, 'release %', np.round(row['release_vs_pedal_sweep_pct'], 2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, default=Path('build-release/render_rv6'))
    parser.add_argument('--output', type=Path, default=Path('build-rv6/shimmer-model/rack'))
    parser.add_argument('positions', nargs='*')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check(args.renderer, args.output, args.positions)
