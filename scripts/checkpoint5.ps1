# Stage 5 renders (Windows): every tools\render\stage5\<source>-*.json through build\fixtures\<source>.wav
# into build\renders\stage5 (WAV + PNG + tick log).
# Optional filter: .\scripts\checkpoint5.ps1 stutter
param([string]$Filter = "")
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
$R = Get-ChildItem build\tools\render -Recurse -Include "fault-render.exe", "FaultRender.exe" | Select-Object -First 1
if (-not $R) { throw "render CLI not built: run .\scripts\build.ps1" }
$out = "build\renders\stage5"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-ChildItem "tools\render\stage5\*$Filter*.json" | ForEach-Object {
    $name = $_.BaseName
    $src = "build\fixtures\$($name.Split('-')[0]).wav"
    & $R.FullName --in $src --out "$out\$name.wav" --params $_.FullName --tables tuning\macro_tables.json --png "$out\$name.png" --ticks "$out\$name.csv"
    if ($LASTEXITCODE -ne 0) { throw "render failed: $name" }
}
