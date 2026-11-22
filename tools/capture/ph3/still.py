#!/usr/bin/env python3
"""The PH-3 standing still (the Res session, 2026-11-11-res, part D: Rate 7:00 stops the sweep in every
mode, Rise and Fall too): each mode's response from a sweep at Res 12:00, 3:00 and 5:00, against
the bypass sweep. Prints Rise's and Fall's peaks and notches; writes SESSION/fit/still.png and
build-ph3/static.npz. Run from the repository root."""
import sys, pathlib, numpy as np, scipy.signal
sys.path.insert(0, "tools/capture"); import scorecard
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
S=pathlib.Path("captures/boss/ph-3/2026-11-11-res"); R=48000
def H(name):
    m,p,y=scorecard.load(S,name)[:3]; a=m["signal_start"]; n=int(11*R)
    mb,pb,yb=scorecard.load(S,"bypass-sweep")[:3]; ab=mb["signal_start"]
    Y=np.fft.rfft(y[a:a+n],n); B=np.fft.rfft(yb[ab:ab+n],n); f=np.fft.rfftfreq(n,1/R)
    h=Y/B; sel=(f>40)&(f<12000)
    # smooth 1/24 octave
    fo=np.geomspace(50,11000,600); mag=np.array([np.sqrt(np.mean(np.abs(h[(f>q/1.015)&(f<q*1.015)])**2)) for q in fo])
    return fo,20*np.log10(mag)
fig,axes=plt.subplots(2,2,figsize=(17,9),sharex=True,sharey=True)
out={}
for ax,mode in zip(axes.flat,("rise","fall","8stage","4stage")):
    for res,c in (("1200","tab:green"),("300","tab:orange"),("500","tab:red")):
        fo,g=H(f"d-stop-res{res}-{mode}-sweep"); out[(mode,res)]=g; ax.semilogx(fo,g,color=c,label=f"Res {res[:-2]}:00")
        if mode in("rise","fall"):
            pk=scipy.signal.find_peaks(g,prominence=4)[0]; nt=scipy.signal.find_peaks(-g,prominence=4)[0]
            print(mode,res,"peaks:"," ".join(f"{fo[i]:.0f}({g[i]:+.1f})" for i in pk),"| notches:"," ".join(f"{fo[i]:.0f}({g[i]:+.1f})" for i in nt), "| median %.1f"%np.median(g))
    ax.set_title(f"{mode}, sweep stopped (Rate 7:00), Depth 5:00"); ax.grid(alpha=.3,which="both"); ax.legend(); ax.set_ylim(-40,30)
fig.tight_layout(); (S/"fit").mkdir(exist_ok=True); fig.savefig(S/"fit/still.png",dpi=70)
np.savez("build-ph3/static.npz",fo=fo,**{f"{k[0]}_{k[1]}":v for k,v in out.items()})
