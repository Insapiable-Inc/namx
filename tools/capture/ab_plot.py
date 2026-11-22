"""A listening pair drawn: the pedal's file and the model's from SESSION/ab/ (tools/capture/ab.py)
as envelopes, spectrograms, the spectrograms' difference (red: the model louder) and the waveform
difference (large whenever the two aren't sample-aligned, which they aren't after a few repeats of
a delay: read the others first).

  ab_plot.py SESSION TAKE [FROM_S TO_S]     writes SESSION/fit/ab-TAKE.png"""
import sys, numpy as np, soundfile, scipy.signal
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
import pathlib
SESSION=pathlib.Path(sys.argv[1]); D=str(SESSION/"ab")+"/"
name=sys.argv[2]; t0=float(sys.argv[3]) if len(sys.argv)>3 else 0; t1=float(sys.argv[4]) if len(sys.argv)>4 else None
p,_=soundfile.read(D+name+".pedal.wav"); m,_=soundfile.read(D+name+".model.wav")
a=int(t0*48000); b=int(t1*48000) if t1 else len(p); p,m=p[a:b],m[a:b]
fig,ax=plt.subplots(5,1,figsize=(16,20))
t=np.arange(len(p))/48000+t0
env=lambda x: 20*np.log10(np.maximum(scipy.signal.sosfiltfilt(scipy.signal.butter(2,40,fs=48000,output="sos"),np.abs(x)),1e-6))
ax[0].plot(t,env(p),label="pedal",lw=0.8); ax[0].plot(t,env(m),label="model",lw=0.8); ax[0].set_ylim(-70,0); ax[0].legend(); ax[0].set_title(name+": envelope, dB"); ax[0].grid(alpha=.3)
kw=dict(fs=48000,nperseg=2048,noverlap=1536)
f,tt,P=scipy.signal.spectrogram(p,**kw); _,_,M=scipy.signal.spectrogram(m,**kw)
P,M=10*np.log10(P+1e-14),10*np.log10(M+1e-14); k=f<10000; vmax=max(P.max(),M.max())
for i,(X,lab) in enumerate(((P,"pedal"),(M,"model"))):
    ax[1+i].pcolormesh(tt+t0,f[k],X[k],vmin=vmax-90,vmax=vmax,shading="auto",cmap="magma"); ax[1+i].set_title(lab); ax[1+i].set_yscale("symlog",linthresh=1000); ax[1+i].set_ylim(60,10000)
d=M-P; d[(P<vmax-85)&(M<vmax-85)]=0
im=ax[3].pcolormesh(tt+t0,f[k],d[k],vmin=-12,vmax=12,shading="auto",cmap="RdBu_r"); ax[3].set_title("model − pedal, dB (red: model louder)"); ax[3].set_yscale("symlog",linthresh=1000); ax[3].set_ylim(60,10000); plt.colorbar(im,ax=ax[3])
ax[4].plot(t,p,lw=0.4,label="pedal"); ax[4].plot(t,m-p,lw=0.4,label="model − pedal"); ax[4].legend(); ax[4].set_title("waveform and difference")
plt.tight_layout(); (SESSION/"fit").mkdir(exist_ok=True); out=str(SESSION/"fit"/f"ab-{name}{'-zoom' if t1 else ''}.png"); plt.savefig(out,dpi=70); print(out)
