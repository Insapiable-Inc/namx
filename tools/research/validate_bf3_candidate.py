"""Discrete-delay and stress checks for the BF-3 hp2_joint research candidate.

python tools/research/validate_bf3_candidate.py DATA_EXPORT
Includes the existing linear interpolation and previous-sample feedback state.
Not a compiled C++ test and not a listening verdict; production code is untouched.
"""
import json
import sys
from pathlib import Path
import numpy as np
from scipy import signal,optimize
from numba import njit

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'capture'))
import analyze_bf3 as bf

data=Path(sys.argv[1]); fits=json.loads((HERE/'bf3_complex_results.json').read_text())
candidate=fits['models']['hp2_joint'];f=bf.FREQS

def rms(x):return float(np.sqrt(np.mean(x*x)))
def biquad(kind,hz,q,fs):
    w=2*np.pi*hz/fs;c=np.cos(w);a=np.sin(w)/(2*q)
    b=[(1+c)/2,-(1+c),(1+c)/2] if kind=='hp' else [(1-c)/2,1-c,(1-c)/2]
    return np.r_[b,1+a,-2*c,1-a]/(1+a)

def coefficients(name,fs):
    if name in ['candidate','candidate_hp1']:
        model=candidate if name=='candidate' else fits['models']['hp1_joint']
        sos=np.array(model['sos'][str(fs)]);gain_db=model['gain_correction_db'];comp=model['tone_delay_compensation_ms']
    else:
        sos=np.array([biquad(k,hz,q,fs) for k,hz,q in [('hp',205.4,.573),('hp',97.3,1.784),('lp',6824.1,.481),('lp',18401.4,.817)]])
        gain_db=0;comp=.068
    gain=10**(gain_db/20)/abs(signal.sosfreqz(sos,worN=[1000],fs=fs)[1][0])
    return sos,gain,comp

rows=[];curves={}
for row in fits['evaluation']:
    s=data/'captures/boss/bf-3'/row['session'];name=row['name']
    ref='bypass-sweep-24' if row['session'].endswith('quiet') else 'bypass-sweep'
    measured=bf.fine_db(np.fft.rfft(bf.impulse(s,name))/np.fft.rfft(bf.impulse(s,ref)))
    d0=bf.MODEL['manual_ms'][bf.POSITIONS.index(row['settings']['Manual'])]
    r=bf.MODEL['res'][bf.POSITIONS.index(row['settings']['Res'])]
    band=(f>=100)&(f<=10000);low=(f>=80)&(f<200);out={};predictions={}
    for name_model in ['current','candidate','candidate_current_wet_feedback','candidate_hp1_current_wet_feedback']:
        filter_name='current' if name_model=='current' else ('candidate_hp1' if 'hp1' in name_model else 'candidate')
        sos,g,comp=coefficients(filter_name,48000)
        w=signal.sosfreqz(sos,worN=f,fs=48000)[1]*g
        z=np.exp(-2j*np.pi*f/48000)
        def predict(d):
            ds=(d-comp)*48;whole=int(np.floor(ds));frac=ds-whole
            wet=w*z**whole*((1-frac)+frac*z)
            # An alternate causal schedule can compute current wet from already
            # stored samples BEFORE writing x+r*wet; delays always exceed one sample.
            fb_delay=1 if name_model.endswith('current_wet_feedback') else z
            return bf.fine_db(10**(-.13/20)+10**(1.45/20)*wet/(1-r*fb_delay*wet))
        grid=np.linspace(d0*.96,d0*1.04,101);errs=[rms((predict(d)-measured)[band]) for d in grid]
        k=int(np.argmin(errs));fit=optimize.minimize_scalar(lambda d:rms((predict(d)-measured)[band]),bounds=(grid[max(0,k-1)],grid[min(100,k+1)]),method='bounded')
        e=predict(fit.x)-measured
        predictions[name_model]=predict(fit.x)
        out[name_model]={'rms_100_10000_db':rms(e[band]),'rms_80_200_db':rms(e[low]),
            'rms_10000_16000_db':rms(e[(f>=10000)&(f<=16000)]),'delay_ms':fit.x,
            'rms_100_10000_table_delay_db':rms((predict(d0)-measured)[band]),
            'at_delay_bound':bool(min(abs(fit.x-grid[0]),abs(fit.x-grid[-1]))<1e-4)}
    rows.append({'take':row['name'],'session':row['session'],'used_in_joint_training':row['used_in_joint_training'],'models':out})
    if name in ['manual-0500','std-res-0500-24']:curves[name]=(measured,predictions)

@njit
def render(x,fs,sos,g,comp,r,rate,current_feedback=False,fixed_ms=-1.):
    line=np.zeros(int(fs*.03)+8);states=np.zeros((len(sos),2));out=np.zeros(len(x))
    idx=0;fb=0.;phase=0.;smooth=0.;a=1-np.exp(-2*np.pi*1.53/fs)
    for n in range(len(x)):
        tri=1-abs(2*phase-1);smooth+=a*(tri-smooth)
        delay_ms=.33+14.582*smooth**2.5 if fixed_ms<0 else fixed_ms
        d=(delay_ms-comp)*fs/1000
        if not current_feedback:line[idx]=x[n]+r*fb
        whole=int(d);frac=d-whole
        wet=line[(idx-whole)%len(line)]*(1-frac)+line[(idx-whole-1)%len(line)]*frac
        for j in range(len(sos)):
            q=sos[j];y=q[0]*wet+states[j,0]
            states[j,0]=q[1]*wet-q[4]*y+states[j,1]
            states[j,1]=q[2]*wet-q[5]*y;wet=y
        fb=wet*g;out[n]=fb
        if current_feedback:line[idx]=x[n]+r*fb
        idx=(idx+1)%len(line);phase=(phase+rate/fs)%1
    return out

