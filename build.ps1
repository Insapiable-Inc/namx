#requires -Version 5.1
<#
.SYNOPSIS
    Build namx on Windows (MSVC + Qt6).

.PARAMETER BuildType
    Release (default) or Debug.

.PARAMETER QtDir
    Path to your Qt6 MSVC install (the directory containing bin/, lib/, etc.).
    Example: C:\Qt\6.7.0\msvc2019_64
    If omitted, the script tries $env:QtDir first, then a few common defaults.

.PARAMETER Generator
    CMake generator. Defaults to "Visual Studio 17 2022". Use "Ninja" if you
    have it installed for a noticeably faster build.

.PARAMETER NoDeploy
    Skip the windeployqt step (which copies Qt DLLs next to the binary).
    Useful when iterating — only needs to run once per build directory.

.EXAMPLE
    .\build.ps1
    Build Release with Visual Studio 2022, autodetect Qt.

.EXAMPLE
    .\build.ps1 -BuildType Debug -QtDir 'C:\Qt\6.7.0\msvc2019_64'
    Debug build with an explicit Qt location.
#>

param(
    [ValidateSet('Release', 'Debug')]
    [string]$BuildType = 'Release',

    [string]$QtDir = '',

    [string]$Generator = 'Visual Studio 17 2022',

    [switch]$NoDeploy
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "build-$($BuildType.ToLower())"
$RepoRoot  = Split-Path -Parent $ScriptDir

# ---------------------------------------------------------------------------
# Locate Qt
# ---------------------------------------------------------------------------
if (-not $QtDir) { $QtDir = $env:QtDir }
if (-not $QtDir) {
    $candidates = @(
        'C:\Qt\6.7.0\msvc2019_64',
        'C:\Qt\6.6.3\msvc2019_64',
        'C:\Qt\6.6.2\msvc2019_64',
        'C:\Qt\6.5.3\msvc2019_64'
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'lib\cmake\Qt6\Qt6Config.cmake')) {
            $QtDir = $c
            break
        }
    }
}
if (-not $QtDir -or -not (Test-Path $QtDir)) {
    Write-Error "Qt6 not found. Pass -QtDir 'C:\path\to\Qt\6.x.x\msvc2019_64' or set `$env:QtDir."
}
Write-Host "==> Qt: $QtDir"

# ---------------------------------------------------------------------------
# Submodules
# ---------------------------------------------------------------------------
Write-Host "==> Initialising git submodules (eigen, AudioDSPTools, signalsmith-linear)..."
Push-Location $RepoRoot
try {
    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule failed" }
} finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
$configureArgs = @(
    '-S', $ScriptDir,
    '-B', $BuildDir,
    "-DCMAKE_PREFIX_PATH=$QtDir"
)

if ($Generator -eq 'Ninja') {
    $configureArgs += @('-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$BuildType")
} else {
    $configureArgs += @('-G', $Generator, '-A', 'x64')
    # Visual Studio is a multi-config generator — build type is chosen at build time.
}

Write-Host "==> Configuring CMake ($BuildType, $Generator)..."
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
$buildArgs = @('--build', $BuildDir, '--parallel')
if ($Generator -ne 'Ninja') {
    $buildArgs += @('--config', $BuildType)
}

Write-Host "==> Building..."
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

# ---------------------------------------------------------------------------
# windeployqt — copies Qt6Core.dll, Qt6Widgets.dll, platforms\qwindows.dll, etc.
# ---------------------------------------------------------------------------
if ($Generator -eq 'Ninja') {
    $ExePath = Join-Path $BuildDir 'namx.exe'
} else {
    $ExePath = Join-Path $BuildDir "$BuildType\namx.exe"
}

if (-not $NoDeploy -and (Test-Path $ExePath)) {
    $WinDeployQt = Join-Path $QtDir 'bin\windeployqt.exe'
    if (Test-Path $WinDeployQt) {
        Write-Host "==> Running windeployqt..."
        & $WinDeployQt $ExePath --no-translations --no-system-d3d-compiler --no-opengl-sw
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "windeployqt failed; you may need to copy Qt6*.dll manually."
        }
    } else {
        Write-Warning "windeployqt.exe not found at $WinDeployQt — skipping DLL deploy."
    }
}

Write-Host ""
Write-Host "Done. Binary: $ExePath"
