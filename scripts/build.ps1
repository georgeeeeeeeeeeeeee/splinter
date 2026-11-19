# Windows: build VST3 + Standalone with Visual Studio 2022 (x64).
#   .\scripts\build.ps1 [-Config Release|Debug] [-Juce C:\JUCE]
param(
    [string]$Config = "Release",
    [string]$Juce = $(if ($env:JUCE_PATH) { $env:JUCE_PATH } else { "C:\JUCE" })
)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not (Test-Path "$Juce\CMakeLists.txt")) { throw "JUCE not found at $Juce (pass -Juce or set JUCE_PATH)" }
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DFAULT_JUCE_PATH=$Juce"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Get-ChildItem "build\Splinter_artefacts\$Config" -Recurse -Include *.vst3, *.exe -Depth 2 | ForEach-Object { Write-Host "built $($_.FullName)" }
