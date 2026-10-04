# NAM-X — Public Release Plan

Goal: take the app from "works on my machine" to a public v1.0 anyone can install on
Linux or Windows. Phases are ordered for execution — work top to bottom, one phase at a
time. Detailed bug analysis lives in `gui/BUGS.md` (BUG-01…08); this file is the
tracking checklist.

Plan drafted 2026-08-09. Line numbers reference the tree at that date and may drift.

## North star (set 2026-08-22)

**Feature baseline: Neural DSP Archetype: Petrucci X — do at least everything it does**
(its feature inventory is mapped phase by phase below). But: **NAM-based** (models come
from the community, not from us), **flat, no skeuomorphism** (no pedals, no wood, no
screws), **pretty**, **native Qt standalone** on **Linux first, Windows supported**,
**FOSS**, **efficient** (measured, not assumed — see Phase 14), and **community-driven**
(browse TONE3000 tones in-app, share presets, swap themes).

Decisions:
- **No DAW plugin, ever** (2026-08-22). Plugin-host-embedded GUIs are the wrong trade;
  everything that makes this app worth having is standalone-shaped. `src/dsp` stays
  Qt-free and all parameters stay in the registry, but that's hygiene, not a plugin plan.
- **Library + TONE3000 ship in v1.0** (2026-08-22). A NAM player whose first-run
  experience is a file dialog is not done. **TONE3000 part reversed 2026-10-02:** after
  a live run the user wasn't convinced (its one real gain: downloads land in the
  library folder) and its API terms add upkeep — the integration was removed from
  `main` and kept on the `experiment/tone3000` branch (Phase 25). The library stays. ~~The `v1.0` tag moves from Phase 8 to the end
  of Phase 12.~~ Superseded 2026-09-12, see next.
- **Packaging goes last** (2026-09-12). Phase 8 runs *after* Phase 12, right before
  release, so the desktop file and CI jobs are built once against the final v1.0
  feature set (Library + TONE3000 + `Qt6::Network`) instead of being redone. The
  `v1.0` tag is back at the end of Phase 8.
- ~~**No AppImage, Flatpak or other bundle** (2026-09-17).~~ **Reversed 2026-10-02: ship
  both an AppImage and a Flatpak (Flathub)** next to the AUR PKGBUILD (and a Windows
  setup `.exe` next to the zip) — the AppImage as
  "try it on any distro", Flathub for store visibility and automatic updates. No Snap.
  Both wrap the same `cmake --install` layout; neither may bundle `libjack` (they use the
  system's or PipeWire's).
- **The app is GPL** (2026-10-01). MIT had only been inherited from upstream's root
  `LICENSE`, never chosen. `gui/` becomes GPLv3 with the user's copyright; the
  upstream NAM core keeps its MIT notice (MIT code may be included in GPL code). This
  also clears the way for the GPLv3 ASIO SDK on Windows. **GPL-3.0-or-later**; applied
  the same day: `gui/LICENSE`, PKGBUILD, AppStream, Windows zip, README.
- **Execution order** (revised 2026-10-02 — **shipping is last**: everything else, hardening,
  naming, docs and packaging included, comes before the release tag). **The first release
  is v0.6.0, not 1.0** (after the existing v0.5.x tags), and it leaves out **themes (19)
  and multiple effect instances (24)** — those come after it, on the way to 1.0.
  Done so far: 0–6, 9–18, 20, the 2026-10-02 hardening pass of 26 and the code of 23.
  **Next, set by the user 2026-10-02:** 21's WASAPI exclusive mode → 7 (naming: **NAM-X**)
  → 8 (packaging). The rest of the order below follows after.
  Remaining before the release, in order:
  27 (benchmarks — the baseline before any DSP change) → 22 (DSP quality, incl. the
  low-latency Transpose) → 25 (TONE3000: compliance or removal) → 21 (Windows audio —
  only if a Windows machine is available; otherwise after the release, Linux-first) →
  26's second full code review → 7 (naming: repo rename + app name) → 28 (cleanup &
  docs — after naming, so screenshots and docs carry the final name) → 8 (packaging —
  built in 2026-09; re-checked after the rename) → **tag v0.6.0**.
  After the release: 24 (multiple effect instances), 19 (themes & community), 21 if it
  waited, … → 1.0. Manual checks (12–17, 23) happen whenever the user is at the guitar.

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

- [x] **Preset round-trip (BUG-01).** Save a preset with non-default knobs → twist
      everything → Load. The *audio* must follow the dials, not just the labels.
      Also: double-click reset and right-click → Reset to Default still change the sound.
- [x] **Toggles (BUG-05).** Every effect toggle flips exactly once per click; the
      keyboard shortcuts still toggle correctly.
- [ ] **Buffer growth (BUG-02).** With a model loaded and audio running:
      `pw-metadata -n settings 0 clock.force-quantum 1024` (then `0` to release).
      Expect at worst a brief glitch while the model re-Resets — no crash, no
      corruption, audio continues at the new quantum.
- [x] **Tuner (BUG-08).** Enable tuner, play notes: note/cents update smoothly and the
      GUI never stutters. Toggle it on/off a few times. **Quit the app** — it must exit
      promptly (worker thread joins in the destructor; a hang here means a bug).
- [ ] **Server death + Reconnect.** Kill the audio server mid-session
      (`systemctl --user stop pipewire wireplumber` or `pkill -9 jackd`) → status shows
      disconnected + Reconnect button. Restart the server, click Reconnect → audio and
      saved port connections come back.
- [ ] **IR error message.** Load a stereo WAV as an IR → the error dialog states the
      reason and the convert-to-mono hint (not the old generic message).
- [x] **VU peak-hold.** Meters should now catch short transients (palm mutes) that the
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
- [x] **Preset schema v2** (started 2026-08-10; all parts landed):
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

- [x] Drag a card (e.g. Delay before the Amp): the strip rearranges live while
      dragging, dropping keeps exactly what was shown, Esc/dropping outside cancels,
      and the *sound* changes accordingly. No crash/glitch while dragging during
      playback (at most a brief tail cut from the moved effect resetting).
      (User, 2026-10-04: "works perfect"; MIDI-learned controls follow their effect
      when it moves.)
- [x] Grips: every effect row (incl. Tuner and Cabinet) shows `≡` and drags only
      from the grip (2026-08-23: the grip replaced the title band; clicking
      knobs/toggles/body never reorders); the Input/Output columns don't drag.
- [x] Tuner mid-chain: place the Tuner after a stereo effect (e.g. after Delay with
      stereo on) — pitch readout still tracks (fed the L/R average) and Mute
      silences both channels.
      (2026-10-04: readout confirmed — after Transpose it shows the shifted note. The
      Mute switch had been lost in the 2026-08-23 rack redesign; back on the Tuner row,
      on by default, session-only. Mute checked by the user the same day.)
- [x] Digit shortcuts now map to the new left-to-right order; Q/W/E stereo keys too.
      (Confirmed 2026-10-04: digits and Q/W/E toggle the effects' On switches by
      position; the Stereo switches have no shortcuts any more.)
