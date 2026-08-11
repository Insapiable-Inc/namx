# NAM Player — Public Release Plan

Goal: take the app from "works on my machine" to a public v1.0 anyone can install on
Linux or Windows. Phases are ordered for execution — work top to bottom, one phase at a
time. Detailed bug analysis lives in `gui/BUGS.md` (BUG-01…08); this file is the
tracking checklist.

Plan drafted 2026-08-09. Line numbers reference the tree at that date and may drift.

---

## Phase 0 — Repo hygiene

Cheap, do first so everything after lands cleanly.

- [x] **Consolidate branches.** Verified `linux-gui`, `windows-compatibility`,
      `gui-beautification` were all fully merged (zero unique commits); deleted the local
      branches. Decision (2026-08-09): stale branches **stay on the remotes**; GitHub
      (`silvercobraa/NeuralAmpModelerGUI`) is the **canonical public home**, Forgejo is a
      mirror. Default branch is `main` on both remotes (confirmed 2026-08-09).
- [x] ~~History cleanup before tagging.~~ **Decision (2026-08-09): keep history as-is.**
      Already public on two remotes; the release-facing surface is tags + release notes,
      not the commit log. No force-push.
- [x] ~~Track the shipped presets.~~ **Decision (2026-08-09): presets stay untracked** —
      `gui/presets/` holds personal presets, not release content. The `presets/`
      gitignore pattern stays.
- [x] **Re-encode `gui/build.ps1`** — now UTF-8 with BOM (PowerShell 5.1-safe), CRLF kept.
- [x] ~~Convert `gui/BUGS.md` findings to issues.~~ **Decision (2026-08-09): fix all
      findings in Phase 1, then delete the file.** It stays untracked as working notes
      meanwhile.
- [x] **Update `CLAUDE.md`.** Rewritten for the `src/ui`/`src/dsp` split, IAudioBackend +
      WASAPI, EffectBase dispatch model, persistence, and the ROADMAP workflow.

## Phase 1 — Bug fixes ✅ (completed 2026-08-09)

All findings from the (now deleted) `gui/BUGS.md` review, fixed and build-verified:

- [x] **BUG-01 (High) — preset knob values never reach the engine.**
      `KnobWidget::setValue` now emits `valueChanged(value())` after updating; the
      redundant manual emit in `resetToDefault()` is gone.
- [x] **BUG-02 (High) — buffer-size growth after model load → OOB write.**
      `NamProcessor` tracks the buffer size the live models were `Reset()` with
      (`mModelResetBuf`) and re-Resets all four instances under `mModelMutex` when it
      grows; `process()` bypasses oversized blocks as a second guard; `loadModel`
      re-checks under the lock in case the size grew during prewarm.
- [x] **BUG-03 (Medium) — effects configured after `jack_activate`.** SR propagation
      moved from post-`start()` into `AudioEngine::onBufferSizeChanged`, which both
      backends invoke before activating the stream; `EQProcessor::onSampleRateChanged`
      now takes the coefficient mutex.
- [x] **BUG-04 (Medium) — denormal protection.** JackBackend already had FTZ/DAZ (BUGS.md
      predated it); added the same to `WasapiBackend::audioThreadMain`.
- [x] **BUG-06 — gate/comp/wah per-channel state.** All three now use `[2]` state arrays
      with a shared `processChannel(buf, n, ch)` and override `processBlockDual`.
      Unblocks Phase 4 reordering.
- [x] **BUG-05 (Low) — `ToggleSwitch` double `toggled`.** Click routed through
      `QAbstractButton::click()`.
- [x] **BUG-07 (Low) — double-buffer swap constraint** documented at both load sites
      (`NamProcessor::loadModel`, `IRLoader::load`); unreachable from the UI in practice.
- [x] **BUG-08 (Minor) quality items:** VU peaks accumulate max with take-and-reset
      (`takeInputPeak`/`takeOutputPeak`); tuner NSDF moved to a worker thread inside
      `TunerProcessor` (GUI polls `lastPitch()`); gain smoothing coefficient recomputed
      per SR for a fixed ~20 ms time constant; IR load failures now surface the wav
      loader's reason incl. a stereo-IR hint (`IRLoader::lastError()`).
- [x] **RT allocation:** `runModel` scratch resize replaced with an assert.
- [x] **Server death UX:** status flips to disconnected (already detected in
      `updateMeters`) and a Reconnect button appears (top bar), wired to
      `restartAudioWithCurrentDevices()`. Also deduplicated the twice-defined status
      lambda into `MainWindow::setAudioStatus` (pulled forward from Phase 2) and made
      the startup label backend-neutral ("Audio: connecting…").

### Phase 1 — manual verification still pending

Code is fixed, Release build is clean, and a 5 s headless run passed — but these need
ears/eyes on real hardware before the phase is truly closed:

