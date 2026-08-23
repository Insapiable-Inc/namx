# NAM Player — Public Release Plan

Goal: take the app from "works on my machine" to a public v1.0 anyone can install on
Linux or Windows. Historical phase numbers are kept stable, but execution follows the
explicit order under "North star"; work one stage at a time. Detailed bug analysis
lives in `gui/BUGS.md` (BUG-01…08); this file is the tracking checklist.

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
  experience is a file dialog is not done.
- **UX before final packaging** (2026-08-23). Make the existing workflow coherent,
  calibrate its controls, and establish the library/preset experience before calling
  the app finished. Packaging is split: Phase 8A produces an installable public beta;
  Phase 8B produces the final distribution formats and `v1.0` release.
- **Naming stays last** (2026-08-23). Keep **NAM Player** / `NAMPlayer` as the working
  name while the product takes shape. Revisit the public brand and repository name only
  after the feature and UX work; freeze it immediately before Phase 8B so final package
  metadata changes once.
- **Execution order:** 18A → 9 → 11 → 13 → 14 → 8A (**public beta**) → 12 →
  15 (absorbs Phase 10 tempo) → 16 → 17 → 18B → 19 → 7 (**name freeze**) →
  8B (**tag v1.0**).

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
      values pending for the near-white tuner/eq/delay). Knob *bodies* stay neutral
      (colored-body variant tried and rejected). Bugfix found on the way: the app
      stylesheet disables parent→child palette propagation, so accents were never
      reaching the dials via `QPalette::Highlight` (knobs were always default green)
      — `tagCard` now pushes accents explicitly via `setAccent` on each
      knob/toggle, and must run **after** the card's children exist:
      | effect | color | | effect | color |
      |---|---|---|---|---|
      | tuner | `#b6aa8d` | | amp (NAM) | `#e4b954` |
      | noise gate | `#8f969e` | | eq | `#a5a38f` |
      | compressor | `#4f78bd` | | chorus | `#8dccf3` |
      | wah | `#d4c19a` | | tremolo | `#19848e` |
      | flanger | `#93477b` | | delay | `#aaa6a0` |
      | phaser | `#4bba5b` | | reverb | `#8f7d99` |
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
- [ ] **App icon + `setWindowIcon`** — needs an actual icon asset; moved to Phase 8A
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
- [ ] **New dirs** `gui/packaging/` and `gui/resources/` + `.qrc` — created in
      Phase 8A together with their content (icon, desktop file, metainfo).
- [x] ~~Magic numbers → constants header.~~ **Decision:** the meter dB math moved
      into `VUMeter`; the remaining literals are single-use and local to their
      widgets. No shared constants header (GUIConstants was deleted in Phase 5).

## Phase 7 — Naming

Historically three names coexisted: window title "NAM Player",
`setApplicationName("NAM Linux")`, and binary/JACK client `NAMPlayer`, while the repo
was `nam-linux-gui`. The application-facing names have since been standardized on the
working name; the repository and final public brand remain deliberately unresolved.

Current working convention: **standardize on "NAM Player"** internally — already the
user-facing title, accurate, and platform-neutral. This avoids churn until the final
public brand is chosen.

**Deferred until immediately before Phase 8B.** `NAM Player` is the working name, not
the final branding decision. Reassess the name after the feature set and UX have landed;
then update the application, repository, URLs, resources, and package metadata together.

- [ ] **Choose and freeze the final public name**, then rename the GitHub repository
      accordingly (canonical home; currently
      `silvercobraa/NeuralAmpModelerGUI`; GitHub auto-redirects the old URL). Optionally
      rename the Forgejo mirror (`nam-linux-gui`) to match. Update the application name,
      display name, repository URLs, resources, package IDs where appropriate, and the
      remote URL as one coordinated change. Preserve settings and JACK compatibility
      through migration/aliases where needed.
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

## Phase 8A — Installable public beta

