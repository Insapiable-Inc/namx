"""Assemble the NS-2 model-selection evidence, sample-rate checks and figures."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np

from analyze_cs3_detector import HERE,FS,rms
from analyze_ns2_history import onset_metrics
from fit_ns2_control_candidates import FAMILIES,baseline,candidate,load_history,prediction
from fit_ns2_joint_control import old_cache,map_thresholds
from validate_ns2_control_candidates import full_rate_trace,full_rate_check


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    args=parser.parse_args();root=args.root
    initial=json.loads((HERE/'ns2_control_candidate_results.json').read_text())
    screened=json.loads((HERE/'ns2_continuous_switch_screening.json').read_text())['families']['continuous_switch']
    old_result=json.loads((HERE/'ns2_control_regression_results.json').read_text())
    joint=json.loads((HERE/'ns2_joint_control_results.json').read_text())
    capped=json.loads((HERE/'ns2_continuous_switch_capped_screening.json').read_text())['families']['continuous_switch_capped']
    joint_capped=json.loads((HERE/'ns2_joint_capped_control_results.json').read_text())
    initial['families']['continuous_switch']=screened
    initial['families']['continuous_switch_capped']=capped
    new,_=load_history(root);old=old_cache();family=FAMILIES['continuous_switch']
    result={'decision':'Reject the tested replacements for production. Keep the baseline. No additional capture requested.',
        'history_screening':{},'joint_validation':{},'joint_capped_validation':{},'sample_rate_history':{},'sample_rate_old':{},
        'provenance':{},'limitations':['Retrospective validation; family selection has seen the data.',
        'Multistart local optimization is not a proof that every parameterization of a family fails.',
        'Gain envelopes do not establish perceptual equivalence; no new listening verdict.',
        'No circuit topology or unique state dimension is identified.']}
    for name,v in initial['families'].items():
        rows=v['all_history_fit'];near=[r for r in rows if r['input_db']!=-44]
        result['history_screening'][name]={'all_12_groups_rms_db':rms([r['rms_db'] for r in rows]),
            'near_threshold_8_groups_rms_db':rms([r['rms_db'] for r in near]),
            'worst_group_rms_db':max(r['rms_db'] for r in rows)}
    frozen=json.loads((HERE/'ns2_history_results.json').read_text())
    original=[r['absolute_rms_db_01_to_79_seconds'] for tag in ['0700','0500'] for r in frozen['evaluation'][tag]['frozen_models']['original_map'] if r['input_db']!=-44]
    result['history_screening']['baseline']={'near_threshold_8_groups_rms_db':rms(original)}
    old_base={r['name']:r['rms_db']['baseline'] for r in old_result['regression']}
    folds=[(name,key,fold) for name,data in [('joint_validation',joint),('joint_capped_validation',joint_capped)] for key,fold in data['folds'].items()]
    for name,key,fold in folds:
        val=[r for r in fold['old_regression'] if not r['training']]
        new_val=[r for r in fold['new_history'] if not r['training']]
        result[name][key]={'old_held_out_count':len(val),
            'old_held_out_baseline_median_rms_db':float(np.median([old_base[r['name']] for r in val])),
            'old_held_out_candidate_median_rms_db':float(np.median([r['rms_db'] for r in val])),
            'old_held_out_worse_by_05db':sum(r['rms_db']>old_base[r['name']]+.5 for r in val),
            'new_withheld_group_rms_db':rms([r['rms_db'] for r in new_val]),
            'new_withheld_worst_rms_db':max(r['rms_db'] for r in new_val),
            'fit_dt_vs_1ms_max_rms_db':fold['fit_step_vs_1ms_max_group_rms_db']}
    history_curves={}
    for take in new:
        take=dict(take,folder='captures/boss/ns-2/2026-10-30-history',file=take['name'],freq=1000,tone_db=0.)
        models=[('history_fit',screened['fit_parameters'],family),*[(f'held_{k}',v['parameters'][:8],family) for k,v in joint['folds'].items()]]
        capped_family=FAMILIES['continuous_switch_capped']
        models += [('capped_history_fit',capped['fit_parameters'],capped_family),*[(f'capped_held_{k}',v['parameters'][:9],capped_family) for k,v in joint_capped['folds'].items()]]
        for label,p,model in models:
            full,g,audio,source=full_rate_trace(root,take,p,model)
            short=prediction(take,p,model)
            rows=[]
            for group in take['groups']:
                idx=group['indices'];begin=group['begin'];level=group['level'];history=group['history']
                row={'input_db':level,'history':history,'coherent_audio_rms_db':rms((full[:len(take['target'])]-take['target'])[idx]),
                    'full_vs_surrogate_rms_db':rms((full[:len(short)]-short)[idx])}
                if level==-44:
                    a=begin*48;b=a+int(.2*FS)
                    row['opening']=onset_metrics(audio[a:b],source[a:b],1.,take['gain0'])
                rows.append(row)
                history_curves[f"{label}_{take['tag']}_m{abs(level)}_{history}"]=full[begin:begin+8000]
            result['sample_rate_history'][label+'_'+take['tag']]={'rows':rows,'finite_gain_and_audio':bool(np.isfinite(g).all() and np.isfinite(audio).all()),
                'gain_bounds_db':[float(g.min()),float(g.max())]}
            print('Full-rate',label,take['tag'],'max surrogate difference',max(r['full_vs_surrogate_rms_db'] for r in rows),flush=True)
    old_selected=['2026-10-27-dynamics/plucks-t0700-d1200','2026-10-27-dynamics/bypass-stairs-slow']
    fold=joint['folds']['-54'];p=np.array(fold['parameters']);mapping=map_thresholds(p)
    for take in old:
        if take['name'] not in old_selected:continue
        changed=dict(take,threshold=mapping[take['threshold_tag']])
        result['sample_rate_old'][take['name']]={
            'baseline':full_rate_check(root,take,None,None,take['valid']),
            'joint_held_minus54':full_rate_check(root,changed,p[:8],family,take['valid'])}
        cp=np.array(joint_capped['folds']['-54']['parameters']);cm=map_thresholds(cp)
        changed_capped=dict(take,threshold=cm[take['threshold_tag']])
        result['sample_rate_old'][take['name']]['capped_joint_held_minus54']=full_rate_check(root,changed_capped,cp[:9],FAMILIES['continuous_switch_capped'],take['valid'])
        print('Full old',take['name'],result['sample_rate_old'][take['name']],flush=True)
    for name in ['fit_ns2_control_candidates.py','validate_ns2_control_candidates.py','fit_ns2_joint_control.py',
                 'ns2_history_curves.npz','ns2_control_candidate_results.json','ns2_continuous_switch_screening.json','ns2_control_regression_results.json','ns2_joint_control_results.json',
                 'ns2_continuous_switch_capped_screening.json','ns2_joint_capped_control_results.json']:
        result['provenance'][name]=hashlib.sha256((HERE/name).read_bytes()).hexdigest()
    (HERE/'ns2_control_selection_results.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    np.savez_compressed(HERE/'ns2_control_selection_curves.npz',**history_curves)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    measured=np.load(HERE/'ns2_history_curves.npz')
    fig,axes=plt.subplots(2,2,figsize=(13,9),layout='constrained');times=np.arange(8000)/1000
    for row,tag in enumerate(['0700','0500']):
        for col,level in enumerate([-56,-54]):
            ax=axes[row,col]
            for history,style in [('quiet','-'),('loud','--')]:
                ax.plot(times[100:7900],measured[f'measured_{tag}_m{abs(level)}_{history}'][100:7900],style,color='#222',lw=1.8,label='Pedal / '+history)
                for label,color,title in [('capped_history_fit','#007d91','Capped: all-history fit'),(f'capped_held_{level}','#d16600','Capped: level withheld')]:
                    ax.plot(times[100:7900],history_curves[f'{label}_{tag}_m{abs(level)}_{history}'][100:7900],style,color=color,lw=1.1,label=title+' / '+history)
            ax.set(title=f'Decay {"minimum" if tag=="0700" else "maximum"}, {level} dB input',xlabel='Seconds after probe starts',ylabel='Gain re bypass (dB)')
            ax.grid(alpha=.25);ax.legend(fontsize=7)
    fig.suptitle('NS-2: a good local fit does not establish level generalization')
    fig.savefig(HERE/'ns2_control_history_selection.png',dpi=150);plt.close(fig)
    shown=['2026-10-27-dynamics/plucks-t0500-d0700','2026-10-27-dynamics/plucks-t0700-d1200',
           '2026-10-27-dynamics/soft-t0300-d1200','2026-10-27-dynamics/bypass-stairs-slow']
    fig,axes=plt.subplots(2,2,figsize=(13,9),layout='constrained')
    for ax,name in zip(axes.flat,shown):
        t=next(t for t in old if t['name']==name);times=(np.arange(len(t['levels']))-t['start'])/1000
        window=(times>=0)&(times<(3 if 'plucks-' in name else (8 if 'soft-' in name else 90)))
        obs=np.where(t['valid'],t['target'],np.nan)
        ax.plot(times[window][::5],obs[window][::5],color='#222',label='Pedal (trusted bins)',lw=1.6)
        orig=baseline(t['levels'],t['threshold'],t['decay'],.001,t['gain0'])+t['tone_db']
        local=prediction(t,screened['fit_parameters'],family)+t['tone_db']
        joint_pred=candidate(t['levels'],cm[t['threshold_tag']],t['decay'],cp[:9],4,.001,t['gain0'])+t['tone_db']
        for arr,color,label in [(orig,'#6b7c93','Production baseline'),(local,'#007d91','Uncapped: all-history fit'),(joint_pred,'#d16600','Capped: joint fit (-54 withheld)')]:
            ax.plot(times[window][::5],arr[window][::5],color=color,lw=1.,label=label)
        ax.set(title=name.split('/')[-1],xlabel='Seconds from signal start',ylabel='Gain re bypass (dB)',ylim=(-55,3))
        ax.grid(alpha=.25);ax.legend(fontsize=7)
    fig.suptitle('NS-2: older notes and slow steps reject the tested replacements')
    fig.savefig(HERE/'ns2_control_old_regression.png',dpi=150);plt.close(fig)
    print(json.dumps({'screening':result['history_screening'],'joint':result['joint_validation'],'joint_capped':result['joint_capped_validation']},indent=2),flush=True)


if __name__=='__main__':main()
