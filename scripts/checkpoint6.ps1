# Stage 6 (Windows): every factory preset through the real plug-in processor (drums and voice) into
# build\renders\presets, with a level report, plus editor snapshots in build\renders\ui.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$P = Get-ChildItem build\tools\presets -Recurse -Include "fault-presets.exe", "FaultPresets.exe" | Select-Object -First 1
if (-not $P) { throw "preset tool not built: run .\scripts\build.ps1" }
& $P.FullName --check --in build\fixtures\drums.wav --out build\renders\presets\drums
if ($LASTEXITCODE -ne 0) { throw "preset check failed (drums)" }
& $P.FullName --check --in build\fixtures\voice.wav --out build\renders\presets\voice
if ($LASTEXITCODE -ne 0) { throw "preset check failed (voice)" }
New-Item -ItemType Directory -Force -Path build\renders\ui | Out-Null
& $P.FullName --snapshot build\renders\ui\main.png --in build\fixtures\drums.wav --preset "Gate Chop" --seconds 5.2
& $P.FullName --snapshot build\renders\ui\mod.png --in build\fixtures\drums.wav --preset "Total Fault" --drawer mod
& $P.FullName --snapshot build\renders\ui\settings.png --in build\fixtures\drums.wav --drawer settings