stress=[];impulse_checks=[];rng=np.random.default_rng(123)
for fs in [44100,48000,96000]:
    sos,g,comp=coefficients('candidate',fs)
    grid=np.linspace(0,fs/2,100000)
    max_gain=float(np.max(abs(signal.sosfreqz(sos,worN=grid,fs=fs)[1]*g)))
    for schedule in [False,True]:
        for rate in [.055,1.,10.]:
            x=np.zeros(8*fs);x[:2*fs]=rng.normal(0,.02,2*fs)
            y=render(x,fs,sos,g,comp,.849,rate,schedule)
            stress.append({'fs':fs,'rate_hz':rate,'current_wet_feedback':schedule,
                'max_static_loop_magnitude_grid':.849*max_gain,
                'wet_peak':float(np.max(abs(y))),'wet_last_second_rms':rms(y[-fs:]),
                'all_finite':bool(np.all(np.isfinite(y)))})
        x=np.zeros(131072);x[0]=1
        y=render(x,fs,sos,g,comp,.849,1.,schedule,.447)
        hz=np.fft.rfftfreq(len(x),1/fs);z=np.exp(-2j*np.pi*hz/fs)
        ds=(.447-comp)*fs/1000;whole=int(ds);frac=ds-whole
        w=signal.sosfreqz(sos,worN=hz,fs=fs)[1]*g*z**whole*(1-frac+frac*z)
        expected=w/(1-.849*(1 if schedule else z)*w)
        error=float(np.max(abs(np.fft.rfft(y)-expected)))
        assert error<1e-8, (fs,schedule,error)
        impulse_checks.append({'fs':fs,'current_wet_feedback':schedule,'max_complex_error':error})
host_rates=[]
hz=np.geomspace(100,16000,1000)
reference_sos,reference_g,_=coefficients('candidate',48000)
reference=signal.sosfreqz(reference_sos,worN=hz,fs=48000)[1]*reference_g
for fs in [44100,96000]:
    sos,g,_=coefficients('candidate',fs)
    e=20*np.log10(abs(signal.sosfreqz(sos,worN=hz,fs=fs)[1]*g/reference))
    host_rates.append({'fs':fs,'max_magnitude_difference_from_48k_below_10k_db':float(np.max(abs(e[hz<10000]))),
        'max_magnitude_difference_from_48k_to_16k_db':float(np.max(abs(e)))})
result={'discrete_delay_evaluation':rows,'stress':stress,'impulse_transfer_checks':impulse_checks,
    'host_rate_response':host_rates,
    'scope':'Standard only; numerical research renderer, no output clipping; maximum Res .849; two seconds noise plus six seconds silence. Static small-gain bounds sampled densely, not an analytic certificate of time-varying stability.'}
(HERE/'bf3_candidate_validation.json').write_text(json.dumps(result,indent=2))
for row in rows: print(row['take'],{k:round(v['rms_100_10000_db'],3) for k,v in row['models'].items()})
print('Stress:',stress)

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axs=plt.subplots(2,2,figsize=(13,8),layout='constrained',sharex=True)
labels={'current':'Current implementation equation',
        'candidate_current_wet_feedback':'HP2 candidate, current wet feedback',
        'candidate_hp1_current_wet_feedback':'HP1 alternative, current wet feedback'}
for column,(name,(measured,predictions)) in enumerate(curves.items()):
    axs[0,column].semilogx(f,measured,color='black',label='Pedal')
    for model,label in labels.items():
        axs[0,column].semilogx(f,predictions[model],label=label,alpha=.8)
        axs[1,column].semilogx(f,predictions[model]-measured,label=label,alpha=.8)
    shown=(f>=80)&(f<=16000)
    levels=np.concatenate([measured[shown]]+[predictions[m][shown] for m in labels])
    residuals=np.concatenate([(predictions[m]-measured)[shown] for m in labels])
    axs[0,column].set(title=name,ylabel='dB relative to bypass',
        ylim=(np.floor(levels.min())-1,np.ceil(levels.max())+1))
    axs[0,column].legend(fontsize=8)
    axs[1,column].axhline(0,color='black',linewidth=.6)
    axs[1,column].set(xlabel='Hz',ylabel='Model minus pedal (dB)',
        ylim=(np.floor(residuals.min())-1,np.ceil(residuals.max())+1))
    for ax in axs[:,column]:ax.set_xlim(80,16000);ax.grid(alpha=.4)
fig.suptitle('BF-3 Standard: linear delay interpolation; bounded delay fit, fixed gains and filters')
fig.savefig(HERE/'bf3_discrete_validation.png',dpi=150)
