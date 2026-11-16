# Redoes the CMake configuration of LogHunter: detects the tools installed on
# the machine, then recreates the build\ folder with their paths.
# Run it again after installing or updating Visual Studio or Qt.
#
#   .\scripts\environment.ps1
#
# Tools looked for (nothing is downloaded or installed):
#  - MSVC: Visual Studio 2022 or newer (or Build Tools);
#  - CMake and Ninja: those of Visual Studio, otherwise those of the PATH;
#  - Qt 6 MSVC 64-bit: the most recent version in C:\Qt, or LOGHUNTER_QT if set.
#
# build.ps1 imports it (with a dot) to load the tools, without recreating build\.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$preset = "msvc-release"

# ── MSVC ─────────────────────────────────────────────────────────────────────
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere not found: install Visual Studio (or the Build Tools) with the 'Desktop development with C++' workload."
    }
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw "No Visual Studio installation with the x64 C++ tools was found." }
    $vcvars = Join-Path $installation "VC\Auxiliary\Build\vcvars64.bat"
    # Reads the environment produced by vcvars64 and applies it to the session.
    $output = cmd /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
}

# ── CMake / Ninja ────────────────────────────────────────────────────────────
# Search order: tools shipped with Visual Studio, then PATH.
$candidates = @()
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -property installationPath
    if ($vs) {
        $cmakeVs = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
        $candidates += (Join-Path $cmakeVs "CMake\bin"), (Join-Path $cmakeVs "Ninja")
    }
}
for ($i = $candidates.Count - 1; $i -ge 0; $i--) {
    if (Test-Path $candidates[$i]) { $env:PATH = "$($candidates[$i]);$env:PATH" }
}
foreach ($tool in "cmake", "ninja") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found: install it (or the 'C++ CMake tools' component of Visual Studio) or add it to the PATH."
    }
}

# ── Qt ───────────────────────────────────────────────────────────────────────
# LOGHUNTER_QT, otherwise the most recent version in C:\Qt (official Qt installer).
if (-not $env:LOGHUNTER_QT) {
    $candidate = Get-Item "C:\Qt\6.*\msvc*_64" -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "lib\cmake\Qt6") } |
        Sort-Object { [version]$_.Parent.Name } -Descending | Select-Object -First 1
    if ($candidate) { $env:LOGHUNTER_QT = $candidate.FullName }
}
if (-not $env:LOGHUNTER_QT -or -not (Test-Path (Join-Path $env:LOGHUNTER_QT "lib\cmake\Qt6"))) {
    throw "Qt 6 (MSVC 64-bit) not found: install it in C:\Qt or set LOGHUNTER_QT."
}
$env:PATH = "$(Join-Path $env:LOGHUNTER_QT 'bin');$env:PATH"

# ── Regeneration of build\ ───────────────────────────────────────────────────
function Reset-Environment {
    Write-Host "MSVC  : $((Get-Command cl.exe).Source)"
    Write-Host "CMake : $((Get-Command cmake).Source)"
    Write-Host "Ninja : $((Get-Command ninja).Source)"
    Write-Host "Qt    : $env:LOGHUNTER_QT"
    if (Test-Path $build) { Remove-Item $build -Recurse -Force }
    Push-Location $root
    try {
        cmake --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
    }
    finally {
        Pop-Location
    }
}

# Run directly (and not imported by build.ps1): regenerates build\.
if ($MyInvocation.InvocationName -ne '.') {
    Reset-Environment
    Write-Host ""
    Write-Host "Environment ready. Build: .\scripts\build.ps1"
}