- [ ] **Preset round-trip (BUG-01).** Save a preset with non-default knobs → twist
      everything → Load. The *audio* must follow the dials, not just the labels.
      Also: double-click reset and right-click → Reset to Default still change the sound.
- [ ] **Toggles (BUG-05).** Every effect toggle flips exactly once per click; the
      keyboard shortcuts still toggle correctly.
- [ ] **Buffer growth (BUG-02).** With a model loaded and audio running:
      `pw-metadata -n settings 0 clock.force-quantum 1024` (then `0` to release).
      Expect at worst a brief glitch while the model re-Resets — no crash, no
      corruption, audio continues at the new quantum.
- [ ] **Tuner (BUG-08).** Enable tuner, play notes: note/cents update smoothly and the
      GUI never stutters. Toggle it on/off a few times. **Quit the app** — it must exit
      promptly (worker thread joins in the destructor; a hang here means a bug).
- [ ] **Server death + Reconnect.** Kill the audio server mid-session
      (`systemctl --user stop pipewire wireplumber` or `pkill -9 jackd`) → status shows
      disconnected + Reconnect button. Restart the server, click Reconnect → audio and
      saved port connections come back.
- [ ] **IR error message.** Load a stereo WAV as an IR → the error dialog states the
      reason and the convert-to-mono hint (not the old generic message).
- [ ] **VU peak-hold.** Meters should now catch short transients (palm mutes) that the
      old per-block snapshot missed. Visual sanity check.
- [ ] **Windows build.** Only the Linux compile was verified. `WasapiBackend.cpp` gained
      an `<immintrin.h>` include and FTZ/DAZ in `audioThreadMain` — needs an MSVC
      compile + run (CI `gui-windows.yml` on next push, or a local Windows build).
- [ ] **General regression sweep.** Model load/unload during playback; device switching;
      gain sweeps feel equally smooth at 44.1 k and 48 k (~20 ms time constant now at
      any rate).
- Not manually testable yet: **BUG-06** (per-channel gate/comp/wah state) has no
  reachable stereo-input path until Phase 4 reordering ships — it gets exercised there.
- Optional deep check: a ThreadSanitizer build for the BUG-03 startup race
  (`-DCMAKE_CXX_FLAGS=-fsanitize=thread` on a Debug config).

## Phase 2 — Parameter descriptor table (keystone refactor) ✅ (completed 2026-08-10)

The parameter list used to be hand-maintained in four places: ~50 one-line slot
forwarders + their declarations in `MainWindow.h`, the knob wiring in `setupUI`, the
preset save block, and the preset load block.

- [x] **One descriptor table for all parameters.** `MainWindow::registerParameters()`
      builds `mParams` (`ParamDesc { group, key, knob|toggle, defaults, inPreset }`),
      one registration line per parameter, grouped per effect. The table drives:
  - [x] signal wiring via lambda connects at registration — all ~50 forwarder slots
        and their header declarations deleted (net −300 lines),
  - [x] preset save and load as a single loop (drift now impossible). JSON schema
        unchanged: v1 files verified key-for-key against real presets, incl. legacy
        `chorus1`/`chorus2` groups being ignored exactly as before,
  - [x] session persistence hook ready (Phase 3 loops over the same table),
  - [x] future MIDI mapping hook ready (group+key = stable parameter name; every
        control is in the registry — `namEnabled` sat behind an `inPreset=false`
        flag until preset schema v2 started saving it and the flag was removed).
- [x] ~~Deduplicate the twice-defined `setJackStatus` lambda.~~ Already landed in
      Phase 1 as `MainWindow::setAudioStatus`.
