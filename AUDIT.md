# NAM Player — Code audit and improvement proposals (2026-09-12)

Scope: everything under `gui/src` (about 7,900 lines) plus `gui/CMakeLists.txt`.
Vendored code (`NAM/`, `Dependencies/`) was not reviewed. Every finding below was
verified against the source at commit `dce3c94`; line numbers refer to that commit.
Paths are relative to `gui/src/`.

Items already on the roadmap and remembered from earlier discussions are **not**
lost: MIDI is Phase 16, preset prev/next is Phase 11 (library arrows) and Phase 13
(‹ › in the top bar, quick slots), the TONE3000 browser is Phase 12.

Proposed handling: a **Phase 7.5 — pre-packaging fixes** block in `ROADMAP.md`
holding Tier 1 plus the starred (★) Tier 2 items; the rest of Tier 2 and all of
Tier 3 fold into Phases 9, 14, 17 and 18 where they already have homes.

---

## Tier 1 — Real bugs (fix before Phase 8 packaging)

Ranked by audible / user impact.

### 1.1 Model sample rate ignored — HIGH
`dsp/NamProcessor.cpp:61,144` call `Reset(engineRate, …)` and never read
`nam::DSP::GetExpectedSampleRate()` (`NAM/dsp.h:100`). A 48 kHz-trained model on
a 44.1 kHz session shifts the whole amp response ~8 %. The official plugin
resamples in/out of the model's rate.
**Fix:** resample around the model (windowed-sinc, like the IR path) when the
rates differ, or at minimum warn in the Amplifier row label.

### 1.2 Freeverb constants swapped — HIGH
`dsp/ReverbProcessor.cpp:47,80`: `feedback = 0.28 + room*0.7`. Freeverb is
`room*0.28 + 0.7` (range 0.70–0.98). Default Room Size 50 gives 0.63 (very
short) and the bottom half of the knob is nearly dead. The `scalewet = 3` factor is
also missing (`:68-69,:91`), so the wet path sits ~9.5 dB low at any Mix.
**Fix:** swap the constants, add `scalewet`, re-check `kReverbRoomDefault`.

### 1.3 JACK server death leaks the client — HIGH
`dsp/JackBackend.cpp:172` (shutdown callback) sets `mRunning=false`; `stop()`
(`:76`) then early-returns, so `jack_client_close` is never called, `mCallback` is
never cleared, and the zombie client leaks when `AudioEngine::start()`
(`dsp/AudioEngine.cpp:170-172`) replaces the backend. `MainWindow::saveConnections()`
(`ui/MainWindow.cpp:155`) is also skipped if the server died before exit, so port
routing is lost.
**Fix:** make `stop()` close the client whenever `mClient` is non-null,
independent of `mRunning`; save connections on the last successful state.

### 1.4 No JACK sample-rate callback — HIGH
`dsp/JackBackend.cpp:38,41-43` cache the rate once at start; there is no
`jack_set_sample_rate_callback`. PipeWire can switch graph rate at runtime;
`AudioEngine::onBufferSizeChanged` (`dsp/AudioEngine.cpp:243`) then reads a
stale value, so effects, IR and NAM keep the old rate.
**Fix:** register the callback, route it through the same path as buffer-size
changes (the IR already rebuilds on engine-rate change).

### 1.5 Stale right-channel state on mono → stereo transitions — HIGH (systemic)
`dsp/EffectBase.h:76-78`: in mono mode only the L state is fed. When an upstream
effect later switches to stereo, the R side starts cold or stale:
- `DelayProcessor.cpp:53-57` — `mBufR` replays an old tail;
- `ReverbProcessor.cpp:105` — `mCombR` bank likewise;
- `ChorusProcessor.cpp:80`, `FlangerProcessor.cpp:81` — `mBufR`;
- `EQProcessor.cpp:60-62` — R biquads from zero;
- `NamProcessor.cpp:162-163` — R model never run while mono, receptive field
  empty → ~50–100 ms glitch on R.
Only `IRLoader.cpp:207-216` handles it (mirrors state).
**Fix:** either feed both states in mono (cheap for filters, doubles NAM cost) or
copy L→R state on the mono→stereo edge (`reset()`-style hook with a flag).