Do the minimum packaging needed to put real builds in testers' hands. Final Linux
distribution formats wait until the local and online library workflows are stable.

- [ ] Desktop file `gui/packaging/NAMPlayer.desktop` (Name, Exec, Icon,
      `MimeType=application/x-nam;application/x-nampreset;`).
- [ ] AppStream metainfo `gui/packaging/cl.pichulero.NAMPlayer.metainfo.xml`.
- [ ] `install(TARGETS …)` + `install(FILES …)` in `gui/CMakeLists.txt` so
      `cmake --install` deploys binary, desktop file, icon, metainfo.
- [ ] CI beta/release job: Ubuntu 22.04 + Arch + Windows, non-native SIMD flags;
      publish downloadable Linux and Windows artifacts.
- [ ] Headless smoke test in CI: `QT_QPA_PLATFORM=offscreen NAMPlayer --self-test-and-exit`.
- [ ] About dialog / `--version` flag verified.
- [ ] Publish a beta release for hands-on workflow and hardware testing. Do not tag
      `v1.0` here.

## Phase 9 — Knob range calibration (pre-beta UX; added 2026-08-10)

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

## Phase 10 — Tempo sync (post-1.0 unless pulled forward; added 2026-08-10)

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

## Phase 11 — Library (v1.0)

Models, IRs and presets become browsable lists, not file dialogs. Everything in
Phases 12, 13 and 16 stands on this.

- [ ] **Library panel** (dockable, left of the rig; collapsible; remembers state):
      three sections — Models, IRs, Presets — one list widget, one behaviour.
- [ ] **Root folders** configurable (several per type; sane defaults, e.g. `~/NAM`,
      `~/Music/IRs`, the presets dir); recursive scan on a worker thread with a cached
      index; `QFileSystemWatcher` keeps it live. Never block the GUI on a scan.
- [ ] **Search-as-you-type**, favorites (★), recents, sort by name/date; **prev/next**
      on keyboard (arrows / PgUp-PgDn when the panel has focus) — MIDI later (Phase 16).
- [ ] **Drag-and-drop** `.nam` / `.wav` / `.nampreset` files onto the window (and onto
      the Amp / IR cards) — `setAcceptDrops` + `dropEvent`.
- [ ] **Model metadata** read from the `.nam` JSON (architecture, sample rate,
      training/loudness info when present) shown in the list and on the Amp card.
- [ ] **Amp and IR become hero cards:** name, metadata, ‹ › arrows cycling the library
      (Petrucci: IR combo box + navigation arrows), Unload demoted to a menu item. The
      two big top bars go away; the two `Load…` buttons become "Browse" (focus the
      library).
- [ ] Keep Import/Export file dialogs for one-offs; they stop being the primary path.

## Phase 12 — TONE3000 integration (v1.0)

A second tab of the Library that looks and behaves exactly like the local one but is
the community. API verified 2026-08-22 (tone3000.com/api): OAuth 2.0 + PKCE for
user-facing apps, **free non-commercial tier** covers OAuth, search / trending / latest,
favorites, the user's downloads and per-model download URLs; 100 req/min; branding,
tone images, creator attribution and per-model licenses (T3K / CC) are requirements.

- [ ] **Prerequisite (user action):** register the app on TONE3000 to obtain a
      `client_id`. No secret ships in the binary (PKCE + loopback redirect).
- [ ] **Login** (system browser → `http://127.0.0.1:<port>/callback`), token stored in
      QSettings (or the platform keychain if cheap), logout.
- [ ] **Browse:** trending, latest, search, filter by make/tag; tone cards with image,
      creator, license; favorite/unfavorite; "open on TONE3000".
- [ ] **Download** a tone's model(s) into the library (sidecar JSON: tone id, creator,
      license, URL) → immediately loadable; already-downloaded marker; offline = the
      local tab still works, the TONE3000 tab says so.
- [ ] **Preset ↔ tone linkage:** presets record the TONE3000 tone/model id next to the
      path; loading a preset whose model is missing offers to fetch it. This is what
      makes *sharing presets* work (Phase 19).
