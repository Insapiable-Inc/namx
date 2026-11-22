"""Recheck old NS-2 transition data and save predictions before the locator takes.

python tools/research/prepare_ns2_locator.py [REPOSITORY]
Does not fit parameters, record, play audio or modify production DSP.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
from numba import njit
from scipy.ndimage import uniform_filter1d

from analyze_cs3_detector import FS, HERE, load


@njit
def ns2_gain(x, threshold, decay_ms):
    """Current sample-level detector equations; x normalized to guitar reference.

    Tone is unity at this 1 kHz carrier and omitted from the internal gain trace.
    Constants use the production header, not the slightly different old fitter.
    """
    peak = 0.; fast = slow = -140.; switch = -7.56; opened = False
    af = 1-np.exp(-1000/(FS*decay_ms))
    av = 1-np.exp(-1000/(FS*decay_ms*7.94))
    ag = 1-np.exp(-1000/(FS*decay_ms*4.42))
    peak_fall = np.exp(-1000/(FS*50.))
    reference = 10**(-6/20)
    out = np.empty(len(x))
    for n in range(len(x)):
        peak = max(abs(x[n]), peak*peak_fall)
        level = 20*np.log10(peak) if peak*reference > 1e-7 else -140.
        fast = level if level > fast else fast+(level-fast)*af
        slow = level if level > slow else slow+(level-slow)*av
        if fast > threshold+1.29:
            opened = True
        elif fast < threshold+1.29-10.47:
            opened = False
        switch = 0. if opened else switch+(-7.56-switch)*ag
        reduction = switch-.889*max(0.,threshold-slow)
        out[n] = .6+max(reduction,-140.)
    return out


def coherent_envelope(y):
    z = y*np.exp(-2j*np.pi*1000*np.arange(len(y))/FS)
    return 2*np.abs(uniform_filter1d(z.real,480)+1j*uniform_filter1d(z.imag,480))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=HERE.parents[1])
    root = parser.parse_args().root.resolve()
    session = root/'captures/boss/ns-2/2026-10-30-threshold-locator'
    protocol = json.loads((session/'analysis-protocol.json').read_text())
    commit = protocol['source_model_commit']
    for name, expected in protocol['source_sha256'].items():
        assert hashlib.sha256(subprocess.check_output(['git','show',commit+':'+name],cwd=root)).hexdigest() == expected
    relative = Path('captures/boss/ns-2/2026-10-27')
    name = 'stairs-threshold-0300'
    m, x, y, body = load(root, relative, name, commit)
    bypass = load(root, relative, 'bypass-stairs', commit)[3]
    length = int(len(m['signal']['levels_db'])*1.5*FS)
    gain = 20*np.log10(np.maximum(coherent_envelope(body[:length,0]),1e-15)/np.maximum(coherent_envelope(bypass[:length,0]),1e-15))
    rows = []
    for k, level in enumerate(m['signal']['levels_db']):
        if -64 <= level <= -44:
            values = [float(np.median(gain[int((k*1.5+p)*FS):int((k*1.5+p+.1)*FS)])) for p in [.05,.5,1.3]]
            rows.append({'step':k,'input_db':level,'direction':'ascending' if k<20 else 'descending',
                         'gain_db_at_005_050_130_seconds':values,'late_minus_early_db':values[2]-values[0]})
    sys.path.insert(0,str(root/'tools/capture'))
    import signals
    plan_path = root/'tools/capture/plans/ns-2-threshold-locator.json'
    plan = json.loads(plan_path.read_text())
    rig = json.loads((session/'rig.json').read_text())
    spec = plan['takes'][1]['signal']
    played, _, start = signals.build(spec,FS,rig['ref_dbfs'])
    internal = ns2_gain(played.astype(float)/10**(rig['ref_dbfs']/20),-49.4,24.6)[start:]
    predictions = []
    for k, level in enumerate(spec['levels_db']):
        predictions.append({'step':k,'input_db':level,'internal_gain_window_medians_db':[
            float(np.median(internal[int((4*k+a)*FS):int((4*k+b)*FS)]))
            for a,b in protocol['gain_windows_seconds']]})
    result = {'model_commit':commit,'plan_sha256':hashlib.sha256(plan_path.read_bytes()).hexdigest(),
              'expected_played_sha256':hashlib.sha256(played.tobytes()).hexdigest(),
              'old_evidence':{'take':str(relative/name),'xruns':m['xruns'],'played_sha256':m['played_sha256'],
                              'recorded_sha256':hashlib.sha256((root/relative/(name+'.wav')).read_bytes()).hexdigest(),
                              'windows_seconds':[[.05,.15],[.5,.6],[1.3,1.4]],'rows':rows},
              'locator_predictions':predictions,
              'reset_design':{'max_decay_slow_tau_seconds':.4603*7.94,
                              'residual_fraction_after_8_seconds':float(np.exp(-8/(.4603*7.94))),
                              'residual_fraction_after_24_seconds':float(np.exp(-24/(.4603*7.94))),
                              'interpretation':'A 24 s low-input reset reduces the fitted slow-state excess to about .14%; physical reset adequacy must still be checked. The locator uses minimum Decay and does not require that long reset.'},
              'scope':'Old evidence is 10 ms coherent magnitude sampled in 100 ms windows relative to true same-session bypass. Locator predictions are frozen internal sample-level gain, not a complete audio transfer simulation. This capture is adaptive localization, not held-out validation.'}
    (HERE/'ns2_locator_preflight.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'old_transition_rows':[r for r in rows if r['input_db'] in [-56,-52]],
                      'reset_design':result['reset_design'],'predictions':predictions},indent=2))


if __name__ == '__main__':
    main()
