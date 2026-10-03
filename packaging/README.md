# Packaging

Everything a distribution or a release job needs, all fed by `cmake --install`:

| File | Purpose |
|------|---------|
| `cl.pichulero.NAMPlayer.desktop` | Launcher; opens `.nam` / `.nampreset` files (`%F`) |
| `cl.pichulero.NAMPlayer.metainfo.xml` | AppStream metadata (software centers, Flathub) |
| `cl.pichulero.NAMPlayer.xml` | shared-mime-info types `application/x-nam`, `application/x-nampreset` |
| `PKGBUILD` | Arch / AUR package (`nam-player`) |
| `NAMPlayer.rc` | Windows icon + version resource (compiled into the exe on MSVC) |

No AppImage, Flatpak or other bundle (decision 2026-09-17): Linux users install from
source or a distro package against the Qt and JACK they already have.

Icons live in `../resources/icons` (`NAMPlayer.svg` source, PNG sizes 16–512,
`NAMPlayer.ico`) and install as `cl.pichulero.NAMPlayer` in the hicolor theme.

Install layout (`cmake --install build --prefix /usr`):

```
bin/NAMPlayer
share/applications/cl.pichulero.NAMPlayer.desktop
share/metainfo/cl.pichulero.NAMPlayer.metainfo.xml
share/mime/packages/cl.pichulero.NAMPlayer.xml
share/icons/hicolor/{16x16,…,512x512}/apps/cl.pichulero.NAMPlayer.png
share/icons/hicolor/scalable/apps/cl.pichulero.NAMPlayer.svg
```

Validate before publishing:

```bash
desktop-file-validate gui/packaging/cl.pichulero.NAMPlayer.desktop
appstreamcli validate --pedantic gui/packaging/cl.pichulero.NAMPlayer.metainfo.xml
xmllint --noout gui/packaging/cl.pichulero.NAMPlayer.xml
```

