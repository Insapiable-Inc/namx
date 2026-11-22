"""Analyze the NS-2 transition locator without fitting its dynamics.

python tools/research/analyze_ns2_locator.py [REPOSITORY]
Uses the true same-session bypass, frozen source and played-signal hashes,
coherent gain and noise estimates. Quiet reset/tail samples are not gain data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from scipy import signal

from analyze_cs3_detector import FS, HERE, biquad, db, load, rms
from prepare_ns2_locator import coherent_envelope, ns2_gain


def spectrum(y, frequency=1000):
    t = np.arange(len(y))/FS
    w = np.hanning(len(y))
    frequencies = [frequency, frequency-100, frequency-75, frequency-50,
                   frequency+50, frequency+75, frequency+100]
    return np.array([2*np.sum(y*w*np.exp(-2j*np.pi*f*t))/w.sum() for f in frequencies])


def window(data, k, bounds):
    a,b = bounds
    return data[int((4*k+a)*FS):int((4*k+b)*FS)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=HERE.parents[1])
    root = parser.parse_args().root.resolve()
    relative = Path('captures/boss/ns-2/2026-10-30-threshold-locator')
    protocol_bytes = (root/relative/'analysis-protocol.json').read_bytes()
    protocol = json.loads(protocol_bytes)
    commit = protocol['source_model_commit']
    for name, expected in protocol['source_sha256'].items():
        assert hashlib.sha256(subprocess.check_output(['git','show',commit+':'+name],cwd=root)).hexdigest()==expected
    preflight_bytes = (HERE/'ns2_locator_preflight.json').read_bytes()
    preflight = json.loads(preflight_bytes)
    names = ['bypass-locator-1000','threshold-0300-decay-0700-locator-1000']
    takes = {n:load(root,relative,n,commit) for n in names}
    bm,bx,by,bb = takes[names[0]]
    m,x,y,body = takes[names[1]]
    assert bm['bypass'] and not m['bypass']
    assert m['settings']=={'Threshold':'3:00','Decay':'7:00','Mode':'Reduction'}
    assert m['signal']['levels_db']==protocol['levels_db']
    assert m['signal']==bm['signal'] and np.array_equal(x,bx)
    assert m['played_sha256']==preflight['expected_played_sha256']
    length=52*FS; start=m['signal_start']; xn=x.astype(float)/10**(m['ref_dbfs']/20)
    internal=ns2_gain(xn,-49.4,24.6)
    sos=np.array([biquad('shelf',8168.4,.623,3.69)])
    norm=1/abs(signal.sosfreqz(sos,worN=[1000],fs=FS)[1][0])
    model=signal.sosfilt(sos,xn*10**(internal/20))*norm
    model=model[start:start+length]; inp=xn[start:start+length]
    measured=db(coherent_envelope(body[:length,0])/np.maximum(coherent_envelope(bb[:length,0]),1e-15))
    predicted=db(coherent_envelope(model)/np.maximum(coherent_envelope(inp),1e-15))
    open_phase=np.angle(spectrum(body[45*FS:47*FS,0])[0]/spectrum(bb[45*FS:47*FS,0])[0])
    rows=[]
    for k,level in enumerate(m['signal']['levels_db']):
        row={'step':k,'input_db':level,'eligible_gain_data':level>-90,'windows':[]}
        for j,bounds in enumerate(protocol['gain_windows_seconds']):
            a=spectrum(window(body[:,0],k,bounds)); b=spectrum(window(bb[:,0],k,bounds))
            z=a[0]/b[0]
            snr=float(db(a[0]/max(float(np.median(abs(a[1:]))),1e-15)))
            by_snr=float(db(b[0]/max(float(np.median(abs(b[1:]))),1e-15)))
            eligible=level>-90 and min(snr,by_snr)>20
            measured_median=float(np.median(window(measured,k,bounds)))
            modeled_median=float(np.median(window(predicted,k,bounds)))
            pre=float(np.median(window(internal[start:],k,bounds)))
            assert abs(pre-preflight['locator_predictions'][k]['internal_gain_window_medians_db'][j])<1e-9
            projected=float((z*np.exp(-1j*open_phase)).real)
            row['windows'].append({'seconds':bounds,'eligible':eligible,
                'gain_db_10ms_magnitude_median':measured_median if eligible else None,
                'hann_coherent_gain_db':float(db(z)) if eligible else None,
                'phase_projected_gain_db':float(db(projected)) if eligible and projected>0 else None,
                'frozen_model_gain_db':modeled_median if eligible else None,
                'carrier_peak_dbfs':float(db(a[0])),
                'carrier_vs_neighbor_bins_db':snr,'bypass_carrier_vs_neighbor_bins_db':by_snr})
        if row['eligible_gain_data'] and all(w['eligible'] for w in row['windows']):
            row['late_minus_early_gain_db']=row['windows'][2]['gain_db_10ms_magnitude_median']-row['windows'][0]['gain_db_10ms_magnitude_median']
            row['frozen_model_late_minus_early_db']=row['windows'][2]['frozen_model_gain_db']-row['windows'][0]['frozen_model_gain_db']
            sl=slice(int((4*k+3)*FS),int((4*k+3.8)*FS))
            # A late slope describes this interval, not a convergence guarantee.
            t=np.arange(len(measured[sl]))/FS
            row['late_gain_slope_db_per_second']=float(np.polyfit(t,measured[sl],1)[0])
        rows.append(row)
    integrity={}
    for name,(mm,xx,yy,bbb) in takes.items():
        integrity[name]={'xruns':mm['xruns'],'recorded_at':mm['recorded_at'],'settings':mm['settings'],
            'latency_samples':mm['latency_samples'],'marker_quality':mm['latency_quality'],
            'played_sha256':mm['played_sha256'],
            'recorded_sha256':hashlib.sha256((root/relative/(name+'.wav')).read_bytes()).hexdigest(),
            'abs_ge_0999':int(np.sum(np.abs(yy)>=.999)),
            'tail_rms_dbfs':db(np.sqrt(np.mean(yy[-FS:]**2,axis=0))).tolist()}
    calibrations=[]
    for k,level in enumerate(m['signal']['levels_db']):
        if level<=-90:continue
        bounds=[3,3.8]
        a=spectrum(window(body[:,1],k,bounds))[0]
        b=spectrum(window(bb[:,1],k,bounds))[0]
        dry=spectrum(window(bb[:,0],k,bounds))[0]
        played=spectrum(window(bx[bm['signal_start']:],k,bounds))[0]
        calibrations.append({'input_db':level,'loop_engaged_minus_bypass_db':float(db(a/b)),
            'loop_phase_equivalent_delay_samples':float(-np.angle(a/b)*FS/(2*np.pi*1000)),
            'bypass_output_over_played_db':float(db(dry/played))})
    # Explicitly match envelope edge, so carrier phase cannot hide cycle offsets.
    edge_shifts=[]
    for k in range(1,13):
        midpoint=k*4*FS
        pair=[]
        for bbb in [bb,body]:
            env=coherent_envelope(bbb[midpoint-FS//10:midpoint+FS//10,1])
            left=float(np.median(env[int(.03*FS):int(.07*FS)]))
            right=float(np.median(env[int(.13*FS):int(.17*FS)]))
            v=(env-left)/(right-left)
            search=v[int(.09*FS):int(.11*FS)]
            pair.append(float((np.flatnonzero(search>=.5)[0]-int(.01*FS))/FS*1000))
        edge_shifts.append({'at_s':4*k,'bypass_ms':pair[0],'engaged_ms':pair[1],'difference_ms':pair[1]-pair[0]})
    old=load(root,Path('captures/boss/ns-2/2026-10-27'),'bypass-jump',commit)
    old_ratio=spectrum(old[3][5*FS:7*FS,0])[0]/spectrum(old[1][old[0]['signal_start']+5*FS:old[0]['signal_start']+7*FS])[0]
    new_ratio=spectrum(bb[45*FS:47*FS,0])[0]/spectrum(bx[bm['signal_start']+45*FS:bm['signal_start']+47*FS])[0]
    trustworthy=np.concatenate([np.arange(int((4*k+.1)*FS),int((4*k+3.8)*FS),48) for k in range(1,12)])
    result={'source_model_commit':commit,'protocol_sha256':hashlib.sha256(protocol_bytes).hexdigest(),
        'preflight_sha256':hashlib.sha256(preflight_bytes).hexdigest(),'integrity':integrity,'windows':rows,
        'calibration':{'per_level':calibrations,'loop_edge_checks':edge_shifts,
                       'new_minus_old_ns2_bypass_at_minus12_db':float(db(new_ratio/old_ratio))},
        'frozen_model_rms_db_on_01_to_38_second_windows':rms(predicted[trustworthy]-measured[trustworthy]),
        'selected_history_input_levels_db':[-56,-54,-44],
        'selection_reason':'-56 dB gives strong partial opening; -54 dB tests approach to fully open; -44 dB is clearly above both measured and nominal-model transition as a prompt-opening control. All are well above the coherent noise floor. Keep Threshold physically unchanged.',
        'scope':'Adaptive localization; no dynamics fit. 10 ms coherent magnitude and preregistered three window medians; Hann complex gain/phase projection are robustness checks. Neighbor-bin ratio is a noise/leakage proxy, not a confidence interval. Reset levels -100 dB and silence are excluded from gain evaluation. One capture per state; no repeatability confidence interval. Later history must not assume four-second endpoints are equilibrium.'}
    (HERE/'ns2_locator_results.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    for row in rows:
        if row['eligible_gain_data']:
            print(row['input_db'],[round(w['gain_db_10ms_magnitude_median'],3) for w in row['windows']],
                  'late-early',round(row['late_minus_early_gain_db'],3),
                  'late slope',round(row['late_gain_slope_db_per_second'],3),flush=True)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(1,2,figsize=(12,4.8),layout='constrained')
    t=np.arange(1,12)
    levels=[rows[k]['input_db'] for k in t]
    for j,label in [(0,'.1–.3 s'),(1,'1–1.5 s'),(2,'3–3.8 s')]:
        axes[0].plot(levels,[rows[k]['windows'][j]['gain_db_10ms_magnitude_median'] for k in t],'o-',label='Pedal '+label)
    axes[0].plot(levels,[rows[k]['windows'][2]['frozen_model_gain_db'] for k in t],'--',color='gray',label='Frozen model, late')
    axes[0].set(xlabel='Input level relative to reference (dB)',ylabel='Gain relative to bypass (dB)',title='Transition and within-step evolution',xlim=(-63,-42))
    for level in [-58,-56,-54,-52]:
        k=m['signal']['levels_db'].index(level)
        indices=np.arange(int((4*k+.02)*FS),int((4*k+3.9)*FS),48)
        axes[1].plot(indices/FS-4*k,measured[indices],label=f'{level} dB')
    axes[1].set(xlabel='Time after step (s)',ylabel='Gain relative to bypass (dB)',title='Constant-input gain trajectories')
    for ax in axes:
        ax.grid(alpha=.3);ax.legend(fontsize=8)
    fig.suptitle('NS-2 locator: Threshold 3:00, Decay minimum, 1 kHz')
    fig.savefig(HERE/'ns2_locator_validation.png',dpi=150)


if __name__=='__main__':main()
