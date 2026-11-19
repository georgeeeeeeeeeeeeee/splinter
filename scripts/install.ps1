# Windows: copy Splinter.vst3 to C:\Program Files\Common Files\VST3 (run PowerShell as Administrator).
#   .\scripts\install.ps1 [-Config Release]
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$src = "build\Splinter_artefacts\$Config\VST3\Splinter.vst3"
$dest = "C:\Program Files\Common Files\VST3"
if (-not (Test-Path $src)) { throw "missing $src: run .\scripts\build.ps1 first" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Remove-Item -Recurse -Force "$dest\Splinter.vst3" -ErrorAction SilentlyContinue
Copy-Item -Recurse $src $dest
Write-Host "installed $dest\Splinter.vst3"
