#!/usr/bin/env bash
# Build NAM-X
# Usage: ./build.sh [Release|Debug]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${SCRIPT_DIR}/build-${BUILD_TYPE,,}"

cd "${SCRIPT_DIR}"
if [ -e .git ]; then
    echo "==> Initialising git submodules (external/: NeuralAmpModelerCore with Eigen, AudioDSPTools; signalsmith-linear, signalsmith-stretch, chowdsp_wdf)…"
    git submodule update --init --recursive
else
    echo "==> Not a git checkout (release tarball): using the vendored external/ sources"
fi

echo "==> Configuring CMake (${BUILD_TYPE})…"
cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DNAM_WERROR=ON

echo "==> Building…"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo ""
echo "Done. Binary: ${BUILD_DIR}/namx"
echo ""
echo "Run with:"
echo "  jackd -d alsa &   # start JACK if not already running"
echo "  ${BUILD_DIR}/namx"
