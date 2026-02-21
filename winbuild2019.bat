@echo off
setlocal EnableDelayedExpansion

:: ------------------------------------------------------------
:: winbuild2019.bat
:: Configure and build confy with Visual Studio 2019 and vcpkg.
::
:: Usage:
::   winbuild2019.bat [Release|Debug]          (default: Release)
::
:: Requirements:
::   - Visual Studio 2019 (v16) installed
::   - vcpkg installed and VCPKG_ROOT set, OR vcpkg on PATH
::
:: Dependencies (curl) are installed automatically via vcpkg manifest mode.
:: ------------------------------------------------------------

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

:: Locate vcpkg toolchain file
set TOOLCHAIN_FILE=
if defined VCPKG_ROOT (
    set TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
) else if defined VCPKG_INSTALLATION_ROOT (
    set TOOLCHAIN_FILE=%VCPKG_INSTALLATION_ROOT%\scripts\buildsystems\vcpkg.cmake
) else (
    for /f "delims=" %%i in ('where vcpkg 2^>nul') do (
        if not defined TOOLCHAIN_FILE (
            set TOOLCHAIN_FILE=%%~dpiscripts\buildsystems\vcpkg.cmake
        )
    )
)

if not defined TOOLCHAIN_FILE (
    echo ERROR: Could not locate vcpkg. Set VCPKG_ROOT or add vcpkg to PATH.
    exit /b 1
)

echo [confy] Using vcpkg toolchain: %TOOLCHAIN_FILE%
echo [confy] Build type: %BUILD_TYPE%

set BUILD_DIR=%~dp0build

echo [confy] Configuring project...
cmake -S "%~dp0" -B "%BUILD_DIR%" ^
    -G "Visual Studio 16 2019" ^
    -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_FILE%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

echo [confy] Building project (%BUILD_TYPE%)...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -- /m
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo [confy] Build complete. Output: %BUILD_DIR%\%BUILD_TYPE%\confy.exe
endlocal
