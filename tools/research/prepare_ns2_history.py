"""Create the selected NS-2 history packet and freeze predictions before capture.

python tools/research/prepare_ns2_history.py [REPOSITORY]
Generated audio stays in captures/stimuli in the repository. Existing frozen
files are only accepted if identical; this script never overwrites a changed
protocol or stimulus. No playback, capture or production-DSP changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

from analyze_cs3_detector import FS, HERE, biquad, db
from prepare_ns2_locator import coherent_envelope, ns2_gain


def freeze_json(path, value):
    data=(json.dumps(value,indent=2,allow_nan=False)+'\n').encode()
    if path.exists():
        assert path.read_bytes()==data, f'Existing frozen file differs: {path}'
    else:
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',nargs='?',type=Path,default=HERE.parents[1])
    root=parser.parse_args().root.resolve()
    locator=Path('captures/boss/ns-2/2026-10-30-threshold-locator')
    session=root/'captures/boss/ns-2/2026-10-30-history'
    session.mkdir(parents=True,exist_ok=True)
    source_bytes=(root/'tools/research/ns2_locator_results.json').read_bytes()
    source=json.loads(source_bytes)
    levels=source['selected_history_input_levels_db']
    assert levels==[-56,-54,-44]
    # One continuous-phase carrier with 1 ms cosine envelope transitions.
    # Quiet resets are actual digital zero; reference noise is not simulated.
    segments=[];position=0
    for level in levels:
        for role,seconds,db_level in [('quiet_reset',24,None),('probe_after_quiet',8,level),
                                     ('loud_conditioning',2,-12),('probe_after_loud',8,level)]:
            segments.append({'start_s':position,'duration_s':seconds,'role':role,
                             'input_db':db_level,'probe_level_db':level})
            position+=seconds
    segments.append({'start_s':position,'duration_s':2,'role':'open_reference','input_db':-12})
    position+=2
    amplitude=np.zeros(position*FS)
    previous=0.;ramp_length=48
    ramp=.5-.5*np.cos(np.pi*np.arange(ramp_length)/(ramp_length-1))
    for segment in segments:
        start=int(segment['start_s']*FS);end=int((segment['start_s']+segment['duration_s'])*FS)
        target=0. if segment['input_db'] is None else 10**(segment['input_db']/20)
        amplitude[start:end]=target
        if previous!=target:
            amplitude[start:start+ramp_length]=previous+(target-previous)*ramp
        previous=target
    # Return to zero in the last millisecond of the final open reference.
    amplitude[-ramp_length:]*=ramp[::-1]
    body=(amplitude*np.sin(2*np.pi*1000*np.arange(len(amplitude))/FS)).astype('float32')
    stimulus_relative=Path('captures/stimuli/ns-2-history-1000.wav')
    stimulus=root/stimulus_relative
    stimulus.parent.mkdir(parents=True,exist_ok=True)
    if stimulus.exists():
        old,rate=sf.read(stimulus,dtype='float32')
        assert rate==FS and np.array_equal(old,body),'Existing stimulus differs'
    else:
        sf.write(stimulus,body,FS,subtype='FLOAT')
    spec={'type':'file','path':str(stimulus_relative),'level_db':0,'tail':2}
    check_spec={'type':'steps','freq':1000,'levels_db':levels+[-12],
                'step_seconds':2,'gap_seconds':0,'level_db':0,'tail':2}
    plan={'pedal':'Boss NS-2 Noise Suppressor (mono: Input -> Output; Send/Return empty)',
          'comment':'Matched quiet/loud history at three selected levels. Keep Threshold at the untouched locator 3:00 position; Mode Reduction. Engaged minimum Decay, engaged maximum Decay, then true bypass. Each long take has deliberately silent 24 s resets; let it run. No manual switching during a take.',
          'knobs':['Threshold','Decay','Mode'],'takes':[]}
    for tag,clock in [('0700','7:00'),('0500','5:00')]:
        plan['takes'].append({'name':f'history-threshold-0300-decay-{tag}',
            'settings':{'Threshold':'3:00','Decay':clock,'Mode':'Reduction'},'signal':spec,
            'note':f'NS-2 ON, Mode Reduction. Leave Threshold exactly where it was for the locator. Set Decay fully {"counter-clockwise" if tag=="0700" else "clockwise"}. The 24-second silent stretches are intentional resets; do not stop, switch or adjust anything during the take.'})
    plan['takes'].append({'name':'bypass-history-check-1000','bypass':True,'signal':check_spec,
        'note':'Switch NS-2 OFF / BYPASSED now. Leave all knobs, cables and interface gain unchanged. This short true-bypass check verifies the locator calibration at the probe levels.'})
    plan_path=root/'tools/capture/plans/ns-2-history.json'
    freeze_json(plan_path,plan)
    rig=json.loads((root/locator/'rig.json').read_text())
    rig['note']='Same fixed Scarlett capture path and reference as NS-2 locator; Threshold remains physically untouched at 3:00, Mode Reduction, Input -> Output with Send/Return empty. Input 2 INST, pad ON, AIR off, physical gain fully counter-clockwise. Only Decay changes between engaged takes. Intentional digital-zero 24 s resets precede each quiet-history probe. Existing absolute calibration retained, not remeasured.'
    rig['capture_plan_sha256']=hashlib.sha256(plan_path.read_bytes()).hexdigest()
    freeze_json(session/'rig.json',rig)
    sys.path.insert(0,str(root/'tools/capture'))
    import signals
    played,_,start=signals.build(spec,FS,rig['ref_dbfs'])
    check_played,_,_=signals.build(check_spec,FS,rig['ref_dbfs'])
    normalized=played.astype(float)/10**(rig['ref_dbfs']/20)
    # Two predefined members of the existing family. The shifted threshold is
    # an illustrative midpoint of the partial/open bracket, not an identified T.
    thresholds={'original_map':-49.4,'shifted_map_example':-54.29}
    decays={'0700':24.6,'0500':460.3}
    open_row=next(r for r in source['windows'] if r['input_db']==-12)
    gain_offset=open_row['windows'][2]['gain_db_10ms_magnitude_median']-.6
    predictions={}
    sos=np.array([biquad('shelf',8168.4,.623,3.69)])
    tone_norm=1/abs(signal.sosfreqz(sos,worN=[1000],fs=FS)[1][0])
    windows=[[.1,.3],[1,1.5],[3,3.8],[6.5,7.5]]
    for variant,threshold in thresholds.items():
        predictions[variant]={}
        for tag,decay in decays.items():
            g=ns2_gain(normalized,threshold,decay)+gain_offset
            out=signal.sosfilt(sos,normalized*10**(g/20))*tone_norm
            # Ratio is evaluated only inside nonzero probes, never during reset.
            gain=db(coherent_envelope(out)/np.maximum(coherent_envelope(normalized),1e-15))
            rows=[]
            for segment in segments:
                if not segment['role'].startswith('probe_'):continue
                idx=start+int(segment['start_s']*FS)
                rows.append({'input_db':segment['input_db'],'history':segment['role'],
                    'body_start_s':segment['start_s'],
                    'gain_window_medians_db':[float(np.median(gain[idx+int(a*FS):idx+int(b*FS)])) for a,b in windows]})
            predictions[variant][tag]=rows
    prior_protocol=json.loads((root/locator/'analysis-protocol.json').read_text())
    reference_commit=subprocess.check_output(['git','rev-parse','23992ce'],cwd=root,text=True).strip()
    protocol={'prepared_from_commit':reference_commit,'source_model_commit':source['source_model_commit'],
        'model_source_sha256':prior_protocol['source_sha256'],
        'locator_results_sha256':hashlib.sha256(source_bytes).hexdigest(),
        'stimulus':{'path':str(stimulus_relative),'rate':FS,'frames':len(body),
                    'float32_samples_sha256':hashlib.sha256(body.tobytes()).hexdigest(),
                    'identity_note':'Float32 sample hash defines the generated stimulus; WAV container metadata may contain a creation timestamp.',
                    'expected_played_sha256':hashlib.sha256(played.tobytes()).hexdigest(),
                    'expected_bypass_played_sha256':hashlib.sha256(check_played.tobytes()).hexdigest(),
                    'transitions':'1 ms cosine amplitude ramps, continuous-phase 1 kHz sine; reset amplitude zero.'},
        'segments':segments,'gain_windows_seconds':windows,
        'levels_db':levels,'model_threshold_variants_db':thresholds,'decay_ms':decays,
        'open_gain_offset_from_locator_db':gain_offset,
        'frozen_predictions':predictions,
        'rules':['Primary scores do not fit new dynamic constants, threshold or gain offsets.',
                 'The original-map and shifted-map predictions share the unchanged model state equations. The shifted example uses a provisional opening point of -53 dB, midpoint between partial -54 and fully open -52 on the ascending locator; this is not a proven comparator threshold.',
                 'Use true locator-bypass coherent transfer at each probe level to normalize the new played signal. Check the closing bypass, -12 dB open references and loopback for changes; report any >0.1 dB bypass discrepancy before pooling. Do not use the nominal bypass takes from the old dynamics session.',
                 'Compare matched quiet/loud trajectories, within-probe early/late evolution and Decay dependence; exclude digital-zero resets from gain-ratio scores.',
                 'The 24 s reset is justified by the existing slow-state model but is not proof that the physical pedal is completely reset. Check recorded silence and probe repeatability; persistent history may require a longer-reset control.',
                 'The -44 dB probe and -12 dB conditioning onsets constrain prompt opening. A better near-threshold fit must not globally slow these controls.',
                 'Before proposing a new state, test whether threshold remapping or a change to an existing upward state law can explain all conditions. No original circuit identification is claimed.']}
    freeze_json(session/'analysis-protocol.json',protocol)
    print('Body duration:',len(body)/FS,'s; total audio:',2*len(played)/FS+len(check_played)/FS,'s')
    print('Selected levels:',levels,'; open gain calibration:',gain_offset,'dB')
    for variant,by_decay in predictions.items():
        for tag,rows in by_decay.items():
            print(variant,tag,[(r['input_db'],r['history'],np.round(r['gain_window_medians_db'],3).tolist()) for r in rows])


if __name__=='__main__':main()
