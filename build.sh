#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "[confy] Cleaning build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

echo "[confy] Recreating build directory"
mkdir -p "${BUILD_DIR}"

echo "[confy] Configuring project"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"

echo "[confy] Compiling project"
cmake --build "${BUILD_DIR}" -j

echo "[confy] Build complete"
