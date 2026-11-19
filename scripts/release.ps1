# Windows release candidate: a Release VST3 with the Macro tables compiled in (no hot-reload), zipped
# with the user README. Output: dist\Splinter-<version>-windows.zip
#   .\scripts\release.ps1 [-Juce C:\JUCE] [-Pluginval C:\path\to\pluginval.exe]
param(
    [string]$Juce = $(if ($env:JUCE_PATH) { $env:JUCE_PATH } else { "C:\JUCE" }),
    [string]$Pluginval = ""
)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not (Test-Path "$Juce\CMakeLists.txt")) { throw "JUCE not found at $Juce (pass -Juce or set JUCE_PATH)" }
$version = (Select-String -Path CMakeLists.txt -Pattern '^project\(Splinter VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$B = "build-release"
cmake -S . -B $B -G "Visual Studio 17 2022" -A x64 "-DFAULT_JUCE_PATH=$Juce" -DFAULT_DEV_TABLES=OFF -DFAULT_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build $B --config Release --parallel --target Splinter_VST3 FaultRender
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# The tuning file must already be baked in: a release never reads tuning\macro_tables.json.
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "fault-baked.inc"
& "$B\tools\render\FaultRender_artefacts\Release\FaultRender.exe" --bake-tables tuning\macro_tables.json --bake-out $tmp | Out-Null
if ((Get-Content $tmp -Raw).Replace("`r`n", "`n") -ne (Get-Content source\dsp\MacroTablesBaked.inc -Raw).Replace("`r`n", "`n")) {
    throw "tuning\macro_tables.json has changes that aren't baked in: run fault-render --bake-tables tuning\macro_tables.json"
}

$out = "dist\Splinter-$version-windows"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$B\Splinter_artefacts\Release\VST3\Splinter.vst3" $out -Recurse
Copy-Item docs\RELEASE_README.txt "$out\README.txt"
if ($Pluginval -ne "") {
    & $Pluginval --strictness-level 8 --validate-in-process --validate "$out\Splinter.vst3"
    if ($LASTEXITCODE -ne 0) { throw "pluginval failed" }
    Write-Host "pluginval (strictness 8): passed"
}
$zip = "dist\Splinter-$version-windows.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $out -DestinationPath $zip
Write-Host "wrote $zip"