### 1.6 Preset load re-loads an already-loaded model / IR — MEDIUM
`ui/MainWindow.cpp:1305,1314`: no `modelPath() == applied.namPath` /
`irPath() == applied.irPath` guard, so every preset load runs `get_dsp` + prewarm
×2 synchronously on the GUI thread (freeze) and swaps model state (audible).
Will hurt badly once presets cycle with arrow keys (Phase 13).
**Fix:** one-line compare before each load.

### 1.7 Knob wheel ignores high-resolution scrolling — MEDIUM
`ui/KnobWidget.cpp:304-312`: any non-zero `angleDelta().y()` applies a full step.
Wayland/libinput touchpads emit many small deltas (±12, ±24), so knobs fly
across their range.
**Fix:** accumulate delta and step per ±120 (or use `pixelDelta`).

### 1.8 Preset dialog quirks — MEDIUM
- `ui/MainWindow.cpp:1231`: Save uses the static getter, so no
  `setDefaultSuffix("nampreset")`; portal / non-native dialogs write `myrig` as-is,
  which the `*.nampreset` filter then hides on load.
- `ui/MainWindow.cpp:1277`: `files/lastPreset` is written before the file is
  opened/parsed, so an unreadable or corrupt file becomes the default next time.
- `ui/MainWindow.cpp:1287`: only `doc.isNull()` is checked. `[]` or `42` parse fine,
  `doc.object()` is empty, and `PresetSerializer::apply` resets `inputGain`,
  `outputGain`, `namEnabled`, `irEnabled` to defaults with no warning. Also no
  `version > 2` check.
- Load applies toggles before the model, so a preset with `namEnabled=true` and a
  missing model plays the *previous* model.

### 1.9 Bypass toggles click; re-enable replays stale tails — MEDIUM
`dsp/EffectBase.h:59-61` is a hard switch, no crossfade. While disabled,
delay/reverb buffers aren't fed, so re-enabling plays whatever was left in them.
NAM has a load fade (`dsp/NamProcessor.cpp:170-181`) but no enable fade, so
toggling the amp is a large step.
**Fix:** ~10 ms equal-power crossfade on enable/disable in `EffectBase::process`;
clear (or keep feeding) time-domain buffers while bypassed.

### 1.10 Smaller DSP defects — MEDIUM / LOW
- **EQ `try_lock` miss = one block of hard bypass** (`dsp/EQProcessor.cpp:54-55`).
  At ±12 dB shelf that's an audible tick whenever the GUI holds the lock; no
  coefficient smoothing (`:21-46`) → zipper on sweeps. Use lock-free
  double-buffered coefficients (same shape as `RtSlot`).
- **Delay time is integer, uninterpolated, unsmoothed** (`dsp/DelayProcessor.cpp:25,29`):
  the read pointer jumps on every knob step → clicks. Needs fractional read +
  time smoothing or a crossfade.
- **Dynamic wah not level-compensated** (`dsp/DynamicWahProcessor.cpp:41-47`):
  raw Chamberlin `bp` gain ≈ Q, up to +18 dB at Res=100 with
  `kWahMixDefault = 100` (`dsp/Constants.h:154`). Also `f = 2π·fc/sr` (`:41`)
  instead of `2·sin(π·fc/sr)` — tuning error grows toward 3 kHz. Env→freq map is
  linear (`:40`); should be exponential.
- **Reverb mono/stereo level mismatch** (`dsp/ReverbProcessor.cpp:52` vs `:85`):
  stereo path uses `(L+R)*0.015` (= 2·L for identical channels), mono uses
  `buf*0.015` → toggling Stereo changes reverb level by 6 dB.
- **Noise gate has no hysteresis / hold** (`dsp/NoiseGateProcessor.cpp:31`):
  single threshold → chatter on decaying notes.
- **Compressor attack/release applied twice** (`dsp/CompressorProcessor.cpp:29-32`
  detector and `:38-41` gain smoother) → effective times ≈ 2× the knob.
- **Chorus/Flanger depth & mix, Phaser depth unsmoothed**
  (`dsp/ChorusProcessor.cpp:46-47`, `dsp/FlangerProcessor.cpp:40`,
  `dsp/PhaserProcessor.cpp:21`) → pitch blip / click per knob step. Rate is fine
  (phase-continuous).
- **Tuner mutex held during a 4096-sample copy** (`dsp/TunerProcessor.cpp:100-106`);
  an audio-thread `try_lock` miss drops a block from the ring → small NSDF wobble.