- [ ] Rate-limit handling, listing cache, their branding/attribution requirements met.
- [ ] Networking via `Qt6::Network` (part of `qt6-base`; CMake `find_package` gains
      `Network`). Phase 8A builds re-verified with it.

## Phase 8B — Final distribution & v1.0

- [ ] AUR PKGBUILD (`qt6-base jack pipewire-jack-client`).
- [ ] AppImage via linuxdeploy-qt.
- [ ] Flatpak manifest (`--socket=pipewire`).
- [ ] Re-run install, headless smoke, and artifact tests with `Qt6::Network` enabled.
- [ ] Publish final release notes and **tag v1.0**.

## Phase 13 — Presets as the live unit

Petrucci parity: preset combo box, ‹ ›, Save, Save As…, Delete, Import, Reset, Locate
file, `*` on unsaved changes, folders/subfolders, and a defined set of things presets
*don't* recall (audio device, tuner, metronome, MIDI map).

- [ ] **Preset name in the top bar** (replaces the redundant "NAM Player" title),
      with `*` when modified; ‹ › cycle the preset list; Save / Save As / Delete /
      Import / Export / Locate folder.
- [ ] **Quick slots 1–8** (`Ctrl+1…8`, MIDI PC later) and **A/B compare**.
- [ ] Presets organised in folders (the library already shows the tree).
- [ ] **Excluded from presets** (documented): audio devices/connections, tuner
      settings, metronome, MIDI map, window state.
- [ ] A handful of **starter presets** shipped with *no model baked in* (or linked to
      permissively-licensed TONE3000 tones via Phase 12) so first run isn't empty.

## Phase 14 — Cockpit: status bar, meters, tuner, efficiency

The "at the speed of a cockpit" part. Also where *efficient* stops being a slogan.

- [ ] **Status bar:** backend · sample rate · buffer · round-trip latency (ms) ·
      **DSP load %** (audio-thread timing, max-hold) · **xrun counter** (JACK xrun
      callback; click to reset) · model / IR / login state. This would have diagnosed
      the 2026-08-22 xrun hunt in five seconds.
- [ ] **Meters at display rate** (60 fps, `QTimer` tied to the window being visible and
      audio running — zero CPU when hidden/idle): proper ballistics, peak-hold, **clip
      indicators** held ~10 s, click to clear (Petrucci parity). Input and output.
- [ ] **Gain-reduction meters** on Compressor and Gate (Petrucci has one on the comp).
- [ ] **Tuner as a mode:** big readout when engaged (note, cents *or* Hz, reference
      pitch 420–460 Hz — parity), mute, live mini-tuner in the top bar option.
- [ ] **Efficiency budget written down and checked:** DSP ≤ 10 % of a 128-frame
      budget on a 2018 laptop core for the default chain; idle UI ≈ 0 % (no periodic
      repaint of static widgets, no polling timers when nothing changes); cold start
      < 1 s; RSS modest. The perf micro-benches from 2026-08-22 (NAM model at 128,
      IR, each effect) move into `tools/` so regressions are measurable.
- [ ] Free perf wins already identified, now also documented here so they're not
      lost: `nam::activations::Activation::enable_fast_tanh()` at startup (2.4× on
      Tanh WaveNets — what the official plugin does), Phaser/Compressor per-block
      coefficient math, `% sz` ring-buffer wraps → compare-and-reset.

## Phase 15 — Practice tools: metronome + global tempo (absorbs Phase 10)

Main use case is practising at home along a song; the metronome is the standalone
feature people actually use. Petrucci parity for the metronome in full.

- [ ] **Global tempo** in the top bar: BPM field (click to type, drag to nudge), **Tap**
      (this *is* Phase 10's BPM; Phase 10's delay/LFO sync modes FREE / TAP(tempo)
      then land here as planned — engine stays dumb, GUI maps note values to ms/Hz).
