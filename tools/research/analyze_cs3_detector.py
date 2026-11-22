"""Frozen-parameter CS-3 detector validation against the 2026-10-30 captures.

python tools/research/analyze_cs3_detector.py [REPOSITORY]
Uses old raw Level and low-level staircase measurements only to map physical noon
controls to the application's gain/threshold parameters. Never fits new dynamics.
Old metadata can be read from the pre-capture Git commit if absent in the checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf
from scipy import ndimage, signal, optimize
from numba import njit

HERE=Path(__file__).resolve().parent
FS=48000
TAU={'0700':(1.97,189.1),'0500':(1.58,16.4)}
CEILING=-14.25
SLOPE=.0117


def db(x):return 20*np.log10(np.maximum(np.abs(x),1e-15))
def rms(x):return float(np.sqrt(np.mean(np.asarray(x)**2)))


def metadata(root,relative,name,commit):
    path=root/relative/(name+'.json')
    if path.exists():return json.loads(path.read_text())
    return json.loads(subprocess.check_output(['git','show',commit+':'+str(relative/(name+'.json'))],cwd=root))


def load(root,relative,name,commit):
    m=metadata(root,relative,name,commit)
    y,fs=sf.read(root/relative/(name+'.wav'))
    x,xfs=sf.read(root/relative/'played'/(name+'.wav'),dtype='float32')
    assert fs==xfs==FS
    assert hashlib.sha256(x.tobytes()).hexdigest()==m['played_sha256']
    start=m['signal_start']+m['latency_samples']
    return m,x,y,y[start:]


def cycle_envelope(y,freq):
    # Same one-carrier-period estimator for measured, bypass and predicted waveforms.
    # At 110 Hz a period is ~9.09 ms; 1 ms output spacing is not independent resolution.
    n=int(round(FS/freq))
    return np.sqrt(np.maximum(ndimage.uniform_filter1d(y*y,size=n,mode='nearest'),1e-30))[::48]


def biquad(kind,hz,q,gain_db=0.):
    w=2*np.pi*hz/FS;c=np.cos(w);alpha=np.sin(w)/(2*q);a=10**(gain_db/40)
    if kind=='hp':b=[(1+c)/2,-(1+c),(1+c)/2];den=[1+alpha,-2*c,1-alpha]
    elif kind=='lp':b=[(1-c)/2,1-c,(1-c)/2];den=[1+alpha,-2*c,1-alpha]
    else:
        rt=2*np.sqrt(a)*alpha
        b=[a*((a+1)-(a-1)*c+rt),2*a*((a-1)-(a+1)*c),a*((a+1)-(a-1)*c-rt)]
        den=[(a+1)+(a-1)*c+rt,-2*((a-1)+(a+1)*c),(a+1)+(a-1)*c-rt]
    return np.r_[b,den]/den[0]


def tone_filter():
    sos=np.array([biquad('hp',38.3,.467),biquad('lp',18000,.482),
                  biquad('shelf',655.8,.480,.57),biquad('shelf',5456.4,.661,1.95)])
    g=1/abs(signal.sosfreqz(sos,worN=[1000],fs=FS)[1][0])
    return sos,g


@njit
def compress(x,attack_ms,release_ms,sustain_db,makeup_db):
    a=1-np.exp(-1000/(FS*attack_ms));b=1-np.exp(-1000/(FS*release_ms))
    env=0.;out=np.empty(len(x));gain_db=np.empty(len(x))
    for n in range(len(x)):
        rectified=abs(x[n]);env+=(a if rectified>env else b)*(rectified-env)
        level=20*np.log10(max(env,1e-15))
        reduction=(1-SLOPE)*max(0.,level+sustain_db-CEILING)
        gain_db[n]=sustain_db+makeup_db-reduction
        out[n]=x[n]*10**(gain_db[n]/20)
    return out,gain_db


def plateau_gain(g,k):return float(np.median(g[k*4000+2500:k*4000+3800]))


def release_metrics(g,start,frequency):
    idx=int(start*1000);initial=float(np.median(g[idx-1500:idx-200]));final=float(np.median(g[idx+2500:idx+3800]))
    first=max(5,int(np.ceil(2*1000/frequency)))
    segment=g[idx:idx+2000];out={'start_gain_db':initial,'final_gain_db':final,'excursion_db':final-initial}
    for fraction in [.632,.9]:
        hits=np.flatnonzero(segment[first:]>=initial+fraction*(final-initial))
        out['t'+str(int(round(fraction*100)))+'_ms']=int(first+hits[0]) if len(hits) else None
    return out


def harmonics(y,freq,start,end):
    v=y[int(start*FS):int(end*FS)];t=np.arange(len(v))/FS
    amp=np.array([2*abs(np.mean(v*np.exp(-2j*np.pi*freq*k*t))) for k in range(1,6)])
    noises=[]
    for k in range(1,6):
        noises.append(float(np.median([2*abs(np.mean(v*np.exp(-2j*np.pi*(freq*k+off)*t))) for off in [-7,-5,5,7]])))
    return {'h2_h5_dbc':db(amp[1:]/amp[0]).tolist(),'thd2_5_percent':float(np.linalg.norm(amp[1:])/amp[0]*100),
            'harmonic_snr_db_vs_nearby_bins':db(amp/np.maximum(noises,1e-15)).tolist()}


def exploratory_release_tau(x,frequency,measured,down,attack_ms,release_ms,sustain,makeup,sos,tone_gain):
    """Single-drop endpoint-normalized tau refit: diagnostic, never a held-out score."""
    segment=x[int((down-2)*FS):int((down+4)*FS)]
    base=cycle_envelope(segment,frequency)
    first=max(5,int(np.ceil(2000/frequency)));last=1200
    target=release_metrics(measured,down,frequency);idx=down*1000
    target_shape=(measured[idx+first:idx+last]-target['start_gain_db'])/target['excursion_db']
    def objective(tau):
        y,_=compress(segment,attack_ms,tau,sustain,makeup)
        y=signal.sosfilt(sos,y)*tone_gain
        g=db(cycle_envelope(y,frequency)/base)
        initial=float(np.median(g[500:1800]));final=float(np.median(g[4500:5800]))
        shape=(g[2000+first:2000+last]-initial)/(final-initial)
        return rms(shape-target_shape)*target['excursion_db']
    fit=optimize.minimize_scalar(objective,bounds=(release_ms*.5,release_ms*1.7),method='bounded',options={'xatol':1e-4})
    return {'fitted_tau_ms':float(fit.x),'factor_over_frozen':float(fit.x/release_ms),
            'endpoint_normalized_rms_db_equivalent':float(fit.fun)}


def onset_metrics(g,start,frequency):
    idx=int(start*1000);settled=float(np.median(g[idx+2500:idx+3800]));segment=g[idx:idx+200]
    duration=max(20,int(np.ceil(2000/frequency)))
    settle=None
    for t in range(len(segment)-duration):
        if np.all(abs(segment[t:t+duration]-settled)<=1.):settle=t;break
    return {'one_cycle_rms_overshoot_db':float(np.max(segment)-settled),
            'within_1db_for_at_least_20ms_at_ms':settle,
            'measurement_window_ms':round(FS/frequency)/FS*1000}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1]);args=parser.parse_args();root=args.root.resolve()
    new=Path('captures/boss/cs-3/2026-10-30-detector-check');old=Path('captures/boss/cs-3/2026-10-27')
    protocol=json.loads((root/new/'analysis-protocol.json').read_text());commit=protocol['model_reference_commit']
    header=subprocess.check_output(['git','show',commit+':'+protocol['model_header']],cwd=root)
    assert hashlib.sha256(header).hexdigest()==protocol['model_header_sha256']
    # The application exposes Threshold and makeup, not a physical Sustain/Level map.
    # Recover only those two nuisance control mappings from OLD captures at 1 kHz.
    old_data={n:load(root,old,n,commit) for n in ['level-1200','level-0500','bypass-stairs','stairs-sustain-1200']}
    makeup=float(db(rms(old_data['level-1200'][3][FS:int(2.8*FS),0])/rms(old_data['level-0500'][3][FS:int(2.8*FS),0])))
    quiet=[]
    for k in range(3):
        sl=slice(int((k+.6)*1.5*FS),int((k+1)*1.5*FS))
        quiet.append(float(db(rms(old_data['stairs-sustain-1200'][3][sl,0])/rms(old_data['bypass-stairs'][3][sl,0]))))
    sustain=float(np.mean(quiet)-makeup)
    sos,tone_gain=tone_filter();integrity={};rows={};curves={};bypass={}
    for freq in [110,1000]:bypass[freq]=load(root,new,f'bypass-steps-{freq}',commit)
    for name in [f'bypass-steps-{f}' for f in [110,1000]]+[f'attack-{a}-steps-{f}' for a in TAU for f in [110,1000]]:
        m,x,y,body=load(root,new,name,commit);frequency=m['signal']['freq'];length=24*FS
        assert m['signal']['levels_db']==[-36,-12,-36,-24,-6,-24]
        integrity[name]={'recorded_at':m['recorded_at'],'settings':m['settings'],'xruns':m['xruns'],
                         'latency_samples':m['latency_samples'],'marker_quality':m['latency_quality'],
                         'played_sha256':m['played_sha256'],'recorded_sha256':hashlib.sha256((root/new/(name+'.wav')).read_bytes()).hexdigest(),
                         'abs_ge_0999':int(np.sum(np.abs(y)>=.999)),
                         'tail_rms_dbfs':db(np.sqrt(np.mean(y[-FS//2:]**2,axis=0))).tolist(),
                         'settled_channel_rms_dbfs':[[float(db(rms(body[int((4*k+2.5)*FS):int((4*k+3.8)*FS),c]))) for c in [0,1]] for k in range(6)]}
        if name.startswith('bypass'):continue
        attack=name.split('-')[1];att,rel=TAU[attack]
        # Render the whole marker/gap preamble in normalized guitar-reference units.
        xn=x.astype(float)/10**(m['ref_dbfs']/20)
        rendered,internal_gain=compress(xn,att,rel,sustain,makeup)
        rendered=signal.sosfilt(sos,rendered)*tone_gain
        start=m['signal_start'];model_body=rendered[start:start+length];input_body=xn[start:start+length]
        measured_y=body[:length,0];reference=bypass[frequency][3][:length,0]
        measured=db(cycle_envelope(measured_y,frequency)/cycle_envelope(reference,frequency))
        predicted=db(cycle_envelope(model_body,frequency)/cycle_envelope(input_body,frequency))
        plateau_m=[plateau_gain(measured,k) for k in range(6)];plateau_p=[plateau_gain(predicted,k) for k in range(6)]
        releases=[]
        for down in [8,20]:
            idx=down*1000;first=max(5,int(np.ceil(2000/frequency)));stop=1200
            gm=release_metrics(measured,down,frequency);gp=release_metrics(predicted,down,frequency)
            error=predicted[idx+first:idx+stop]-measured[idx+first:idx+stop]
            # Secondary endpoint normalization removes a gain offset AND an excursion
            # difference, so it is a shape diagnostic, not absolute prediction accuracy.
            shape_m=(measured[idx+first:idx+stop]-gm['start_gain_db'])/gm['excursion_db']
            shape_p=(predicted[idx+first:idx+stop]-gp['start_gain_db'])/gp['excursion_db']
            releases.append({'drop_at_s':down,'evaluation_from_ms':first,'evaluation_to_ms':stop,
                             'measured':gm,'predicted':gp,'absolute_rms_db':rms(error),
                             'final_offset_removed_rms_db':rms(error-(gp['final_gain_db']-gm['final_gain_db'])),
                             'endpoint_normalized_shape_rms_fraction':rms(shape_p-shape_m),
                             'endpoint_normalized_shape_rms_db_equivalent':rms(shape_p-shape_m)*gm['excursion_db'],
                             'first_200ms_shape_rms_db_equivalent':rms((shape_p-shape_m)[:200-first])*gm['excursion_db'],
                             'diagnostic_tau_refit':exploratory_release_tau(input_body,frequency,measured,down,att,rel,sustain,makeup,sos,tone_gain)})
        harmonic_rows=[]
        for k in [1,3,4]:
            harmonic_rows.append({'input_db':m['signal']['levels_db'][k],
                                  'measured':harmonics(measured_y,frequency,k*4+2.8,k*4+3.8),
                                  'model':harmonics(model_body,frequency,k*4+2.8,k*4+3.8),
                                  'bypass':harmonics(reference,frequency,k*4+2.8,k*4+3.8)})
        rows[name]={'frequency':frequency,'attack_tau_ms':att,'release_tau_ms':rel,'settled_gain_measured_db':plateau_m,
                    'settled_gain_predicted_db':plateau_p,'settled_gain_error_db':(np.array(plateau_p)-plateau_m).tolist(),
                    'releases':releases,'harmonics':harmonic_rows,
                    'attacks':[{'rise_at_s':up,'measured':onset_metrics(measured,up,frequency),
                                'predicted':onset_metrics(predicted,up,frequency)} for up in [4,16]]}
        curves[name]=(measured,predicted)
        print(name,'plateau errors',np.round(np.array(plateau_p)-plateau_m,3),'release',[(v['absolute_rms_db'],v['endpoint_normalized_shape_rms_db_equivalent'],v['measured']['t63_ms'],v['predicted']['t63_ms']) for v in releases],flush=True)
    # Reproduce the old 30 dB experiment under the same renderer and extractor.
    # This separates a new generalization failure from a renderer/calibration error.
    old_comparison={}
    by=load(root,old,'bypass-jump',commit)[3][:12*FS,0]
    for attack,(att,rel) in TAU.items():
        m,x,y,body=load(root,old,'jump-attack-'+attack,commit)
        xn=x.astype(float)/10**(m['ref_dbfs']/20);out,_=compress(xn,att,rel,sustain,makeup)
        out=signal.sosfilt(sos,out)*tone_gain;start=m['signal_start']
        gm=db(cycle_envelope(body[:12*FS,0],1000)/cycle_envelope(by,1000))
        gp=db(cycle_envelope(out[start:start+12*FS],1000)/cycle_envelope(xn[start:start+12*FS],1000))
        dm=release_metrics(gm,8,1000);dp=release_metrics(gp,8,1000);mask=slice(8005,9200)
        shape=(gp[mask]-dp['start_gain_db'])/dp['excursion_db']-(gm[mask]-dm['start_gain_db'])/dm['excursion_db']
        old_comparison[attack]={'measured':dm,'predicted':dp,'absolute_rms_db':rms(gp[mask]-gm[mask]),
                                'endpoint_normalized_shape_rms_db_equivalent':rms(shape)*dm['excursion_db']}
    loop_checks={}
    for name in rows:
        m,x,y,body=load(root,new,name,commit);f=m['signal']['freq'];ref=bypass[f][3]
        sl=slice(int(6.5*FS),int(7.5*FS));t=np.arange(FS)/FS
        carrier=np.exp(-2j*np.pi*f*t)
        ratio=np.mean(body[sl,1]*carrier)/np.mean(ref[sl,1]*carrier)
        loop_checks[name]={'fundamental_gain_change_db':float(db(ratio)),
                           'phase_equivalent_delay_samples':float(-np.angle(ratio)*FS/(2*np.pi*f))}
    result={'source_model_commit':commit,'source_model_header_sha256':protocol['model_header_sha256'],
            'old_capture_mapping':{'level_noon_makeup_db':makeup,'uncompressed_noon_gain_db':float(np.mean(quiet)),
                                   'sustain_gain_db':sustain,'threshold_db_re_reference':CEILING-sustain,
                                   'source_takes':list(old_data),'source_session':str(old)},
            'frozen_constants':{'ceiling_db':CEILING,'slope':SLOPE,'tau_ms':TAU},'integrity':integrity,'evaluation':rows,
            'old_30db_jump_reproduction':old_comparison,
            'loopback_vs_same_carrier_bypass':loop_checks,
            'scope':'Sample-rate 48 kHz; double-precision equation renderer, not compiled C++; primary scores have no new-data parameter fit. Separate single-drop tau refits and endpoint-normalized scores are diagnostics only. Centered one-carrier-period RMS gain with 1 ms output grid; this is ~9.09 ms resolution at 110 Hz.'}
    (HERE/'cs3_detector_results.json').write_text(json.dumps(result,indent=2)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axs=plt.subplots(2,2,figsize=(12,8),layout='constrained')
    for row,attack in enumerate(TAU):
        for col,down in enumerate([8,20]):
            ax=axs[row,col]
            for freq,color in [(110,'#007e87'),(1000,'#ca6500')]:
                measured,predicted=curves[f'attack-{attack}-steps-{freq}'];idx=down*1000
                first=max(5,int(np.ceil(2000/freq)));last=1200 if attack=='0700' else 200
                t=np.arange(first,last)
                ax.plot(t,measured[idx+first:idx+last],color=color,label=f'{freq} Hz pedal')
                ax.plot(t,predicted[idx+first:idx+last],color=color,linestyle='--',label=f'{freq} Hz model')
            ax.set(title=f'Attack {attack}; drop at {down} s',xlabel='ms after drop',ylabel='Gain relative to bypass (dB)');ax.grid(alpha=.4);ax.legend(fontsize=8)
    fig.suptitle('CS-3 new jumps: existing time constants and old-session control calibration')
    fig.savefig(HERE/'cs3_detector_validation.png',dpi=150)


if __name__=='__main__':main()
