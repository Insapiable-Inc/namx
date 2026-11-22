"""Exploratory CS-3 one-state log-release candidate, selected after crossed data.

python tools/research/fit_cs3_release_candidate.py [REPOSITORY]
All candidate parameters are estimated from the earlier detector-check 1 kHz
takes. Crossed data informed the choice of model family, so the later scores are
retrospective generalization checks, NOT a new preregistered blind validation.
Static calibration is given equally to the existing amplitude-release baseline.
No production code is changed.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from numba import njit
from scipy import optimize, signal

from analyze_cs3_detector import (
    FS, HERE, TAU, CEILING, SLOPE, compress, cycle_envelope, db, load,
    plateau_gain, release_metrics, rms, tone_filter, harmonics, onset_metrics,
)
from analyze_cs3_crossed import score


@njit
def log_release_compress(x, attack_ms, release_ms, asymptotic_gain_db, sustain_db, makeup_db):
    """One amplitude state; retain original amplitude-domain attack update.

    During release, log(envelope) relaxes toward a fixed floor. Clamp the
    envelope to the nonnegative current target to avoid overshoot at recapture.
    This exact research update uses log/pow, not optimized audio-thread code.
    """
    floor_db = (makeup_db + SLOPE*sustain_db + (1-SLOPE)*CEILING - asymptotic_gain_db)/(1-SLOPE)
    floor = 10**(floor_db/20)
    env = floor
    a = 1-np.exp(-1000/(FS*attack_ms))
    q = np.exp(-1000/(FS*release_ms))
    out = np.empty(len(x))
    for n in range(len(x)):
        target = max(abs(x[n]), floor)
        if target > env:
            env += a*(target-env)
        else:
            level = floor_db + q*(20*np.log10(max(env, floor))-floor_db)
            env = max(target, 10**(level/20))
        level = 20*np.log10(max(env, floor))
        gain_db = sustain_db+makeup_db-(1-SLOPE)*max(0., level+sustain_db-CEILING)
        out[n] = x[n]*10**(gain_db/20)
    return out


def case(root, relative, name, bypass, commit):
    m, x, _, b = load(root, relative, name, commit)
    by = load(root, relative, bypass, commit)[3]
    frequency = m['signal']['freq']
    length = 24*FS
    xn = x.astype(float)/10**(m['ref_dbfs']/20)
    start = m['signal_start']
    gain = db(cycle_envelope(b[:length, 0], frequency)/cycle_envelope(by[:length, 0], frequency))
    return {'name': name, 'm': m, 'xn': xn, 'start': start, 'length': length,
            'frequency': frequency, 'gain': gain, 'measured': b[:length, 0],
            'bypass': by[:length, 0], 'input_env': cycle_envelope(xn[start:start+length], frequency)}


def render(data, kind, attack, release, asymptote, sustain, makeup):
    if kind == 'candidate':
        out = log_release_compress(data['xn'], attack, release, asymptote, sustain, makeup)
    else:
        out, _ = compress(data['xn'], attack, release, sustain, makeup)
    sos, tg = tone_filter()
    out = signal.sosfilt(sos, out)*tg
    out = out[data['start']:data['start']+data['length']]
    return db(cycle_envelope(out, data['frequency'])/data['input_env']), out


def fit_kernel(data):
    entries = []
    for down in [8, 20]:
        mm = release_metrics(data['gain'], down, 1000)
        v = data['gain'][down*1000:down*1000+1200]
        t = np.arange(1200)
        mask = (t >= 5) & (v < mm['final_gain_db']-1)
        entries.append((t[mask], v[mask], mm))
    def residual(p):
        return np.concatenate([p[0]+(m['start_gain_db']-p[0])*np.exp(-t/p[1])-v for t, v, m in entries])
    initial = [30, 1200 if '0700' in data['name'] else 100]
    fit = optimize.least_squares(residual, initial, bounds=([5, 10], [100, 5000]),
                                 xtol=1e-11, ftol=1e-11, gtol=1e-11)
    def ramp_residual(p):
        return np.concatenate([m['start_gain_db']+p[0]*t-v for t, v, m in entries])
    ramp = optimize.least_squares(ramp_residual, [.04])
    # Individual fits expose parameter variation; no confidence interval is inferred.
    single = []
    for t, v, m in entries:
        f = optimize.least_squares(lambda p: p[0]+(m['start_gain_db']-p[0])*np.exp(-t/p[1])-v,
                                   fit.x, bounds=([5, 10], [100, 5000]))
        single.append({'asymptotic_gain_db': float(f.x[0]), 'release_ms': float(f.x[1]), 'rms_db': rms(f.fun)})
    return {'asymptotic_gain_db': float(fit.x[0]), 'release_ms': float(fit.x[1]),
            'fit_rms_db': rms(fit.fun), 'fit_sample_count': int(len(fit.fun)),
            'linear_db_ramp_fit_rms_db': rms(ramp.fun), 'single_drop_fits': single,
            'fit_scope': 'Both earlier 1 kHz drops, 5 ms onward, excluding the last 1 dB below settled gain; correlated 1 ms samples. Two shared dynamic parameters.'}


def kernel_prediction(initial, final, asymptote, tau):
    return np.minimum(final, asymptote+(initial-asymptote)*np.exp(-np.arange(1200)/tau))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=HERE.parents[1])
    root = parser.parse_args().root.resolve()
    protocol = json.loads((root/'captures/boss/cs-3/2026-10-30-detector-crossed/analysis-protocol.json').read_text())
    commit = protocol['source_model_commit']
    check = Path('captures/boss/cs-3/2026-10-30-detector-check')
    crossed = Path('captures/boss/cs-3/2026-10-30-detector-crossed')
    data = {}
    for attack in TAU:
        for f in [1000, 110]:
            name = f'attack-{attack}-steps-{f}'
            data[name] = case(root, check, name, f'bypass-steps-{f}', commit)
    name = 'attack-0700-crossed-1000'
    data[name] = case(root, crossed, name, 'bypass-crossed-1000', commit)
    kernels = {a: fit_kernel(data[f'attack-{a}-steps-1000']) for a in TAU}
    print('Kernels:', json.dumps(kernels, indent=2), flush=True)

    # Does the same starting level follow the same path until recapture?
    collapse = []
    before = data['attack-0700-steps-1000']['gain']
    after = data['attack-0700-crossed-1000']['gain']
    for old_down, new_down in [(8, 20), (20, 8)]:
        a = release_metrics(before, old_down, 1000)
        b = release_metrics(after, new_down, 1000)
        va = before[old_down*1000:old_down*1000+1200]
        vb = after[new_down*1000:new_down*1000+1200]
        t = np.arange(1200)
        mask = (t >= 5) & (va < a['final_gain_db']-1) & (vb < b['final_gain_db']-1)
        error = (va-a['start_gain_db'])-(vb-b['start_gain_db'])
        collapse.append({'previous_drop_s': old_down, 'crossed_drop_s': new_down,
                         'window_start_ms': 5, 'window_last_ms': int(t[mask][-1]),
                         'gain_change_rms_difference_db': rms(error[mask])})

    # Crossed envelope predictions use earlier measured static gains, including
    # BOTH endpoints; no gain offset or endpoints are estimated from crossed data.
    kernel_cross = []
    source = data['attack-0700-steps-1000']
    kernel = kernels['0700']
    static = {level: plateau_gain(source['gain'], source['m']['signal']['levels_db'].index(level)) for level in [-36, -24, -12, -6]}
    for down, start_level, end_level in [(8, -6, -36), (20, -12, -24)]:
        pred = np.full(24000, static[start_level])
        pred[down*1000:down*1000+1200] = kernel_prediction(static[start_level], static[end_level], kernel['asymptotic_gain_db'], kernel['release_ms'])
        pred[down*1000+1200:] = static[end_level]
        row = score(after, pred, down)
        row['drop_at_s'] = down
        kernel_cross.append(row)

    # Both architectures receive the same opportunity to recalibrate two static
    # nuisance mappings from prior 1 kHz data. The baseline additionally refits
    # its one global release tau on those two recoveries; no per-drop constants.
    parameters = {}
    old_mapping = protocol['frozen_model_control_mapping']
    for attack, (attack_ms, release_ms) in TAU.items():
        source = data[f'attack-{attack}-steps-1000']
        k = kernels[attack]
        target_static = np.array([plateau_gain(source['gain'], j) for j in range(6)])
        def static_objective(p, kind, tau):
            gain, _ = render(source, kind, attack_ms, tau, k['asymptotic_gain_db'], *p)
            return np.array([plateau_gain(gain, j) for j in range(6)])-target_static
        p0 = [old_mapping['sustain_gain_db'], old_mapping['level_noon_makeup_db']]
        fit = optimize.least_squares(static_objective, p0, args=('candidate', k['release_ms']),
                                     diff_step=1e-4, xtol=1e-7, ftol=1e-7, gtol=1e-7)
        candidate = {'attack_ms': attack_ms, 'release_ms': k['release_ms'],
                     'asymptotic_gain_db': k['asymptotic_gain_db'], 'sustain_db': float(fit.x[0]),
                     'makeup_db': float(fit.x[1]), 'static_fit_rms_db': rms(fit.fun)}
        candidate['detector_floor_db_re_reference'] = (candidate['makeup_db']+SLOPE*candidate['sustain_db']+(1-SLOPE)*CEILING-k['asymptotic_gain_db'])/(1-SLOPE)
        # Profile out static mappings for each baseline tau. Score full release
        # windows equally, after settling calibration on both input packets.
        last_fit = {}
        def baseline_objective(tau):
            f = optimize.least_squares(static_objective, p0, args=('baseline', tau),
                                       diff_step=1e-4, xtol=1e-6, ftol=1e-6, gtol=1e-6)
            gain, _ = render(source, 'baseline', attack_ms, tau, 0, *f.x)
            errors = np.concatenate([gain[d*1000+5:d*1000+1200]-source['gain'][d*1000+5:d*1000+1200] for d in [8, 20]])
            last_fit[float(tau)] = (f.x, rms(f.fun))
            return rms(errors)
        best = optimize.minimize_scalar(baseline_objective, bounds=(release_ms*.5, release_ms*1.7),
                                        method='bounded', options={'xatol':1e-3})
        bp, static_error = last_fit[float(best.x)]
        baseline = {'attack_ms': attack_ms, 'release_ms': float(best.x), 'asymptotic_gain_db': 0,
                    'sustain_db': float(bp[0]), 'makeup_db': float(bp[1]),
                    'static_fit_rms_db': static_error, 'training_release_rms_db': float(best.fun)}
        parameters[attack] = {'candidate': candidate, 'baseline': baseline}
        print('Calibrated', attack, json.dumps(parameters[attack]), flush=True)

    evaluations, curves = {}, {}
    for name, d in data.items():
        attack = name.split('-')[1]
        evaluations[name] = {}
        for kind, p in parameters[attack].items():
            gain, waveform = render(d, kind, p['attack_ms'], p['release_ms'], p['asymptotic_gain_db'], p['sustain_db'], p['makeup_db'])
            releases = []
            for down in [8, 20]:
                # score() has 1 kHz exclusion; replace its scores at 110 Hz with
                # the shared two-cycle exclusion, preserving timing definitions.
                mm = release_metrics(d['gain'], down, d['frequency'])
                pm = release_metrics(gain, down, d['frequency'])
                first = max(5, int(np.ceil(2000/d['frequency'])))
                sl = slice(down*1000+first, down*1000+1200)
                delta = (gain[sl]-pm['start_gain_db'])/pm['excursion_db']-(d['gain'][sl]-mm['start_gain_db'])/mm['excursion_db']
                releases.append({'drop_at_s': down, 'measured': mm, 'predicted': pm,
                                 'evaluation_from_ms': first, 'absolute_rms_db': rms(gain[sl]-d['gain'][sl]),
                                 'endpoint_normalized_shape_rms_db_equivalent': rms(delta)*mm['excursion_db'],
                                 'first_200ms_shape_rms_db_equivalent': rms(delta[:200-first])*mm['excursion_db']})
            evaluations[name][kind] = {
                'role': 'training' if name.endswith('steps-1000') else 'retrospective generalization',
                'settled_gain_errors_db': [plateau_gain(gain, j)-plateau_gain(d['gain'], j) for j in range(6)],
                'releases': releases,
                'attacks': [{'rise_at_s': up, 'measured': onset_metrics(d['gain'], up, d['frequency']),
                            'predicted': onset_metrics(gain, up, d['frequency'])} for up in [4, 16]],
                'harmonics': [{'input_db': d['m']['signal']['levels_db'][j],
                              'measured': harmonics(d['measured'], d['frequency'], j*4+2.8, j*4+3.8),
                              'predicted': harmonics(waveform, d['frequency'], j*4+2.8, j*4+3.8)} for j in [1, 3, 4]],
            }
            curves[name, kind] = gain
            print(name, kind, 'release RMS', [round(r['absolute_rms_db'], 4) for r in releases],
                  't63', [r['predicted']['t63_ms'] for r in releases], flush=True)

    # Original 0 -> -30 dB data: separate conditional kernel test. Its measured
    # endpoints are supplied, because the controls were hand-reset on that day.
    old = Path('captures/boss/cs-3/2026-10-27')
    old_by = load(root, old, 'bypass-jump', commit)[3]
    old_checks, old_maps = {}, {}
    for attack in ['0700', '0900', '1200', '0300', '0500']:
        _, _, _, body = load(root, old, 'jump-attack-'+attack, commit)
        gain = db(cycle_envelope(body[:12*FS, 0], 1000)/cycle_envelope(old_by[:12*FS, 0], 1000))
        mm = release_metrics(gain, 8, 1000)
        v = gain[8000:9200]; t = np.arange(1200)
        mask = (t >= 5) & (v < mm['final_gain_db']-1)
        fit = optimize.least_squares(lambda p: p[0]+(mm['start_gain_db']-p[0])*np.exp(-t[mask]/p[1])-v[mask],
                                     [30, 1200 if attack == '0700' else 200], bounds=([5, 10], [100, 5000]))
        old_maps[attack] = {'asymptotic_gain_db': float(fit.x[0]), 'release_ms': float(fit.x[1]),
                            'fit_rms_db': rms(fit.fun), 'scope': 'Fit to original jump only; intermediate settings have no new-level validation.'}
        if attack in kernels:
            k = kernels[attack]
            pred = kernel_prediction(mm['start_gain_db'], mm['final_gain_db'], k['asymptotic_gain_db'], k['release_ms'])
            old_checks[attack] = {'conditional_rms_db': rms(pred[5:]-v[5:]),
                                  'predicted_t63_ms': int(np.flatnonzero(pred >= mm['start_gain_db']+.632*mm['excursion_db'])[0]),
                                  'predicted_t90_ms': int(np.flatnonzero(pred >= mm['start_gain_db']+.9*mm['excursion_db'])[0]),
                                  'measured': mm}

    result = {'scope': __doc__, 'source_model_commit': commit,
              'kernel_fits': kernels, 'same_start_different_destination': collapse,
              'kernel_crossed_using_prior_static_endpoints': kernel_cross,
              'full_sample_renderer_parameters': parameters, 'full_sample_renderer_evaluation': evaluations,
              'original_jump_conditional_kernel_check': old_checks,
              'original_jump_exploratory_knob_map': old_maps,
              'limitations': 'Model family chosen after seeing crossed data. Numerical parameters use earlier 1 kHz takes only; static maps also fitted there. No new-data offset, per-drop tau, or timing fit in full-renderer scores. Intermediate Attack fits are original-data fits only. No listening validation, production C++, sample-rate generalization, or broad Sustain/Tone/Level validation.'}
    (HERE/'cs3_release_candidate_results.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    selections = [('attack-0700-crossed-1000', 8), ('attack-0700-crossed-1000', 20),
                  ('attack-0700-steps-110', 8), ('attack-0500-steps-110', 8)]
    for ax, (name, down) in zip(axes.flat, selections):
        d = data[name]; first = max(5, int(np.ceil(2000/d['frequency'])))
        last = 200 if '0500' in name else 1200
        t = np.arange(first, last); sl = slice(down*1000+first, down*1000+last)
        ax.plot(t, d['gain'][sl], color='black', label='Pedal')
        for kind, color in [('baseline', '#a65e00'), ('candidate', '#00818a')]:
            ax.plot(t, curves[name, kind][sl], '--', color=color, label=kind)
        label = 'Crossed 1 kHz' if 'crossed' in name else ('110 Hz / fast Attack' if '0500' in name else '110 Hz / slow Attack')
        ax.set(title=f'{label}; drop at {down} s', xlabel='ms after drop', ylabel='Gain relative to bypass (dB)')
        ax.grid(alpha=.3); ax.legend(fontsize=8)
    fig.suptitle('CS-3 exploratory full waveform renderer; all parameters fit to prior 1 kHz takes')
    fig.savefig(HERE/'cs3_release_candidate.png', dpi=150)


if __name__ == '__main__':
    main()
