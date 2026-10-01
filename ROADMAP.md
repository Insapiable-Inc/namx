# NAM Player — Public Release Plan

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
  experience is a file dialog is not done. ~~The `v1.0` tag moves from Phase 8 to the end
  of Phase 12.~~ Superseded 2026-09-12, see next.
- **Packaging goes last** (2026-09-12). Phase 8 runs *after* Phase 12, right before
  release, so the desktop file and CI jobs are built once against the final v1.0
  feature set (Library + TONE3000 + `Qt6::Network`) instead of being redone. The
  `v1.0` tag is back at the end of Phase 8.
- **No AppImage, Flatpak or other bundle** (2026-09-17). Linux users are assumed to
  have Qt 6 and JACK installed; distribution is source + distro packages (PKGBUILD).
- **Execution order:** 7 → 11 → 12 → 8 (packaging) → **tag v1.0** → 13 → 14 →
  15 (absorbs Phase 10 tempo) → 16 → 17 → 18 → 19; Phase 9 (knob tapers) slots in
  wherever a knob-heavy phase touches the widgets (17 at the latest).

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
- [ ] Grips: every effect row (incl. Tuner and Cabinet) shows `≡` and drags only
      from the grip (2026-08-23: the grip replaced the title band; clicking
      knobs/toggles/body never reorders); the Input/Output columns don't drag.
- [ ] Tuner mid-chain: place the Tuner after a stereo effect (e.g. after Delay with
      stereo on) — pitch readout still tracks (fed the L/R average) and Mute
      silences both channels.
- [ ] Digit shortcuts now map to the new left-to-right order; Q/W/E stereo keys too.
- [ ] Order survives relaunch, and a preset saved with a custom order restores it.
      Loading a v1 preset leaves the order untouched.
- [ ] **BUG-06 exercise (finally reachable):** put Gate/Comp/Wah *after* a
      stereo-mode Delay or Chorus and play — both channels behave identically
      (per-channel envelopes, no mono collapse, no channel pumping).

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

- [ ] Resize the window down to 1280×720: everything stays usable, the strip scrolls.
- [ ] New accent colors: stripes, titles, knob rings/ticks, and toggle "on" states
      all show the palette hex verbatim (revised palette values to come from the
      user for the near-white and very dark entries).
- [ ] Ctrl+S / Ctrl+O open the save/load dialogs; About shows v1.0.
- [ ] Load a model while playing: no click, a soft ~15 ms fade-in instead. Same on
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

- [ ] **Repo rename → `nam-player` on GitHub** (canonical home; currently
      `silvercobraa/NeuralAmpModelerGUI`; GitHub auto-redirects the old URL). Optionally
      rename the Forgejo mirror (`nam-linux-gui`) to match. **User action** (no `gh` on
      this machine): GitHub → repo Settings → General → Repository name. Then
      `git remote set-url github https://github.com/silvercobraa/nam-player.git`.
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
  cents). **Tuner mute removed** (decision 2026-08-23).
- Full labels everywhere (Noise Gate, Amplifier, Equalizer, Cabinet,
  Threshold, Sensitivity, Resonance, Makeup Gain, Room Size,
  Bass/Middle/Treble); no em dashes in UI strings; Chorus gained its missing
  Stereo toggle (DSP always had it).
- Theme scoped to the main window and file dialogs routed through the XDG
  portal, so dialogs are the desktop's own (Breeze on Plasma).

Phase-list fallout is annotated inline below: parts of Phase 14 are built,
most Phase 18 layout/visual fixes landed or became obsolete, and Phase 5's
pending palette revision shipped.

## Phase 8 — Packaging & ship (runs after Phase 12 — decision 2026-09-12) → tag **v1.0** at the end

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
- [x] ~~AppImage via linuxdeploy-qt.~~ **Dropped (decision 2026-09-17): no AppImage.**
- [x] ~~Flatpak manifest (`--socket=pipewire`).~~ **Dropped (decision 2026-09-17): no
      Flatpak.** Linux users are assumed to have Qt 6 and JACK installed; they build
      from source or a distro package.
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
- [ ] **Tag v1.0.** (Was moved to Phase 12 on 2026-08-22; back here since 2026-09-12
      because packaging itself now runs last.) **User action**, in this order: add
      the `TONE3000_CLIENT_ID` repository secret (Phase 12 prerequisite), make the
      GitHub repo public / rename it (Phase 7), then `git tag v1.0.0 && git push
      github v1.0.0` — the release workflow does the rest. Fill the PKGBUILD's
      `sha256sums` from the published tarball and push it to the AUR.

## Phase 9 — Knob range calibration (post-1.0 unless pulled forward; added 2026-08-10)

Make every knob feel right across its whole travel, not just near the default.

- [ ] **Audit all ranges/defaults in `Constants.h`** against musical usefulness —
      e.g. does the comp ratio need 20:1, is the gate threshold floor right, do the
      LFO rate extremes get used.
- [ ] **Per-knob response curves (taper).** `KnobWidget` maps dial↔value linearly
      today (`dialToValue`/`valueToDial`, 10000 steps); add an optional
      logarithmic/exponential taper so e.g. rates (Hz) and delay time spend more
      travel in the musically dense low region. dB-valued knobs are already
      log-domain and stay linear-in-dB.
