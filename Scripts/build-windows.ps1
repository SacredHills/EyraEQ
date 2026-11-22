# Configure, build and test EyraEQ on Windows (x64) with Visual Studio 2022.
# Usage:  powershell -ExecutionPolicy Bypass -File Scripts\build-windows.ps1 [-JuceDir C:\path\to\JUCE]
param(
    [string]$JuceDir = ""
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$extra = @()
if ($JuceDir -ne "") { $extra += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceDir" }

cmake --preset windows @extra
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset windows-release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --preset windows-release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "VST3:       build\windows\EyraEQ_artefacts\Release\VST3\EyraEQ.vst3"
Write-Host "Standalone: build\windows\EyraEQ_artefacts\Release\Standalone\EyraEQ.exe"
