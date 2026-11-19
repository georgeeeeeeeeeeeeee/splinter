# Stage 7 (Windows): unit tests, state round trips, the random-automation fuzz, the CPU benchmark,
# and pluginval at strictness 8 if you pass its path.
#   .\scripts\checkpoint7.ps1 [-Minutes 10] [-Pluginval C:\path\to\pluginval.exe]
param([double]$Minutes = 10, [string]$Pluginval = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$H = "build\tools\harden\FaultHarden_artefacts\Release\FaultHarden.exe"
New-Item -ItemType Directory -Force build\stage7 | Out-Null
& build\tests\Release\fault_tests.exe | Select-Object -Last 1
& $H --state; if ($LASTEXITCODE -ne 0) { throw "state checks failed" }
& $H --fuzz --minutes $Minutes --seed 20261114 | Tee-Object build\stage7\fuzz.txt; if ($LASTEXITCODE -ne 0) { throw "fuzz failed" }
& $H --cpu --seconds 10 | Tee-Object build\stage7\cpu.txt
if ($Pluginval -ne "") {
    & $Pluginval --strictness-level 8 --validate-in-process --validate "C:\Program Files\Common Files\VST3\Splinter.vst3"
    if ($LASTEXITCODE -ne 0) { throw "pluginval failed" }
}
