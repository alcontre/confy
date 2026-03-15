#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
USE_VCPKG=0

for arg in "$@"; do
	case "${arg}" in
		--vcpkg)
			USE_VCPKG=1
			;;
		-h|--help)
			cat <<'EOF'
Usage: ./build.sh [--vcpkg]

Options:
  --vcpkg    Configure CMake with the local vcpkg toolchain.
EOF
			exit 0
			;;
		*)
			echo "[confy] Unknown argument: ${arg}" >&2
			echo "[confy] Usage: ./build.sh [--vcpkg]" >&2
			exit 1
			;;
	esac
done

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

CMAKE_ARGS=()

if CMAKE_VERSION="$(cmake --version 2>/dev/null | awk 'NR==1 { print $3 }')" && [[ -n "${CMAKE_VERSION}" ]]; then
	CMAKE_MAJOR_VERSION="${CMAKE_VERSION%%.*}"
	if [[ "${CMAKE_MAJOR_VERSION}" -ge 4 ]]; then
		CMAKE_ARGS+=("-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
	fi
fi

if [[ "${USE_VCPKG}" -eq 1 ]]; then
	if ! TOOLCHAIN_FILE="$(find_vcpkg_toolchain)"; then
		cat >&2 <<'EOF'
[confy] Could not locate vcpkg.
[confy] Install it with:
[confy]   git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
[confy]   "$HOME/vcpkg/bootstrap-vcpkg.sh"
[confy] Then either export VCPKG_ROOT="$HOME/vcpkg" or re-run this script with --vcpkg.
EOF
		exit 1
	fi

	CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
fi

echo "[confy] Cleaning build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

echo "[confy] Recreating build directory"
mkdir -p "${BUILD_DIR}"

if [[ "${USE_VCPKG}" -eq 1 ]]; then
	echo "[confy] Using vcpkg toolchain: ${TOOLCHAIN_FILE}"
else
	echo "[confy] Configuring without vcpkg"
fi
echo "[confy] Configuring project"
if [[ ${#CMAKE_ARGS[@]} -gt 0 ]]; then
	cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
else
	cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
fi

echo "[confy] Compiling project"
cmake --build "${BUILD_DIR}" -j

echo "[confy] Build complete"
