# Third-party notices

NAM-X is GPL-3.0-or-later (`LICENSE`, notice in `COPYRIGHT`). Its builds include the
following code, each under its own license; the license texts are in this folder.

| Component | License | File | Source |
|---|---|---|---|
| NeuralAmpModelerCore — Steven Atkinson (NAM-X uses a fork with one fix) | MIT | `LICENSE-MIT` | https://github.com/sdatkinson/NeuralAmpModelerCore, fork: https://github.com/Insapiable-Inc/NeuralAmpModelerCore |
| AudioDSPTools — Steven Atkinson | MIT | `AudioDSPTools-LICENSE.txt` | https://github.com/sdatkinson/AudioDSPTools |
| Lanczos resampler from iPlug2 and Cockos WDL (in AudioDSPTools) | zlib-style | in its source files | https://github.com/iPlug2/iPlug2, https://www.cockos.com/wdl |
| Eigen | MPL-2.0 | `Eigen-MPL2.txt` | https://gitlab.com/libeigen/eigen |
| JSON for Modern C++ — Niels Lohmann | MIT | `nlohmann-json-MIT.txt` | https://github.com/nlohmann/json |
| signalsmith-linear — Signalsmith Audio | MIT | `signalsmith-linear-LICENSE.txt` | https://github.com/Signalsmith-Audio/linear |
| Signalsmith Stretch — Geraint Luff / Signalsmith Audio | MIT | `signalsmith-stretch-LICENSE.txt` | https://github.com/Signalsmith-Audio/signalsmith-stretch |
| chowdsp_wdf — Chowdhury-DSP | BSD-3-Clause | `chowdsp_wdf-LICENSE.txt` | https://github.com/Chowdhury-DSP/chowdsp_wdf |
| Qt 6 — The Qt Company (bundled in the AppImage and the Windows builds as shared libraries) | LGPL-3.0 | `LGPL-3.0.txt` (+ `LICENSE`, the GPL it builds on) | https://download.qt.io/official_releases/qt/ |
| Qt SVG — The Qt Company (in the AppImage: the plugins that draw the desktop's SVG icons) | LGPL-3.0; part of its SVG parser also HPND-sell-variant (USC/Information Sciences Institute) | `LGPL-3.0.txt`, `QtSvg-ISI-HPND.txt` | https://download.qt.io/official_releases/qt/ |

The exact versions are the submodule commits of the NAM-X release's source tarball. Qt is
linked dynamically: you can replace the bundled Qt libraries with your own build of the
same Qt version.
