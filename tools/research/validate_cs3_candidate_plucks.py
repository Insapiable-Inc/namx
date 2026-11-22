"""Check existing 220 Hz picked-note captures with the frozen research candidate.

No pluck-data fit. Uses parameters from the previous detector-check 1 kHz takes.
The 0.1–0.3 s output offset is reported separately; removing it is only a shape
diagnostic. Evaluate four repeated notes, not four independent experiments.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from analyze_cs3_detector import FS, HERE, cycle_envelope, db, load, rms
from fit_cs3_release_candidate import render


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=HERE.parents[1])
    root = parser.parse_args().root.resolve()
    source_bytes = (HERE/'cs3_release_candidate_results.json').read_bytes()
    source = json.loads(source_bytes)
    old = Path('captures/boss/cs-3/2026-10-27')
    commit = source['source_model_commit']
    by = load(root, old, 'bypass-plucks', commit)[3][:12*FS, 0]
    bypass_env = cycle_envelope(by, 220)
    evaluations, curves = {}, {}
    for attack in ['0700', '0500']:
        name = 'plucks-attack-'+attack
        m, x, _, b = load(root, old, name, commit)
        xn = x.astype(float)/10**(m['ref_dbfs']/20)
        start = m['signal_start']
        inp = cycle_envelope(xn[start:start+12*FS], 220)
        measured = cycle_envelope(b[:12*FS, 0], 220)
        gm = db(measured/bypass_env)
        d = {'xn': xn, 'start': start, 'length': 12*FS, 'frequency': 220, 'input_env': inp}
        evaluations[name] = {}
        for kind, p in source['full_sample_renderer_parameters'][attack].items():
            gp, out = render(d, kind, p['attack_ms'], p['release_ms'], p['asymptotic_gain_db'], p['sustain_db'], p['makeup_db'])
            # Scale predicted output envelope by the measured bypass/input
            # envelope, preserving the previously measured hardware path.
            ep = cycle_envelope(out, 220)/inp*bypass_env
            rows = []
            for note in range(4):
                idx = note*3000
                offset = float(np.median(gp[idx+100:idx+300]-gm[idx+100:idx+300]))
                row = {'note': note+1, 'sustain_offset_db': offset}
                for key, a, z in [('onset', 5, 30), ('sustain', 30, 600), ('tail', 600, 2500)]:
                    error = gp[idx+a:idx+z]-gm[idx+a:idx+z]
                    row[key] = {'absolute_envelope_rms_db': rms(error),
                                'sustain_offset_removed_envelope_rms_db': rms(error-offset)}
                row['measured_pick_spike_db'] = float(np.max(db(measured[idx:idx+30]))-np.median(db(measured[idx+100:idx+300])))
                row['predicted_pick_spike_db'] = float(np.max(db(ep[idx:idx+30]))-np.median(db(ep[idx+100:idx+300])))
                rows.append(row)
            evaluations[name][kind] = rows
            curves[attack, kind] = db(ep)
            print(name, kind, 'offset', [round(v['sustain_offset_db'], 3) for v in rows],
                  'shape RMS sustain/tail', [(round(v['sustain']['sustain_offset_removed_envelope_rms_db'], 3), round(v['tail']['sustain_offset_removed_envelope_rms_db'], 3)) for v in rows],
                  'pick error', [round(v['predicted_pick_spike_db']-v['measured_pick_spike_db'], 3) for v in rows], flush=True)
        curves[attack, 'measured'] = db(measured)
    result = {'candidate_parameters_sha256': hashlib.sha256(source_bytes).hexdigest(),
              'scope': __doc__, 'evaluation': evaluations,
              'limits': '220 Hz decaying sinusoids, not guitar or polyphonic audio. Same session nominal controls but hand-reset between experiments. One-cycle envelope ~4.54 ms, 1 ms output grid. Relative output shape after removing sustain offset is not an absolute match. No phase-aligned waveform NRMSE or subjective listening claim.'}
    (HERE/'cs3_candidate_plucks_results.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 2, figsize=(12, 4), layout='constrained')
    for ax, attack in zip(axes, ['0700', '0500']):
        # Second repeated note avoids emphasizing the marker-conditioned first.
        for kind, color in [('measured', 'black'), ('baseline', '#a65e00'), ('candidate', '#00818a')]:
            v = curves[attack, kind][3000:5500]
            ax.plot(np.arange(2500), v-np.median(v[100:300]), color=color,
                    linestyle='-' if kind == 'measured' else '--', label=kind)
        ax.set(title=f'Attack {attack}: 220 Hz picked note', xlabel='ms after note onset', ylabel='Output relative to 100–300 ms sustain (dB)')
        ax.grid(alpha=.3); ax.legend(fontsize=8)
    fig.suptitle('Existing captures: output envelope shape; each trace sustain-normalized')
    fig.savefig(HERE/'cs3_candidate_plucks.png', dpi=150)


if __name__ == '__main__':
    main()
