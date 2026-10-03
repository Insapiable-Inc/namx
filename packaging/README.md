# Packaging

Everything a distribution or a release job needs, all fed by `cmake --install`:

| File | Purpose |
|------|---------|
| `cl.pichulero.namx.desktop` | Launcher; opens `.nam` / `.nampreset` files (`%F`) |
| `cl.pichulero.namx.metainfo.xml` | AppStream metadata (software centers, Flathub) |
| `cl.pichulero.namx.xml` | shared-mime-info types `application/x-nam`, `application/x-nampreset` |
| `PKGBUILD` | Arch / AUR package (`namx`) |
| `namx.rc` | Windows icon + version resource (compiled into the exe on MSVC) |

An AppImage and a Flatpak (Flathub) are planned for the first release (ROADMAP
Phase 8; decision 2026-10-02, reversing 2026-09-17's "no bundles"). Neither may bundle
`libjack`: they use the system's or PipeWire's.

Icons live in `../resources/icons` (`namx.svg` source, PNG sizes 16–512,
`namx.ico`) and install as `cl.pichulero.namx` in the hicolor theme.

Install layout (`cmake --install build --prefix /usr`):

```
bin/namx
share/applications/cl.pichulero.namx.desktop
share/metainfo/cl.pichulero.namx.metainfo.xml
share/mime/packages/cl.pichulero.namx.xml
share/icons/hicolor/{16x16,…,512x512}/apps/cl.pichulero.namx.png
share/icons/hicolor/scalable/apps/cl.pichulero.namx.svg
```

Validate before publishing:

```bash
desktop-file-validate gui/packaging/cl.pichulero.namx.desktop
appstreamcli validate --pedantic gui/packaging/cl.pichulero.namx.metainfo.xml
xmllint --noout gui/packaging/cl.pichulero.namx.xml
```

