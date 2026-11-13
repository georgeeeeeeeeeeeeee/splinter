# Stage 4 A/B grid (Windows): every tools\render\stage4\<source>-*.json through build\fixtures\<source>.wav
# into build\renders\stage4 (WAV + PNG + tick log). <source>-A-off is the reference with Corrupt off.
# Optional filter: .\scripts\checkpoint4.ps1 vinyl
param([string]$Filter = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$R = Get-ChildItem build\tools\render -Recurse -Include "fault-render.exe", "FaultRender.exe" | Select-Object -First 1
if (-not $R) { throw "render CLI not built: run .\scripts\build.ps1" }
$out = "build\renders\stage4"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-ChildItem "tools\render\stage4\*$Filter*.json" | ForEach-Object {
    $name = $_.BaseName
    $src = "build\fixtures\$($name.Split('-')[0]).wav"
    & $R.FullName --in $src --out "$out\$name.wav" --params $_.FullName --tables tuning\macro_tables.json --png "$out\$name.png" --ticks "$out\$name.csv"
    if ($LASTEXITCODE -ne 0) { throw "render failed: $name" }
}