- [ ] **No schema impact:** presets/session store real units; the taper only
      changes the dial position mapping. Re-tune `setScrollStep` per taper
      (steps should feel constant-size perceptually).

## Phase 10 — Tempo sync → done inside Phase 15 (2026-10-01)

Global tempo, so time-based parameters can be set in note values.

- [ ] **Global tempo (BPM)** control in the top bar; persisted in session and
      preset (schema bump). Tap tempo and MIDI clock become natural extensions
      once MIDI lands (see parameter registry hook).
- [ ] **Sync mode for time-based params:** delay time, and the tremolo / chorus /
      flanger / phaser LFO rates. Each gets free (ms / Hz, as today) vs. synced
      (note-fraction) mode — fractions like 1/1, 1/2, 1/3, 1/4, 1/8 plus dotted /
      triplet variants.
- [ ] **Engine stays dumb:** sync is a GUI-layer mapping — tempo × fraction is
      converted to the existing ms/Hz atomics, and a tempo change re-pushes every
      synced parameter. No DSP or RT changes needed.
- [ ] **Persistence:** per-param mode + fraction and the BPM join the session and
      preset schema (versioned, so old files keep loading).

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
- [ ] Click a model: it loads, the row gets the accent bar, the Amplifier row shows
      its name + "WaveNet · 48 kHz · …". Hold ↓ in the list: one load when the key
      rests, not one per row. ‹ › on the row follow the visible (filtered/sorted) order
      and wrap around.
- [ ] Type in the search field: digits/letters filter, effects don't toggle. Click
      the list and press `1`: the first effect toggles.
- [ ] ★ on a row toggles the favorite without loading it; **★** mode shows only those.
      **Recent** lists the last loads, newest first, including drops and imports.
- [ ] Drop a `.nam` from Dolphin onto the strip, onto a card and onto the dock: all
      three load it. Drop a `.nampreset`: it applies.
- [ ] Save a preset into the default folder: it appears in **Presets** within a
      second without a rescan click. Delete a file on disk: it disappears.
- [ ] Library → Folders…: add `gui/presets`, OK: presets appear; remove it: gone.
- [ ] Close the dock, quit, relaunch: still closed; `Ctrl+L` brings it back at the
      same width.

## Phase 12 — TONE3000 integration (v1.0) ✅ (code complete 2026-09-17; needs the key + a live run) → then Phase 8 (packaging) → tag **v1.0**

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

- [ ] **Prerequisite (user action):** register the app on TONE3000 to obtain a
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

- [ ] With the key: Continue → browser sign-in → pick a tone → back in the app on that
      tone's page. Does TONE3000 accept the unregistered `localhost` redirect URI
      (their docs say localhost is allowed in development)? If not, register it.
- [ ] Trending / Latest show real cards with images; Favorites reflects ★ toggles made
      in the app and on the site; Downloads lists what was fetched.
- [ ] Download a model: file + sidecar land in the TONE3000 folder, the local tab shows
      it with "TONE3000 · @creator", Load puts it in the rig, the Amplifier row shows
      the T3K mark and clicking it opens the tone.
- [ ] Save a preset with that model, delete the file, load the preset → download offer
      → model back and loaded.
- [ ] Pull the network: lists say offline, the local library works; reconnect and ↻.
- [ ] Sign out, sign in again; wait > 1 h and use a list (refresh path).

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

- [ ] Twist a knob → `*` appears; twist it back → gone. Ctrl+S clears it; relaunch:
      same name, same `*` state.
- [ ] ‹ › and the name menu walk the presets; Starter shows as a submenu.
- [ ] Empty slot click stores the preset; Ctrl+N recalls it; right-click → Clear.
- [ ] A/B: tweak on B, switch to A and back — both states survive; A→B copies.
- [ ] Import a file from Downloads → copied into the presets folder and loaded;
      Export doesn't change the current name; Delete moves to the trash → Untitled.
- [ ] Tuner state survives loading any preset.

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

- [ ] DSP % moves with the chain (enable a stereo effect before the amp → roughly
      doubles); kill/restart the server → xruns count; click resets.
- [ ] Meters: smooth at 60 fps, hold line waits 1 s then falls; minimize → the GUI
      thread goes quiet (`top -H`).
