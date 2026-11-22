"""Evaluate pre-recording CS-3 crossed-level predictions without fitting dynamics.

python tools/research/analyze_cs3_crossed.py [REPOSITORY]
Outputs JSON, plot and console log in tools/research beside this script.
The three hypotheses use previously frozen effective time constants. They are
numerical extrapolations, not three deployable level-dependent detector laws.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy import signal

from analyze_cs3_detector import (
    FS, HERE, TAU, compress, cycle_envelope, db, load, plateau_gain,
    release_metrics, rms, tone_filter,
)


def score(measured, predicted, down):
    mm = release_metrics(measured, down, 1000)
    pm = release_metrics(predicted, down, 1000)
    sl = slice(down * 1000 + 5, down * 1000 + 1200)
    a = (measured[sl] - mm['start_gain_db']) / mm['excursion_db']
    b = (predicted[sl] - pm['start_gain_db']) / pm['excursion_db']
    return {
        'measured': mm, 'predicted': pm,
        'absolute_gain_rms_db': rms(predicted[sl] - measured[sl]),
        'endpoint_normalized_shape_rms_fraction': rms(b - a),
        'endpoint_normalized_shape_rms_db_equivalent': rms(b - a) * mm['excursion_db'],
        't63_error_ms': pm['t63_ms'] - mm['t63_ms'],
        't90_error_ms': pm['t90_ms'] - mm['t90_ms'],
    }


def fundamental(y, start, end, channel):
    v = y[int(start * FS):int(end * FS), channel]
    return 2 * np.mean(v * np.exp(-2j * np.pi * 1000 * np.arange(len(v)) / FS))


def edge_times(body, channel):
    """Envelope midpoints in linear amplitude, on the same 1 ms analysis grid."""
    env = cycle_envelope(body[:, channel], 1000)
    edges = []
    for k in range(1, 6):
        idx = 4000 * k
        lo = float(np.median(env[idx-200:idx-50]))
        hi = float(np.median(env[idx+50:idx+200]))
        v = (env[idx-10:idx+11] - lo) / (hi - lo)
        hits = np.flatnonzero(v >= .5)
        edges.append(int(hits[0] - 10) if len(hits) else None)
    return edges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=HERE.parents[1])
    root = parser.parse_args().root.resolve()
    new = Path('captures/boss/cs-3/2026-10-30-detector-crossed')
    old = Path('captures/boss/cs-3/2026-10-30-detector-check')
    protocol_bytes = (root/new/'analysis-protocol.json').read_bytes()
    protocol = json.loads(protocol_bytes)
    previous_bytes = (root/protocol['source_results']).read_bytes()
    assert hashlib.sha256(previous_bytes).hexdigest() == protocol['source_results_sha256']
    previous = json.loads(previous_bytes)
    commit = protocol['source_model_commit']
    names = ['bypass-crossed-1000', 'attack-0700-crossed-1000']
    captures = {n: load(root, new, n, commit) for n in names}
    prior_names = ['bypass-steps-1000', 'attack-0700-steps-1000', 'attack-0500-steps-1000']
    prior = {n: load(root, old, n, commit) for n in prior_names}
    integrity = {}
    for name, (m, x, y, body) in captures.items():
        assert m['signal']['levels_db'] == [-36, -6, -36, -24, -12, -24]
        assert m['signal']['freq'] == 1000 and m['signal']['step_seconds'] == 4
        integrity[name] = {
            'recorded_at': m['recorded_at'], 'settings': m['settings'],
            'xruns': m['xruns'], 'latency_samples': m['latency_samples'],
            'marker_quality': m['latency_quality'],
            'played_sha256': m['played_sha256'],
            'recorded_sha256': hashlib.sha256((root/new/(name+'.wav')).read_bytes()).hexdigest(),
            'abs_ge_0999': int(np.sum(np.abs(y) >= .999)),
            'tail_rms_dbfs': db(np.sqrt(np.mean(y[-FS//2:]**2, axis=0))).tolist(),
            'aligned_loop_edge_offsets_ms': edge_times(body, 1),
        }
    by = captures[names[0]][3]
    m, x, y, body = captures[names[1]]
    assert m['settings'] == {'Level': '12:00', 'Tone': '12:00', 'Attack': '7:00', 'Sustain': '12:00'}
    length = 24*FS
    measured = db(cycle_envelope(body[:length, 0], 1000) / cycle_envelope(by[:length, 0], 1000))
    levels = m['signal']['levels_db']
    loop_checks = []
    for k in range(6):
        ratio = fundamental(body, 4*k+2.5, 4*k+3.5, 1)/fundamental(by, 4*k+2.5, 4*k+3.5, 1)
        loop_checks.append({
            'input_db': levels[k], 'gain_change_db': float(db(ratio)),
            'phase_equivalent_delay_samples': float(-np.angle(ratio)*FS/(2*np.pi*1000)),
        })
    # Same-level comparisons, using long steady windows, not unlike-level samples.
    bypass_checks = []
    for k in range(6):
        om, _, _, ob = prior['bypass-steps-1000']
        old_k = om['signal']['levels_db'].index(levels[k])
        channels = {}
        for c in [0, 1]:
            ratio = fundamental(by, 4*k+2.5, 4*k+3.5, c)/fundamental(ob, 4*old_k+2.5, 4*old_k+3.5, c)
            channels['pedal' if c == 0 else 'loop'] = {'gain_change_db': float(db(ratio)),
                'phase_equivalent_delay_samples': float(-np.angle(ratio)*FS/(2*np.pi*1000))}
        bypass_checks.append({'input_db': levels[k], 'channels': channels})
    plateaus = [plateau_gain(measured, k) for k in range(6)]
    gain_repeat = []
    for k in range(6):
        row = {'input_db': levels[k], 'crossed_gain_db': plateaus[k]}
        for attack in TAU:
            name = f'attack-{attack}-steps-1000'
            old_k = prior[name][0]['signal']['levels_db'].index(levels[k])
            old_gain = previous['evaluation'][name]['settled_gain_measured_db'][old_k]
            row[f'previous_attack_{attack}_gain_db'] = old_gain
            row[f'change_from_attack_{attack}_db'] = plateaus[k]-old_gain
        gain_repeat.append(row)

    # Replay each frozen effective-tau hypothesis across the complete signal.
    # A hypothesis with two tau values is scored one drop per independent render.
    xn = x.astype(float)/10**(m['ref_dbfs']/20)
    start = m['signal_start']
    input_env = cycle_envelope(xn[start:start+length], 1000)
    sos, tg = tone_filter()
    mapping = protocol['frozen_model_control_mapping']
    evaluations, curves = {}, {}
    for hypothesis, predictions in protocol['predictions'].items():
        evaluations[hypothesis] = []
        for entry in predictions:
            out, _ = compress(xn, TAU['0700'][0], entry['effective_release_tau_ms'],
                              mapping['sustain_gain_db'], mapping['level_noon_makeup_db'])
            out = signal.sosfilt(sos, out)*tg
            predicted = db(cycle_envelope(out[start:start+length], 1000)/input_env)
            down = entry['drop_at_s']
            result = score(measured, predicted, down)
            # Confirm that the actual playback reproduces the pre-capture numbers.
            frozen = entry['prediction']
            for key in ['t63_ms', 't90_ms']:
                assert result['predicted'][key] == frozen[key], (hypothesis, key)
            for key in ['start_gain_db', 'final_gain_db', 'excursion_db']:
                assert abs(result['predicted'][key]-frozen[key]) < .001, (hypothesis, key)
            result.update({'drop_at_s': down, 'from_db': levels[down//4-1],
                           'to_db': levels[down//4], 'effective_release_tau_ms': entry['effective_release_tau_ms']})
            evaluations[hypothesis].append(result)
            curves[hypothesis, down] = predicted
            print(hypothesis, f"{result['from_db']} -> {result['to_db']}",
                  't63 measured/predicted', result['measured']['t63_ms'], result['predicted']['t63_ms'],
                  't90', result['measured']['t90_ms'], result['predicted']['t90_ms'],
                  'shape RMS dB equivalent', round(result['endpoint_normalized_shape_rms_db_equivalent'], 4), flush=True)
    result = {
        'source_model_commit': commit,
        'protocol_sha256': hashlib.sha256(protocol_bytes).hexdigest(),
        'previous_results_sha256': protocol['source_results_sha256'],
        'integrity': integrity,
        'loopback_engaged_vs_bypass': loop_checks,
        'bypass_vs_previous_session_same_input_level': bypass_checks,
        'return_to_minimum_attack_gain': gain_repeat,
        'frozen_hypothesis_evaluation': evaluations,
        'scope': '48 kHz, 1 kHz carrier, Attack minimum, Level/Tone/Sustain noon. No new-data fit. Centered one-period RMS gain on 1 ms grid; shape scores 5–1200 ms after drop, normalized independently to each trace endpoints. Shape scores remove gain offset and excursion errors; they are not absolute output accuracy. All three are extrapolations from the previous experiment, not identified circuit models. One take per condition; no confidence intervals.',
    }
    print('Gain return', json.dumps(gain_repeat, indent=2), flush=True)
    (HERE/'cs3_crossed_results.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    colors = {'frozen_baseline': '#777777', 'destination_level_effect': '#c36a00', 'starting_level_effect': '#007d91'}
    for col, down in enumerate([8, 20]):
        idx = down*1000
        t = np.arange(5, 1200)
        sl = slice(idx+5, idx+1200)
        mm = release_metrics(measured, down, 1000)
        axes[0, col].plot(t, measured[sl], color='black', label='Pedal')
        axes[1, col].plot(t, (measured[sl]-mm['start_gain_db'])/mm['excursion_db'], color='black', label='Pedal')
        for hypothesis in evaluations:
            pred = curves[hypothesis, down]
            pm = release_metrics(pred, down, 1000)
            label = hypothesis.replace('_', ' ')
            axes[0, col].plot(t, pred[sl], '--', color=colors[hypothesis], label=label)
            axes[1, col].plot(t, (pred[sl]-pm['start_gain_db'])/pm['excursion_db'], '--', color=colors[hypothesis], label=label)
        axes[0, col].set(title=f'{levels[down//4-1]} → {levels[down//4]} dB', ylabel='Gain relative to bypass (dB)')
        axes[1, col].set(ylabel='Fraction of own gain recovery')
        for row in range(2):
            axes[row, col].set(xlabel='ms after drop')
            axes[row, col].grid(alpha=.3)
            axes[row, col].legend(fontsize=8)
    fig.suptitle('CS-3 crossed levels: pre-recording predictions; no new parameter fit')
    fig.savefig(HERE/'cs3_crossed_validation.png', dpi=150)


if __name__ == '__main__':
    main()