### 1.11 Windows-specific — MEDIUM
- **Device disappears → misleading label + silent fallback**
  (`ui/AudioStatusBar.cpp:70,179`): a stored ID that no longer enumerates shows the
  *first* device's name, while WASAPI falls back to the default endpoint
  (`dsp/WasapiBackend.cpp:257,270`). The stale ID persists forever.
- **`AUDCLNT_E_DEVICE_INVALIDATED` unhandled** (`dsp/WasapiBackend.cpp:581-584,588`):
  the loop spins on 2 s timeouts; the GUI is never told.
- **N-channel capture averaged to mono** (`dsp/WasapiBackend.cpp:618-623`): a
  guitar on ch1 of a 2-in interface loses 6 dB and picks up ch2's noise. Needs an
  input-channel selector.
- **`mBufferSize` = whole capture buffer, not the period** (`dsp/WasapiBackend.cpp:413`):
  status-bar latency is overstated.

### 1.12 Cosmetic / dead code — LOW
- `ui/MainWindow.cpp:811`: "Reset Tuner/Amplifier/Cabinet to defaults" context
  entries do nothing (only the skipped enable toggle lives there).
- `ui/Theme.h:50` says `tagCard` overrides `QPalette::Highlight` — it doesn't
  (`Theme.cpp:130`; the global palette does at `:350`).
- `ui/Theme.cpp:267-310`: `QComboBox` stylesheet rules have no consumer.
- `ui/KnobWidget.h:39` `setIntegerDisplay` and `VUMeter(Qt::Horizontal)` are unused.
- `main.cpp:49` hard-codes `"1.0"`; CMake says `1.0.0` — pass `PROJECT_VERSION` as
  a define.
- `CLAUDE.md` names an `inPreset` field on `ParamDesc` that doesn't exist.
- `ui/MainWindow.cpp:1213,1218` use `section('/', -1)` for file names — use
  `QFileInfo::fileName()` for hand-written Windows paths.
- `ui/KnobWidget.cpp` always emits `valueChanged` on `setValue` even when
  unchanged, so `restoreSessionState` schedules a session save 1 s after every
  launch. Harmless now; Phase 13 dirty-tracking must not hang off these signals.
- Keypad digits don't trigger effect shortcuts (`ui/EffectShortcuts.cpp:12,47`:
  `QKeySequence(Key_1)` ≠ `Key_1|KeypadModifier`). Bare digits / Backspace are
  window-wide and will fight the Phase 11 search field — plan a
  `WidgetWithChildrenShortcut` context or disable while a line-edit has focus.
- Window geometry saved only in the destructor (`ui/MainWindow.cpp:153`); a crash
  loses it. Also save from `resizeEvent` via the existing debounce timer.
- Label inconsistencies: rows say "Amplifier"/"Cabinet", dialogs say "Load NAM
  Model"/"Load Impulse Response"; every error dialog is titled "Error"; menu says
  "Preset", filter says "NAM Preset".

---

## Tier 2 — Cheap UX wins (each well under an hour)

★ = proposed for Phase 7.5.

- ★ **Enable NAM fast tanh.** `nam::activations::Activation::enable_fast_tanh()`
  (`NAM/activations.h:163`) is never called in `gui/`. One line at startup,
  ~2.4× faster on Tanh WaveNets; the official plugin does it.
  (`NAM_ENABLE_A2_FAST` in `CMakeLists.txt:137` is a different path.)
- ★ **Quit action / Ctrl+Q.** None in any menu (`ui/MainWindow.cpp:224-247`).
- ★ **JACK device pickers are decoys.** `dsp/JackBackend.cpp:94-101` enumerate one
  fake "JACK Server" entry, so the In:/Out: status-bar items offer a single choice.
  Enumerate physical ports with `jack_get_ports` — `restoreConnections`
  (`ui/MainWindow.cpp:1150`) already stores exactly that.
- ★ **Auto-reconnect** when JACK/PipeWire appears (2 s retry while not connected)
  instead of requiring a click on the status item (`ui/AudioStatusBar.cpp:134`).
- ★ **First-run hint when audio is not connected.** Today the status bar shows
  "● Not connected" and the rack looks alive but is silent. One line in the strip:
  "Audio not connected — start PipeWire/JACK and click here".