- [x] **Hand-rolled cards migrated where feasible:** EQ, Amp, and IR now use
      `makeEffectColumn`; Input/Output share a `makeGainCard` lambda. Tuner stays
      custom (note/cents/bar layout doesn't fit the knob-column shape).

Verified: clean Release build, 5 s offscreen smoke run. Manual check recommended:
save → twist → load a preset and confirm knobs *and* audio follow.

## Phase 3 — Persistent state ✅ (completed 2026-08-10; effect order landed with Phase 4)

Everything restores on relaunch exactly as you left it. Nearly free once Phase 2 exists.

- [x] **Session state**, saved on exit + debounced on change (1 s single-shot timer
      restarted by every registry widget signal — crash-safe):
  - [x] all knob values and toggles via the Phase-2 table → QSettings `session/`
        (`session/<group>/<key>`); effect order joins when Phase 4 defines the IDs,
  - [x] model path and IR path (already at QSettings `files/lastNAM`/`lastIR` — kept),
  - [x] window geometry (`saveGeometry` in the destructor, restored before show),
  - [x] device & port-connection keys (already working — kept).
- [ ] **Preset schema v2** (started 2026-08-10):
  - [x] add a `version` field (presets now write `version: 2`); v1 files still load,
  - [x] fix the asymmetry: `namEnabled` saved alongside `irEnabled`,
  - [x] add `effectOrder` (list of stable effect IDs; unknown/missing IDs fall back to
        default position) — landed with Phase 4 (2026-08-10),
  - [x] embedded model/IR paths. **Decision (2026-08-10): always embed, no
        checkbox.** Load applies a non-empty path if the file exists and loads,
        otherwise warns and keeps the current model/IR; an empty path never unloads.
- [x] **First-run auto-connect:** when `connections/saved` is absent,
      `restoreConnections` wires `system:capture_1 → input` and
      `output_{L,R} → system:playback_{1,2}`. A new user hears sound without opening
      qpwgraph; failed connects on exotic setups are harmless no-ops.

## Phase 4 — Effect reordering ✅ (completed 2026-08-10, code; manual verification below)

Requires BUG-06 fixed (Phase 1).

- [x] **Data model:** stable string IDs (`tuner gate comp wah flanger phaser nam eq
      chorus tremolo delay reverb ir`) in `AudioEngine::mEffectTable`; the chain is a
      permutation over the fixed instances. **Never rename an ID** — they are the
      session/preset contract.
- [x] **RT-safe swap:** two chain buffers + atomic index (model-swap pattern). The
      audio thread reads the index once per callback; `setChainOrder` sanitizes the
      ID list, writes the inactive buffer, swaps, and uses a completed-callback
      counter to wait (bounded) before a retired buffer is ever rewritten. Moved
      effects get `requestReset()`, consumed by the audio thread at the top of their
      next block (all `reset()` impls are allocation-free fills).
- [x] **UI:** cards drag-and-drop within the strip (`MainWindow::eventFilter`).
      Every effect card carries a `≡` grip glyph in the title, and dragging starts
      **from the title band only** — clicks on the card body/knobs can never
      reorder. While dragging, the strip **re-lays out live to the prospective
      order** (the real cards move, dividers follow NAM/EQ, the source card is
      dimmed at its landing slot) — dropping commits exactly what is shown,
      Esc/leaving cancels back to the engine order. Digit/QWE shortcuts re-assign
      left-to-right on every commit (`EffectShortcuts::clear()`).
- [x] **Constraints — decision (2026-08-10): nothing is pinned.** All 13 effects,
      including Tuner and IR, reorder freely (no special cases). To make that safe,
      `TunerProcessor` gained a proper stereo path (`processBlockDual` feeds the
      detector the L/R average, mute silences both channels) and an audio-thread-safe
      `try_lock` reset. Only the Input/Output *gain* cards sit outside the chain,
      fixed at the ends.
- [x] **Persistence:** order saved as the ID list in `session/effectOrder` and preset
      v2 `effectOrder`; unknown IDs are dropped, missing effects re-enter at their
      default position.

### Phase 4 — manual verification pending

- [ ] Drag a card (e.g. Delay before the Amp): the strip rearranges live while
      dragging, dropping keeps exactly what was shown, Esc/dropping outside cancels,
      and the *sound* changes accordingly. No crash/glitch while dragging during
      playback (at most a brief tail cut from the moved effect resetting).
- [ ] Grips: every effect card (incl. Tuner and IR) shows `≡` in the title and drags
      only from the title band (clicking knobs/toggles/body never reorders);
      Input/Output titles are plain and don't drag.
- [ ] Tuner mid-chain: place the Tuner after a stereo effect (e.g. after Delay with
      stereo on) — pitch readout still tracks (fed the L/R average) and Mute
      silences both channels.
- [ ] Digit shortcuts now map to the new left-to-right order; Q/W/E stereo keys too.
- [ ] Order survives relaunch, and a preset saved with a custom order restores it.
      Loading a v1 preset leaves the order untouched.
- [ ] **BUG-06 exercise (finally reachable):** put Gate/Comp/Wah *after* a
      stereo-mode Delay or Chorus and play — both channels behave identically
      (per-channel envelopes, no mono collapse, no channel pumping).

## Phase 5 — Aesthetics

- [ ] **Theme consolidation.** Hex colors are painted directly in `KnobWidget.cpp`,
      `ToggleSwitch.cpp`, the VU gradient (`MainWindow.cpp:76`), and the tuner bar
      (`:329`). Move behind `Theme` accessors — palette in one file. Dark-only is fine
      for v1.
- [ ] **Resurrect or delete `GUIConstants.h`.** Currently 82 lines of dead code (zero
      references) with values drifted from reality. Either make it the single source for
      sizes/margins/scroll-steps and use it everywhere, or delete it.
- [ ] **Window sizing.** `setMinimumSize(1800, 1000)` excludes most laptops. Make the
      effect strip horizontally scrollable and drop the minimum to ~1280×720. Biggest
      first-minute issue for public users.
- [ ] **Backend-neutral status UI.** Startup label says "JACK: connecting…" — that's what
      a Windows/WASAPI user sees. Rename label text to "Audio: …" and the
      `jackStatusOk/Err` QSS selectors to match.
- [ ] **Polish:** app icon + `setWindowIcon`; knob tooltips; About dialog showing the
      version (set in `main.cpp` but never displayed); click-free model swap (short
      crossfade or mute); preset menu shortcuts — **Ctrl+S** Save Preset, **Ctrl+O**
      Load Preset (`QKeySequence` on the existing `Preset` menu actions; requested
      2026-08-10).

## Phase 6 — Source tree & code cleanup

- [ ] **Split `MainWindow.cpp` (1347 lines):** extract `PresetSerializer` (Phase-2
      schema), `AudioDevicePanel` (device dropdowns + connection save/restore), and a
      `VUMeter` widget. `setupUI` (570 lines) shrinks to a card-assembly loop.
- [ ] **Logging.** ~40 `std::cerr`/`cout` sites are invisible on Windows
      (`WIN32_EXECUTABLE TRUE`). Replace with `QLoggingCategory`-based
      `qInfo`/`qWarning` (OutputDebugString on Windows, `QT_LOGGING_RULES` control).
- [ ] **Distributable builds.** `-march=native` ties Release binaries to the build
      machine. Add a CMake option (`NAM_NATIVE_OPT`, default ON locally; CI/release
      builds use OFF → `-mavx2 -mfma`). Mandatory before publishing binaries.
- [ ] **New dirs:** `gui/packaging/` (.desktop, icon, AppStream metainfo, PKGBUILD,
      Flatpak manifest) and `gui/resources/` + `.qrc` for the embedded icon.
- [ ] **Magic numbers** in `MainWindow.cpp` (window size, timer intervals, meter dB math,
      tuner geometry, repeated margins/widths) → `GUIConstants.h` once resurrected.

## Phase 7 — Naming

Three names coexist: window title "NAM Player", `setApplicationName("NAM Linux")`
(bakes "Linux" into the *Windows* config path), binary/JACK client `NAMPlayer`, repo
`nam-linux-gui`.

Decision: **standardize on "NAM Player"** — already the user-facing title, accurate,
platform-neutral. (Shortlisted alternatives if a more distinctive name is ever wanted:
NAM Rig, NAM Stage.)

- [ ] Repo rename → `nam-player` on GitHub (canonical home; currently
      `silvercobraa/NeuralAmpModelerGUI`; GitHub auto-redirects the old URL). Optionally
      rename the Forgejo mirror (`nam-linux-gui`) to match. Update clone URLs in
      README/BUILD.md.
- [ ] `setApplicationName("NAM Player")` + one-time QSettings migration copying keys from
      the old "NAM Linux" store if the new one is empty (otherwise users lose saved
      connections on upgrade).
- [ ] Binary, CMake target, and JACK client name **stay `NAMPlayer`** — no breakage for
      existing `jack_connect` scripts.
- [ ] Rename CI files `gui-linux.yml` → keep per-OS jobs but platform-neutral artifact
      naming (`NAMPlayer-linux-x64`, `NAMPlayer-windows-x64` are fine).
- [ ] Add a courtesy line to README: not affiliated with the official Neural Amp Modeler
      plugin; built on sdatkinson's NeuralAmpModelerCore.

## Phase 8 — Packaging & ship

- [ ] Desktop file `gui/packaging/NAMPlayer.desktop` (Name, Exec, Icon,
      `MimeType=application/x-nam;application/x-nampreset;`).
- [ ] AppStream metainfo `gui/packaging/cl.pichulero.NAMPlayer.metainfo.xml`.
- [ ] `install(TARGETS …)` + `install(FILES …)` in `gui/CMakeLists.txt` so
      `cmake --install` deploys binary, desktop file, icon, metainfo.
- [ ] AUR PKGBUILD (`qt6-base jack pipewire-jack-client`).
- [ ] AppImage via linuxdeploy-qt.
- [ ] Flatpak manifest (`--socket=pipewire`).
- [ ] CI release job on tag push: Ubuntu 22.04 + Arch + Windows, non-native SIMD flags.
- [ ] Headless smoke test in CI: `QT_QPA_PLATFORM=offscreen NAMPlayer --self-test-and-exit`.
- [ ] About dialog / `--version` flag verified; tag **v1.0**.

## Deferred (post-1.0)

- Sample-rate renegotiation while running (effect SR is cached at start).
- Preset dropdown (list `*.nampreset` from a known dir instead of a file dialog).
- Optional output limiter.
- Light theme / system-theme detection.
- MIDI control (see parameter registry, Phase 2).
