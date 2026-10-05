# Packaging

Everything a distribution or a release job needs, all fed by `cmake --install`:

| File | Purpose |
|------|---------|
| `io.github.Insapiable_Inc.namx.desktop` | Launcher; opens `.nam` / `.nampreset` files (`%F`) |
| `io.github.Insapiable_Inc.namx.metainfo.xml` | AppStream metadata (software centers) |
| `io.github.Insapiable_Inc.namx.xml` | shared-mime-info types `application/x-nam`, `application/x-nampreset` |
| `namx.rc` | Windows icon + version resource (compiled into the exe on MSVC) |
| `windows/namx.iss` | Windows installer (Inno Setup), built by `release.yml` from the windeployqt folder |

The AppImage is the Linux release, built by `.github/workflows/release.yml` (user,
2026-10-04: AppImage only; no Flatpak, Snap or AUR package). It must not bundle
`libjack`: it uses the system's or PipeWire's.

Icons live in `../resources/icons` (`namx.svg` source, PNG sizes 16–512,
`namx.ico`) and install as `io.github.Insapiable_Inc.namx` in the hicolor theme.

Install layout (`cmake --install build --prefix /usr`):

```
bin/namx
share/applications/io.github.Insapiable_Inc.namx.desktop
share/metainfo/io.github.Insapiable_Inc.namx.metainfo.xml
share/mime/packages/io.github.Insapiable_Inc.namx.xml
share/icons/hicolor/{16x16,…,512x512}/apps/io.github.Insapiable_Inc.namx.png
share/icons/hicolor/scalable/apps/io.github.Insapiable_Inc.namx.svg
```

Validate before publishing:

```bash
desktop-file-validate packaging/io.github.Insapiable_Inc.namx.desktop
appstreamcli validate --pedantic packaging/io.github.Insapiable_Inc.namx.metainfo.xml
xmllint --noout packaging/io.github.Insapiable_Inc.namx.xml
```

