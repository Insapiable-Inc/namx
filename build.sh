#!/usr/bin/env bash
# Build namx GUI
# Usage: ./build.sh [Release|Debug]
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${SCRIPT_DIR}/build-${BUILD_TYPE,,}"

echo "==> Initialising git submodules (eigen, AudioDSPTools, signalsmith-linear, signalsmith-stretch, chowdsp_wdf)…"
cd "${SCRIPT_DIR}/.."
git submodule update --init --recursive

echo "==> Configuring CMake (${BUILD_TYPE})…"
cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "==> Building…"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo ""
echo "Done. Binary: ${BUILD_DIR}/namx"
echo ""
echo "Run with:"
echo "  jackd -d alsa &   # start JACK if not already running"
echo "  ${BUILD_DIR}/namx"
