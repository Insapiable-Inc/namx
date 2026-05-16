# NAMPlayer Release Roadmap

Punch list of work needed to take NAMPlayer from "works on my machine" to "anyone can install it." Ordered by priority within each tier.

## Must have for a credible release

- [ ] **Desktop integration**
  - [ ] `gui/packaging/NAMPlayer.desktop` (Name, Exec, Icon, `MimeType=application/x-nam;application/x-nampreset;`)
  - [ ] `gui/packaging/icon.svg` (or 256×256 PNG) + call `setWindowIcon` in `MainWindow`
  - [ ] `install(TARGETS NAMPlayer ...)` + `install(FILES ...)` in `gui/CMakeLists.txt` so `cmake --install` deploys binary, desktop file, icon, and metainfo
- [ ] **Replace root README.** It's still upstream `NeuralAmpModelerCore`'s (talks about `loadmodel`, `benchmodel`, Eigen alignment). Either rewrite focused on NAMPlayer, or put a prominent pointer to `gui/BUILD.md`.
- [ ] **Drop the untracked duplicate** `gui/src/MainWindow.cpp`. CMake builds `gui/src/ui/MainWindow.cpp`; the other copy is a divergence trap.
- [ ] **Git hygiene before tagging.** Commits like `Checkin whatever.` / `Let Claude autopilot lmao` won't read well in a release changelog — squash on the release branch if it matters.

## Strongly recommended

- [ ] **First-run auto-connect.** A new user gets silence until they open qpwgraph. When `connections/saved` is absent, auto-wire `system:capture_1 → NAMPlayer:input` and `NAMPlayer:output_{L,R} → system:playback_{1,2}`.
- [ ] **About dialog + `--version` CLI flag.** `app.setApplicationVersion("1.0")` is set in `main.cpp` but never displayed.
- [ ] **AppStream metainfo** at `gui/packaging/cl.pichulero.NAMPlayer.metainfo.xml`. Required by KDE Discover, GNOME Software, Flathub.
- [ ] **Click-free model swap.** `NamProcessor::loadModel` swaps a fresh model into the active slot; the transient is audible. Cross-fade or briefly mute during swap.
- [ ] **Tooltips on knobs.** `KnobWidget` doesn't call `setToolTip`. Low-effort polish.

## Distribution

- [ ] **AUR PKGBUILD** at `gui/packaging/PKGBUILD`. Depends: `qt6-base jack pipewire-jack-client`.
- [ ] **AppImage** via `linuxdeploy-qt` so non-Arch users get a single-file download.
- [ ] **Flatpak manifest** for Flathub. Audio needs `--socket=pipewire` finish.
- [ ] **GitHub Actions release job** building on Ubuntu 22.04 + Arch, uploading artifacts on tag push.

## Quality / safety

- [ ] **Model handling on buffer-size change.** `onBufferSizeChanged` resizes scratch but doesn't re-`Reset` an already-loaded model. Either re-Reset under `mModelMutex` or document that quantum changes require a manual reload.
- [ ] **Remove RT-thread allocation** in `NamProcessor::runModel` (`scratch.resize(n)` at line 76). Now redundant thanks to the buffer-size callback — replace with an `assert`.
- [ ] **JACK shutdown UX.** When the server dies the GUI keeps running with frozen meters. Show a banner, offer reconnect.
- [ ] **CI smoke test.** Even a headless `QT_QPA_PLATFORM=offscreen NAMPlayer --self-test-and-exit` would catch link/MOC regressions.

## Functional gaps worth considering

- [ ] **Sample-rate change handling.** Currently the SR is cached in effects at start; if PipeWire renegotiates, effect SR is stale.
- [ ] **Preset dropdown.** Replace file-by-file Open dialog with a list of `*.nampreset` from a known dir.
- [ ] **Optional output limiter.** A single accidental gain-up shouldn't blow eardrums.
