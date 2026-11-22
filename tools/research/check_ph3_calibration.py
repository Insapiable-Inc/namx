"""Test whether PH-3 followup gain change is shared by bypass and engaged recordings.

python tools/research/check_ph3_calibration.py DATA_EXPORT
Writes JSON; does not adjust captures or production constants.
"""
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'capture'))
import analysis

root=Path(sys.argv[1])/'captures/boss/ph-3'
def db(x):return 20*np.log10(np.maximum(abs(x),1e-20))
def rms(x):return np.sqrt(np.mean(x*x))
rows=[]; signals={}
for s in sorted(root.iterdir()):
    if not s.is_dir():continue
    for name in ['bypass-sweep','bypass-noise','s4-rate-0500']:
        path=s/(name+'.json')
        if not path.exists():continue
        t=analysis.Take(s,name);m=t.meta;all_y,fs=sf.read(path.with_suffix('.wav'))
        marker=m['marker_start']+m['latency_samples']
        part=all_y[marker:marker+2400]
        y=t.signal()[fs:-fs]; x,xfs=sf.read(s/'played'/(name+'.wav'))
        x=x[m['signal_start']:m['signal_start']+len(t.signal())][fs:-fs]
        row={'session':s.name,'take':name,'recorded_at':m['recorded_at'],
             'marker_pedal_over_loop_db':float(db(rms(part[:,0])/rms(part[:,1]))),
             'output_rms_dbfs':float(db(rms(y))), 'output_over_played_rms_db':float(db(rms(y)/rms(x)))}
        rows.append(row);signals[(s.name,name)]=y

def find(s,n):return next(v for v in rows if v['session']==s and v['take']==n)
base='2026-10-28'; follow='2026-10-28-followup'
bypass_delta=find(follow,'bypass-noise')['output_over_played_rms_db']-find(base,'bypass-noise')['output_over_played_rms_db']
on_delta=find(follow,'s4-rate-0500')['output_over_played_rms_db']-find(base,'s4-rate-0500')['output_over_played_rms_db']
# Equal-duration Welch spectra expose frequency-dependent changes hidden by total RMS.
f,a=signal.welch(signals[(base,'bypass-noise')],fs=48000,nperseg=8192)
_,b=signal.welch(signals[(follow,'bypass-noise')],fs=48000,nperseg=8192)
mask=(f>=100)&(f<=16000)
ratio=10*np.log10(b[mask]/a[mask]);offset=float(np.median(ratio))
result={'takes':rows,'bypass_change_db':bypass_delta,'engaged_repeated_setting_change_db':on_delta,
    'difference_after_session_bypass_normalization_db':on_delta-bypass_delta,
    'bypass_welch_median_change_db':offset,'bypass_welch_shape_rms_after_gain_db':float(rms(ratio-offset)),
    'conclusion':'A shared pedal-side gain change explains the tested repeated setting. Its physical cause and persistence in every frozen take remain unidentified. Use same-session bypass; no automatic correction of raw WAVs.'}
(HERE/'ph3_calibration_results.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k!='takes'},indent=2))
