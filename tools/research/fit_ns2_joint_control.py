"""Fit the continuous NS-2 candidate with shared dynamics and a monotone knob map.

Whole input levels and all older picked-note/phrase takes are excluded from
parameter estimation. This is retrospective validation, not a blind experiment.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy import optimize

from analyze_cs3_detector import HERE,rms
from fit_ns2_control_candidates import load_history,FAMILIES,candidate,prediction,evaluate


def old_cache():
    z=np.load(HERE/'ns2_control_regression_cache.npz',allow_pickle=False)
    rows=json.loads(str(z['metadata_json']))
    for i,row in enumerate(rows):
        for k in ['levels','target','valid']:row[k]=z[f'{i}_{k}']
    return rows


def map_thresholds(p):
    # p[0] is the shared shift inside candidate(), so this unshifted map uses
    # 3:00 as its fixed coordinate anchor. Four positive increments enforce order.
    noon=-49.4-np.exp(p[-4]);nine=noon-np.exp(p[-3]);seven=nine-np.exp(p[-2])
    return {'0700':seven,'0900':nine,'1200':noon,'0300':-49.4,'0500':-49.4+np.exp(p[-1])}


def prepare_fit(new,old,held_level):
    items=[]
    for t in new:
        for g in t['groups']:
            if g['level']==held_level:continue
            items.append((t,g['indices'],0.))
    for t in old:
        name=t['file']
        if not (name.startswith('stairs-threshold') or name.startswith('jump-') or name=='bypass-jump'):continue
        idx=np.flatnonzero(t['valid'])[::10]
        if len(idx):items.append((t,idx,t['tone_db']))
    # Same full-history simulation for every condition; 5 ms fitting step.
    grouped={}
    for t,idx,tone in items:
        key=t['name']
        if key not in grouped:
            n=len(t['levels'])//5
            grouped[key]={'levels':t['levels'][:n*5].reshape(n,5).mean(axis=1),
                          'threshold_tag':t.get('threshold_tag','0300'),'decay':t['decay'],
                          'gain0':t['gain0'],'target':t['target'],'groups':[]}
        valid=idx[idx//5<len(grouped[key]['levels'])]
        grouped[key]['groups'].append((valid,valid//5,tone))
    return list(grouped.values())


def residual(p,items,kind,nd):
    mapping=map_thresholds(p);errors=[]
    for t in items:
        pred=candidate(t['levels'],mapping[t['threshold_tag']],t['decay'],p[:nd],kind,.005,t['gain0'])
        for idx,coarse,tone in t['groups']:
            errors.append((pred[coarse]+tone-t['target'][idx])/np.sqrt(len(idx)))
    return np.concatenate(errors)


def score_old(old,p,family):
    mapping=map_thresholds(p);rows=[]
    for t in old:
        pred=candidate(t['levels'],mapping[t['threshold_tag']],t['decay'],p[:len(family['names'])],family['kind'],.001,t['gain0'])+t['tone_db']
        training=t['file'].startswith(('stairs-threshold','jump-')) or t['file']=='bypass-jump'
        rows.append({'name':t['name'],'training':training,'rms_db':rms((pred-t['target'])[t['valid']]),
                     'bias_db':float(np.median((pred-t['target'])[t['valid']]))})
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    parser.add_argument('--starts',type=int,default=2)
    parser.add_argument('--family',choices=['continuous_switch','continuous_switch_capped'],default='continuous_switch')
    args=parser.parse_args();new,_=load_history(args.root);old=old_cache()
    screened=json.loads((HERE/('ns2_'+args.family+'_screening.json')).read_text())['families'][args.family]
    p0=np.r_[screened['fit_parameters'],np.log([17.32,2.3,.38,22.07])]
    family=FAMILIES[args.family];nd=len(family['names'])
    lo=np.r_[family['lower'],np.log([1.,.02,.02,1.])]
    hi=np.r_[family['upper'],np.log([40.,15.,15.,40.])]
    result={'scope':'Joint fitting of original stairs/jumps and new history, with a monotone Threshold map. Decay map fixed. All older plucks/phrases/soft/slow stairs remain outside fitting. Whole probe input levels are withheld. Family selection has seen the dataset: retrospective validation, not blind.',
            'fitting_dt_seconds':.005,'evaluation_dt_seconds':.001,'folds':{}}
    for held in [-54,-56]:
        items=prepare_fit(new,old,held);print('Held level',held,'training takes',len(items),flush=True)
        fits=[]
        for start in range(args.starts):
            initial=p0 if start==0 else np.r_[family['initial'],p0[-4:]]
            fit=optimize.least_squares(residual,initial,args=(items,family['kind'],nd),bounds=(lo,hi),max_nfev=120,ftol=1e-6,xtol=1e-6,gtol=1e-6)
            fits.append(fit)
            print('Held',held,'start',start,'cost',fit.cost,'nfev',fit.nfev,flush=True)
        best=min(fits,key=lambda r:r.cost);p=best.x
        new_rows=evaluate(new,p[:nd],family,lambda t,g:g['level']!=held)
        old_rows=score_old(old,p,family)
        coarse_checks=[]
        for t in new:
            a=prediction(t,p[:nd],family)
            n=len(t['levels'])//5
            b=candidate(t['levels'][:n*5].reshape(n,5).mean(axis=1),t['threshold'],t['decay'],p[:nd],family['kind'],.005,t['gain0'])
            for g in t['groups']:
                idx=g['indices'];coarse_checks.append(rms(a[idx]-b[idx//5]))
        singular=np.linalg.svd(best.jac,compute_uv=False)
        result['folds'][str(held)]={'parameters':p.tolist(),'effective_threshold_map_db':{k:v+p[0] for k,v in map_thresholds(p).items()},
             'start_costs':[float(f.cost) for f in fits],'start_parameters':[f.x.tolist() for f in fits],
             'nfev':[f.nfev for f in fits],'new_history':new_rows,'old_regression':old_rows,
             'fit_step_vs_1ms_max_group_rms_db':max(coarse_checks),
             'jacobian_singular_values':singular.tolist(),
             'jacobian_condition_number':float(singular[0]/singular[-1])}
        output='ns2_joint_control_results.json' if args.family=='continuous_switch' else 'ns2_joint_capped_control_results.json'
        (HERE/output).write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        print('Held',held,'new RMS',[(r['decay'],r['input_db'],r['history'],round(r['rms_db'],3)) for r in new_rows],flush=True)
        print('Old held-out median',np.median([r['rms_db'] for r in old_rows if not r['training']]),'coarse max',max(coarse_checks),'condition',singular[0]/singular[-1],flush=True)


if __name__=='__main__':main()
