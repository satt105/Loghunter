# Builds LogHunter and puts the application, ready to run, in dist\
# (LogHunter.exe with the Qt libraries and the Visual C++ runtime).
#
#   .\scripts\build.ps1
#
# On the first run, the build\ folder is prepared automatically.
# The language files (translations\*.ts) receive the new identifiers of the code.

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "environment.ps1")

$configuration = Join-Path $build $preset
$dist = Join-Path $root "dist"

if (-not (Test-Path (Join-Path $configuration "build.ninja"))) {
    Reset-Environment
}

# ── Translations ─────────────────────────────────────────────────────────────
$translations = @{}
foreach ($file in Get-ChildItem (Join-Path $root "translations") -Filter *.ts) {
    $translations[$file.FullName] = @((Get-FileHash $file.FullName).Hash, $file.LastWriteTimeUtc)
}
cmd /c "cmake --build `"$configuration`" --target update_translations 2>&1" | Where-Object { $_ -notmatch "has no source" }
if ($LASTEXITCODE -ne 0) { throw "Updating the translations failed." }
foreach ($path in $translations.Keys) {
    if ((Get-FileHash $path).Hash -eq $translations[$path][0]) {
        (Get-Item $path).LastWriteTimeUtc = $translations[$path][1]
    }
}

cmake --build $configuration
if ($LASTEXITCODE -ne 0) {
    throw "The build failed. After an update of Visual Studio or Qt, run .\scripts\environment.ps1 then try again."
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
cmake --install $configuration --prefix $dist
if ($LASTEXITCODE -ne 0) { throw "Copying to dist\ failed." }

Write-Host ""
Write-Host "Application ready: $(Join-Path $dist 'LogHunter.exe')"
