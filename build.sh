#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

find_vcpkg_toolchain() {
	local vcpkg_root=""

	if [[ -n "${VCPKG_ROOT:-}" ]]; then
		vcpkg_root="${VCPKG_ROOT}"
	elif [[ -n "${VCPKG_INSTALLATION_ROOT:-}" ]]; then
		vcpkg_root="${VCPKG_INSTALLATION_ROOT}"
	elif command -v vcpkg >/dev/null 2>&1; then
		local vcpkg_bin
		vcpkg_bin="$(command -v vcpkg)"
		vcpkg_root="$(cd "$(dirname "${vcpkg_bin}")" && pwd)"
	elif [[ -d "${HOME}/vcpkg" ]]; then
		vcpkg_root="${HOME}/vcpkg"
	fi

	if [[ -z "${vcpkg_root}" ]]; then
		return 1
	fi

	local toolchain_file="${vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
	if [[ ! -f "${toolchain_file}" ]]; then
		echo "[confy] vcpkg toolchain file not found: ${toolchain_file}" >&2
		return 1
	fi

	printf '%s\n' "${toolchain_file}"
}

if ! TOOLCHAIN_FILE="$(find_vcpkg_toolchain)"; then
	cat >&2 <<'EOF'
[confy] Could not locate vcpkg.
[confy] Install it with:
[confy]   git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
[confy]   "$HOME/vcpkg/bootstrap-vcpkg.sh"
[confy] Then either export VCPKG_ROOT="$HOME/vcpkg" or re-run this script.
EOF
	exit 1
fi

echo "[confy] Cleaning build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

echo "[confy] Recreating build directory"
mkdir -p "${BUILD_DIR}"

echo "[confy] Using vcpkg toolchain: ${TOOLCHAIN_FILE}"
echo "[confy] Configuring project"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
	-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"

echo "[confy] Compiling project"
cmake --build "${BUILD_DIR}" -j

echo "[confy] Build complete"
