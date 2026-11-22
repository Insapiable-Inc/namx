"""Regression checks of exploratory NS-2 controls on untouched older captures.

No old gain/threshold/initial-state adjustment is fitted to candidate predictions.
The old dynamics files labelled bypass are treated as engaged at Threshold maximum.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from scipy.ndimage import uniform_filter1d, maximum_filter1d
from scipy import signal

from analyze_cs3_detector import FS,HERE,load,db,rms,biquad
from fit_ns2_control_candidates import COMMIT,THRESHOLDS,DECAYS,detector,baseline,candidate,FAMILIES,load_history,prediction


def amplitude(y,freq):
    size=max(480,int(round(5*FS/freq)))
    z=y*np.exp(-2j*np.pi*freq*np.arange(len(y))/FS)
    return 2*np.abs(uniform_filter1d(z.real,size)+1j*uniform_filter1d(z.imag,size))


def tag(position):return position.replace(':','').zfill(4)


def old_takes(root):
    first=Path('captures/boss/ns-2/2026-10-27')
    m,x,_,body=load(root,first,'bypass-jump',COMMIT)
    # True bypass at 1 kHz; the original dynamics analysis also used this
    # reference for 220 Hz. Its frequency-transfer limitation stays explicit.
    a=int(5*FS);b=int(7.5*FS)
    ref=float(np.median(amplitude(body[:,0],1000)[a:b])/np.median(amplitude(x[m['signal_start']:],1000)[a:b]))
    takes=[]
    for folder in [first,Path('captures/boss/ns-2/2026-10-27-dynamics')]:
        paths=subprocess.check_output(['git','ls-tree','-r','--name-only',COMMIT,str(folder)],cwd=root,text=True).splitlines()
        names=[Path(p).stem for p in paths if p.endswith('.json') and '/fit/' not in p]
        for name in names:
            if folder==first and not name.startswith(('stairs-threshold','jump-','plucks-')):continue
            if folder!=first and name=='rig':continue
            m,x,y,body=load(root,folder,name,COMMIT)
            assert m['xruns']==0,name
            settings=m['settings'] or {'Threshold':'5:00','Decay':'12:00'}
            th=tag(settings['Threshold']);dc=tag(settings['Decay']);freq=m['signal']['freq']
            source=x.astype(float)/10**(m['ref_dbfs']/20)
            levels=detector(source,FS);n=len(levels)//48
            levels=levels[:n*48].reshape(n,48).mean(axis=1)
            start=m['signal_start']//48
            input_amp=amplitude(x,freq)[::48][:n]
            output_amp=amplitude(y[m['latency_samples']:m['latency_samples']+n*48,0],freq)[::48]
            n=min(n,len(output_amp));levels=levels[:n];input_amp=input_amp[:n]
            measured=db(output_amp/np.maximum(input_amp*ref,1e-15))
            noise=float(np.median(output_amp[-500:]))
            input_db=db(input_amp/10**(m['ref_dbfs']/20))
            valid=(np.arange(n)>=start+30)&(input_db>-95)&(input_db<-6)&(output_amp>noise*10)
            # Equal masks for all models; remove estimator boundaries and abrupt
            # transitions. Noise criterion is a tail-carrier proxy, not an SNR CI.
            transitions=np.r_[False,abs(np.diff(input_db))>.5]
            valid&=~maximum_filter1d(transitions,size=61)
            if m['signal']['type']=='steps':
                step=m['signal']['step_seconds']
                for k in range(len(m['signal']['levels_db'])+1):
                    edge=start+int(k*step*1000);valid[max(0,edge-30):edge+30]=False
            gain0=.6
            tone=np.array([biquad('shelf',8168.4,.623,3.69)])
            response=signal.sosfreqz(tone,worN=[1000,freq],fs=FS)[1]
            tone_db=float(db(response[1]/response[0]))
            takes.append({'name':str(folder.name)+'/'+name,'folder':str(folder),'file':name,
                'threshold':THRESHOLDS[th],'threshold_tag':th,'decay':DECAYS[dc],'tag':dc,
                'gain0':gain0,'tone_db':tone_db,'levels':levels,'target':measured,'valid':valid,
                'start':start,'freq':freq,'reference_transfer':ref,'noise_proxy_dbfs':float(db(noise)),
                'played_sha256':m['played_sha256'],
                'recorded_sha256':hashlib.sha256((root/folder/(name+'.wav')).read_bytes()).hexdigest(),
                'reinterpretation':'Actually engaged: Threshold 5:00, Decay 12:00' if m['bypass'] else None})
    return takes,ref


def scores(takes,fits):
    rows=[]
    for t in takes:
        mask=t['valid'];row={k:t[k] for k in ['name','threshold_tag','tag','freq','noise_proxy_dbfs','played_sha256','recorded_sha256','reinterpretation']}
        row['scored_seconds']=float(mask.sum()/1000)
        preds={'baseline':baseline(t['levels'],t['threshold'],t['decay'],.001,t['gain0'])}
        for name,p in fits.items():preds[name]=prediction(t,p,FAMILIES[name])
        row['rms_db']={k:rms((p+t['tone_db']-t['target'])[mask]) for k,p in preds.items()}
        row['bias_db']={k:float(np.median((p+t['tone_db']-t['target'])[mask])) for k,p in preds.items()}
        rows.append(row)
    return rows


def full_rate_trace(root,take,p,family):
    folder=Path(take['folder']);m,x,_,_=load(root,folder,take['file'],COMMIT)
    source=x.astype(float)/10**(m['ref_dbfs']/20)
    levels=detector(source,FS)
    if family is None:g=baseline(levels,take['threshold'],take['decay'],1/FS,take['gain0'])
    else:g=candidate(levels,take['threshold'],take['decay'],np.asarray(p),family['kind'],1/FS,take['gain0'])
    sos=np.array([biquad('shelf',8168.4,.623,3.69)])
    norm=1/abs(signal.sosfreqz(sos,worN=[1000],fs=FS)[1][0])
    audio=signal.sosfilt(sos,source*10**(g/20))*norm
    measured_gain=db(amplitude(audio,take['freq'])/np.maximum(amplitude(source,take['freq']),1e-15))[::48]
    return measured_gain,g,audio,source


def full_rate_check(root,take,p,family,indices):
    measured_gain,g,audio,_=full_rate_trace(root,take,p,family)
    if family is None:surrogate=baseline(take['levels'],take['threshold'],take['decay'],.001,take['gain0'])+take.get('tone_db',0.)
    else:surrogate=prediction(take,p,family)+take.get('tone_db',0.)
    return {'coherent_waveform_vs_surrogate_rms_db':rms((measured_gain[:len(surrogate)]-surrogate)[indices]),
            'coherent_waveform_vs_measured_rms_db':rms((measured_gain[:len(take['target'])]-take['target'])[indices]),
            'max_abs_predicted_gain_db':float(np.max(abs(g))),
            'finite_output':bool(np.isfinite(audio).all())}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    args=parser.parse_args();root=args.root
    summary=json.loads((HERE/'ns2_control_candidate_results.json').read_text())['families']
    continuous=json.loads((HERE/'ns2_continuous_switch_screening.json').read_text())['families']['continuous_switch']
    fits={k:v['fit_parameters'] for k,v in summary.items()};fits['continuous_switch']=continuous['fit_parameters']
    capped=json.loads((HERE/'ns2_continuous_switch_capped_screening.json').read_text())['families']['continuous_switch_capped']
    fits['continuous_switch_capped']=capped['fit_parameters']
    takes,ref=old_takes(root)
    print('Loaded',len(takes),'older takes; bypass transfer',float(db(ref)),flush=True)
    rows=scores(takes,fits)
    result={'source_commit':COMMIT,'calibration':{'original_1khz_bypass_transfer_db':float(db(ref)),
        'limitation':'Original 220 Hz recordings use the 1 kHz bypass calibration; open-path spectral flatness is only known to about 0.2 dB. No candidate-specific recalibration. Old dynamics bypass names are engaged captures.'},
        'mask':'Recorded carrier >20 dB above tail coherent-amplitude median, input between -95 and -6 dB re reference; discard abrupt-transition +/-30 ms. Same mask for every model. Internal gain at 1 ms is an approximation, checked by sample-rate rendering.',
        'fit_parameters':fits,'regression':rows,'aggregate':{},'sample_rate_checks':{}}
    for model in ['baseline',*fits]:
        vals=np.array([r['rms_db'][model] for r in rows])
        result['aggregate'][model]={'median_take_rms_db':float(np.median(vals)),'worst_take_rms_db':float(max(vals)),
            'equal_take_rms_db':rms(vals),'takes_worse_than_baseline_by_05db':sum(r['rms_db'][model]>r['rms_db']['baseline']+.5 for r in rows)}
    selected=['2026-10-27-dynamics/plucks-t0300-d1200','2026-10-27-dynamics/plucks-t0500-d1200','2026-10-27-dynamics/soft-t0300-d1200','2026-10-27-dynamics/stairs-slow-t1200-d0700']
    for t in takes:
        if t['name'] in selected:
            result['sample_rate_checks'][t['name']]=full_rate_check(root,t,fits['continuous_switch'],FAMILIES['continuous_switch'],t['valid'])
            print('Full rate',t['name'],result['sample_rate_checks'][t['name']],flush=True)
    (HERE/'ns2_control_regression_results.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps(result['aggregate'],indent=2),flush=True)
    # Reusable measured curves and fixed masks for plots/refitting; raw recordings
    # remain untouched. This local cache is reproducible and not needed in Git.
    arrays={};meta=[]
    for i,t in enumerate(takes):
        for k in ['levels','target','valid']:arrays[f'{i}_{k}']=t[k]
        meta.append({k:v for k,v in t.items() if k not in ['levels','target','valid']})
    arrays['metadata_json']=np.array(json.dumps(meta))
    np.savez_compressed(HERE/'ns2_control_regression_cache.npz',**arrays)


if __name__=='__main__':main()
