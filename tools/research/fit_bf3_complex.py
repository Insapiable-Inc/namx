"""Fit small stable BF-3 wet filters, then evaluate untrained frozen combs.

python tools/research/fit_bf3_complex.py /tmp/namx-dsp-audit/branch

The data argument is an export containing capture metadata and linked WAVs.
Training: long-delay Manual 7:00 wet window; joint candidates also use Manual 12:00.
Evaluation adjusts delay within +/-4%, but never gain or filter coefficients.
This is retrospective parameter holdout, not a prospective blind experiment.
No production code is written. Outputs include coefficients at 44.1/48/96 kHz.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy import optimize, signal

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('data', type=Path)
args = parser.parse_args()
sys.path.insert(0, str(HERE.parent/'capture'))
import analyze_bf3 as bf

FS = 48000
SESS = args.data/'captures/boss/bf-3/2026-10-27-standard'
st = bf.Static(SESS)
freq = bf.FREQS
f = np.geomspace(100, 14000, 600)

def complex_interp(x, xp, y):
    return np.interp(x, xp, y.real) + 1j*np.interp(x, xp, y.imag)

def db(x): return 20*np.log10(np.maximum(np.abs(x), 1e-12))
def rms(x): return float(np.sqrt(np.mean(np.square(x))))

# Fixed reference delay and gain conventions, shared with the original model.
BASE_MS = bf.MODEL['manual_ms'][0]
WET_GAIN = 10**(1.45/20)
target_full = st.wet/st.bypass * np.exp(2j*np.pi*freq*(st.dry_ms+BASE_MS)/1000) / WET_GAIN
target = complex_interp(f, freq, target_full)

def biquad(kind, hz, q, fs):
    w = 2*np.pi*hz/fs
    c, a = np.cos(w), np.sin(w)/(2*q)
    if kind == 'hp': b = [(1+c)/2, -(1+c), (1+c)/2]
    else: b = [(1-c)/2, 1-c, (1-c)/2]
    return np.array([*b, 1+a, -2*c, 1-a])/ (1+a)

def sections(p, kind, fs=FS):
    kind=kind.removesuffix('_joint')
    vals = np.exp(p[:-2])
    if kind == 'hp1':
        g = np.tan(np.pi*vals[0]/fs)
        sos = [[1/(1+g),-1/(1+g),0,1,(g-1)/(g+1),0]]
        rest = vals[1:]
    else:
        sos = [biquad('hp',vals[0],vals[1],fs)]
        rest = vals[2:]
    for i in range(0,len(rest),2): sos.append(biquad('lp',rest[i],rest[i+1],fs))
    return np.array(sos)

def response(p, kind, hz, fs=FS):
    h=signal.sosfreqz(sections(p,kind,fs),worN=hz,fs=fs)[1]
    ref=abs(signal.sosfreqz(sections(p,kind,fs),worN=[1000],fs=fs)[1][0])
    return h/ref * 10**(p[-1]/20) * np.exp(2j*np.pi*hz*p[-2]/1000)

# Relative complex residual: floor limits leverage from uncertain deep bass/wall bins.
weight = 1/np.maximum(abs(target),.18)
rng=np.random.default_rng(20261030)
models={}
for kind in ['hp1','hp2']:
    init=[180,6000,.55,18000,.8] if kind=='hp1' else [180,.55,6000,.55,18000,.8]
    lo=[30,1500,.25,9000,.25] if kind=='hp1' else [30,.25,1500,.25,9000,.25]
    hi=[600,12000,2,21000,2] if kind=='hp1' else [600,2,12000,2,21000,2]
    p0=np.r_[np.log(init),.05,0]
    low=np.r_[np.log(lo),0,-1.5]; high=np.r_[np.log(hi),.2,1.5]
    def fun(p):
        e=(response(p,kind,f)-target)*weight
        return np.r_[e.real,e.imag]
    fits=[]
    for k in range(8):
        start=p0.copy() if k==0 else np.clip(p0+np.r_[rng.normal(0,.2,len(init)),rng.normal(0,.01),rng.normal(0,.1)],low+1e-7,high-1e-7)
        fit=optimize.least_squares(fun,start,bounds=(low,high),max_nfev=2500,ftol=1e-10,xtol=1e-10,gtol=1e-10)
        fits.append(fit)
    fit=min(fits,key=lambda x:np.sum(x.fun**2));p=fit.x
    models[kind]={'p':p.tolist(),'weighted_complex_rms':rms(fit.fun),
        'tone_delay_compensation_ms':float(p[-2]),'gain_correction_db':float(p[-1]),
        'sections_hz_q':np.exp(p[:-2]).tolist(), 'start_costs':[rms(x.fun) for x in fits],
        'sos':{str(fs):sections(p,kind,fs).tolist() for fs in [44100,48000,96000]},
        'max_pole_radius':{str(fs):float(max(abs(np.roots(row[3:])).max() for row in sections(p,kind,fs))) for fs in [44100,48000,96000]}}
    print(kind,models[kind],flush=True)

# The wet window is least reliable in the bass. Test whether a second full response
# constrains that region without increasing runtime filter order. Its common clock
# shift is a nuisance separate from the dry-to-wet delay, neither changes the gain.
train_name='manual-1200'
train_h=complex_interp(f,freq,np.fft.rfft(bf.impulse(SESS,train_name))/np.fft.rfft(bf.impulse(SESS,'bypass-sweep')))
train_h*=np.exp(2j*np.pi*f*st.dry_ms/1000)
for base in ['hp1','hp2']:
    start=np.r_[models[base]['p'],2.005,0.0]
    n=len(start)-4
    lo0=[30,1500,.25,9000,.25] if base=='hp1' else [30,.25,1500,.25,9000,.25]
    hi0=[600,12000,2,21000,2] if base=='hp1' else [600,2,12000,2,21000,2]
    lower=np.r_[np.log(lo0),0,-1.5,2.005*.96,-.04]
    upper=np.r_[np.log(hi0),.2,1.5,2.005*1.04,.04]
    def objective(v):
        w=response(v[:-2],base,f)
        # Preserve long-delay information above 250 Hz; the full comb constrains bass.
        e0=(w-target)*weight*np.where(f<250,.1,1)
        pred=(10**(-.13/20)+WET_GAIN*w*np.exp(-2j*np.pi*f*v[-2]/1000))*np.exp(-2j*np.pi*f*v[-1]/1000)
        e1=(pred-train_h)/np.maximum(abs(train_h),.35)
        return np.r_[e0.real,e0.imag,e1.real,e1.imag]
    fit=optimize.least_squares(objective,start,bounds=(lower,upper),max_nfev=2500,ftol=1e-10,xtol=1e-10,gtol=1e-10)
    p=fit.x[:-2]; kind=base+'_joint'
    models[kind]={'p':p.tolist(),'weighted_complex_rms':rms(fit.fun),
        'tone_delay_compensation_ms':float(p[-2]),'gain_correction_db':float(p[-1]),
        'sections_hz_q':np.exp(p[:-2]).tolist(),'training_extra':train_name,
        'training_delay_ms':float(fit.x[-2]),'training_clock_shift_ms':float(fit.x[-1]),
        'sos':{str(fs):sections(p,kind,fs).tolist() for fs in [44100,48000,96000]},
        'max_pole_radius':{str(fs):float(max(abs(np.roots(row[3:])).max() for row in sections(p,kind,fs))) for fs in [44100,48000,96000]}}
    print(kind,models[kind]['sections_hz_q'],models[kind]['weighted_complex_rms'],flush=True)

current={**bf.MODEL, 'tone':[('hp',205.4,.573),('hp',97.3,1.784),('lp',6824.1,.481),('lp',18401.4,.817)], 'dry_db':-.13,'wet_db':1.45}
bf.MODEL=current
tone_current=bf.model_tone(freq)*np.exp(2j*np.pi*freq*.068/1000)
tones={'current':tone_current,**{kind:response(np.array(v['p']),kind,freq) for kind,v in models.items()}}
measurements=[]
cases=[(SESS,n,'bypass-sweep') for n in ['manual-0900','manual-1200','manual-0300','manual-0500','res-0900','res-1200','res-0300','res-0300-manual-0700','res-0300-manual-0500']]
quiet=args.data/'captures/boss/bf-3/2026-10-27-res-quiet'
# The other quiet captures are ULTRA (recorded in free-text notes, not settings).
cases += [(quiet,n,'bypass-sweep-24') for n in ['std-manual-1200-24','std-res-0500-24']]
curves={}
for s,name,ref in cases:
    m=json.loads((s/(name+'.json')).read_text())
    measured=bf.fine_db(np.fft.rfft(bf.impulse(s,name))/np.fft.rfft(bf.impulse(s,ref)))
    delay=bf.MODEL['manual_ms'][bf.POSITIONS.index(m['settings']['Manual'])]
    r=bf.MODEL['res'][bf.POSITIONS.index(m['settings']['Res'])]
    band=(freq>=100)&(freq<=10000)
    row={'session':s.name,'name':name,'settings':m['settings'],
         'used_in_joint_training':s==SESS and name==train_name,'models':{}}
    ys={}
    for label,tone in tones.items():
        def pred(d):
            z=tone*np.exp(-2j*np.pi*freq*d/1000)
            return bf.fine_db(10**(-.13/20)+WET_GAIN*z/(1-r*z))
        # Global coarse bracket avoids confusing neighboring comb minima.
        grid=np.linspace(delay*.96,delay*1.04,81)
        vals=[rms((pred(d)-measured)[band]) for d in grid]
        k=int(np.argmin(vals)); a=grid[max(0,k-1)];b=grid[min(len(grid)-1,k+1)]
        fit=optimize.minimize_scalar(lambda d:rms((pred(d)-measured)[band]),bounds=(a,b),method='bounded')
        y=pred(fit.x);e=y-measured;ys[label]=y
        metrics={'delay_ms':float(fit.x),'rms_db':rms(e[band]),'rms_db_table_delay':rms((pred(delay)-measured)[band])}
        for low,high in [(80,200),(200,1000),(1000,10000),(10000,16000)]:
            metrics[f'rms_{low}_{high}_db']=rms(e[(freq>=low)&(freq<high)])
        metrics['at_delay_bound']=bool(min(abs(fit.x-grid[0]),abs(fit.x-grid[-1]))<1e-4)
        row['models'][label]=metrics
    measurements.append(row)
    if name in ['manual-1200','manual-0500','res-0300','std-res-0500-24']:curves[name]=(measured,ys)
    print(name,{k:round(v['rms_db'],3) for k,v in row['models'].items()},flush=True)

out={'training':'Manual 7:00 wet window, 100 Hz–14 kHz log grid; _joint also uses Manual 12:00 full complex response',
     'models':models,'evaluation':measurements,
     'limitations':['No modulation/time-domain render yet','IIR pole stability alone does not certify feedback stability',
                    'The measured wet window may be contaminated at low frequencies','Each evaluation fits bounded delay, not gain']}
(HERE/'bf3_complex_results.json').write_text(json.dumps(out,indent=2))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axs=plt.subplots(2,2,figsize=(12,8),layout='constrained')
for ax,(name,(measured,ys)) in zip(axs.flat,curves.items()):
    ax.semilogx(freq,measured,label='Measured',color='black')
    for label,y in ys.items():ax.semilogx(freq,y,label=label,alpha=.8)
    ax.set(title=name,xlim=(75,1500),ylim=(-18,12),xlabel='Hz',ylabel='dB re bypass');ax.grid();ax.legend()
fig.savefig(HERE/'bf3_complex_validation.png',dpi=150)
