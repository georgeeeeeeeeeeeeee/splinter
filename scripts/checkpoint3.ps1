# Stage 3 render grid (Windows): every tools\render\stage3\<source>-*.json through build\fixtures\<source>.wav,
# with the Macro tables from tuning\macro_tables.json, into build\renders\stage3 (WAV + PNG + tick log).
# Optional filter, for the tuning loop: .\scripts\checkpoint3.ps1 bend
param([string]$Filter = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$R = Get-ChildItem build\tools\render -Recurse -Include "fault-render.exe", "FaultRender.exe" | Select-Object -First 1
if (-not $R) { throw "render CLI not built: run .\scripts\build.ps1" }
$out = "build\renders\stage3"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-ChildItem "tools\render\stage3\*$Filter*.json" | ForEach-Object {
    $name = $_.BaseName
    $src = "build\fixtures\$($name.Split('-')[0]).wav"
    & $R.FullName --in $src --out "$out\$name.wav" --params $_.FullName --tables tuning\macro_tables.json --png "$out\$name.png" --ticks "$out\$name.csv"
    if ($LASTEXITCODE -ne 0) { throw "render failed: $name" }
}