- ★ **Wheel over a knob hijacks strip scrolling** (`ui/KnobWidget.cpp:301`).
  Scrolling the rack while the cursor crosses a 40 px dial changes a parameter.
  Only consume the wheel when the dial has focus or the pointer has rested on it.
- **Output mute / panic** toggle in the Output column; registry param excluded
  from presets; bound to a key.
- **Shift+drag fine adjust** (`ui/KnobWidget.cpp:58` has a single 200 px full-range
  gesture); Ctrl+wheel fine step; **typed value entry** via the existing context
  menu; **undo** for knob moves (a per-param last-value stack over the registry).
- **Double-click a row name to bypass** (Phase 18, but ~10 lines in the existing
  `eventFilter` at `ui/MainWindow.cpp:761`).
- **Copy / paste effect settings** between presets. `resetCardToDefaults`
  (`ui/MainWindow.cpp:750-758`) already isolates a card's params via
  `isAncestorOf` — 90 % of "copy card as JSON".
- **Async model / IR loading.** `loadModel`/`loadIR` run synchronously on the GUI
  thread (`ui/MainWindow.cpp:911,937,1140`) including prewarm ×2; big WaveNets
  freeze the UI and stall the meters. Build on a worker thread, publish through the
  existing `RtSlot`.
- **Drag-and-drop files onto the window / cards** (Phase 11). `mStrip` already
  accepts drops for card reorder (`ui/MainWindow.cpp:575`); adding `hasUrls()` is
  ~20 lines. Same for **opening a preset / model from `argv`**, which Phase 8's
  MIME registration will need anyway.
- **Preset path portability.** Presets store absolute paths
  (`ui/PresetSerializer.cpp:285-286`); a Linux preset on Windows always warns.
  Try `presetDir/basename` and the last-used model dir before warning.
- **Tooltips with range / default / unit** — `ui/KnobWidget.cpp:176` only says
  "drag or scroll"; the knob knows all three at construction.
- **IR truncation is silent.** `dsp/IRLoader.h:17` caps at 4096 taps (~85 ms at
  48 kHz) with a hard cut and no fade window → spectral ripple on long IRs, and the
  tail of a 500 ms cab IR is simply gone. At minimum show "truncated to N ms" in
  the Cabinet label and apply a short fade-out window.
- **Startup model restore failure is silent** (`ui/MainWindow.cpp:1140`) and leaves
  `files/lastNAM` pointing at the bad file. Show the reason once in the row label.
- **Recent files submenu** — only single `files/last*` paths exist
  (`ui/MainWindow.cpp:917,944`). A `QSettings` list under a submenu is cheaper than
  waiting for Phase 11 recents.
- **Menu structure:** only Preset + Help. No Audio menu (reconnect hidden in the
  status bar), model/IR loading only from row buttons.
- **Meter timer** (`ui/MainWindow.cpp:141`, 20 fps) runs while minimized; and
  `VUMeter::setPeak` (`ui/VUMeter.cpp:50-51`) calls `QProgressBar::setValue`
  (synchronous repaint) then `update()` — two paints per meter per tick.
  Track the value directly instead of inheriting `QProgressBar` (Phase 14).
- **Keyboard focus on dials:** arrow keys move 1/10000 of the range (invisible),
  no focus ring is drawn.
- **Fixed pixel geometry vs font scaling:** `kNameColumnWidth = 120`,
  `kRowCardHeight = 62` (`ui/MainWindow.cpp:52,57`), dial 40/64 px, knob tile
  142 px, while fonts are in pt. With a larger system font "Makeup Gain" /
  "Sensitivity" clip. `setMinimumSize(1280, 720)` (`:116`) is below-taskbar on
  1366×768 (Phase 18 knows).
- **App icon:** no `setWindowIcon` anywhere (Phase 8).
- **Parentless file dialogs** (`ui/MainWindow.cpp:908,934,1231,1274`) — deliberate
  to keep them out of the window stylesheet, but on Wayland the portal dialog has
  no transient parent (can open behind / off-centre) and on Windows the native
  dialog isn't owned. Pass `this` and keep the theme scoped via `applyTo`.

---

## Tier 3 — DSP quality: what would make it the best

Gaps against a commercial plugin (Petrucci X baseline). Most extend Phase 17.

