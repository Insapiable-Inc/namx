"""Evaluate NS-2 matched-history captures against prerecording predictions.

python tools/research/analyze_ns2_history.py [REPOSITORY]
Primary results never fit new parameters. Separate exponential fits are local
diagnostics, not replacement DSP or independently validated parameter estimates.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from scipy import signal, optimize

from analyze_cs3_detector import FS, HERE, biquad, db, load, rms
from analyze_ns2_locator import spectrum
from prepare_ns2_locator import coherent_envelope, ns2_gain


def envelope(y, samples=480):
    from scipy.ndimage import uniform_filter1d
    z=y*np.exp(-2j*np.pi*1000*np.arange(len(y))/FS)
    return 2*np.abs(uniform_filter1d(z.real,samples)+1j*uniform_filter1d(z.imag,samples))


def slice_seconds(x,a,b):return x[int(a*FS):int(b*FS)]


def onset_metrics(measured,played,reference,steady_gain):
    # A 1 ms coherent window and 1 ms grid. Input transitions already take 1 ms;
    # neither a zero nor a one here establishes sub-millisecond response.
    ey=envelope(measured,48)[::48];ex=envelope(played,48)[::48]*reference
    g=db(ey/np.maximum(ex,1e-15))
    valid=ex>np.median(ex[50:150])*.5
    valid[:1]=False
    opening=None
    for i in range(1,150):
        if np.all(valid[i:i+20]) and np.all(g[i:i+20]>=steady_gain-1):
            opening=i;break
    return {'within_1db_for_20ms_at_ms':opening,'coherent_window_ms':1,
            'stimulus_ramp_ms':1,'evaluation_limit_ms':150}


def fit_local_exponential(curve,stop_ms):
    t=np.arange(8000)/1000;mask=(t>=.05)&(t<stop_ms/1000)
    fit=optimize.least_squares(lambda p:p[0]-p[1]*np.exp(-t[mask]/p[2])-curve[mask],
                               [-4,15,1],bounds=([-100,0,.01],[50,200,30]))
    pred=fit.x[0]-fit.x[1]*np.exp(-t/fit.x[2])
    return {'asymptotic_gain_db':float(fit.x[0]),'excursion_db':float(fit.x[1]),
            'tau_seconds':float(fit.x[2]),'training_rms_db':rms(fit.fun),
            'fit_window_seconds':[.05,stop_ms/1000]},pred


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    root=parser.parse_args().root.resolve()
    relative=Path('captures/boss/ns-2/2026-10-30-history')
    locator=Path('captures/boss/ns-2/2026-10-30-threshold-locator')
    protocol_bytes=(root/relative/'analysis-protocol.json').read_bytes()
    p=json.loads(protocol_bytes);commit=p['source_model_commit']
    assert hashlib.sha256((HERE/'ns2_locator_results.json').read_bytes()).hexdigest()==p['locator_results_sha256']
    for name,expected in p['model_source_sha256'].items():
        assert hashlib.sha256(subprocess.check_output(['git','show',commit+':'+name],cwd=root)).hexdigest()==expected
    lm,lx,_,lb=load(root,locator,'bypass-locator-1000',commit)
    refs={};loop_refs={}
    for level in p['levels_db']+[-12]:
        k=lm['signal']['levels_db'].index(level);a=k*4+3;b=k*4+3.8
        xp=spectrum(slice_seconds(lx[lm['signal_start']:],a,b))[0]
        refs[level]=spectrum(slice_seconds(lb[:,0],a,b))[0]/xp
        loop_refs[level]=spectrum(slice_seconds(lb[:,1],a,b))[0]/xp
    names=['history-threshold-0300-decay-0700','history-threshold-0300-decay-0500','bypass-history-check-1000']
    integrity={};evaluation={};curves={};calibration={};open_checks={};quiet_checks={}
    bm,bx,by,bb=load(root,relative,names[2],commit)
    assert bm['bypass'] and bm['played_sha256']==p['stimulus']['expected_bypass_played_sha256']
    closing=[]
    for k,level in enumerate(bm['signal']['levels_db']):
        a=2*k+.5;b=2*k+1.5
        xspec=spectrum(slice_seconds(bx[bm['signal_start']:],a,b))[0]
        h=spectrum(slice_seconds(bb[:,0],a,b))[0]/xspec
        closing.append({'input_db':level,'gain_change_from_locator_db':float(db(h/refs[level])),
                        'phase_equivalent_delay_samples':float(-np.angle(h/refs[level])*FS/(2*np.pi*1000))})
    assert max(abs(v['gain_change_from_locator_db']) for v in closing)<.1,'Calibration gate failed: do not pool without review'
    calibration['closing_bypass_vs_locator']=closing
    sos=np.array([biquad('shelf',8168.4,.623,3.69)])
    tone_norm=1/abs(signal.sosfreqz(sos,worN=[1000],fs=FS)[1][0])
    probes=[s for s in p['segments'] if s['role'].startswith('probe_')]
    for name in names:
        m,x,y,body=load(root,relative,name,commit)
        integrity[name]={'xruns':m['xruns'],'recorded_at':m['recorded_at'],'settings':m['settings'],
            'latency_samples':m['latency_samples'],'marker_quality':m['latency_quality'],
            'played_sha256':m['played_sha256'],
            'recorded_sha256':hashlib.sha256((root/relative/(name+'.wav')).read_bytes()).hexdigest(),
            'abs_ge_0999':int(np.sum(abs(y)>=.999)),
            'tail_rms_dbfs':db(np.sqrt(np.mean(y[-FS:]**2,axis=0))).tolist()}
        if m['bypass']:continue
        tag=name.split('-')[-1]
        assert m['settings']=={'Threshold':'3:00','Decay':'7:00' if tag=='0700' else '5:00','Mode':'Reduction'}
        assert m['played_sha256']==p['stimulus']['expected_played_sha256']
        start=m['signal_start'];input_body=x[start:].astype(float)
        ey=coherent_envelope(body[:,0]);ex=coherent_envelope(input_body)
        n=min(len(ey),len(ex));rawgain=db(ey[:n]/np.maximum(ex[:n],1e-15))
        measured_rows=[];calibration[tag]=[];open_checks[tag]=[];quiet_checks[tag]=[]
        for s in p['segments']:
            a=s['start_s'];z=a+s['duration_s']
            if s['role']=='quiet_reset':
                quiet_checks[tag].append({'reset_start_s':a,
                    'last_5_seconds_channel_rms_dbfs':db(np.sqrt(np.mean(slice_seconds(body,z-5,z)**2,axis=0))).tolist(),
                    'scope':'Output silence cannot certify hidden detector-state reset.'})
                continue
            level=s['input_db']
            xm=spectrum(slice_seconds(input_body,z-1.5,z-.5))[0]
            loop=spectrum(slice_seconds(body[:,1],z-1.5,z-.5))[0]/xm
            calibration[tag].append({'body_start_s':a,'input_db':level,
                'loop_gain_change_from_locator_db':float(db(loop/loop_refs[level])),
                'loop_phase_equivalent_delay_samples':float(-np.angle(loop/loop_refs[level])*FS/(2*np.pi*1000))})
            if level==-12:
                g=float(np.median(rawgain[int((z-1.5)*FS):int((z-.5)*FS)]))-float(db(refs[level]))
                open_checks[tag].append({'body_start_s':a,'gain_db':g,
                    'opening':onset_metrics(slice_seconds(body[:,0],a,a+.2),slice_seconds(input_body,a,a+.2),abs(refs[level]),g)})
            if not s['role'].startswith('probe_'):continue
            history='quiet' if s['role']=='probe_after_quiet' else 'loud'
            measured=rawgain[int(a*FS):int(z*FS):48]-float(db(refs[level]))
            key=f'measured_{tag}_m{abs(level)}_{history}'
            curves[key]=measured
            row={'input_db':level,'history':history,'body_start_s':a,'windows':[]}
            for left,right in p['gain_windows_seconds']:
                spec=spectrum(slice_seconds(body[:,0],a+left,a+right))
                xp=spectrum(slice_seconds(input_body,a+left,a+right))[0]
                coherent=spec[0]/(xp*refs[level])
                margin=float(db(spec[0]/max(float(np.median(abs(spec[1:]))),1e-15)))
                row['windows'].append({'seconds':[left,right],
                    'gain_db':float(np.median(measured[int(left*1000):int(right*1000)])),
                    'hann_coherent_gain_db':float(db(coherent)),
                    'carrier_peak_dbfs':float(db(spec[0])),
                    'carrier_vs_neighbor_bins_db':margin})
            row['late_minus_early_db']=row['windows'][-1]['gain_db']-row['windows'][0]['gain_db']
            row['slope_6_to_7_seconds_db_per_second']=float(np.polyfit(np.arange(1000)/1000,measured[6000:7000],1)[0])
            if level==-44:
                row['opening']=onset_metrics(slice_seconds(body[:,0],a,a+.2),slice_seconds(input_body,a,a+.2),abs(refs[level]),row['windows'][-1]['gain_db'])
            measured_rows.append(row)
        evaluation[tag]={'measured':measured_rows,'frozen_models':{}}
        normalized=x.astype(float)/10**(m['ref_dbfs']/20)
        den=coherent_envelope(normalized)
        for variant,threshold in p['model_threshold_variants_db'].items():
            g=ns2_gain(normalized,threshold,p['decay_ms'][tag])+p['open_gain_offset_from_locator_db']
            out=signal.sosfilt(sos,normalized*10**(g/20))*tone_norm
            gp=db(coherent_envelope(out)/np.maximum(den,1e-15))
            rows=[]
            for i,s in enumerate(probes):
                a=s['start_s'];level=s['input_db'];history='quiet' if s['role']=='probe_after_quiet' else 'loud'
                idx=start+int(a*FS)
                predicted=gp[idx:idx+8*FS:48]
                vals=[float(np.median(gp[idx+int(left*FS):idx+int(right*FS)])) for left,right in p['gain_windows_seconds']]
                frozen=p['frozen_predictions'][variant][tag][i]
                assert frozen['input_db']==level and frozen['body_start_s']==a
                assert np.max(abs(np.array(vals)-frozen['gain_window_medians_db']))<1e-8
                measured=curves[f'measured_{tag}_m{abs(level)}_{history}']
                curves[f'{variant}_{tag}_m{abs(level)}_{history}']=predicted
                error=predicted-measured
                rows.append({'input_db':level,'history':history,'gain_window_medians_db':vals,
                    'absolute_rms_db_01_to_79_seconds':rms(error[100:7900]),
                    'late_offset_removed_rms_db_01_to_79_seconds':rms(error[100:7900]-np.median(error[6500:7500])),
                    'late_minus_early_db':vals[-1]-vals[0]})
            evaluation[tag]['frozen_models'][variant]=rows
        # Non-periodic boundary check relative to the exact file after marker
        # alignment. Onsets from silence avoid a whole-cycle phase ambiguity.
        edges=[]
        for s in probes:
            if s['role']!='probe_after_quiet':continue
            a=s['start_s'];pair=[]
            for data in [input_body,body[:,1]]:
                v=envelope(slice_seconds(data,a-.1,a+.1),48)
                base=float(np.median(v[int(.03*FS):int(.07*FS)]))
                high=float(np.median(v[int(.13*FS):int(.17*FS)]))
                z=(v-base)/(high-base)
                hits=np.flatnonzero(z[int(.095*FS):int(.105*FS)]>=.5)
                pair.append((int(hits[0])-int(.005*FS))/FS*1000)
            edges.append({'at_s':a,'input_ms':pair[0],'loop_ms':pair[1],'difference_ms':pair[1]-pair[0]})
        calibration[tag+'_onset_edges']=edges
        print(tag,[(r['input_db'],r['history'],[round(w['gain_db'],3) for w in r['windows']]) for r in measured_rows],flush=True)

    comparisons={};diagnostics={}
    for level in p['levels_db']:
        a=curves[f'measured_0700_m{abs(level)}_quiet'];b=curves[f'measured_0500_m{abs(level)}_quiet']
        comparisons[str(level)]={'quiet_start_decay_comparison':{
            'rms_db_01_to_15_seconds':rms((a-b)[100:1500]),
            'rms_db_01_to_3_seconds':rms((a-b)[100:3000]),
            'rms_db_3_to_79_seconds':rms((a-b)[3000:7900])},'history_differences':{}}
        for tag in ['0700','0500']:
            difference=curves[f'measured_{tag}_m{abs(level)}_loud']-curves[f'measured_{tag}_m{abs(level)}_quiet']
            comparisons[str(level)]['history_differences'][tag]={
                'loud_minus_quiet_db': [float(np.median(difference[int(left*1000):int(right*1000)])) for left,right in p['gain_windows_seconds']]}
        if level!=-44:
            early,pred=fit_local_exponential(a,3000)
            early['same_fit_vs_max_decay_005_to_3_seconds_rms_db']=rms((pred-b)[50:3000])
            early['extrapolated_min_decay_3_to_79_seconds_rms_db']=rms((pred-a)[3000:7900])
            whole,_=fit_local_exponential(a,7900)
            diagnostics[str(level)]={'early_window_fit':early,'whole_window_fit':whole,
                'interpretation':'Fitted asymptotes are not measured equilibrium. Fit-window sensitivity tests a universal one-pole claim, not circuit topology.'}
    result={'source_model_commit':commit,'protocol_sha256':hashlib.sha256(protocol_bytes).hexdigest(),
        'locator_results_sha256':p['locator_results_sha256'],'integrity':integrity,'calibration':calibration,
        'open_path_checks':open_checks,'quiet_reset_checks':quiet_checks,
        'evaluation':evaluation,'comparisons':comparisons,'posthoc_exponential_diagnostics':diagnostics,
        'curve_artifact':'ns2_history_curves.npz',
        'scope':'48 kHz and 1 kHz carrier, Threshold fixed at 3:00, Decay extremes, Input->Output only. Primary predictions frozen before recording, including one locator-derived open gain offset. No new-data fit in primary scores. Measured 10 ms coherent magnitude on 1 ms grid, separately checked with Hann projections/noise bins. RMS 0.1–7.9 s per probe; no reset gain ratios. Curve archive includes valid_gain_mask excluding 20 ms at both boundaries; centered-window contamination otherwise includes the next loud transition. One take per setting, no confidence intervals. Local exponential diagnostics are explicitly post-hoc; no production replacement is identified.'}
    (HERE/'ns2_history_results.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    curves['time_seconds']=np.arange(8000)/1000
    curves['valid_gain_mask']=(curves['time_seconds']>=.02)&(curves['time_seconds']<7.98)
    np.savez_compressed(HERE/'ns2_history_curves.npz',**curves)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,3,figsize=(14,8),layout='constrained')
    colors={'quiet':'#007d91','loud':'#bd6700'}
    for row,tag in enumerate(['0700','0500']):
        for col,level in enumerate(p['levels_db']):
            ax=axes[row,col]
            for history in ['quiet','loud']:
                ax.plot(curves['time_seconds'][20:7980],curves[f'measured_{tag}_m{abs(level)}_{history}'][20:7980],color=colors[history],label=f'Pedal after {history}')
                ax.plot(curves['time_seconds'][20:7980],curves[f'original_map_{tag}_m{abs(level)}_{history}'][20:7980],'--',color=colors[history],alpha=.7,label=f'Frozen after {history}')
            ax.set(title=f'Decay {"minimum" if tag=="0700" else "maximum"}; input {level} dB',xlabel='Seconds after probe starts',ylabel='Gain relative to bypass (dB)')
            ax.grid(alpha=.3);ax.legend(fontsize=7)
            if level==-44:ax.set(ylim=(-1,1.5))
    fig.suptitle('NS-2 matched history: measured trajectories and original frozen model')
    fig.savefig(HERE/'ns2_history_validation.png',dpi=150)
    fig,axes=plt.subplots(1,2,figsize=(11,4.5),layout='constrained')
    for ax,level in zip(axes,[-56,-54]):
        for tag,label in [('0700','Minimum Decay'),('0500','Maximum Decay')]:
            ax.plot(curves['time_seconds'][20:7980],curves[f'measured_{tag}_m{abs(level)}_quiet'][20:7980],label=label)
        ax.set(title=f'After 24 s silence: {level} dB probe',xlabel='Seconds after probe starts',ylabel='Gain relative to bypass (dB)')
        ax.grid(alpha=.3);ax.legend(fontsize=8)
    fig.suptitle('Nearly shared early opening; Decay-dependent late behavior')
    fig.savefig(HERE/'ns2_history_opening.png',dpi=150)
    print('Comparisons',json.dumps(comparisons,indent=2),flush=True)
    print('Diagnostics',json.dumps(diagnostics,indent=2),flush=True)


if __name__=='__main__':main()