- [ ] GR meters move with the compressor/gate working; bypass → 0.
- [ ] Tuner: A4 = 432 makes a 440 Hz A read about +32¢; Hz toggle shows the
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
      four sound sets (Click, Wood, Beep, Cowbell — synthesized at sample-rate change,
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

- [ ] Click tempo matches a reference metronome over a minute (no drift); BPM drag,
      wheel, typing and Tap all land; Space starts/stops (and still types a space in
      the library search field).
- [ ] 7/8 with custom accents, 6/8 default accents, triplet subdivision, all four
      sounds; level/pan audible; count-in lights amber, silent-after keeps lights only.
- [ ] Load presets while the metronome runs: it doesn't stop or stutter.
- [ ] Delay Sync at 1/8. and 120 BPM ≈ 375 ms; changing the tempo moves the echoes;
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

- [ ] With the Scarlett's DIN MIDI or a USB controller: Learn a knob from a CC, a
      switch from a footswitch (toggle per press), a slot from a program change.
- [ ] Unplug/replug a USB controller: it reconnects by itself; disabling it in
      MIDI → Devices stops its messages.
- [ ] Relative encoder mode; min/max range on an expression pedal.
- [ ] Save a `.nammidi`, Defaults, Load it back.
- [ ] Follow MIDI Clock from a DAW or drum machine; tempo settles within a few beats.
- [ ] Windows build: WinMM backend compiles and receives.

## Phase 17 — DSP parity with Petrucci X (the "at least everything it does" gap)

Ranked by musical value ÷ effort. NAM models replace the four amps; "Piezo" (acoustic
sim) is out of scope — users load acoustic models.

- [ ] **Overdrive/boost pedal** pre-amp (Drive / Tone / Level, TS-style) — the one
      thing every high-gain rig needs in front of the model. *easy*
- [ ] **Doubler** (randomised 3–20 ms spread copy before the post section; off when
      input is already stereo). *easy*
- [ ] **Cab: dual IR slots** with level / pan / phase / blend + HPF/LPF. *easy–medium*
      (IR resampling to the session rate landed 2026-09-12 — windowed-sinc in
      `IRLoader`, kernel rebuilt on engine-rate change.)
- [ ] **EQ → 4-band semi-parametric** (frequency per band, ±12 dB) + HPF/LPF. *easy*
- [ ] **Delay:** sync FREE / TAP (Phase 15), dual L/R times + cross-feed, low/high cut,
      tape saturation, modulation, tap tempo button. *medium* — "Crystals" (pitched-up
      layers) needs the pitch shifter, see Transpose.
- [ ] **Reverb:** pre-delay, decay, low/high cut. *medium* — Shimmer needs the pitch
      shifter.
- [ ] **Chorus modes** (Normal/Tri, Vintage/Spatial), **Phaser Vibe mode**, **Flanger
      Range**. *easy*
- [ ] **Wah: manual/expression position mode** beside the current envelope mode;
      **Volume pedal** with mid-point. *easy*
- [ ] **Output limiter** (was deferred). *easy*
- [ ] **Stereo-amp toggle** (run NAM per channel only when asked; sum to mono
      otherwise — halves the cost of a stereo pre-effect). *easy*
- [ ] **Transpose ±12 st** (and with it Shimmer / Crystals) — a real pitch shifter,
      latency vs quality trade-off; evaluate Rubber Band (GPL, fine for us) vs own
      implementation. *hard* — last.
- [ ] Phase 9 **knob tapers** land alongside whichever of the above touches the most
      knobs.

## Phase 18 — Settings, layout discipline, platform

- [ ] **Settings dialog:** audio (device, requested buffer size / sample rate; on
      Windows later WASAPI-exclusive), MIDI devices, library folders, theme, meter
      options, a **shortcuts sheet** (discoverability of the digit/QWE toggles).
- [ ] **Layout:** ~~bypassed cards dim~~ (done 2026-08-23); ~~2-column knob grid for
      one-height columns~~ and ~~device row moved to Settings~~ (both superseded
      2026-08-23 — uniform 62 px rows and the status bar device pickers); remaining:
      compact mode for 1366×768 (today the strip scrolls), window sizes S/M/L + free
      resize (parity), right-click / double-click a row name to bypass it (parity).
- [x] **Visual fixes** — all resolved by the 2026-08-23 redesign: Compressor/Reverb
      accents revised (`#4f78bd`/`#8f7d99`), tuner readout rebuilt, stereo toggles
      labelled "Stereo", "Dynamic Wah" named in full.
- [ ] Windows: **WASAPI exclusive mode** (shared mode's 10–20 ms is not a guitar
      latency); keep shared as fallback.

## Phase 19 — Themes & community

- [ ] **Theme files** (palette + per-effect accents as JSON in the config dir),
      built-in Dark / Light / High-contrast stage; user themes shareable; `Theme` is
      already the single owner of colour, so this is mostly plumbing.
- [ ] **Preset sharing:** export bundles that carry TONE3000 tone references (Phase 12)
      so a shared `.nampreset` can fetch its model; a community presets/themes repo
      linked from the app ("Get more…"). Investigate whether TONE3000 will host
      presets for third-party apps.
- [ ] Courtesy/credits screen: NAM core, TONE3000, contributors.

## Ideas parking lot (not committed)

- Song mode: cue list of preset changes on a song timeline, clock from an external
  player (MPRIS / SMTC) or an in-app file player with A/B loop, slow-down, pitch.
- Headless CLAP was discussed and **rejected** with the no-plugin decision.
- Partitioned FFT convolution for full-length IRs at flat cost.
- Sample-rate renegotiation while running (effect SR is cached at start).

## Deferred — superseded

The old post-1.0 list is absorbed: preset dropdown → Phase 11/13; MIDI → Phase 16;
limiter → Phase 17; light theme / system theme → Phase 19.
