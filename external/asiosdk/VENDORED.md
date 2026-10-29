# Steinberg ASIO SDK 2.3.4 (vendored, host side only)

The files a host application needs, copied unmodified from Steinberg's release
`ASIO-SDK_2.3.4_2025-10-15.zip` (https://download.steinberg.net/sdk_downloads/,
SHA-256 `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca`), which has no
public git repository to use as a submodule. NAM-X uses them under the **GPLv3** option of
the SDK's dual license (`LICENSE.txt`); `host/` carries its own BSD-style notice. Left out:
the driver-side sources, the sample driver, the PDFs and the logo artwork (NAM-X shows no
ASIO logo). "ASIO" is a trademark of Steinberg Media Technologies GmbH.

Used by `src/dsp/AsioBackend.cpp` on Windows builds only (CMake target `asio_sdk`).
