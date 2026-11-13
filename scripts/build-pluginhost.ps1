# Windows: build JUCE's AudioPluginHost (for Stage 0-4 testing without Live).
#   .\scripts\build-pluginhost.ps1 [-Juce C:\JUCE]
param([string]$Juce = $(if ($env:JUCE_PATH) { $env:JUCE_PATH } else { "C:\JUCE" }))
$ErrorActionPreference = "Stop"
$out = Join-Path (Split-Path $PSScriptRoot -Parent) "build-host"
cmake -S $Juce -B $out -G "Visual Studio 17 2022" -A x64 -DJUCE_BUILD_EXTRAS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build $out --config Release --target AudioPluginHost --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
$exe = Get-ChildItem $out -Recurse -Filter AudioPluginHost.exe | Select-Object -First 1
Write-Host "AudioPluginHost: $($exe.FullName)"
