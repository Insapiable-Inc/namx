"""Offline NS-2 control-law screening. No production files are changed.

The source signals, complete preambles and capture-derived gains are used; no
per-probe initial states are fitted. Control-rate fitting is a surrogate, whose
error is checked against the previously frozen sample-rate model.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from numba import njit
from scipy import optimize

from analyze_cs3_detector import FS, HERE, load, rms

COMMIT='cde5c60'
THRESHOLDS={'0700':-69.4,'0900':-69.02,'1200':-66.72,'0300':-49.4,'0500':-27.33}
DECAYS={'0700':.0246,'0900':.0635,'1200':.093,'0300':.1451,'0500':.4603}


@njit(cache=True)
def detector(x,rate):
    peak=0.;out=np.empty(len(x));fall=np.exp(-1/(rate*.05))
    for i in range(len(x)):
        peak=max(abs(x[i]),peak*fall)
        out[i]=20*np.log10(peak) if peak*10**(-6/20)>1e-7 else -140.
    return out


@njit(cache=True)
def baseline(levels,threshold,decay,dt,gain0):
    fast=slow=-140.;switch=-7.56;opened=False;out=np.empty(len(levels))
    af=-np.expm1(-dt/decay);av=-np.expm1(-dt/(decay*7.94));ag=-np.expm1(-dt/(decay*4.42))
    for i in range(len(levels)):
        level=levels[i]
        fast=level if level>fast else fast+(level-fast)*af
        slow=level if level>slow else slow+(level-slow)*av
        if fast>threshold+1.29:opened=True
        elif fast<threshold+1.29-10.47:opened=False
        switch=0. if opened else switch+(-7.56-switch)*ag
        out[i]=gain0+max(-140.,switch-.889*max(0.,threshold-slow))
    return out


@njit(cache=True)
def candidate(levels,threshold,decay,p,kind,dt,gain0):
    out=np.empty(len(levels));v=1e-9;fast=-140.;q=0.
    for i in range(len(levels)):
        level=levels[i]
        if kind<2:
            # Asymmetric amplitude follower, optionally with simultaneous leak.
            th=threshold+p[0];u=10**(min(100.,level-th)/20)
            rise=np.exp(p[3])*np.exp(min(30.,max(-30.,-(level-th)/p[4])))
            a=1/max(rise,1e-6);b=1/(decay*np.exp(p[5]))
            if kind==0:
                if u>v:v+=(u-v)*(-np.expm1(-dt*a))
                else:v=10**((20*np.log10(max(v,1e-15))+(level-th-20*np.log10(max(v,1e-15)))*(-np.expm1(-dt*b)))/20)
            else:
                if u>v:
                    target=a*u/(a+b)
                    v=target+(v-target)*np.exp(-dt*(a+b))
                else:v*=np.exp(-dt*b)
            deficit=-20*np.log10(max(v,1e-15))
            z=deficit/p[2]
            reduction=p[1]*p[2]*(max(z,0.)+np.log1p(np.exp(-abs(z))))
            out[i]=gain0-min(140.,reduction)
        elif kind==2:
            # Bounded charge, nonlinear discharge. q's coordinate is fixed by
            # gain=20 log10(q), removing an arbitrary state/output exponent.
            drive=np.exp(min(30.,max(-80.,p[0]+(level-threshold+5.6)/p[1])))
            leak=1/(decay*np.exp(p[2]));power=p[3]
            v=min(1.,v+.5*dt*drive)
            v=(max(v,1e-15)**(1-power)+(power-1)*dt*leak)**(1/(1-power))
            v=min(1.,v+.5*dt*drive)
            out[i]=gain0+max(-140.,20*np.log10(max(v,1e-15)))
        else:
            # Reuse the original fast, slow and switching-state slots. The slow
            # state rises in amplitude; the switch is a bounded continuous charge.
            th=threshold+p[0]
            fast=level if level>fast else fast+(level-fast)*(-np.expm1(-dt/decay))
            u=10**((level-th)/20)
            rise=np.exp(p[3]+min(30.,max(-30.,-(level-th+5.6)/p[4])))
            if kind==4:rise=min(rise,np.exp(p[8]))
            if u>v:v+=(u-v)*(-np.expm1(-dt/max(1e-6,rise)))
            else:
                vd=20*np.log10(max(v,1e-15))
                v=10**((vd+(level-th-vd)*(-np.expm1(-dt/(7.94*decay))))/20)
            drive=np.exp(min(30.,max(-80.,p[5]+(fast-th+5.6)/p[6])))
            leak=1/(decay*np.exp(p[7]));target=drive/leak
            q=min(1.,target+(q-target)*np.exp(-dt*leak))
            out[i]=gain0-max(0.,min(140.,p[2]*(1-q)+p[1]*max(0.,-20*np.log10(max(v,1e-15)))))
    return out


FAMILIES={
    'asymmetric_envelope':{'kind':0,'names':['threshold_shift_db','gain_slope','knee_db','log_rise_seconds','rise_scale_db','log_decay_ratio'],
       'initial':[-3.,2.,1.,np.log(.8),2.,np.log(8.)],
       'lower':[-15.,.1,.05,np.log(.001),.3,np.log(.1)],
       'upper':[10.,8.,8.,np.log(30.),20.,np.log(100.)]},
    'leaky_envelope':{'kind':1,'names':['threshold_shift_db','gain_slope','knee_db','log_rise_seconds','rise_scale_db','log_decay_ratio'],
       'initial':[-3.,2.,1.,np.log(.8),2.,np.log(8.)],
       'lower':[-15.,.1,.05,np.log(.001),.3,np.log(.1)],
       'upper':[10.,8.,8.,np.log(30.),20.,np.log(100.)]},
    'nonlinear_charge':{'kind':2,'names':['log_drive_at_T_minus_5p6_per_second','drive_scale_db','log_decay_ratio','discharge_power'],
       'initial':[np.log(.6),2.,np.log(12.),3.],
       'lower':[np.log(.001),.3,np.log(.05),1.01],
       'upper':[np.log(100.),20.,np.log(1000.),12.]},
    'continuous_switch':{'kind':3,'names':['threshold_shift_db','gain_slope','switch_depth_db','log_rise_at_T_minus_5p6_seconds','rise_scale_db','log_switch_drive_at_T_minus_5p6_per_second','switch_drive_scale_db','log_switch_decay_ratio'],
       'initial':[-4.,.9,2.,np.log(1.),2.,np.log(.2),2.,np.log(10.)],
       'lower':[-15.,.1,0.,np.log(.001),.3,np.log(.0001),.3,np.log(.05)],
       'upper':[10.,8.,30.,np.log(100.),20.,np.log(100.),20.,np.log(1000.)]}}

FAMILIES['continuous_switch_capped']={k:(v.copy() if isinstance(v,list) else v) for k,v in FAMILIES['continuous_switch'].items()}
FAMILIES['continuous_switch_capped'].update(kind=4)
FAMILIES['continuous_switch_capped']['names']+=['log_max_rise_seconds']
FAMILIES['continuous_switch_capped']['initial']=[.1089,.308,3.567,1.375,1.075,-.798,.625,2.903,np.log(20.)]
FAMILIES['continuous_switch_capped']['lower']+=[np.log(.01)]
FAMILIES['continuous_switch_capped']['upper']+=[np.log(100.)]


def load_history(root):
    folder=Path('captures/boss/ns-2/2026-10-30-history')
    protocol=json.loads((root/folder/'analysis-protocol.json').read_text())
    curves=np.load(HERE/'ns2_history_curves.npz')
    takes=[]
    for tag in ['0700','0500']:
        name='history-threshold-0300-decay-'+tag
        m,x,_,_=load(root,folder,name,COMMIT)
        levels=detector(x.astype(float)/10**(m['ref_dbfs']/20),FS)
        # A complete detector block represents the interval preceding the output
        # point. This mean smooths carrier ripple, not signal history.
        n=len(levels)//48;levels=levels[:n*48].reshape(n,48).mean(axis=1)
        start=m['signal_start']//48
        target=np.zeros(n);groups=[]
        for s in protocol['segments']:
            if not s['role'].startswith('probe_'):continue
            level=s['input_db'];history='quiet' if s['role']=='probe_after_quiet' else 'loud'
            begin=start+int(s['start_s']*1000)
            target[begin:begin+8000]=curves[f'measured_{tag}_m{abs(level)}_{history}']
            idx=np.arange(begin+100,begin+7900,10)
            groups.append({'level':level,'history':history,'indices':idx,'begin':begin})
        takes.append({'name':name,'levels':levels,'target':target,'groups':groups,
                      'threshold':THRESHOLDS['0300'],'decay':DECAYS[tag],'tag':tag,'gain0':.660050616414343})
    return takes,curves


def prediction(take,p,family,dt=.001):
    return candidate(take['levels'],take['threshold'],take['decay'],np.asarray(p),family['kind'],dt,take['gain0'])


def residuals(p,family,takes,selection):
    rows=[]
    for take in takes:
        pred=prediction(take,p,family)
        for g in take['groups']:
            if selection(take,g):rows.append((pred-take['target'])[g['indices']]/np.sqrt(len(g['indices'])))
    return np.concatenate(rows)


def fit(family,takes,selection,starts,seed):
    rng=np.random.default_rng(seed);lo=np.array(family['lower']);hi=np.array(family['upper'])
    results=[]
    for i in range(starts):
        x=np.array(family['initial']) if i==0 else np.clip(np.array(family['initial'])+rng.normal(size=len(lo))*(hi-lo)*.13,lo+1e-5,hi-1e-5)
        result=optimize.least_squares(residuals,x,args=(family,takes,selection),bounds=(lo,hi),max_nfev=250,ftol=2e-7,xtol=2e-7,gtol=2e-7)
        results.append(result)
        print('start',i,'equal_group_rms_db',np.linalg.norm(result.fun)/np.sqrt(len(result.fun)/780),'nfev',result.nfev,flush=True)
    best=min(results,key=lambda r:r.cost)
    return best,{'start_costs':[float(r.cost) for r in results],
                 'start_parameters':[r.x.tolist() for r in results],
                 'function_evaluations':[r.nfev for r in results]}


def evaluate(takes,p,family,training):
    rows=[]
    for take in takes:
        pred=prediction(take,p,family)
        for g in take['groups']:
            idx=g['indices'];begin=g['begin'];error=pred-take['target']
            rows.append({'decay':take['tag'],'input_db':g['level'],'history':g['history'],
                'training':training(take,g),'rms_db':rms(error[idx]),
                'late_bias_db':float(np.median(error[begin+6500:begin+7500])),
                'predicted_gain_early_late_db':[float(np.median(pred[begin+a:begin+b])) for a,b in [(100,300),(6500,7500)]]})
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    parser.add_argument('--starts',type=int,default=3)
    parser.add_argument('--family',choices=list(FAMILIES))
    args=parser.parse_args();takes,curves=load_history(args.root)
    checks=[]
    for take in takes:
        pred=baseline(take['levels'],take['threshold'],take['decay'],.001,take['gain0'])
        for g in take['groups']:
            exact=curves[f"original_map_{take['tag']}_m{abs(g['level'])}_{g['history']}"]
            checks.append({'decay':take['tag'],'level':g['level'],'history':g['history'],
                'control_vs_frozen_sample_rate_rms_db':rms(pred[g['begin']+100:g['begin']+7900]-exact[100:7900])})
    print('Surrogate checks',checks,flush=True)
    result={'source_commit':COMMIT,'history_archive_sha256':hashlib.sha256((HERE/'ns2_history_curves.npz').read_bytes()).hexdigest(),
            'scope':'Exploratory post-hoc control-law fits; complete preambles; equal weight per level/history/Decay group; no per-probe resets or gains. No production recommendation implied.',
            'control_rate_baseline_checks':checks,'families':{}}
    all_groups=lambda t,g:True
    for name,family in FAMILIES.items():
        if args.family and name!=args.family:continue
        if not args.family and name.startswith('continuous_switch'):continue
        print('Fitting',name,flush=True)
        best,search=fit(family,takes,all_groups,args.starts,230)
        result['families'][name]={'definition':family,'parameters':dict(zip(family['names'],best.x.tolist())),
            'fit_parameters':best.x.tolist(),'search':search,'all_history_fit':evaluate(takes,best.x,family,all_groups)}
        filename='ns2_control_candidate_results.json' if not args.family else 'ns2_'+args.family+'_screening.json'
        (HERE/filename).write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        print(name,'parameters',best.x,'scores',[(r['decay'],r['input_db'],r['history'],round(r['rms_db'],3)) for r in result['families'][name]['all_history_fit']],flush=True)


if __name__=='__main__':main()