- **Log tapers on every rate / time knob** (Phase 9). `KnobWidget` is linear for
  everything; the bottom decade of attack, release (0.1–100 ms), LFO rate
  (0.1–5 Hz) and delay time is unusable. The single biggest feel improvement.
  Pull Phase 9 forward.
- **Partitioned FFT convolution** for full-length IRs (currently in Ideas).
  Today: brute-force direct convolution, 4096 taps × 48 kHz ≈ 200 M MAC/s per
  channel (`dsp/IRLoader.cpp:166-168`), 5–10× the cost of partitioned FFT, and
  hard-capped at ~85 ms. Also mono only (`dsp/IRLoader.cpp:109-110`), no
  low/high-cut, no dual-IR blend, no IR gain.
- **Per-block coefficient math** (Phase 14 free wins). Per-sample `std::sin`
  LFOs in Chorus/Flanger/Phaser/Tremolo (`ChorusProcessor.cpp:45,72,102-103`,
  `FlangerProcessor.cpp:39`, `PhaserProcessor.cpp:20`, `TremoloProcessor.cpp:14`);
  Phaser `pow` + `tan` per sample (`PhaserProcessor.cpp:21-23`); Compressor
  `log10` + `pow` per sample (`CompressorProcessor.cpp:27,43`); integer `%` in
  every delay line (`ChorusProcessor.cpp:52`, `FlangerProcessor.cpp:46`,
  `DelayProcessor.cpp:33`, `ReverbProcessor.h:54,74` — 24 filters × 2 ch,
  `TunerProcessor.cpp:67`) → compare-and-wrap.
- **NAM:** in/out resampling (1.1); input/output calibration (dBu) handling.
- **EQ:** fixed 250 Hz / 1 kHz / 4 kHz, fixed Q 0.707 (`dsp/EQProcessor.cpp:22-44`).
  Phase 17's 4-band semi-parametric + HPF/LPF; presence/depth.
- **Delay:** fractional interpolated reads, feedback tone filter (repeats are
  harsh), modulation, tempo/tap (Phase 15), dual L/R times. `kDelayTimeMax = 1000`
  (`dsp/Constants.h:115`) while the buffer holds 2000 ms; `kDelayTimeMin` comment
  (`:112`) is wrong — a 1–5 ms floor would allow slap/doubling.
- **Reverb:** plain Freeverb — no pre-delay, early reflections, wet HP/LP, or
  equal-power dry/wet.
- **Chorus:** single voice, no feedback, no wet HPF, sine only; ±2.5 ms max depth
  is shallow (`dsp/ChorusProcessor.h:38-39`).
- **Flanger:** no wet HPF; feedback path unfiltered / unclipped at 99 %.
- **Phaser:** fixed 4 stages, no stage-count or spread control.
- **Tremolo:** sine only (no triangle/square/harmonic), no stereo-pan amount.
- **Compressor:** hard knee, no lookahead, peak-only detector, no sidechain HPF.
  Gate: no hysteresis / hold / sidechain HPF.
- **Wah:** fixed 2 ms / 80 ms envelope, no up/down direction, no manual /
  expression / LFO mode (Phase 17).
- **Windows low-latency audio.** Shared-mode WASAPI gives ~10 ms periods, 20–40 ms
  round trip — not a guitar latency. `IAudioClient3` low-latency shared mode or
  exclusive mode (Phase 18); worth pulling earlier if Windows matters for v1.0.
- **Input channel selector** for multi-input interfaces (see 1.11).

---

## Verified OK (no action)

`RtSlot` ordering and reclaim logic (`dsp/RtSlot.h:47-70`); IR mirror-write delay
line indices (`dsp/IRLoader.cpp:151-175`); EQ cookbook coefficients including
peaking `a1 = b1` (`dsp/EQProcessor.cpp:122`); Freeverb length scaling by
`sr/44100` (`dsp/ReverbProcessor.cpp:14`); FTZ/DAZ set in both backends; no
allocations, locks or logging in any audio path (all `assign`/`resize` happen in
`onSampleRateChanged` / `prepareBuffer` off the RT thread); `NamProcessor`
oversize-block guard (`:158`); chain-reorder reset flags; no dangling card
pointers across `layoutStrip` (cards are reused, `ui/MainWindow.cpp:591-606`);
all timers parented; knob `setValue` blocks the dial signal so nothing
double-applies; every QSettings key written is read and vice versa.
