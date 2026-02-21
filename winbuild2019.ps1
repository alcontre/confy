<#
.SYNOPSIS
    Configure and build confy with Visual Studio 2019 and vcpkg.

.DESCRIPTION
    Locates the vcpkg toolchain (via VCPKG_ROOT, VCPKG_INSTALLATION_ROOT, or PATH),
    then configures with the VS 2019 generator and builds the project.
    Dependencies (curl) are installed automatically via vcpkg manifest mode.

.PARAMETER BuildType
    Build configuration: Release or Debug. Defaults to Release.

.EXAMPLE
    .\winbuild2019.ps1
    .\winbuild2019.ps1 Debug
#>

param(
    [ValidateSet('Release', 'Debug')]
    [string]$BuildType = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Locate vcpkg toolchain file
$toolchainFile = $null
if ($env:VCPKG_ROOT) {
    $toolchainFile = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
} elseif ($env:VCPKG_INSTALLATION_ROOT) {
    $toolchainFile = Join-Path $env:VCPKG_INSTALLATION_ROOT 'scripts\buildsystems\vcpkg.cmake'
} else {
    $vcpkgExe = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgExe) {
        # vcpkg.exe lives directly in the vcpkg root directory
        $vcpkgRoot = Split-Path $vcpkgExe.Source -Parent
        $toolchainFile = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    }
}

if (-not $toolchainFile) {
    Write-Error 'Could not locate vcpkg. Set VCPKG_ROOT or add vcpkg to PATH.'
    exit 1
}

if (-not (Test-Path $toolchainFile)) {
    Write-Error "vcpkg toolchain file not found: $toolchainFile"
    exit 1
}

Write-Host "[confy] Using vcpkg toolchain: $toolchainFile"
Write-Host "[confy] Build type: $BuildType"

$buildDir = Join-Path $PSScriptRoot 'build'

Write-Host '[confy] Configuring project...'
cmake -S $PSScriptRoot -B $buildDir `
    -G 'Visual Studio 16 2019' `
    -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
    -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE -ne 0) {
    Write-Error 'CMake configure failed.'
    exit 1
}

Write-Host "[confy] Building project ($BuildType)..."
cmake --build $buildDir --config $BuildType -- /m
if ($LASTEXITCODE -ne 0) {
    Write-Error 'Build failed.'
    exit 1
}

Write-Host "[confy] Build complete. Output: $buildDir\$BuildType\confy.exe"
