"""Read-only audit of WAVs and a separately exported pedals/all tree.

python tools/research/audit_captures.py /tmp/namx-dsp-audit/branch
Results are written beside this script. Production sources/captures are never changed.
Export used: git archive pedals/all tools/capture captures src/dsp | tar -x -C EXPORT
"""
import sys, json, hashlib, collections
from pathlib import Path
import numpy as np
import scipy.signal as sig
import scipy.optimize as opt
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
EXPORT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(EXPORT / 'tools/capture'))
import analysis, analyze_dd8 as dd, analyze_cs3 as cs, analyze_ge7 as ge
import analyze_bf3 as bf, analyze_ce5 as ce, analyze_ns2 as ns, analyze_oc5 as oc
import fit_tr2 as tr
OUT = Path(__file__).parent
results = {'source_branch': 'pedals/all', 'scope': 'Offline analysis; no C++ render performed'}

def save():
    (OUT/'audit_results.json').write_text(json.dumps(results, indent=2, default=lambda x: x.tolist() if isinstance(x,np.ndarray) else float(x)))

def db(x): return 20*np.log10(np.maximum(np.abs(x),1e-20))
def rms(x): return np.sqrt(np.mean(np.square(x)))
def session(p, s): return EXPORT/'captures/boss'/p/s

# Link WAVs into the disposable metadata export so original tools can read them.
rows=[]; missing=[]
for p in sorted((ROOT/'captures/boss').rglob('*.wav')):
    target=EXPORT/p.relative_to(ROOT)
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists(): target.symlink_to(p)
    if p.parent.name=='played': continue
    meta=target.with_suffix('.json')
    if not meta.exists(): missing.append(str(p.relative_to(ROOT))); continue
    m=json.loads(meta.read_text()); y,fs=sf.read(p)
    x,xfs=sf.read(p.parent/'played'/p.name,dtype='float32')
    start=m['signal_start']+m['latency_samples']
    marker=x[m['marker_start']:m['marker_start']+int(.05*fs)]
    expected=m['marker_start']+m['latency_samples']
    z=y[expected-8:expected+len(marker)+9,1]
    corr=sig.correlate(z,marker,mode='valid'); k=int(np.argmax(corr))
    peak=corr[k]; frac=.5*(corr[k-1]-corr[k+1])/(corr[k-1]-2*peak+corr[k+1]) if 0<k<len(corr)-1 else 0
    rel=p.relative_to(ROOT/'captures/boss')
    rows.append(dict(pedal=rel.parts[0],session=rel.parts[1],take=str(Path(*rel.parts[2:]).with_suffix('')),
        rate=fs,channels=y.shape[1],seconds=len(y)/fs,xruns=m.get('xruns',0),
        latency=m['latency_samples'],marker_offset_samples=k-8+frac,
        hash_ok=hashlib.sha256(x.tobytes()).hexdigest()==m['played_sha256'],
        digital_rail_samples=int(np.sum(np.abs(y)>=.999)),
        peak_dbfs=db(np.max(np.abs(y[start:]),axis=0)).tolist(),
        premarker_rms_dbfs=db(np.sqrt(np.mean(y[:int(.25*fs)]**2,axis=0))).tolist()))
results['inventory']={'counts':dict(collections.Counter(r['pedal'] for r in rows)),
    'missing_sidecars':missing,'bad_hashes':[r for r in rows if not r['hash_ok']],
    'xruns':[r for r in rows if r['xruns']], 'rail_hits':[r for r in rows if r['digital_rail_samples']],
    'sample_rates':sorted(set(r['rate'] for r in rows)),
    'marker_offset_range_samples':[min(r['marker_offset_samples'] for r in rows),max(r['marker_offset_samples'] for r in rows)],
    'takes':rows}
save(); print('inventory',results['inventory']['counts'],flush=True)

# Show the bypass window's bias, without interpreting LTV sweeps as LTI.
window=[]
for p in sorted((EXPORT/'captures/boss').glob('*/*/bypass-sweep*.json')):
    if not p.with_suffix('.wav').exists(): continue
    m=json.loads(p.read_text())
    if m['signal']['type']!='sweep': continue
    h,fs=dd.impulse_response(p.parent,p.stem); n=fs//2
    old=np.fft.rfft(h[:n]); good=np.fft.rfft(dd.head(h,n)); f=np.fft.rfftfreq(n,1/fs)
    a=(f>500)&(f<2000)
    window.append({'take':str(p.relative_to(EXPORT)), 'old_minus_corrected_db_500_2000':float(np.median(db(old[a]/good[a]))),
        'corrected_gain_db_500_2000':float(np.median(db(good[a]))),
        'negative_time_energy_fraction':float(np.sum(h[-dd.PRE_ROLL:]**2)/np.sum(h**2))})
results['bypass_window']=window; save(); print('bypass windows',len(window),flush=True)