- [ ] **Metronome:** play/stop (MIDI-assignable), volume, pan, 40–240 BPM, time
      signatures incl. compound/complex, per-beat accents (click to cycle), sound
      sets, subdivision ("rhythm"), beat LEDs; keeps running across preset changes.
      RT-safe: preloaded click samples, a generator mixed into the output stage on
      its own gain, outside the effect chain.
- [ ] **Count-in** and a keyboard shortcut for play/stop.
- [ ] *(Ideas, not committed)* in-app local file player with A/B loop, slow-down,
      pitch; "song mode" cue list driven by an external player's position over
      MPRIS (Firefox ≥ 131 / Chromium expose it for YouTube) or SMTC on Windows.
      Parked — see Ideas.

## Phase 16 — MIDI

Petrucci parity: CC / PC / NOTE, MIDI Learn from a right-click, a Mappings window,
absolute vs relative CC, mapping presets saved/loaded, device enable checkboxes.
The parameter registry (Phase 2) was built for this: every `ParamDesc` is mappable.

- [ ] **Backends:** Linux — ALSA sequencer (covers USB, DIN via interfaces, and
      PipeWire/JACK bridges); Windows — WinMM (or WinRT MIDI). Device list with
      enable checkboxes in Settings. Hot-plug.
- [ ] **MIDI Learn** on every knob/toggle (context menu → "Learn", widget highlighted
      while armed), plus quick slots / prev-next preset / tuner / metronome play-stop
      as learnable actions.
- [ ] **Mappings window:** bypass, type (CC/PC/NOTE), target, channel, number, value /
      absolute-relative, delete; **save/load mapping presets**; default mapping.
- [ ] **Delivery:** MIDI thread → lock-free queue → applied on the GUI thread through
      the registry (widgets and engine can't drift; keyboard shortcuts already go this
      way) — latency is one event-loop hop, fine for stomps and pedals.
- [ ] **Expression pedal** targets: wah position (Phase 17 manual wah), volume pedal,
      any knob.
- [ ] MIDI clock → global tempo (Phase 15) as a follow-up.

## Phase 17 — DSP parity with Petrucci X (the "at least everything it does" gap)

Ranked by musical value ÷ effort. NAM models replace the four amps; "Piezo" (acoustic
sim) is out of scope — users load acoustic models.

- [ ] **Overdrive/boost pedal** pre-amp (Drive / Tone / Level, TS-style) — the one
      thing every high-gain rig needs in front of the model. *easy*
- [ ] **Doubler** (randomised 3–20 ms spread copy before the post section; off when
      input is already stereo). *easy*
- [ ] **Cab: dual IR slots** with level / pan / phase / blend + HPF/LPF, and **IR
      resampling to the session rate** (today a 44.1 k IR plays ~9 % sharp at 48 k —
      a correctness bug). *easy–medium*
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

## Phase 18A — UX foundation (before Phase 9)

- [ ] **Basic Settings dialog:** audio device and requested buffer size / sample rate,
      plus a **shortcuts sheet** for discoverability of the digit/QWE toggles. Library,
      MIDI, theme, and meter pages join as those features land.
- [x] **Bypass state:** bypassed cards dim while their controls remain interactive;
      mouse, keyboard, preset, and session changes all update the visual state.
- [ ] **Layout:** cards with > 4 knobs use a 2-column knob grid so
      all columns are one height; responsive wrap / compact mode for 1366×768;
      device row hidden on JACK (one choice) and moved to Settings; window sizes S/M/L
      + free resize (parity); right-click / double-click a card title to bypass it
      (parity).
- [x] **Visual fixes:** Compressor / Reverb accents are visible on the dark ground,
      clipped tuner "±0¢", stereo toggle labelling, "Dyn.Wah" naming.

## Phase 18B — Settings completion & platform polish

- [ ] Extend Settings with MIDI devices, library folders, themes, and meter options as
      their owning phases land; keep feature configuration out of `MainWindow`.
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
