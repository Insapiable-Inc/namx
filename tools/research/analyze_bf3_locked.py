"""Prospective BF-3 evaluation: frozen filters, gain and Res; delay from first Res-min take.

python tools/research/analyze_bf3_locked.py [SESSION]
Outputs stay in tools/research. Requires numpy, scipy, soundfile and matplotlib.
No production code or capture is changed. Retimed scores are explicitly diagnostic.
The archived mis-set take is excluded from the primary evaluation.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import optimize, signal

HERE = Path(__file__).resolve().parent
FS, N_IR, PRE = 48000, 24000, 2400
F = np.fft.rfftfreq(N_IR, 1 / FS)
Z = np.exp(-2j * np.pi * F / FS)
BANDS = [(80, 200), (100, 10000), (200, 1000), (1000, 10000), (10000, 16000)]
BASE = 'std-res-0700-before-24'
BEFORE, AFTER = 'bypass-before-24', 'bypass-after-24'
R = {'std-res-0700-before-24': 0., 'std-res-0300-24': .668,
     'std-res-0500-24': .849, 'std-res-0700-after-24': 0.}
GD, GW = 10 ** (-.13 / 20), 10 ** (1.45 / 20)


def rms(x):
    return float(np.sqrt(np.mean(np.abs(x) ** 2)))


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-15))


def fine_db(h):
    cumulative = np.r_[0., np.cumsum(np.abs(h) ** 2)]
    low = np.searchsorted(F, F / 1.01)
    high = np.searchsorted(F, F * 1.01, side='right')
    return 10 * np.log10(np.maximum((cumulative[high] - cumulative[low]) /
                                    np.maximum(high - low, 1), 1e-30))


def deconvolve(x, y):
    n = 1 << int(np.ceil(np.log2(2 * len(x))))
    f = np.fft.rfftfreq(n, 1 / FS)
    mask = (f > 25) & (f < 19000)
    h = np.zeros(n // 2 + 1, complex)
    h[mask] = np.fft.rfft(y, n)[mask] / np.fft.rfft(x, n)[mask]
    impulse = np.fft.irfft(h, n)
    return np.fft.rfft(np.r_[impulse[:N_IR - PRE], impulse[-PRE:]])


def load_take(session, name):
    meta = json.loads((session / (name + '.json')).read_text())
    y, fs = sf.read(session / (name + '.wav'))
    x, xfs = sf.read(session / 'played' / (name + '.wav'), dtype='float32')
    assert fs == xfs == FS and y.ndim == 2 and y.shape[1] == 2
    played_hash = hashlib.sha256(x.tobytes()).hexdigest()
    assert played_hash == meta['played_sha256']
    start = meta['signal_start']; latency = meta['latency_samples']
    body = y[start + latency:start + latency + len(x) - start]
    xb = x[start:start + len(body)].astype(float)
    response = np.array([deconvolve(xb, body[:,ch]) for ch in [0, 1]])
    stats = {'recorded_at':meta['recorded_at'], 'settings':meta['settings'],
             'xruns':meta['xruns'], 'latency_samples':latency,
             'marker_quality':meta['latency_quality'], 'played_sha256':played_hash,
             'recorded_file_sha256':hashlib.sha256((session / (name + '.wav')).read_bytes()).hexdigest(),
             'abs_ge_0999_count':int(np.sum(np.abs(y) >= .999)),
             'body_peak_dbfs':db(np.max(np.abs(body[:10*FS]), axis=0)).tolist(),
             'final_half_second_rms_dbfs':db(np.sqrt(np.mean(y[-FS//2:]**2, axis=0))).tolist()}
    return response, stats


def biquad(kind, hz, q):
    w = 2 * np.pi * hz / FS; c = np.cos(w); a = np.sin(w) / (2 * q)
    b = [(1+c)/2, -(1+c), (1+c)/2] if kind == 'hp' else [(1-c)/2, 1-c, (1-c)/2]
    return np.r_[b, 1+a, -2*c, 1-a] / (1+a)


def models(fits):
    current = np.array([biquad(k, f, q) for k,f,q in
                        [('hp',205.4,.573), ('hp',97.3,1.784),
                         ('lp',6824.1,.481), ('lp',18401.4,.817)]])
    out = {}
    for name, key, feedback in [('current',None,True),
                               ('candidate_cached_feedback','hp2_joint',True),
                               ('candidate_current_feedback','hp2_joint',False),
                               ('hp1_current_feedback','hp1_joint',False)]:
        if key:
            row = fits['models'][key]
            sos = np.array(row['sos'][str(FS)])
            comp = row['tone_delay_compensation_ms']; gain_db = row['gain_correction_db']
        else:
            sos = current; comp = .068; gain_db = 0.
        g = 10**(gain_db/20) / abs(signal.sosfreqz(sos, worN=[1000], fs=FS)[1][0])
        out[name] = {'wet':signal.sosfreqz(sos,worN=F,fs=FS)[1]*g,
                     'comp':comp, 'cached':feedback}
    return out


def predict(model, delay_ms, r):
    d = (delay_ms-model['comp'])*FS/1000
    n = int(np.floor(d)); mu = d-n
    wet = model['wet'] * Z**n * (1-mu+mu*Z)
    return GD + GW*wet/(1-r*(Z if model['cached'] else 1)*wet)


def fit_delay(model, measured, r):
    band = (F>=100)&(F<=10000)
    def loss(d):return rms((fine_db(predict(model,d,r))-measured)[band])
    grid = np.linspace(2.005*.96,2.005*1.04,161)
    k = int(np.argmin([loss(d) for d in grid]))
    fit = optimize.minimize_scalar(loss, bounds=(grid[max(0,k-1)],grid[min(len(grid)-1,k+1)]),
                                   method='bounded',options={'xatol':1e-11})
    return float(fit.x), float(fit.fun)


def errors(predicted, measured):
    error = predicted-measured
    out = {}
    for a,b in BANDS:
        e = error[(F>=a)&(F<=b)]
        out[f'{a}_{b}'] = {'rms_db':rms(e), 'max_abs_db':float(np.max(np.abs(e))),
                           'p95_abs_db':float(np.percentile(np.abs(e),95)), 'mean_db':float(np.mean(e))}
    return out


def waveform_spectra(session, name):
    """Actual aligned sweep and output, for a band-limited waveform error via Parseval."""
    meta=json.loads((session/(name+'.json')).read_text())
    x,_=sf.read(session/'played'/(name+'.wav'))
    y,_=sf.read(session/(name+'.wav'))
    start=meta['signal_start']; latency=meta['latency_samples']
    body=y[start+latency:start+latency+len(x)-start,0]
    x=x[start:start+len(body)]
    n=1<<int(np.ceil(np.log2(2*len(x))))
    f=np.fft.rfftfreq(n,1/FS);keep=(f>=100)&(f<=10000)
    return np.fft.rfft(x,n),np.fft.rfft(body,n),n,keep


def waveform_error(h, spectra):
    """Convolve played sweep with predicted I/O impulse, preserving negative times.

    h includes measured bypass transfer and one fixed transport delay. No gain or
    alignment fit is performed on the evaluated take. Parseval gives normalized
    waveform RMS after a common ideal 100 Hz–10 kHz band limit, including tails.
    """
    x,y,n,band=spectra
    short=np.fft.irfft(h,N_IR); impulse=np.zeros(n)
    impulse[:N_IR-PRE]=short[:N_IR-PRE];impulse[-PRE:]=short[-PRE:]
    predicted=np.fft.rfft(impulse)*x
    return float(np.sqrt(np.sum(abs(predicted[band]-y[band])**2)/np.sum(abs(y[band])**2)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session',nargs='?',type=Path,
                        default=HERE.parents[1]/'captures/boss/bf-3/2026-10-30-standard-locked')
    args = parser.parse_args(); session = args.session
    fit_path = HERE/'bf3_complex_results.json'
    fits = json.loads(fit_path.read_text()); model_set = models(fits)
    names = [BEFORE, BASE, 'std-res-0300-24', 'std-res-0500-24', 'std-res-0700-after-24', AFTER]
    responses, integrity = {}, {}
    for name in names:responses[name],integrity[name] = load_take(session,name)
    reference = responses[BEFORE][0]
    measured = {name:fine_db(h[0]/reference) for name,h in responses.items()}
    repeat = {}
    for a,b in [(AFTER,BEFORE),('std-res-0700-after-24',BASE)]:
        repeat[a+'_over_'+b] = errors(measured[a],measured[b])
        band=(F>=100)&(F<=10000)
        repeat[a+'_over_'+b]['unsmoothed_rms_100_10000_db']=rms(
            db(responses[a][0]/responses[b][0])[band])
    loop = {}
    mask = (F>=100)&(F<=16000)
    for name,h in responses.items():
        ratio = h[1]/responses[BEFORE][1]; gain = db(ratio)[mask]
        phase = np.unwrap(np.angle(ratio[mask])); line = np.polyfit(F[mask],phase,1)
        loop[name] = {'median_change_db':float(np.median(gain)),
                      'shape_rms_after_median_db':rms(gain-np.median(gain)),
                      'phase_slope_delta_samples':float(-line[0]*FS/(2*np.pi)),
                      'phase_residual_rms_rad':rms(phase-np.polyval(line,F[mask]))}
    # Separate the first dry arrival from the wet (~2.5 ms after the aligned input).
    # Only the FIRST minimum-Res take supplies transport for all waveform predictions.
    times=np.arange(N_IR);times=np.where(times<N_IR-PRE,times,times-N_IR)/FS
    dry={}
    for name in R:
        h=np.fft.irfft(responses[name][0],N_IR)
        dry[name]=np.fft.rfft(np.where((times>=-.001)&(times<.0015),h,0.))/reference
    dry_band=(F>=300)&(F<=5000)
    slope=np.polyfit(F[dry_band],np.unwrap(np.angle(dry[BASE][dry_band])),1)
    transport_s=-slope[0]/(2*np.pi)
    dry_checks={}
    for name,h in dry.items():
        ratio=h/dry[BASE]
        slope=np.polyfit(F[dry_band],np.unwrap(np.angle(ratio[dry_band])),1)
        dry_checks[name]={'relative_delay_samples':float(-slope[0]*FS/(2*np.pi)),
                          'relative_magnitude_rms_db':rms(db(ratio[dry_band]))}
    wave_spectra={name:waveform_spectra(session,name) for name in R}
    evaluation, curves = {}, {}
    for model_name,model in model_set.items():
        fitted_delay, loss = fit_delay(model,measured[BASE],0.)
        model_rows = {}; curves[model_name] = {}
        for name,r in R.items():
            complex_prediction=predict(model,fitted_delay,r)
            predicted = fine_db(complex_prediction)
            free_delay,free_error = fit_delay(model,measured[name],r)
            model_rows[name] = {'calibration_take':name==BASE, 'r':r,
                                'frozen_delay_metrics':errors(predicted,measured[name]),
                                'diagnostic_refitted_delay_ms':free_delay,
                                'diagnostic_delay_change_samples':(free_delay-fitted_delay)*FS/1000,
                                'diagnostic_refitted_rms_100_10000_db':free_error,
                                'waveform_nrmse_100_10000':waveform_error(
                                    complex_prediction*reference*np.exp(-2j*np.pi*F*transport_s),
                                    wave_spectra[name])}
            curves[model_name][name] = predicted
        evaluation[model_name] = {'delay_from_first_minimum_res_ms':fitted_delay,
                                  'calibration_rms_100_10000_db':loss, 'takes':model_rows}
    supplementary={}
    archive=session/'discarded/std-res-0300-24-actual-res-0900'
    if (archive/'correction.json').exists():
        correction=json.loads((archive/'correction.json').read_text())
        assert correction['actual_settings_correction']['Res']=='9:00'
        h,stats=load_take(archive,'std-res-0300-24')
        target=fine_db(h[0]/reference)
        supplemental_rows={}
        for model_name,model in model_set.items():
            d=evaluation[model_name]['delay_from_first_minimum_res_ms']
            supplemental_rows[model_name]=errors(fine_db(predict(model,d,.146)),target)
        supplementary={'role':'Unplanned supplementary check only; actual Res 9:00 identified by user, correction preserved alongside original files.',
                        'archive':str(archive),'actual_res':'9:00','r':.146,
                        'integrity_original_metadata':stats,'evaluation':supplemental_rows}
    result = {'session':str(session), 'frozen_parameter_artifact_sha256':hashlib.sha256(fit_path.read_bytes()).hexdigest(),
              'integrity':integrity,'loop_repeatability':loop,'pedal_repeatability':repeat,'evaluation':evaluation,
              'waveform_transport_ms_from_first_dry':float(transport_s*1000),
              'dry_path_repeatability_300_5000':dry_checks,
              'supplementary_actual_res_0900':supplementary,
              'waveform_method':'Played sweep convolved with model times measured before-bypass transfer and one transport delay from first dry arrival. Comparison with actual aligned pedal recording via Parseval after common ideal 100 Hz–10 kHz band limit; no evaluation gain or timing fit. Double-precision frequency-domain model, not compiled C++.',
              'method':'Same 0.5 s impulse window with 50 ms negative-time preservation as earlier BF-3 study; 1% power smoothing; uniform FFT bins. Separate model delay fitted only on first minimum-Res response, then frozen. All gains, filters and Res maps fixed before new captures. Refitted-delay results are post-hoc diagnostics only.',
              'limitations':['Replacement Res 3:00 recorded after final bypass; its reference is not temporally bracketed.',
                             'Loop phase-slope agreement is not a complete test for arbitrary mid-take dropouts/drift.',
                             'No independent THD, modulation or listening validation from these quiet static sweeps.',
                             'Physical controls remaining untouched is the capture protocol, not independently observable.']}
    (HERE/'bf3_locked_results.json').write_text(json.dumps(result,indent=2)+'\n')
    for model_name,row in evaluation.items():
        print(model_name, 'delay',row['delay_from_first_minimum_res_ms'])
        for name,take in row['takes'].items():
            print(' ',name, 'fixed',round(take['frozen_delay_metrics']['100_10000']['rms_db'],4),
                  'free',round(take['diagnostic_refitted_rms_100_10000_db'],4),
                  'delay shift samples',round(take['diagnostic_delay_change_samples'],4))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axs = plt.subplots(2,2,figsize=(13,8),layout='constrained',sharex=True)
    labels = {'current':'Current model', 'candidate_cached_feedback':'Candidate / cached feedback',
              'candidate_current_feedback':'Candidate / current wet feedback'}
    for col,name in enumerate(['std-res-0300-24','std-res-0500-24']):
        axs[0,col].semilogx(F,measured[name],color='black',label='Pedal',linewidth=1.6)
        for key,label in labels.items():
            axs[0,col].semilogx(F,curves[key][name],label=label,alpha=.8)
            axs[1,col].semilogx(F,curves[key][name]-measured[name],alpha=.8)
        axs[0,col].set(title=name,ylabel='dB relative to bypass');axs[0,col].legend(fontsize=8)
        axs[1,col].axhline(0,color='black',linewidth=.7)
        axs[1,col].set(xlabel='Hz',ylabel='Model minus pedal (dB)')
        visible=(F>=80)&(F<=16000)
        levels=np.concatenate([measured[name][visible]]+[curves[k][name][visible] for k in labels])
        residual=np.concatenate([(curves[k][name]-measured[name])[visible] for k in labels])
        axs[0,col].set_ylim(np.floor(levels.min())-1,np.ceil(levels.max())+1)
        axs[1,col].set_ylim(np.floor(residual.min())-1,np.ceil(residual.max())+1)
        for ax in axs[:,col]:ax.set_xlim(80,16000);ax.grid(alpha=.4)
    fig.suptitle('New BF-3 captures: delay calibrated once at minimum Res, then frozen')
    fig.savefig(HERE/'bf3_locked_validation.png',dpi=150)


if __name__=='__main__':main()
