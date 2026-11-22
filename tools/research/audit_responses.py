"""Additional BF-3, PH-3 and DD-8 diagnostics; same export argument as audit_captures.py."""
import sys,json
from pathlib import Path
import numpy as np
import scipy.optimize as opt
import soundfile as sf
ROOT=Path(sys.argv[1]).resolve(); sys.path.insert(0,str(ROOT/'tools/capture'))
import analyze_bf3 as bf, analyze_ph3 as ph, analyze_dd8 as dd
out={}
def session(p,s):return ROOT/'captures/boss'/p/s
def rms(x):return float(np.sqrt(np.mean(x*x)))
def db(x):return 20*np.log10(np.maximum(abs(x),1e-15))
s=session('bf-3','2026-10-27-standard'); st=bf.Static(s); f=bf.FREQS
# Preserve the historical comparison even after analyze_bf3.MODEL is corrected.
old={**bf.MODEL,'tone':[('hp',191.8,.519),('hp',94.0,1.556),('lp',6293.0,.542),('lp',17471.7,.923)],'dry_db':1.79,'wet_db':3.41}
current={**old,'tone':[('hp',205.4,.573),('hp',97.3,1.784),('lp',6824.1,.481),('lp',18401.4,.817)],'dry_db':-.13,'wet_db':1.45}
dry=st.dry/st.bypass*np.exp(2j*np.pi*f*st.dry_ms/1000)
wet=st.wet/st.bypass*np.exp(2j*np.pi*f*st.dry_ms/1000)
results={}; curves={}
for name in ['manual-0900','manual-1200','manual-0300','manual-0500']:
 m=json.loads((s/(name+'.json')).read_text()); idx=bf.POSITIONS.index(m['settings']['Manual']); delay=old['manual_ms'][idx]
 measured=bf.fine_db(np.fft.rfft(bf.impulse(s,name))/np.fft.rfft(bf.impulse(s,'bypass-sweep')))
 band=(f>=100)&(f<=10000); low=(f>=80)&(f<=200)
 def empirical(d):return dry+wet*np.exp(-2j*np.pi*f*(d-(st.wet_ms-st.dry_ms))/1000)
 rows={}; ys={}
 for label,model in [('old_script',old),('current_header',current),('measured_complex_template',None)]:
  if model is not None:bf.MODEL=model
  # Historical audit compares ideal fractional delays, Res off. Keep that definition
  # independent of later corrections to the runtime-aware analysis function.
  def ideal(d):
   z=bf.model_tone(f)*np.exp(-2j*np.pi*f*(d-bf.MODEL['tone_delay_ms'])/1000)
   return 10**(bf.MODEL['dry_db']/20)+10**(bf.MODEL['wet_db']/20)*z
  fn=empirical if model is None else ideal
  fit=opt.minimize_scalar(lambda d:rms((bf.fine_db(fn(d))-measured)[band]),bounds=(delay*.96,delay*1.04),method='bounded')
  y=bf.fine_db(fn(fit.x));e=y-measured
  rows[label]={'fitted_delay_ms':fit.x,'rms_100_10000_db':rms(e[band]),'rms_80_200_db':rms(e[low]),
    'at_100hz_db':float(y[np.argmin(abs(f-100))]),'measured_100hz_db':float(measured[np.argmin(abs(f-100))])}
  ys[label]=y
 results[name]=rows;curves[name]=(measured,ys)
out['bf3']=results; print(json.dumps(results,indent=2),flush=True)

# PH-3 bypass amplitude discrepancies, using both capture channels and actual played WAVs.
rows=[]
for p in sorted((ROOT/'captures/boss/ph-3').glob('*/bypass-sweep.json')):
 m=json.loads(p.read_text()); y,fs=sf.read(p.with_suffix('.wav')); x,_=sf.read(p.parent/'played'/p.with_suffix('.wav').name)
 stt=m['signal_start']; lat=m['latency_samples'];n=2**20; X=np.fft.rfft(x[stt:],n); fr=np.fft.rfftfreq(n,1/fs)
 a=(fr>500)&(fr<2000)
 levels=[float(np.median(db(np.fft.rfft(y[stt+lat:,ch],n)[a]/X[a]))) for ch in [0,1]]
 rows.append({'session':p.parent.name,'pedal_over_played_db':levels[0],'loop_over_played_db':levels[1]})
out['ph3_bypass']=rows
rows={}
for mode,tag in [('4 Stage','s4'),('8 Stage','s8'),('10 Stage','s10'),('12 Stage','s12')]:
 p=session('ph-3','2026-10-28'); name=tag+'-depth-0700-sweep'
 if not (p/(name+'.wav')).exists():
  print('PH3 static names', [x.stem for x in p.glob('*.wav') if 'depth' in x.stem or 'static' in x.stem],flush=True);break
 fr,h=ph.response(p,name); model=ph.FINDINGS['static'][mode]
 pred=ph.phaser(fr,model['mix'],model['sections'],model['tone'],passes=model.get('passes',1))
 a=(fr>40)&(fr<18000)
 rows[mode]=rms((np.maximum(db(pred),-30)-np.maximum(db(h),-30))[a])
out['ph3_old_static_fit_errors']=rows

s=session('dd-8','2026-10-26-standard')
out['dd8']={'sweeps':dd.measure_sweeps(s)[0],'hold':dd.feedback_max(s)}
Path(__file__).with_name('response_results.json').write_text(json.dumps(out,indent=2))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axs=plt.subplots(2,2,figsize=(11,7),layout='constrained')
for ax,(name,(measured,ys)) in zip(axs.flat,curves.items()):
 ax.semilogx(f,measured,label='Measured')
 for label in ['current_header','measured_complex_template']:ax.semilogx(f,ys[label],label=label)
 ax.set(xlim=(70,1500),ylim=(-14,10),title=name,xlabel='Hz',ylabel='dB re bypass');ax.legend(fontsize=7);ax.grid()
fig.savefig(Path(__file__).with_name('bf3_phase_diagnostic.png'),dpi=150)
