#!/usr/bin/env python3
"""The AW-3 jumps session (2026-11-11-jumps): per take, where the filter sits in the quiet noise and how
it returns after each loud stretch (10/63/90 % of the way back, ms). Writes the tracks to
build-aw3/jumps2.npz for fit_jumps.py. Run from the repository root."""
import sys, json, pathlib, numpy as np
sys.argv = sys.argv[:1]
sys.path.insert(0, "tools/capture"); sys.path.insert(0, "tools/capture/aw3")
import scorecard, focused as F
S = pathlib.Path("captures/boss/aw-3/2026-11-11-jumps"); RATE = 48000; C = F.constants()
TAB = F.static_gains(C, False, F.BIN_HZ); B = 10
meta, played, pedal, _ = scorecard.load(S, "bypass-jumps")
h = scorecard.chain_filter(played, pedal)
out = {}
for p in sorted(S.glob("sens-*.json")):
    name = p.stem
    meta, played, pedal, _ = scorecard.load(S, name)
    dry = scorecard.through(h, played); db = F.spectra(dry); pb = F.spectra(pedal)[:, :db.shape[1]]
    g = 20 * np.log10(np.maximum(pb, 1e-12) / np.maximum(db, 1e-12))
    n = g.shape[1] // B
    gm = np.median(g[:, :n * B].reshape(len(g), n, B), axis=2)
    e = np.sqrt(np.mean((TAB[:, :, None] - gm[None]) ** 2, axis=1))
    f = F.GRID[np.argmin(e, axis=0)]
    a = meta["signal_start"] / RATE
    out[name] = dict(settings=meta["settings"], x=(played / 10 ** (meta["ref_dbfs"] / 20)).astype(np.float32), f=f, start=meta["signal_start"])
    rest = np.median(f[int((a + 2) * 100):int((a + 3.4) * 100)])
    text = f"{name:24s} quiet {rest:5.0f} Hz |"
    for k, (lv, t0) in enumerate(((0, 7.0), (-12, 14.0), (-24, 21.0))):
        seg = f[int((a + t0) * 100):int((a + t0 + 3.4) * 100)]
        top = np.median(f[int((a + t0 - 1) * 100):int((a + t0 - 0.1) * 100)]); end = np.median(seg[-60:])
        ts = [int(np.argmax(seg < top - q * (top - end)) * 10) for q in (0.1, 0.63, 0.9)]
        text += f" {lv:3d} dB: at {top:4.0f} Hz, back 10/63/90 % {ts[0]:4d} {ts[1]:4d} {ts[2]:4d} ms, to {end:4.0f} |"
    print(text)
np.savez_compressed("build-aw3/jumps2.npz", data=np.array(out, dtype=object))