- [x] Order survives relaunch, and a preset saved with a custom order restores it.
      Loading a v1 preset leaves the order untouched.
      (Confirmed 2026-10-04: relaunch, MIDI mappings, preset save/load. The v1-preset
      case wasn't tried.)
- [x] **BUG-06 exercise (finally reachable):** put Gate/Comp/Wah *after* a
      stereo-mode Delay or Chorus and play — both channels behave identically
      (per-channel envelopes, no mono collapse, no channel pumping).
      (User, 2026-10-04: all three fine after a stereo Delay and the like.)

## Phase 5 — Aesthetics ✅ (completed 2026-08-10, code; icon moved to Phase 8)

- [x] **Theme consolidation.** All widget hex colors moved behind `Theme` accessors
      (knob chrome, toggle tracks) or into the Theme stylesheet via objectNames
      (`vuMeter`, `tunerBar`, `stereoLabel`). Palette lives in `Theme.cpp` only.
      Dark-only for v1.
- [x] **Per-effect accent colors** (requested 2026-08-10). `Theme::Category` replaced
      by per-effect accents (`Theme::tagCard(card, fxId)`, QSS rules generated from
      one table). **Decision (2026-08-10): palette hex is rendered verbatim** —
      stripe, title, knob value-ring/tick/value-label, toggle "on" state — with no
      lightness clamping; the user tunes the palette values themselves (revised
      values landed 2026-08-23 with the rack redesign, which also removed the
      accent *stripes* — accents now live in row names, knob rings, toggles). Knob *bodies* stay neutral
      (colored-body variant tried and rejected). Bugfix found on the way: the app
      stylesheet disables parent→child palette propagation, so accents were never
      reaching the dials via `QPalette::Highlight` (knobs were always default green)
      — `tagCard` now pushes accents explicitly via `setAccent` on each
      knob/toggle, and must run **after** the card's children exist:
      | effect | color | | effect | color |
      |---|---|---|---|---|
      | tuner | `#f0e9d7` | | amp (NAM) | `#e4b954` |
      | noise gate | `#c2c5c8` | | eq | `#d5d3c5` |
      | compressor | `#0f377a` | | chorus | `#8dccf3` |
      | wah | `#d4c19a` | | tremolo | `#19848e` |
      | flanger | `#93477b` | | delay | `#eae9e5` |
      | phaser | `#4bba5b` | | reverb | `#3d3c3d` |
      | IR | `#ef661a` | | | |
- [x] ~~Resurrect or delete `GUIConstants.h`.~~ **Deleted** (82 lines, zero references,
      drifted values). Real UI constants arrive with the Phase 6 `MainWindow` split.
- [x] **Window sizing.** Minimum dropped to 1280×720 (default still 1800×1000, and the
      session restores the user's geometry); the effect strip scrolls horizontally
      *and* vertically as needed.
- [x] **Backend-neutral status UI.** `jackStatusOk/Err` QSS selectors and the label
      member renamed to `audioStatus*` (label text was already "Audio: …").
- [x] **Polish:** knob tooltips (name + usage hint); Help → About dialog showing the
      version; click-free model swap (~15 ms audio-thread fade-in after load/unload
      masks the discontinuity, incl. amp↔dry); **Ctrl+S** Save Preset / **Ctrl+O**
      Load Preset on the menu actions.
- [x] **App icon + `setWindowIcon`** — landed with Phase 8 (2026-09-17):
      `gui/resources/icons/NAMPlayer.svg` + `setWindowIcon` in `main.cpp`. ~~moved to Phase 8~~
      packaging (`gui/resources/` + `.qrc` land there anyway).

### Phase 5 — manual verification pending

- [x] Resize the window down to 1280×720: everything stays usable, the strip scrolls.
      (2026-10-04: at 1080p the bottom 3 effects needed scrolling → rows 62 → 48 px.
      User: fits 1080p at 100 %; 2K has room to spare up to 125 %, scrolls above —
      fine.)
- [x] New accent colors: stripes, titles, knob rings/ticks, and toggle "on" states
      all show the palette hex verbatim (revised palette values to come from the
      user for the near-white and very dark entries).
- [x] Ctrl+S / Ctrl+O open the save/load dialogs; About shows the build's version.
      (2026-10-04: Ctrl+S, Ctrl+Shift+S, Ctrl+O confirmed; About not looked at.)
- [x] Load a model while playing: no click, a soft ~15 ms fade-in instead. Same on
      unload and when switching between two models.

## Phase 6 — Source tree & code cleanup ✅ (completed 2026-08-10)

- [x] **Split `MainWindow.cpp`:** extracted `PresetSerializer` (schema v2 JSON ↔
      registry, in `PresetSerializer.h/cpp` + shared `ParamRegistry.h`),
      `AudioDevicePanel` (device dropdowns + QSettings persistence, emits
      `deviceSelectionChanged`), and `VUMeter` (dB mapping + styling). MainWindow
      keeps window assembly, the registry, strip/DnD, and session state
      (1369 lines, from ~1600). The card-creation blocks in `setupUI` stay — they
      are already registry/chain-driven; a data-driven card table wasn't worth the
      churn.
- [x] **Logging.** All 32 `std::cerr`/`cout` sites → `qCWarning`/`qCInfo` on the
      `nam.audio` category (`src/dsp/Log.h`; printf-style, MSVC-safe casts).
      Visible via OutputDebugString on Windows; runtime control with
      `QT_LOGGING_RULES`. DSP *headers* remain Qt-free.
- [x] **Distributable builds.** `NAM_NATIVE_OPT` CMake option: ON (default) →
      `-march=native`; OFF → portable `-mavx2 -mfma` (verified in generated flags).
      MSVC always `/arch:AVX2`. **CI/release builds must pass `-DNAM_NATIVE_OPT=OFF`.**
- [x] **New dirs** `gui/packaging/` and `gui/resources/` + `.qrc` — landed 2026-09-17 with
      Phases 11–12 (resources) and 8 (packaging). ~~created in
      Phase 8 together with their content (icon, desktop file, metainfo).
- [x] ~~Magic numbers → constants header.~~ **Decision:** the meter dB math moved
      into `VUMeter`; the remaining literals are single-use and local to their
      widgets. No shared constants header (GUIConstants was deleted in Phase 5).

## Phase 7 — Naming

Three names coexist: window title "NAM Player", `setApplicationName("NAM Linux")`
(bakes "Linux" into the *Windows* config path), binary/JACK client `NAMPlayer`, repo
`nam-linux-gui`.

Decision: **standardize on "NAM Player"** — already the user-facing title, accurate,
platform-neutral. (Shortlisted alternatives if a more distinctive name is ever wanted:
NAM Rig, NAM Stage.)

**Renamed again 2026-10-03: the app is NAM-X** (user's pick, 2026-10-01; the
similarity to Reinarc's "Nam XT" is an accepted coincidence). Display name **NAM-X**;
binary / QSettings app name **`namx`** (config `…/pichulero.cl/namx.conf`); app id
**`cl.pichulero.namx`**; C++ namespace **`namx`**; JACK and ALSA MIDI client **NAM-X**
(ports `NAM-X:input`, `NAM-X:output_L/R`; saved connections store only the other end, so
routing survives); Documents folder `NAM-X/` (an existing `NAM Player/` keeps being
used until `NAM-X/` exists); repo **`nam-x`**. The one-time migration now copies the
newest legacy store ("NAM Player", else "NAM Linux") into an empty `namx` one.

- [ ] **Repo rename → `nam-x` on GitHub** (canonical home; currently
      `silvercobraa/NeuralAmpModelerGUI`; GitHub auto-redirects the old URL). Optionally
      rename the Forgejo mirror (`nam-linux-gui`) to match. **User action** (no `gh` on
      this machine): GitHub → repo Settings → General → Repository name. Then
      `git remote set-url github https://github.com/silvercobraa/nam-x.git`. (Docs and
      packaging already point at `nam-x`.)
- [x] **Clean repo init** (user decision 2026-10-03, "some other day", before the
      release): the GitHub repo is a fork of `sdatkinson/NeuralAmpModelerCore` — PRs
      default to upstream, search ranks it low, it reads as a copy of the core — and it
      carries upstream's tags `v0.0.0`…`v0.5.4`, so our planned `v0.6.0` would look like
      the core's next version and manual release runs get named from `git describe`
      (`0.5.4-NNN-g…`). New non-fork `nam-x` repo with our history, without upstream's
      tags; then pick the first version (0.6.0 or a fresh series) and set it in
      `project(namx VERSION …)` (still 1.0.0).
      **Shape chosen 2026-10-03 (user): NeuralAmpModelerCore as a submodule** — the new
      repo holds our app (today's `gui/`) and pulls the core in like Eigen. Our one
      change to the core (3be052e, review fixes 02/03: checked weight iterator, LSTM
      dimension checks) then needs a home: offer it upstream (Phase 26 item) and point
      the submodule at our own core fork until it's merged. Also moves: the upstream
      `tools/` tests we touched, CI paths, `NAM_DEPS_PATH`, docs that say "fork".
      **Done 2026-10-04:** new repo `Insapiable-Inc/namx` (+ Forgejo `namx` mirror), our
      269 commits rewritten with `git filter-repo` (gui/ → root; no upstream commits or
      tags); app at the root, code dependencies as submodules under `external/`; the core
      is the fork `Insapiable-Inc/NeuralAmpModelerCore`, branch `namx` = upstream 3cde95c
      + our fix (3be052e there as 564d53d). The old fork repo stays as an archive. First
      version still to pick (CMake says 1.0.0).
- [x] Clone URLs in README/BUILD.md now point at the *current* GitHub name with
      `… nam-player` as the checkout dir (2026-08-22) — valid before and after the
      rename thanks to the redirect; flip them to the new name once it lands.
- [x] `setApplicationName("NAM Player")` (+ `setApplicationDisplayName`) and a one-time
      QSettings migration (`migrateLegacySettings()` in `main.cpp`): if the new store is
      empty and the legacy "NAM Linux" store has keys, copy them all; legacy store kept
      (2026-08-22).
- [x] Binary, CMake target, and JACK client name **stay `NAMPlayer`** — no breakage for
      existing `jack_connect` scripts. (BUILD.md's port table fixed on the way: it
      documented a single `output`; the real ports are `output_L`/`output_R`.)
- [x] CI: per-OS jobs kept; artifact names were already platform-neutral
      (`NAMPlayer-linux-x64`, `NAMPlayer-windows-x64`) — nothing to rename.
- [x] README: "not affiliated with the official Neural Amp Modeler plugin; built on
      NeuralAmpModelerCore" courtesy line; title → "NAM Player"; stale feature list
      (double chorus, no wah, fixed pre/post split) corrected (2026-08-22).

## Interlude — rack redesign (2026-08-23, unplanned)

A full UX overhaul landed between Phases 7 and 8 (branch `ux/horizontal-cards`),
driven by new requirements rather than this list:

- Effects are uniform full-width **rows** (≡ grip · name · On/Off toggle ·
  Stereo toggle · knobs, Mix first), all exactly 62 px tall; the whole rack
  fits a 1080p screen with slack. Input/Output are full-height columns
  flanking the strip; only the strip scrolls. Section dividers removed.
- Knobs have a compact row form (40 px dial, name over value beside it) and a
  stacked form for the gain knobs; the recessed knob tile and the per-effect
  accent stripes are gone (accents live in names, rings, toggles).
- **Status bar** (bottom right, VS Code style, `AudioStatusBar`): backend
  state · sample rate · buffer · latency · clickable device pickers. Replaced
  the device row (`AudioDevicePanel` deleted), the status pill, and the top
  button row — preset actions live in the Preset menu.
- Amplifier/Cabinet rows host Load/Unload + the loaded file name in the
  effect's accent color; the two big top file bars are gone (a head start on
  Phase 11's hero-card shape).
- **REAPER-style meters** (absolute zones on a -60..+6 dB scale, red only for
  the part above 0 dBFS, falling peak-hold line) and a **TU-3-style tuner
  display** (`TunerDisplay`: LED ladder, glowing seven-segment note, signed
  cents). **Tuner mute removed** (decision 2026-08-23; reversed 2026-10-04 — back on
  the Tuner row, on by default).
- Full labels everywhere (Noise Gate, Amplifier, Equalizer, Cabinet,
  Threshold, Sensitivity, Resonance, Makeup Gain, Room Size,
  Bass/Middle/Treble); no em dashes in UI strings; Chorus gained its missing
  Stereo toggle (DSP always had it).
- Theme scoped to the main window and file dialogs routed through the XDG
  portal, so dialogs are the desktop's own (Breeze on Plasma).

Phase-list fallout is annotated inline below: parts of Phase 14 are built,
most Phase 18 layout/visual fixes landed or became obsolete, and Phase 5's
pending palette revision shipped.

## Phase 8 — Packaging & ship → tag **v0.6.0** at the end (the very last step; the first release is 0.x, not 1.0 — 2026-10-02)

Landed 2026-09-17 (everything but the tag). App id `cl.pichulero.NAMPlayer`; the
desktop file is named after it (AppStream wants the launchable id to match) rather
than `NAMPlayer.desktop`. Icon: `gui/resources/icons/NAMPlayer.svg` (flat rack tile,
Amplifier-gold knob, meter-green trace), rasterized 16–512 + `.ico`, installed as the
app id in hicolor, `setWindowIcon` from the qrc, Windows exe icon + version block via
`packaging/NAMPlayer.rc`. Closes Phase 5's icon item and Phase 6's dirs item.

- [x] Desktop file `gui/packaging/cl.pichulero.NAMPlayer.desktop` (Name, Exec `%F`,
      Icon, `MimeType=application/x-nam;application/x-nampreset;`), plus the
      shared-mime-info definition `cl.pichulero.NAMPlayer.xml` so those types exist.
      `NAMPlayer file…` opens models/IRs/presets from the file manager.
- [x] AppStream metainfo `gui/packaging/cl.pichulero.NAMPlayer.metainfo.xml`
      (validates; the pedantic run only notes the uppercase id and the not-yet-public
      GitHub URLs).
- [x] `install(TARGETS …)` + `install(FILES …)` in `gui/CMakeLists.txt` so
      `cmake --install` deploys binary, desktop file, mime, icons, metainfo
      (layout verified with a DESTDIR install).
- [x] AUR PKGBUILD (`qt6-base jack`; `pipewire-jack` as optdepend — both provide
      `jack`). Source: the release job's submodule-vendored tarball; `sha256sums`
      to be filled at publish time. *Not run through makepkg yet (needs the tarball).*
- [ ] **AppImage** (back 2026-10-02, reversing the 2026-09-17 drop): built in
      `release.yml` with `linuxdeploy` + its Qt plugin on an old base (Ubuntu 22.04-ish,
      for glibc reach) with a newer GCC for C++23, bundling `libstdc++` but **not
      `libjack`**; update information (zsync) for AppImageUpdate / Gear Lever; attached
      to the GitHub release. The new static type-2 runtime (no `libfuse2` needed).
- [ ] **First-run self-integration** for the AppImage: offer to add NAM Player to the
      applications menu (writes the `.desktop` file, icon and MIME types — `.nam`,
      `.nampreset` — into `~/.local/share`), with a matching "remove" option; only when
      running as an AppImage (`$APPIMAGE` set) and nothing integrated it already.
- [ ] **Windows installer** (user request 2026-10-02): `NAMPlayer-<v>-setup.exe` built in
      the release workflow's Windows job (Inno Setup, available on the GitHub Windows
      runners) from the same windeployqt folder as the zip — Program Files install,
      Start-menu entry, uninstaller, `.nam` / `.nampreset` associations, per-user or
      machine-wide. The portable zip stays. Unsigned at first: Windows SmartScreen will
      warn ("unknown publisher") — code signing (e.g. SignPath's free plan for open
      source) is a later step.
- [ ] **Flatpak / Flathub** (back 2026-10-02): manifest `cl.pichulero.NAMPlayer.yml` on the
      KDE runtime (Qt 6); permissions `--socket=pipewire` (JACK through PipeWire),
      `--filesystem=home` (the library scans folders and presets store real paths — the
      document portal's paths would break them), `--device=all` (ALSA MIDI),
      `--share=ipc`, `--socket=wayland`/`x11`; CI builds a test `.flatpak` bundle; then
      submit to Flathub (its own repo under flathub/, bumped each release). Known limit:
      classic `jackd` (non-PipeWire) setups are awkward from the sandbox — point them to
      the AUR/source build. Config lives in `~/.var/app/cl.pichulero.NAMPlayer/` (document
      it in the manual).
- [x] CI release job on tag push: `.github/workflows/release.yml` — Ubuntu 22.04 and
      Arch container (build + tests + install layout), Windows (windeployqt zip),
      source tarball, GitHub release with the artifacts;
      `-DNAM_NATIVE_OPT=OFF` everywhere; TONE3000 key from the `TONE3000_CLIENT_ID`
      repository secret. `gui-linux.yml` / `gui-windows.yml` updated likewise.
- [x] Headless smoke test in CI: `QT_QPA_PLATFORM=offscreen NAMPlayer --self-test-and-exit`
      (plus `--version` and `Tone3000ClientTest`) in both the push and release jobs.
- [x] About dialog / `--version` flag verified: both read `PROJECT_VERSION` (1.0.0)
      through the `NAM_VERSION` define; `--help` lists the flags. On Windows the
      console flags print nothing (GUI subsystem) — About is the way there.
- [ ] **Tag v0.6.0** — the first release (0.x, not 1.0 — 2026-10-02). (Was moved to Phase 12 on 2026-08-22; back here since 2026-09-12
      because packaging itself now runs last.) **User action**, in this order: add
      the `TONE3000_CLIENT_ID` repository secret (Phase 12 prerequisite), make the
      GitHub repo public / rename it (Phase 7), then `git tag v1.0.0 && git push
      github v1.0.0` — the release workflow does the rest. Fill the PKGBUILD's
      `sha256sums` from the published tarball and push it to the AUR.

## Phase 9 — Knob range calibration ✅ (tapers in Phase 17; range audit done 2026-10-02)

Make every knob feel right across its whole travel, not just near the default.

- [x] **Audit all ranges/defaults in `Constants.h`** (done 2026-10-02: compared with
      the Petrucci X / Gojira X / Cory Wong X manuals and Fractal's Blocks Guide —
      delay Time max 1000 → 3000 ms (6 s judged too much), phaser Rate max 5 → 10 Hz;
      EQ ±12 dB, Transpose ±12 st, flanger feedback ±99 %, comp 20:1 confirmed;
      no published numbers for EQ band frequencies, wah, drive, volume Mid) against
      musical usefulness —
      e.g. does the comp ratio need 20:1, is the gate threshold floor right, do the
      LFO rate extremes get used. A first pass landed 2026-06-09 (`114c2b6`), before
      this phase was written; Phase 17 added many knobs since (EQ bands, delay
      R Time / Cross-feed, pre-delay, wah Position…).
- [x] **Per-knob response curves (taper)** — done in Phase 17 (`3a92121`): log dials
      for times, rates and frequencies, dial resolution 10× finer; dB knobs stay
      linear-in-dB.
- [x] **No schema impact** — presets and the session store real units; the taper only
      changes the dial mapping.

## Phase 10 — Tempo sync ✅ (done inside Phase 15, 2026-10-01)

Global tempo, so time-based parameters can be set in note values.

- [x] **Global tempo (BPM)** in the tempo bar, with Tap and MIDI clock. *Changed in
      Phase 15:* tempo is a **session** setting, not stored in presets (presets keep
      note values and follow the tempo you play at).
- [x] **Sync mode for time-based params:** delay time and the tremolo / chorus /
      flanger / phaser rates — `Sync` switch + note-value knob (4 bars … 1/32 incl.
      dotted / triplet).
- [x] **Engine stays dumb:** the GUI pushes tempo × note as ms / Hz
      (`MainWindow::push_synced`); no DSP or RT changes.
- [x] **Persistence:** `<fx>/sync` and `<fx>/syncNote` are registry params (session +
      preset); old presets load with Sync off. No schema bump was needed.

## Phase 11 — Library (v1.0) ✅ (completed 2026-09-17, code; manual verification below)

Models, IRs and presets become browsable lists, not file dialogs. Everything in
Phases 12, 13 and 16 stands on this.

- [x] **Library panel** (dockable, left of the rig; collapsible; remembers state):
      three sections — Models, IRs, Presets — one list widget, one behaviour.
      *(`QDockWidget` + `LibraryPanel`; `Ctrl+L` toggles; position/visibility in
      `window/state`.)*
- [x] **Root folders** configurable (several per type; sane defaults, e.g. `~/NAM`,
      `~/Music/IRs`, the presets dir); recursive scan on a worker thread with a cached
      index; `QFileSystemWatcher` keeps it live. Never block the GUI on a scan.
      *(`LibraryIndex`: Library → Folders… dialog, per-kind roots, cache keyed by
      path+size+mtime in `AppDataLocation/library-index.json`, 600 ms debounced
      rescans on directory changes; presets default to
      `~/Documents/NAM Player/Presets`, which Save Preset also defaults to.)*
- [x] **Search-as-you-type**, favorites (★), recents, sort by name/date; **prev/next**
      on keyboard (arrows / PgUp-PgDn when the panel has focus) — MIDI later (Phase 16).
      *(Search covers name, folder and metadata; `Ctrl+F` focuses it. Arrow browsing
      loads after a 180 ms rest so a held key doesn't queue model rebuilds. Typed
      keys in the search field don't fire the effect shortcuts; list type-ahead is
      off so digits in the list still toggle effects.)*
- [x] **Drag-and-drop** `.nam` / `.wav` / `.nampreset` files onto the window (and onto
      the Amp / IR cards) — `setAcceptDrops` + `dropEvent`. *(Anywhere on the window,
      strip and cards included; several files at once load one per kind.)*
- [x] **Model metadata** read from the `.nam` JSON (architecture, sample rate,
      training/loudness info when present) shown in the list and on the Amp card.
      *(Also IR rate/channels/length from the WAV header and the model/IR a preset
      references. Out-of-library files are described on load.)*
- [x] **Amp and IR become hero cards:** name, metadata, ‹ › arrows cycling the library
      (Petrucci: IR combo box + navigation arrows), Unload demoted to a menu item.
      *(The two big top bars are already gone — 2026-08-23 moved Load/Unload + file
      name onto the Amplifier/Cabinet rows.)* The two `Load…` buttons become
      "Browse" (focus the library). *(Unload + Import… live in the row's context menu.)*
- [x] Keep Import/Export file dialogs for one-offs; they stop being the primary path.
      *(Library → Import Model… / Import Impulse Response…; Preset → Save/Load.)*
- [x] On the way (AUDIT 1.6 / 1.8): a preset that references the already-loaded
      model/IR no longer rebuilds it; Save Preset appends `.nampreset` when the
      dialog didn't; a preset file that isn't a JSON object is rejected instead of
      resetting the gains.

Manual verification (2026-09-17 — needs a human at the keyboard):

- [ ] Fresh config: the dock opens on the left with `~/NAM` scanned; the footer says
      "Scanning…" briefly and the counts land without the UI stalling.
      (2026-10-04: to try on a fresh computer.)
- [x] Click a model: it loads, the row gets the accent bar, the Amplifier row shows
      its name + "WaveNet · 48 kHz · …". Hold ↓ in the list: one load when the key
      rests, not one per row. ‹ › on the row follow the visible (filtered/sorted) order
      and wrap around.
- [x] Type in the search field: digits/letters filter, effects don't toggle. Click
      the list and press `1`: the first effect toggles.
- [x] ★ on a row toggles the favorite without loading it; **★** mode shows only those.
      **Recent** lists the last loads, newest first, including drops and imports.
- [x] Drop a `.nam` from Dolphin onto the strip, onto a card and onto the dock: all
      three load it. Drop a `.nampreset`: it applies.
- [x] Save a preset into the default folder: it appears in **Presets** within a
      second without a rescan click. Delete a file on disk: it disappears.
- [x] Library → Folders…: add `gui/presets`, OK: presets appear; remove it: gone.
      (2026-10-04: works; files already loaded from a removed folder stay loaded —
      expected, they still exist; ‹ › then start from the first/last library entry.)
- [x] Close the dock, quit, relaunch: still closed; `Ctrl+L` brings it back at the
      same width.

## Phase 12 — TONE3000 integration — removed from main 2026-10-02 (Phase 25); the work lives on the `experiment/tone3000` branch

A second tab of the Library that looks and behaves exactly like the local one but is
the community. API verified 2026-08-22 (tone3000.com/api): OAuth 2.0 + PKCE for
user-facing apps, **free non-commercial tier** covers OAuth, search / trending / latest,
favorites, the user's downloads and per-model download URLs; 100 req/min; branding,
tone images, creator attribution and per-model licenses (T3K / CC) are requirements.

**Re-read 2026-09-17:** the free tier is narrower than noted above — it allows *only*
the OAuth prompt flows (`select_tone`, `load_tone`) and the bounded lists (favorited,
downloaded, created, trending, latest). `/tones/search` is commercial-tier. Decision:
**no in-app catalog search**; "Browse TONE3000…" runs the `select_tone` flow (their
catalog with search/filters, in the browser) and the picked tone comes back into the
app. Downloads use each model's `model_url` (the archive endpoint is partners-only).
No unauthenticated endpoint exists, so everything sits behind sign-in.

- [x] **Prerequisite (user action):** (done 2026-10-02: key verified against /oauth/authorize; kept out of the repo) register the app on TONE3000 to obtain a
      `client_id`. No secret ships in the binary (PKCE + loopback redirect).
      *(tone3000.com → Settings → API Keys; redirect URI
      `http://localhost:51731/callback`. Pass it as `-DNAM_TONE3000_CLIENT_ID=…` for
      release builds or paste it in the tab's setup page. Until then the tab shows the
      setup page.)*
- [x] **Login** (system browser → `http://localhost:<port>/callback`), token stored in
      QSettings (keychain judged not cheap: three platforms, no Qt API), logout.
      *(`Tone3000Client`: PKCE S256, `state` check, loopback `QTcpServer` on
      51731–51734 (v4+v6), 5-min timeout, pre-emptive refresh 60 s before expiry,
      401 → refresh → one retry, dead refresh token → signed out.)*
- [x] **Browse:** trending, latest, ~~search, filter by make/tag~~ (→ select flow, see
      above); tone cards with image, creator, license; favorite/unfavorite; "open on
      TONE3000". *(Lists: Trending, Latest, Favorites, Downloads, Mine; paged where the
      API pages. Cards: thumb, title, gear · format, avatar + @creator, ★, ✓ when a
      model is already in the library.)*
- [x] **Download** a tone's model(s) into the library (sidecar JSON: tone id, creator,
      license, URL) → immediately loadable; already-downloaded marker; offline = the
      local tab still works, the TONE3000 tab says so. *(`~/Documents/NAM Player/TONE3000/
      <creator>/<tone>/<model>.nam` + `.t3k.json`; the folder is an implicit library
      root; "Load" on a downloaded row; Download all; Bearer never forwarded to the
      storage redirect.)*
- [x] **Preset ↔ tone linkage:** presets record the TONE3000 tone/model id next to the
      path; loading a preset whose model is missing offers to fetch it. This is what
      makes *sharing presets* work (Phase 19). *(`tone3000.model/ir` in the preset;
      resolution order: same model id elsewhere in the library → download offer.)*
- [x] Rate-limit handling, listing cache, their branding/attribution requirements met.
      *(429 + Retry-After ≤ 30 s → one automatic retry with a notice; 5-min in-memory
      listing cache, invalidated on favorite/download; image disk cache. Branding:
      official logo pack in `gui/resources/tone3000`, full logo on splash + list header,
      T3K mark on the Browse button and as the hero-row origin marker, recommended
      partnership copy, signed-in avatar + username, creator on every card.)*
- [x] Networking via `Qt6::Network` (part of `qt6-base`; CMake `find_package` gains
      `Network`). Phase 8 packaging (which follows) must bundle it.
- [x] **Automated test** (added): `-DNAM_BUILD_TESTS=ON` → `Tone3000ClientTest` runs
      the client against `tests/mock_tone3000.py` (sign-in, refresh, 429, lists,
      favorites, models, download + sidecar, images, sign-out). 40 checks, passing.
- [ ] ~~Tag v1.0.~~ → end of Phase 8 (2026-09-12).

Manual verification (2026-09-17 — needs the real key and a human):

- [x] (confirmed 2026-10-02) With the key: Continue → browser sign-in → pick a tone → back in the app on that
      tone's page. Does TONE3000 accept the unregistered `localhost` redirect URI
      (their docs say localhost is allowed in development)? If not, register it.
- [ ] (2026-10-02: Favorites lists the user's ★ tones) Trending / Latest show real cards with images; Favorites reflects ★ toggles made
      in the app and on the site; Downloads lists what was fetched.
- [ ] (2026-10-02: an IR pack downloaded — 4 files + sidecars under TONE3000/<creator>/<tone>/,
      all decode; the rest below still to check) Download a model: file + sidecar land in the TONE3000 folder, the local tab shows
      it with "TONE3000 · @creator", Load puts it in the rig, the Amplifier row shows
      the T3K mark and clicking it opens the tone.
- [ ] Save a preset with that model, delete the file, load the preset → download offer
      → model back and loaded.
- [ ] Pull the network: lists say offline, the local library works; reconnect and ↻.
- [ ] (2026-10-02: sign out / in works; the > 1 h refresh path not checked) Sign out, sign in again; wait > 1 h and use a list (refresh path).

## Phase 13 — Presets as the live unit ✅ (completed 2026-10-01, code; manual verification below)

Petrucci parity: preset combo box, ‹ ›, Save, Save As…, Delete, Import, Reset, Locate
file, `*` on unsaved changes, folders/subfolders, and a defined set of things presets
*don't* recall (audio device, tuner, metronome, MIDI map).

- [x] **Preset bar above the rack** (`PresetBar`): ‹ [name ▾] › with `*` when the live
      state differs from the file (compared against a baseline snapshot, so turning a
      knob back clears it); Save / Save As; ⋯ = the Preset menu (Open, Import, Export,
      Show in Folder, Delete → trash, Reset). Window title = `Name* — NAM Player`.
      Current preset + baseline persist in the session.
- [x] **Quick slots 1–8** (`Ctrl+1…8`; QSettings `presets/slots`; MIDI PC later) and
      **A/B compare** (two in-memory states per preset; A→B copies; a preset load clears them).
- [x] Presets organised in folders: the name button's menu mirrors the folder tree.
- [x] **Excluded from presets** (documented in BUILD.md → Presets): audio
      devices/connections, tuner (`ParamDesc::inPreset = false`), library/TONE3000
      state, window state, slot assignments; metronome and MIDI map join the list
      when they exist.
- [x] Eight **starter presets** (`resources/presets/`, no model baked in), copied once
      into `<presets>/Starter` on first run (`presets/starterSeeded`).

### Phase 13 — manual verification pending

- [x] Twist a knob → `*` appears; twist it back → gone. Ctrl+S clears it; relaunch:
      same name, same `*` state.
- [x] ‹ › and the name menu walk the presets; Starter shows as a submenu.
      (2026-10-04: found while checking — Transpose, Drive and Volume kept the previous
      preset's state, because presets older than them have no group for them and an
      absent group was left untouched. Fixed: absent groups now load as defaults.)
- [ ] Empty slot click stores the preset; Ctrl+N recalls it; right-click → Clear.
      (2026-10-04: recall by click and Ctrl+N works. Every slot holding the loaded preset
      used to light up — all 8 did after testing filled them with one preset; now only
      the last slot used lights.)
- [x] A/B: tweak on B, switch to A and back — both states survive; A→B copies.
- [ ] Import a file from Downloads → copied into the presets folder and loaded;
      Export doesn't change the current name; Delete moves to the trash → Untitled.
      (2026-10-04: Import and Export confirmed; Delete not yet.)
- [x] ~~Tuner state survives loading any preset.~~ Reversed 2026-10-04 (user): the
      Tuner's On and Mute are saved in presets like every other control. Check: they
      change with the preset.
      (Checked by the user 2026-10-04.)

## Phase 14 — Cockpit: status bar, meters, tuner, efficiency ✅ (completed 2026-10-01, code; manual verification below)

The "at the speed of a cockpit" part. Also where *efficient* stops being a slogan.

- [x] **Status bar:** backend state · rate · buffer · **round-trip latency** (JACK
      capture + playback latency around our ports; one buffer when unknown) ·
      **DSP load %** (engine times every block; max over 0.5 s, all-time max in the
      tooltip, amber ≥ 70 %, red ≥ 90 %) · **xrun counter** (JACK xrun callback,
      WASAPI capture discontinuities; click resets) · device pickers; left side:
      model / IR / TONE3000 sign-in dots.
- [x] **Meters at display rate:** ~60 fps timer, paused while minimized/hidden;
      time-based ballistics (instant attack, 24 dB/s release, 1 s hold → 12 dB/s);
      repaint only on movement. Zone coloring + red peak-hold line kept (decision
      2026-08-23).
- [x] **Gain-reduction meters** on Compressor and Gate (in their unused stereo slot).
- [x] **Tuner:** Hz readout toggle, reference pitch 420–460 Hz (QSettings `tuner/*`,
      not in presets — tuner is `inPreset = false`).
- [x] **Efficiency budget written down and checked** (BUILD.md → High CPU usage):
      `gui/bench/NAMPlayerBench` (`-DNAM_BUILD_BENCH=ON`) times each stage in chain
      order; `--self-test-and-exit` prints cold start + peak RSS. i9-12900K, 128 @ 48 k,
      standard WaveNet + IR, everything on: 4.1 % of the block; cold start 135 ms;
      peak RSS 170 MB. *Still to do:* a run on a 2018-class laptop core.
- [x] Free perf wins: `enable_fast_tanh()` in the engine (amp 166 → 67 µs/block),
      Phaser coefficients at control rate (every 16 samples; 2.8 → 1.4 µs),
      Compressor log2/exp2 instead of log10/pow, `% sz` ring wraps → compare-and-reset
      (Delay 0.66 → 0.26 µs).

### Phase 14 — manual verification pending

- [x] DSP % moves with the chain (enable a stereo effect before the amp → roughly
      doubles); kill/restart the server → xruns count; click resets.
- [x] Meters: smooth at 60 fps, hold line waits 1 s then falls; minimize → the GUI
      thread goes quiet (`top -H`).
- [x] GR meters move with the compressor/gate working; bypass → 0.
- [x] Tuner: A4 = 432 makes a 440 Hz A read about +32¢; Hz toggle shows the
      frequency; both survive relaunch and preset loads.
- [ ] Fast tanh: A/B a high-gain model against an older build — no audible change.

## Phase 15 — Practice tools: metronome + global tempo (absorbs Phase 10) ✅ (completed 2026-10-01, code; manual verification below)

Main use case is practising at home along a song; the metronome is the standalone
feature people actually use. Petrucci parity for the metronome in full.

Decisions (2026-10-01): **tempo is a session setting, not part of presets** (reverses
Phase 10's "persisted in preset") — presets store note values and follow the tempo you
play at; **BPM counts quarter notes** (DAW convention, shared with sync), so x/8 meters
click eighths at 2 × BPM.

- [x] **Global tempo** in the tempo bar under the preset bar (`TempoBar`): BPM field
      (drag / Shift-fine / wheel / click to type, 40–240) and **Tap** (last four
      intervals). Sync for delay time and tremolo / chorus / flanger / phaser rate:
      `Sync` switch + note-value knob (`TempoSync::kNotes`, 4 bars … 1/32 incl. dotted /
      triplet) as registry params (`<fx>/sync`, `<fx>/syncNote`); the GUI pushes
      tempo × note as ms / Hz (`MainWindow::pushSynced`), engine untouched.
- [x] **Metronome** (`src/dsp/Metronome`): play/stop, level, pan, 40–240 BPM, any meter
      1–16 over 2/4/8/16 (presets + Custom…), per-beat accents (strong / normal / soft /
      mute, click the beat lights to cycle; compound defaults stress groups of three),
      three sound sets (Click, Wood, Beep — Cowbell removed 2026-10-04, user; synthesized at sample-rate change,
      no RT allocation), subdivisions (eighths, triplets, sixteenths), beat lights;
      mixed after the output gain, outside the chain and the meters; keeps running
      across preset loads. Settings in QSettings `metronome/*`.
- [x] **Count-in** (0–2 bars, optional "silent after count-in" = lights only) and
      **Space** for play/stop.
- [ ] *(Ideas, not committed)* in-app local file player with A/B loop, slow-down,
      pitch; "song mode" cue list driven by an external player's position over
      MPRIS (Firefox ≥ 131 / Chromium expose it for YouTube) or SMTC on Windows.
      Parked — see Ideas.

### Phase 15 — manual verification pending

- [x] Click tempo matches a reference metronome over a minute (no drift); BPM drag,
      wheel, typing and Tap all land; Space starts/stops (and still types a space in
      the library search field).
- [x] 7/8 with custom accents, 6/8 default accents, triplet subdivision, all four
      sounds; level/pan audible; count-in lights amber, silent-after keeps lights only.
- [x] Load presets while the metronome runs: it doesn't stop or stutter.
- [x] Delay Sync at 1/8. and 120 BPM ≈ 375 ms; changing the tempo moves the echoes;
      a preset saved with sync reloads synced; tremolo sync at 1/8 pulses in time.

## Phase 16 — MIDI ✅ (completed 2026-10-01, code; manual verification below)

Petrucci parity: CC / PC / NOTE, MIDI Learn from a right-click, a Mappings window,
absolute vs relative CC, mapping presets saved/loaded, device enable checkboxes.
The parameter registry (Phase 2) was built for this: every `ParamDesc` is mappable.

- [x] **Backends** (`src/midi/`): Linux — ALSA sequencer client "NAM Player" with one
      input port, every readable port auto-subscribed unless disabled, hot-plug via the
      system announce port; Windows — WinMM, device list polled every 2 s (compiled but
      not yet built on Windows). Device enable checkboxes in MIDI → Devices and the
      Mappings window (QSettings `midi/disabledDevices`). New Linux dependency: alsa-lib.
- [x] **MIDI Learn** on every registry knob/toggle (knob context menu hook, card menu
      for switches) and on actions: slots 1–8, next/previous preset, A/B, metronome
      start/stop, tap. The target glows while armed; Esc cancels; "Forget MIDI" unbinds.
- [x] **Mappings window** (`MidiMappingsDialog`): on, source CC/PC/NOTE, channel (any or
      1–16), number, target, mode (absolute / relative / toggle / momentary), min/max
      range, per-row Learn, delete; **Save/Load `.nammidi`**; **Defaults** = program
      0–7 → slots 1–8 (also the first-run mapping).
- [x] **Delivery:** backend thread → queued Qt signal → `MidiMapper::handle` on the GUI
      thread → widgets (registry wiring updates engine, session, preset `*`).
      `tests/MidiMapperTest` covers learn, modes, edges, channels, files, clock.
- [x] **Expression pedal** targets: any knob, with min/max range (wah position and the
      volume pedal arrive with Phase 17).
- [x] MIDI clock → global tempo (MIDI → Follow MIDI Clock; 4-beat average).

### Phase 16 — manual verification pending

- [x] With the Scarlett's DIN MIDI or a USB controller: Learn a knob from a CC, a
      switch from a footswitch (toggle per press), a slot from a program change.
      (2026-10-04: knob from an expression pedal, switch from a footswitch, and forget —
      all work; program change not tried.)
- [x] Unplug/replug a USB controller: it reconnects by itself; disabling it in
      MIDI → Devices stops its messages.
      (2026-10-04: hot-plug of the MIDI pedalboard works; the disable switch not tried.
      Several devices: all enabled ones are listened to at once — untested, one device.)
- [ ] Relative encoder mode; min/max range on an expression pedal.
- [ ] Save a `.nammidi`, Defaults, Load it back.
- [ ] Follow MIDI Clock from a DAW or drum machine; tempo settles within a few beats.
- [ ] Windows build: WinMM backend compiles and receives.

## Phase 17 — DSP parity with Petrucci X ✅ (completed 2026-10-01, code; manual verification below)

NAM models replace the four amps; "Piezo" (acoustic sim) is out of scope — users load
acoustic models. UI: cards gained ▾ detail rows for advanced knobs (`addDetailRow`).

- [x] **Drive** (new ID before the amp): TS-style — 720 Hz high-passed path through
      gain + soft clip, added to the clean signal; Tone low-pass; Level.
- [x] **Doubler** (after the EQ): right = 3–20 ms copy with random drift; stereo input
      passes untouched.
- [x] **Cab: dual IR slots** — level / pan / phase per slot, A↔B blend, Low/High Cut;
      slot B picker in the ▾ row; presets carry `irPathB`.
- [x] **EQ → 4-band semi-parametric** (frequency per band, ±12 dB) + Low/High Cut;
      3-band presets unchanged (same keys and default frequencies).
- [x] **Delay:** sync (Phase 15), R Time ratio + Cross-feed (ping-pong ↔ dual), Low/High
      Cut + Saturation in the loop, Modulation, **Crystals**; tap = the global Tap.
- [x] **Reverb:** Pre-delay, Decay (= Room Size), Low/High Cut, **Shimmer**.
- [x] **Chorus modes** (Normal / Tri / Vintage / Spatial), **Phaser Vibe**, **Flanger Range**.
- [x] **Wah: Pedal mode + Position**; **Volume pedal** (new ID, end of chain) with Mid.
- [x] **Output limiter** (Limit switch on the Output card; meter reads after it).
- [x] **Stereo-amp toggle** (Amplifier row; off = sum a stereo input → one model).
- [x] **Transpose ±12 st** (new ID after the tuner) + Shimmer + Crystals — own
      two-tap + WSOLA shifter (`PitchShifter.h`), no GPL dependency; ~25 ms latency
      when active. Rubber Band remains an option if quality ever demands it (would make
      the binary GPL).
- [x] Phase 9 **knob tapers**: log dials for times, rates, frequencies.

### Phase 17 — manual verification pending

- [x] Drive in front of a clean/crunch model: tighter, mid-forward; Tone/Level sane.
      (Checked on the WDF circuit Drive, 2026-10-04.)
- [x] Stereo switch: flanger-stereo before the amp sounds mono with it off (default!
      — this changed the sound of sessions that had a stereo effect before the amp),
      wide with it on.
- [x] EQ: sweep each frequency; Low/High Cut "Off" at the ends; an old 3-band preset
      sounds identical.
- [x] Delay: Cross-feed 100 → 0, R Time 75 %, Saturation, Modulation, Shimmer.
- [x] Reverb Shimmer stays stable at 100 % with long Decay.
- [ ] ~~Cabinet: two IRs, Blend, Ø B (thin when out of phase), pans → stereo.~~ Not
      reachable: the dual-cab controls are hidden since 2026-10-01 (only Level is back).
- [x] Transpose −1/−2 (drop tuning feel) and ±12; Tuner before it reads the real string.
- [x] Limiter catches peaks with Output +20 dB; no pumping at normal levels.
- [x] Wah Pedal mode with a MIDI expression pedal on Position.

## Phase 18 — Settings, layout discipline, platform ✅ (completed 2026-10-01)

- [x] **Settings dialog:** audio (device, requested buffer size / sample rate; on
      Windows later WASAPI-exclusive), MIDI devices, library folders, theme, meter
      options, a **shortcuts sheet** (discoverability of the digit/QWE toggles).
      Done 2026-10-01: Audio (devices; rate/buffer read-only — server-owned), MIDI,
      Library (replaces the Folders dialog), Tuner, Shortcuts (built live from the
      window); instant apply. Theme waits for Phase 19; "meter options" dropped. The
      Shortcuts page showed the gap (13 keys for 16 effects); fixed the same day: effect
      on/off keys are now `1…0` then `Q…P` by row position (20 keys), and the Stereo
      switches have no keys any more (user's call: live switching is for MIDI).
- [x] **Doubler removed** (2026-10-01): sounded like a flanger to the user. Old presets,
      sessions and the stored order drop the `doubler` ID through the sanitizer.
      Transpose tried on distorted guitar: "not bad, not professional grade" — see the
      Transpose latency note in the ideas list.
- [x] **Layout:** ~~bypassed cards dim~~ (done 2026-08-23); ~~2-column knob grid for
      one-height columns~~ and ~~device row moved to Settings~~ (both superseded
      2026-08-23 — uniform 62 px rows and the status bar device pickers); remaining:
      ~~compact mode for 1366×768~~ and ~~window sizes S/M/L~~ — dropped 2026-10-01: the
      full strip fits a maximized 1080p window (checked by the user after the Sync
      column landed), and free resize/maximize cover the rest. ~~Right-click /
      double-click a row name to bypass it~~ — dropped 2026-10-01: too easy to hit by
      accident.
- [x] **Visual fixes** — all resolved by the 2026-08-23 redesign: Compressor/Reverb
      accents revised (`#4f78bd`/`#8f7d99`), tuner readout rebuilt, stereo toggles
      labelled "Stereo", "Dynamic Wah" named in full.
- [~] Windows: **WASAPI exclusive mode** → moved to Phase 21 (2026-10-01): no Windows
      machine to test on; all untestable Windows audio work waits together.

## Phase 19 — Themes & community (after the v0.6.0 release)

- [ ] **Theme files** (palette + per-effect accents as JSON in the config dir),
      built-in Dark / Light / High-contrast stage; user themes shareable; `Theme` is
      already the single owner of colour, so this is mostly plumbing.
- [ ] **Preset sharing:** export bundles that carry TONE3000 tone references (Phase 12)
      so a shared `.nampreset` can fetch its model; a community presets/themes repo
      linked from the app ("Get more…"). Investigate whether TONE3000 will host
      presets for third-party apps.
- [ ] Courtesy/credits screen: NAM core, TONE3000, contributors.

## Phase 20 — Code to the C++ guidelines ✅ (completed 2026-10-01; runs before Phase 18)

Bring `gui/` in line with `gui/CPP_GUIDELINES.md` (agreed 2026-10-01).
No behavior changes: every step builds, passes the tests and the bench, and is its own
commit.

- [x] **C++23:** `CMAKE_CXX_STANDARD 23`; CI (`gui-linux.yml`, `release.yml`) from
      `ubuntu-22.04` to `ubuntu-24.04` with GCC 14; `BUILD.md` minimums (GCC 14 /
      Clang 18 / MSVC 17.10, Ubuntu 24.04+).
- [x] **Format:** one `clang-format -i` commit over `gui/src`, `gui/tests`, `gui/bench`
      with `gui/.clang-format` (4 spaces, 100 cols, braces on own line for types and
      namespaces only, always braces, no one-line functions, no column alignment).
      Include regrouping sorts the Win32 headers in `WasapiBackend.cpp` /
      `WinMidiInput.cpp`: put order-sensitive ones in `// clang-format off` first
      (`<windows.h>` before the rest) and get a Windows build checked by CI.
      Add a CI step that fails on unformatted files.
- [x] **Warnings:** `-Wall -Wextra -Wconversion -Wsign-conversion -Wsuggest-override` (`/W4` on MSVC);
      clear them (named casts, `enum class`, explicit signed/unsigned conversions),
      then turn on `-Werror` (`/WX`). Done 2026-10-01 for GCC/Clang (via `DelayLine`,
      `size_t` frame counts and types fixed at the source — not casts); MSVC `/WX`
      waits for a Windows build.
- [x] **Namespace:** wrap `gui/` in `namespace npl` (placeholder — renamed along with
      the app once it has a final name) with the shared `using std::…` header.
- [x] **Naming:** `snake_case` methods/locals/members, `SCREAMING_SNAKE_CASE`
      constants, no `m`/`k`/`s` prefixes; unit suffixes `_ms`/`_db`/`_hz`. Accessors:
      public members on plain containers, `get_`/`set_` on classes with an invariant.
      Mechanical, one directory per commit (`dsp/`, `midi/`, `net/`, `ui/`).
      Done 2026-10-01 in passes (clang-tidy case conversion → getter collisions →
      no name reuse → prefix strip), all by symbol (libclang/clang-tidy), not text;
      `-Wshadow` clean. Windows-only files renamed by the same map, unverified until a
      Windows CI build. Accessors: `get_` where a getter collided with its member;
      other getters (`status_string()`, `value()`…) keep their names for now.
- [x] **`auto`:** spell out built-in types and function results; keep it for class
      casts, iterators, lambdas, range-for.
- [x] **Lambdas:** one-statement bodies; the ~110 multi-line ones (32 over 10 lines,
      mostly `Tone3000Client` reply handlers, `Tone3000Panel`, `MainWindow` row
      setup) become named functions the lambda forwards to.
- [x] **Ownership:** `MidiInput::create(parent)` returns a Qt-owned plain pointer
      (no `unique_ptr` around a QObject).
- [~] **Effect interface + generic parameters → deferred** to the optional milestone
      below (decided 2026-10-01). Effects keep `EffectBase` and their atomics for now.

### Optional milestone — parameter model (not scheduled)

Goal: one definition per parameter driving presets, session, MIDI and the knob
widgets; prerequisite for **multiple instances of an effect** (today `AudioEngine`
holds exactly one of each, addressed by fixed IDs). `Constants.h` stays the single
place where the numbers are edited. Options weighed on 2026-10-01:
- **Small:** each effect gets a knob table (key, label, unit, range from
  `Constants.h`, setter); the registry loops over the tables. ≈ −150 lines, no
  multiple instances.
- **OO:** pure `Effect` interface + shared routing; `Param` objects as effect members
  (`threshold.get()`), listed for generic code (hand list, or self-registering via a
  `ParamList`). Spec syntax not settled — designated initializers, builder and
  spelled-out constructors were all rejected or unconvincing.
- **Data-oriented:** spec tables in `Constants.h`, one flat `ParamStore` with a slice
  per instance, per-type value/state structs and `process_*` free functions, chain of
  `{type, instance}`. Best fit for multiple instances; biggest refactor; snapshot
  filled by position (struct order = spec order, guarded by a test).
- Whichever is picked: golden test bit-identical at every step, plus a test that the
  8 starter presets load to the same values.

## Phase 26 — Hardening: the 2026-10-01 code review (added 2026-10-02; P1 pass + six P2s done 2026-10-02)

From the review in `ISSUES.md` (repo root, untracked — 34 findings with locations and
suggested fixes; some reproduced with AddressSanitizer). Re-check each finding against
the current code before fixing (the review predates later commits). One commit per
fix; regression tests use fixed, hand-made inputs (never random), in temp dirs.

**Scope decided 2026-10-02:** fix the platform-independent P1s now; backend issues wait
for the backend work (Phase 21), and a second full review follows the big changes.

**Now — P1, platform-independent:**
- [x] **01** (done 2026-10-02 with 10 and 11: the GUI reads IRs with its own strict `WavReader`, no longer compiling AudioDSPTools' `wav.cpp`; `WavReaderTest`, clean under ASan/UBSan) Malformed WAV data lengths overrun the heap (`AudioDSPTools/dsp/wav.cpp`) —
      reachable by loading any IR, including downloads. Validate chunk sizes first.
- [x] **02** (done 2026-10-02: `nam::weights_iterator` in `NAM/weights.h` throws instead of reading past the end; LSTM/ConvNet/WaveNet check the exact count) Truncated model weights are read past the end (`NAM/lstm.cpp`, ConvNet,
      WaveNet) — validate the weight count before construction.
- [x] **03** (done 2026-10-02: LSTM dimensions and weight count validated before allocating) LSTM `input_size` vs channel count disagree → out-of-bounds write
      (`NAM/lstm.cpp`).
- [x] **04** (done 2026-10-02 with 18: lock-free EQ — atomics + version, the audio thread owns all filter state; TSan: 16 races before, 0 after) EQ `reset()` on the audio thread races the GUI's `rebuild()` (before
      Phase 24 creates effects while audio runs).
- [x] **06** (done 2026-10-02: `QSaveFile` for presets, `.nammidi`, the library cache; test in `MidiMapperTest`) Preset and MIDI saves truncate the old file and report success even on a
      failed write → `QSaveFile` + check, dirty state only after success (before
      Phase 24 rewrites saving).
- [x] **08** (P2, same reason; done 2026-10-02: `PresetSerializer::validate` — version,
      types, "is it a preset at all" — before anything changes; all 29 presets on disk pass)
      Preset version/schema validated before anything is applied.
- [x] **15** (done 2026-10-02: models run at their training rate behind the official plugin's Lanczos resampler when the engine rate differs; 44.1/96 kHz match 48 kHz native; 0 allocations on the audio thread) A model's training sample rate is ignored (a 48 kHz model at 44.1/96 kHz
      sounds wrong, silently) → resample around the model, or warn.
- [ ] 01–03 live in upstream code (`NAM/`, `Dependencies/AudioDSPTools`): offer the
      fixes upstream too.

**Moot:** 07, 12, 13 (TONE3000 download issues) — the integration was removed (Phase 25).
**Deferred:**
05 (JACK buffer-size race), 16, 17 → Phase 21 with the backend work.

- [ ] **Second full code review** after Phases 21 and 24 (new backends, effect
      instances), before any major release.

**P2 — fold into the phase they touch, or fix here when small:**
- [ ] Small and standalone: 09 (A/B can't restore an unloaded model/IR), ~~10–11~~ (done with 01), ~~12, 13~~ (moot: TONE3000 removed), 14 (closing waits for
      the library scan), 16–17 (JACK: client not closed on shutdown; sample-rate
      changes not propagated), ~~18~~ (done with 04),
      19 (mono→stereo exposes stale right-channel state), 21 (model change freezes
      the GUI — halved 2026-10-02: one parse, no double prewarm, 27 → 13.5 ms per switch;
      a background loader only if big models still stutter), ~~22~~ (done 2026-10-02),
      ~~23~~ (done 2026-10-02), ~~24~~ (IR truncation: faded and at 1 s since 2026-10-03; still only logged),
      26 (limiter scans the whole look-ahead per sample), ~~27~~ (done 2026-10-02), ~~28~~ (done 2026-10-02).
- [ ] Belong elsewhere: ~~20~~ (bypass cuts tails → "Trails": **not wanted**, user 2026-10-03),
      ~~25~~ (done 2026-10-03 with Phase 22's FFT convolution), 29–31 (WASAPI
      device loss, clock drift, capture averaging → Phase 21), 12–13 also touch
      Phase 25 (TONE3000).
- [ ] P3: 32 (MainWindow too big), 33 (naming leftovers), 34 (loader/transition test
      coverage — grows with the fixes above).

## Phase 27 — Benchmarking: measured against the best (added 2026-10-02; done 2026-10-02 except the loopback latency)

"The world's best" has to be measured. What exists: `NAMPlayerBench` (per-stage time
against the block budget), `--self-test-and-exit` (cold start, peak RSS), the DSP %
and xrun counters, `DspGoldenTest` (unchanged output within 0.1 %, not quality — bit-exact until 2026-10-03). This phase adds the
comparisons and the quality numbers, and keeps them from regressing. Measurements are
deterministic (fixed signals and models, no random input); results are dated, with the
machine, in `gui/BENCHMARKS.md`.

- [~] **NAM CPU vs the official plugin:** (2026-10-02: vs the upstream core's benchmodel instead — the official plugin isn't on Linux; same core, so the core comparison is the meaningful one) the same models (standard / lite / feather /
      nano WaveNet, an A2 model, an LSTM) at 32 / 64 / 128 / 256 frames and 44.1 / 48 /
      96 kHz, ours vs NeuralAmpModelerPlugin (and the upstream core's `benchmodel`
      for the inference alone). Same machine, `performance` governor.
- [x] **NAM accuracy vs the official plugin:** (done 2026-10-02 against upstream's reference renderer; fast tanh = the official plugin's accuracy) render the same input through both and
      null them; ours must match within float tolerance at the model's own rate, and
      report how each handles other rates (ties to Phase 26 item 15).
- [ ] **Latency:** measured round trip with a loopback cable (`jack_iodelay`) against
      what the status bar reports, ours vs the official plugin in a host at the same
      buffer; plus each effect's added latency (Transpose, limiter look-ahead) — ties
      to review item 28.
- [x] **Effect quality numbers** (done 2026-10-02: `NAMPlayerQuality`, BENCHMARKS.md → Effect quality) (the yardstick Phase 22 needs before replacing
      anything): Drive aliasing (sine sweep, energy outside the harmonics), Transpose
      pitch error / latency / crossfade artefacts, reverb tail decay vs Decay, delay
      modulation smoothness, EQ response vs the Audio EQ Cookbook curves, limiter
      ceiling overshoot.
- [x] **Whole app** (done 2026-10-02, BENCHMARKS.md → Whole app; 22.6 % idle CPU with a rig loaded under `powersave` — the governor, not the code)**:** cold start, peak RSS, model-load time (no GUI freeze — review
      item 21), idle CPU with the window open (the LED meters repaint at up to
      60 fps), CPU with every effect on.
- [x] **No regressions** (done 2026-10-02: `gui/bench/instructions.py` + gui-linux.yml step; per-compiler baselines — CI's GCC 14 entry still to record)**:** a CI job runs the bench and compares against a stored
      baseline — instruction counts (e.g. `valgrind --tool=callgrind` on a short render)
      rather than wall time, since CI machines are noisy; the budget in BUILD.md
      becomes a checked number.
- [ ] Optional: CPU and features vs Archetype: Petrucci X (the feature baseline), in a
      DAW at the same buffer — if a license is at hand.

## Phase 22 — DSP quality: state-of-the-art effects (added 2026-10-01; next, after the Phase 27 benchmarks)

**Order for the rest (user, 2026-10-03):** Shimmer + Crystals (Signalsmith) → built-in
Drive (WDF Tube Screamer) → analog modulation (BBD, Uni-Vibe) → Wah last (it already
sounds good). No Trails.

Research in `gui/DSP_RESEARCH.md` (options per effect, real-time flags, licenses,
sources). Each step keeps the golden test green for effects it doesn't touch, re-records
it for the one it replaces, and is measured with `NAMPlayerBench`.

- [x] **Discrete selector widget** (user request 2026-10-01; done 2026-10-03 as a *look*,
      not a new widget — "simple and effective"): every snapped KnobWidget with ≤ 32
      positions draws a dot per position on its ring, the current one lit, instead of the
      sweep arc (Reverb Type, Sync note values, Transpose Discrete). Presets, MIDI,
      drag/scroll unchanged; click-to-advance skipped (it would fight double-click
      reset). Original plan: a stepped, rotary-switch
      style control with N labelled positions (detents, the current label shown,
      click/scroll/drag steps one position, MIDI maps to positions). For the reverb
      type (Plate / Hall / Room / Spring / IR), Chorus modes (to bring back Tri /
      Vintage / Spatial), Phaser/Flanger modes. Replaces the "continuous knob with
      snap" workaround; registered like a knob (integer value, preset key unchanged).
      **Must look clearly different from a continuous knob** (user, 2026-10-03): today's
      stepped controls — Reverb Type, every Sync note value, Transpose's Discrete
      semitones — are plain knobs that only snap, so nothing tells you they click
      between positions (e.g. tick marks per position, the positions' labels around it).
- [ ] **Accurate fast tanh** (from the 2026-10-02 benchmarks): the core's fast tanh
      keeps the amp 46–65 dB off the exact reference (same as the official plugin);
      exact tanh is −118…−139 dB but costs 1.7–2.5× amp CPU. A vectorized tanh accurate
      to ~1e-7 (Eigen-style rational / SIMD) could give both — measure accuracy and CPU
      with BENCHMARKS.md's null test and matrix. Decision 2026-10-02: **keep fast tanh
      for now**; no runtime switch (it's a core-wide global: layers capture the
      activation at model build, and the LSTM reads a plain static bool per sample).
- [x] **Pitch shifter** (2026-10-03): Transpose runs on a filter-bank shifter
      (`FilterBankShifter.h`, gammatone bank + per-band rotation + Hohmann resynthesis),
      picked by ear over WSOLA and a dozen variants; ~20 ms in the bass, ~4 ms above.
      Signalsmith Stretch measured: wrong bass pitch at playable latencies — kept for
      Shimmer/Crystals (below). Details in `DSP_RESEARCH.md` §1.
- [x] **Partitioned FFT convolution** for the cabinet (2026-10-03): 64 direct taps +
      uniformly partitioned FFT convolution (`signalsmith-linear` submodule, MIT); zero
      added latency, IRs up to 1 s (faded beyond, instead of cut at 4,096 taps), 3× less
      CPU on a typical cab IR. IR reverbs will add non-uniform partitions on top.
- [x] **Reverb:** Freeverb → Dattorro plate + FDN hall/room (done 2026-10-03, chosen by
      ear: "all 3 of them"). Type knob (Plate / Hall / Room — a snapped, labelled knob
      until the selector widget above exists), Decay shown in seconds (0.2–10 s, log,
      within ~10 % from 0.5 s up at the default Damping; Plate bottoms out at ~0.6 s,
      Hall ~0.3 s; Damping's full strength kept as approved — easing it made the types
      sound alike), Pre-delay
      sync (the switch in the main row's Sync column, like the other cards), Room on 16 lines (8 rang metallic behind
      a long pre-delay). Was: Decay spread 0–75 % = 0.19–0.83 s, 100 % = 9 s.
      Decided 2026-10-03 (user): types **Plate, Hall, Room** (no Spring for now); Decay in
      seconds, the same scale for every type; **old presets are mapped** onto the new
      controls (option A — no "Classic" Freeverb kept); new algorithms uncommitted until
      heard, as with Transpose. **No IR reverb inside Reverb** — if ever, a separate effect
      with amp/cab-style file loading.
- [x] **Revisit the Stereo switches** (user, 2026-10-03 — decided the same day): Delay,
      Flanger, Phaser, Tremolo and Chorus keep their switch as is (each can make stereo
      from mono, and mono before the amp is a real choice); Reverb stays always stereo;
      Cabinet needs none (one IR can't widen mono; stereo input is convolved per side);
      Amplifier always stereo (switch removed). Gate and Compressor stay **unlinked** on
      purpose: each side is gated/compressed on its own — e.g. two guitars on L and R
      stay independent all the way through two amp instances.
- [x] **Drive:** a WDF Tube Screamer with anti-aliasing for the built-in Drive. (The NAM
      "Pedal" slot moved to Phase 24, 2026-10-03: it's a second Amplifier instance.)
      Done 2026-10-04 (user: "sounds amazing"): `TubeScreamerClipper` (chowdsp_wdf
      submodule, BSD-3), 4× oversampled; the old approximation ("Classic") removed. New
      defaults: Drive 25 %, Tone 55 %, Level +6 dB — a boost into the amp (~+4 dB over
      bypass). Accent: TS green.
- [x] **Phaser:** MXR Phase 90 circuit model (done 2026-10-04, user: preferred over the
      old one by ear) — `Phase90Circuit.h`, from the polimi-ispl paper's MIT MATLAB
      code; details in `DSP_RESEARCH.md` §5. The digital Classic and the Vibe switch are
      gone (preset key `phaser/mode` is ignored now). New defaults Rate 0.4 Hz,
      Feedback 0 % (the script pedal; 50 % sounded like a wah); Feedback stops at 90 %
      (the model runs away near 98 %).
- [ ] **Modulation:** BBD core for Chorus/Flanger (+ analog Delay mode); virtual-analog
      Uni-Vibe (lamp + photocells + 4 staggered stages) — its own card or mode, to decide.
- [ ] **Wah:** Crybaby nodal-DK model; **Delay:** tape mode (ChowTape model);
      **Compressor:** smooth-branching detector + soft knee.
- [x] Re-enable Shimmer and Crystals on a high-quality shifter (done 2026-10-03, "sound
      amazing"): Signalsmith Stretch 1.4.0 submodule (`StretchShifter`, default 120 ms
      block); Crystals fed 150 ms early so repeats stay on the beat; both knobs back in
      the ▾ rows, default 0 % (a knob, not a switch — user). WSOLA `PitchShifter` deleted.

## Phase 23 — Expression pedals: range, reverse, whammy (added 2026-10-02; code done 2026-10-02, pedal check pending)

The pedal-like effects — Dynamic Wah (Position, Manual mode), Transpose (Semitones) and
the Volume pedal — get the controls a real expression setup has. MIDI multi-assign
(one pedal → several targets, Min above Max reverses a sweep; tested in
`MidiMapperTest`) already works and must keep working.

- [x] **Heel/toe range in real units** (done 2026-10-02: Min/Max cells are spin boxes in the knob's unit and range; "—" for switches/actions; `.nammidi` unchanged). MIDI → Mappings… already has Min/Max per row,
      but as a percentage of the knob's span — "Transpose 0 to +12 st" is Min 50 %,
      Max 100 % today. Show and edit Min/Max in the target's own unit (st, %, dB),
      so a wah can stop short of the toe, Transpose sweeps 0…+12 or 0…−12 st, and the
      Volume pedal stays above a floor. Stored as today (0…1 share): no file change.
- [x] **Reverse switch** (done 2026-10-02: `MidiTarget::reverse` swaps the mapping's lo/hi, so it composes with Min/Max; preset keys `<fx>/reverse`) in the Stereo column of Wah, Transpose and Volume (none of
      them uses that column): flips which end of the pedal is heel and which is toe
      for every controller driving that parameter. Decide first how it relates to
      per-mapping Min > Max (probably: Reverse flips the effect's pedal axis, and the
      mapping range is applied on top). Registered with `toggle_param`, saved in
      presets; the knob always shows the real value.
- [x] **Transpose: Discrete / Continuous** switch in the Sync column (done 2026-10-02:
      preset key `transpose/continuous`, registered before Semitones so a fractional
      value survives loading; continuous mode keeps the shifter running at 0 st so a
      sweep through 0 doesn't jump between dry and delayed; Sync column → 112 px; the
      per-block ratio change has no audible zipper on paper — listen with a pedal). Discrete =
      today's whole-semitone snap; Continuous = smooth glides for whammy-style
      bends (`PitchShifter::set_semitones` already takes fractions). Smooth the
      ratio per block so a fast pedal doesn't zipper; listen for the WSOLA
      crossfade wobble on sweeps, and re-check with the Phase 22 shifter.
- [ ] Manual check with a real pedal: wah stopped short of the toe, 0…+12 st whammy,
      reversed volume, a crossfade volume ↔ wah on one pedal.
      (2026-10-04: Reverse with an expression pedal works; the rest not tried yet.)

## Phase 24 — Multiple effect instances (added 2026-10-02; big; after the v0.6.0 release)

Any effect can appear more than once (two delays, a drive before and after the amp,
two amps blended later…), and effects can be added and removed instead of all 16
always sitting in the strip. Today everything assumes one instance per effect: the
stable ID table (`AudioEngine::effect_table`), the chain sanitizer (unknown/duplicate
IDs dropped, missing ones re-inserted), one named card + `sw_*`/`knob_*` member per
effect in `MainWindow`, registry groups = effect IDs (`delay/mix`), the session keys
(`session/delay/mix`), MIDI targets (`param:delay/mix`) and the preset's top-level
`namPath`/`irPath`.

**First user: a NAM "Pedal" before the amp** (moved here from Phase 22, user 2026-10-03).
A pedal capture is the same `.nam` file and engine as an amp — no special slot, just a
second Amplifier instance. What it needs beyond instancing:
- [ ] Per-instance **loudness normalization**, defaulting from the file's `gear_type`
      metadata: on for amps (−18 dB), **off for pedals** — a boost or Tube Screamer works
      by its level into the amp, which normalizing to −18 dB would flatten.
- [ ] Per-instance model file in presets (today's single `namPath`), and the library /
      ‹ › / drag-and-drop loading into the selected instance.
- [ ] The library can show and sort by gear type (amp / pedal / pedal + amp).

**Decide with the user first (not designed yet):**
- [ ] How adding/removing looks: a "+ Add effect" row at the end of the strip, a `+`
      between cards, right-click a card → Duplicate / Remove, a palette of effect
      types… Remove needs an undo or a confirmation.
- [ ] Which effects can repeat. Singletons by nature: Tuner (and the Input/Output
      stages, limiter and metronome, which aren't chain effects). The Amplifier and
      Cabinet can repeat in principle (CPU: each NAM instance is one model run), which
      overlaps Phase 22's "second NAM slot before the amp" — one design for both.
- [ ] Naming: "Delay", "Delay 2"…, and maybe a user label per card.
- [ ] A cap on instances per effect / total (CPU budget, `NAMPlayerBench`).

**Data formats (must land before any UI):**
- [ ] **Instance IDs:** the first instance keeps today's ID (`delay`), more get a
      numbered one (`delay-2`, `delay-3`; the separator must be safe in QSettings
      keys, JSON keys and MIDI target IDs). An instance's ID never changes while it
      exists (removing `delay-2` doesn't renumber `delay-3`), so MIDI mappings and
      presets keep pointing at the right card. The effect type is part of the ID.
- [ ] **Preset schema v3:** the same layout as v2 with instance IDs as group keys and
      in `effectOrder` — a v2 file *is* a valid single-instance v3 file. Difference
      in meaning: in v3 `effectOrder` is authoritative (an effect missing from it is
      removed); v2 files keep today's "missing effects re-enter at their default
      slot". Per-instance file paths (`namPath`/`irPath`) move into the amp/cab
      groups when those can repeat; the top-level keys stay readable. Starter
      presets and `PresetSerializer` tests cover v1, v2 and v3 loading.
- [ ] **Session** (`session/effectOrder` + `session/<id>/<key>`): same rules as the
      preset; an old session restores unchanged.
- [ ] **MIDI** (`midi/mappings`, `.nammidi`): targets use instance IDs
      (`param:delay-2/mix`); a mapping whose instance is gone stays in the table,
      marked missing, instead of being deleted silently (it comes back if the
      instance does). `MidiMapperTest` covers it; multi-assign keeps working.

**Engine and UI:**
- [ ] `AudioEngine` creates and destroys effect instances on the GUI thread and
      publishes them through the chain's `RtSlot` (no allocation on the audio
      thread; removed instances retire like old chains). Golden test: per instance,
      and a two-delay chain.
- [ ] `MainWindow`: cards built per instance from one factory per effect type
      (replacing the named `sw_*`/`knob_*` members), registered in the parameter
      registry with the instance ID as the group — still the single source of truth.
- [ ] Effect shortcuts, drag-reorder, the bypass dimming and the card accents work
      per instance; `EffectShortcuts` already keys on position.
- [ ] Manual check: two delays in series, a removed effect staying removed after a
      preset reload, an old v2 preset and an old session loading as before.

## Phase 25 — TONE3000: compliance or removal ✅ (removed 2026-10-02; kept on the `experiment/tone3000` branch)

TONE3000's API page (https://www.tone3000.com/api#design-requirements) sets design
requirements for apps using it. Checked 2026-10-02: the OAuth select flow, the
bounded list endpoints only (no `/tones/search`), the splash copy (their recommended
wording, verbatim), the list cards (image, title, gear type, format, creator +
avatar), the signed-in user, the persistent "Browse TONE3000…" path, the tone detail
(image, creator, description, per-model load/download) and the logo-before-mark
order already comply. **Decide first: bring the gaps into compliance, or remove the
integration** (2026-10-02, after the live run: the user is "not very excited about this
integration" — its one real gain is that downloads land in the library folder; leaning
removal, not decided) (it reverses the north-star decision "Library + TONE3000 ship in v1.0").
**Decided 2026-10-02: removal (Path B)**, done the same day; the full integration is kept
on the `experiment/tone3000` branch.

**Path A — compliance (the four gaps):**
- [ ] **Entry point in the signal block:** "the TONE3000 entry point is associated
      with adding a tone to a signal block" — add a TONE3000 entry on the Amplifier and
      Cabinet rows (next to ‹ ›) that opens the TONE3000 tab (the splash when signed
      out). Today it is only the dock tab and Library → Browse TONE3000….
- [ ] **Loaded tone image in the signal block:** show the tone's image beside the T3K
      mark on the Amplifier/Cabinet rows; cache it next to the download (sidecar) so it
      shows offline. Pack title, gear type, format and creator where space allows.
- [ ] **"Discover more" in partial lists:** a last card in Trending / Latest / … that
      invites to the full catalog (opens the select flow).
- [ ] **Tab names:** "Mine" → **Created**, "Downloads" → **Downloaded** (required tabs:
      Favorites, Created, Downloaded).
- [ ] Not code (user): register the app (the Phase 12 key prerequisite) and, for
      production, ask support@tone3000.com about the 100 requests/min default limit.
      The free tier is non-commercial — fine while the app is free; selling it needs
      their commercial agreement and sign-off before release.

**Path B — removal:**
- [x] Remove `Tone3000Client`, `Tone3000Panel`, the dock's TONE3000 tab, the
      Library → Browse TONE3000… action, the hero-row T3K origin mark, the TONE3000
      folder scan (`with_tone3000_dir`) and sidecar fields, `resources/tone3000/`,
      `Tone3000ClientTest` + `mock_tone3000.py`, the `NAM_TONE3000_CLIENT_ID` option,
      and the BUILD.md / CLAUDE.md / ROADMAP sections about it.
- [x] Keep old data harmless: presets with a `tone3000` object still load (the object
      is ignored; the embedded `namPath`/`irPath` keep working); already downloaded
      models stay in the library as plain files (the user's download folder can be
      added as a normal library root); stale `tone3000/*` QSettings keys (including
      tokens) are deleted once.
- [x] Update the north star (decision reversed, with the date and the reason).

## Phase 28 — Cleanup & documentation (added 2026-10-02; before the release, after naming)

The repo and its docs have to describe the app as it is. After Phase 7 so everything
carries the final name.

- [ ] **README** rewritten for the current app: what it is, the features as they are now
      (it still says "3-band EQ"; no MIDI, metronome, library, LED meters…), platform
      status, install, screenshots, license, credits.
- [ ] **Mission / vision**, stated up front: "the world's best" free, open-source, native
      NAM player — Linux first, no DAW, no subscriptions, batteries included (tuner,
      metronome, MIDI). Draft points are in `MARKETING.md`.
- [ ] **Fresh screenshots** (offscreen grab workflow): main window, library, a preset,
      MIDI mappings, settings; stored in the repo (e.g. `docs/screenshots/`), referenced
      from the README and the AppStream metadata.
- [ ] **Build instructions** checked and updated: `gui/BUILD.md` per distro (Arch,
      Ubuntu 24.04+, Fedora), Windows (MSVC 2022 + Qt + vcpkg), CMake options, tests;
      the AudioDSPTools submodule is now only needed for the resampler header.
- [ ] **User manual**: split the "how to use" half of `gui/BUILD.md` into its own
      manual (getting started, routing audio, the rack, presets & slots, library, MIDI
      incl. multi-assign / Reverse / ranges, tuner & metronome, settings, shortcuts,
      troubleshooting); linked from Help.
- [ ] **Changelog** since v0.5.4.
- [ ] Repo tidy: `AUDIT.md` / `ISSUES.md` (archive or keep private), stale notes,
      `CMakeLists.txt` project version → 0.6.0, CLAUDE.md / ROADMAP current.

## Phase 21 — Windows audio: WASAPI exclusive + ASIO (added 2026-10-01; before the release only if a Windows machine is available)

- [x] **WASAPI exclusive mode** (from Phase 18; code 2026-10-03): shared mode's 10–20 ms
      is not a guitar latency; keep shared as fallback. Settings → Audio → Exclusive mode
      (`audio/exclusive`); native format search (float32 / int32 / 24-in-32 / int24 /
      int16) at a common rate, the devices' minimum period with the
      BUFFER_SIZE_NOT_ALIGNED retry, full-buffer writes per render event, fallback to
      shared with a status-bar notice. **Compile-checked only** (MinGW + CI's MSVC) —
      needs a run on real Windows hardware: latency, a USB interface, a busy device,
      int formats.

- [ ] **Verify the license first:** Steinberg's ASIO SDK is believed to be dual-licensed
      (proprietary / GPLv3) since late 2025. Confirm the terms and the trademark/logo
      rules before writing code. GPLv3 matches the app's GPL-3.0-or-later.
- [ ] `AsioBackend` behind `IAudioBackend`, opt-in via `-DNAM_WITH_ASIO=ON` (SDK fetched,
      not vendored, unless the license allows it); WASAPI stays the default.
- [ ] Runtime backend choice on Windows (WASAPI shared / exclusive / ASIO) in the status
      bar and Settings; persisted like the device IDs.
- [ ] Release workflow: decide whether the official Windows zip ships with ASIO.

## Ideas parking lot (not committed)

- **Two inputs / stereo input** (from the Stereo review, 2026-10-03): today there is one
  input port (`NAM-X:input`) and the chain starts mono. A stereo mode with two input
  ports feeding L and R from the start would let two guitars (or a stereo source) run
  independently through the unlinked Gate/Compressor and the per-side amp.

- **Card badges** (user, 2026-10-03, nice to have): a small text tag after an effect's
  title — "Beta", "Experimental", "Deprecated", … — with a tooltip saying why (e.g.
  Transpose: the faint beat on long held chords). One optional flag on
  `make_effect_row`, colors from `Theme`. The whole app being pre-1.0 already says
  "experimental", so this is for individual features once that's no longer true.
- *(Superseded 2026-10-03: Transpose runs on the filter-bank shifter — Phase 22.)* **Transpose latency** (measured 2026-10-01): pitch is accurate (median within ±0.3 cents
  over 110–330 Hz, −12…+12 st; brief ±10–15 cent wobble at crossfades when shifting up),
  but a shift costs ~30 ms of latency (50 ms window; 3 ms at 0 st) — noticeable for
  drop tunings. A 25–30 ms window would give ~13–15 ms at the cost of more artifacts on
  low notes; a lower-latency algorithm is a bigger project. Chords not evaluated.

- ~~**A catchy name for the app**~~ → done 2026-10-03: **NAM-X** (see Phase 7).
- **Experiment — activity bar for the library** (2026-10-02): a VS Code–style icon
  strip at the window edge (Models / IRs / Presets / TONE3000) replacing both tab rows
  of the dock; clicking the active icon collapses the panel (the dock lost its close
  button with the title bar). Try it on a branch and compare side by side: the user
  finds the current labelled tabs ("Models (179)") clearer, so the bar has to
  communicate as well before it replaces them — tooltips, counts (badge or caption),
  per-view shortcuts. Costs: four icons that read at ~20 px (no QtSvg in the build:
  QPainter or PNG), the panel stops being a movable dock, and LibraryDock /
  LibraryPanel / window state / Settings → Library all change.
- Song mode: cue list of preset changes on a song timeline, clock from an external
  player (MPRIS / SMTC) or an in-app file player with A/B loop, slow-down, pitch.
- Headless CLAP was discussed and **rejected** with the no-plugin decision.
- Sample-rate renegotiation while running (effect SR is cached at start).

## Deferred — superseded

The old post-1.0 list is absorbed: preset dropdown → Phase 11/13; MIDI → Phase 16;
limiter → Phase 17; light theme / system theme → Phase 19.