# A true settings holdout: fit without the two TR-2 cross takes.
s=session('tr-2','2026-10-26-psu'); cycles=tr.measure_cycles(s)
all_shapes=tr.SHAPE_TAKES.copy(); tr.SHAPE_TAKES={k:v for k,v in all_shapes.items() if not k.startswith('cross')}
c,w,d,t,err,ph=tr.fit(cycles); tr.SHAPE_TAKES=all_shapes
res={'train_errors':err,'constants':c,'wave':w,'depth':d,'top':t,'holdout':{}}
curves={}
for name in ['cross-min','cross-max']:
    wi,di=all_shapes[name]; hz,y=cycles[name]
    def pred(p): return tr.model_cycle(c,w[wi],d[di],t[di],hz,p)
    fit=opt.minimize_scalar(lambda p: np.mean((pred(p)-y)**2),bounds=(-.2,.2),method='bounded')
    yh=pred(fit.x); theta=2*np.pi*(np.arange(len(y))+.5)/len(y)
    # Best possible constant plus sinusoid: generous lower error bound for generic tremolo.
    A=np.column_stack([np.ones(len(y)),np.sin(theta),np.cos(theta)])
    simple=A@np.linalg.lstsq(A,y,rcond=None)[0]
    res['holdout'][name]={'rms_gain':rms(yh-y),'max_gain':np.max(np.abs(yh-y)),
        'esr_db_envelope':db(rms(yh-y)/rms(y)), 'best_sinusoid_rms_gain':rms(simple-y),
        'h2_over_h1':abs(np.fft.rfft(y)[2]/np.fft.rfft(y)[1]),'fitted_phase_only':fit.x}
    curves[name]=(y,yh,simple)
results['tr2']=res; save(); print('TR2 holdout',res['holdout'],flush=True)

# GE-7 fit only isolated bands/Level; combinations are held out, not independently retuned.
ss=[session('ge-7','2026-10-28'+x) for x in ['', '-level','-retake','-marks']]
takes=ge.sweeps(ss)
train=[q for q in takes if sum(v!='0' for k,v in q['settings'].items() if k in ge.BANDS)<=1]
test=[q for q in takes if q not in train]
fit=ge.fit_tone(train); p=np.array(fit['parameters']); tests={}
for q in test:
    e=ge.model(p,q['settings'],q['session'])-q['db']; band=ge.GRID_HZ<5000
    tests[ge.label(q)]={'rms_db':rms(e),'rms_below_5k_db':rms(e[band]),'max_abs_db':np.max(np.abs(e))}
results['ge7']={'training_count':len(train),'holdout_count':len(test),'training_fit':fit,'holdout':tests}
save(); print('GE7 holdout',tests,flush=True)

# CS-3: test the claimed exponential recovery in dB using two shapes on identical windows.
s=session('cs-3','2026-10-27'); dyn=cs.dynamics(s); recovery={}
for tag in ['0700','0900','1200','0300','0500']:
    name='jump-attack-'+tag; g=cs.gain_envelope(s,name)
    tm=np.arange(5,1200)/1000; y=g[8005:9200]; end=np.median(g[11000:11900])
    # A dB one-pole versus logarithm of a linearly decaying excess detector level.
    def f1(p): return end-p[0]*np.exp(-tm/p[1])
    def f2(p): return end-20*np.log10(1+p[0]*np.exp(-tm/p[1]))
    p1=opt.least_squares(lambda p:f1(p)-y,[25,.15],bounds=([0,.0001],[100,5]))
    p2=opt.least_squares(lambda p:f2(p)-y,[20,.1],bounds=([0,.0001],[1e4,5]))
    recovery[name]={'t90_over_t63':dyn[name]['release_ms']['90%']/dyn[name]['release_ms']['63%'],
        'db_exponential_rms_db':rms(p1.fun),'linear_detector_log_gain_rms_db':rms(p2.fun),
        'linear_detector_tau_ms':p2.x[1]*1000}
results['cs3']={'static':cs.static_curve(s),'dynamics':dyn,'release_shape':recovery}; save()
print('CS3 recovery',recovery,flush=True)

# Re-evaluate selected independent descriptors directly from WAVs for every remaining pedal.
results['ce5']={'dry':ce.dry_path(session('ce-5','2026-10-27-mono')),
    'wet':ce.wet_tone(session('ce-5','2026-10-27-wet'),session('ce-5','2026-10-27-mono'))}
bst=bf.Static(session('bf-3','2026-10-27-standard'))
results['bf3']={'dry':bf.dry_path(bst), 'wet':bf.wet_tone(bst)}
results['oc5']={'dry':oc.dry_path(session('oc-5','2026-10-29')),
    'clip':oc.dry_clip(session('oc-5','2026-10-29'),session('oc-5','2026-10-29-followup'))}
save(); print('CE5 BF3 OC5 descriptors done',flush=True)
nt=ns.model_takes(session('ns-2','2026-10-27'),session('ns-2','2026-10-27-dynamics'))
results['ns2']={'errors_db':ns.evaluate(nt,ns.MODEL),'open_path':ns.open_path(session('ns-2','2026-10-27'))}
save(); print('NS2 done',flush=True)

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axs=plt.subplots(2,2,figsize=(11,7),layout='constrained')
for ax,(name,(y,yh,ys)) in zip(axs[0],curves.items()):
    phase=(np.arange(len(y))+.5)/len(y)
    ax.plot(phase,y,label='Measured'); ax.plot(phase,yh,label='Fit excluding this setting')
    ax.plot(phase,ys,':',label='Best sinusoid'); ax.set(title=name,xlabel='LFO phase',ylabel='Gain re bypass'); ax.legend(fontsize=8)
ax=axs[1,0]; ax.barh(list(tests),[v['rms_db'] for v in tests.values()]); ax.set(title='GE-7 combinations held out',xlabel='RMS magnitude error (dB)')
ax=axs[1,1]; ax.plot([v['release_ms']['63%'] for k,v in dyn.items() if k.startswith('jump-attack')],
    [v['release_ms']['90%'] for k,v in dyn.items() if k.startswith('jump-attack')],'o',label='CS-3 measurement')
ax.plot([0,500],[0,500*np.log(10)],'--',label='Single exponential in dB')
ax.set(xlabel='63% recovery (ms)',ylabel='90% recovery (ms)'); ax.legend(fontsize=8)
fig.savefig(OUT/'audit_residuals.png',dpi=150)
print('Saved',OUT/'audit_results.json',flush=True)
