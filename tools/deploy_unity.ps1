param(
    [Parameter(Mandatory = $true)]
    [string]$GameRoot,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build\bin\Release")
)

$ErrorActionPreference = "Stop"

$dataDir = Get-ChildItem -Path $GameRoot -Directory -Filter "*_Data" | Select-Object -First 1
if (-not $dataDir) { throw "Unity *_Data folder not found in $GameRoot" }

$pluginsDir = Join-Path $dataDir.FullName "Plugins\x86_64"
if (-not (Test-Path $pluginsDir)) { throw "Plugins\x86_64 not found: $pluginsDir" }

$files = @(
    "EOSSDK-Win64-Shipping.dll"
)

foreach ($name in $files) {
    $src = Join-Path $BuildDir $name
    if (-not (Test-Path $src)) { throw "Missing build artifact: $src" }
    Copy-Item $src (Join-Path $pluginsDir $name) -Force
    Copy-Item $src (Join-Path $GameRoot $name) -Force
}

Write-Host "Deployed to:"
Write-Host "  $pluginsDir"
Write-Host "  $GameRoot"
Write-Host "Run the game once and check NemirtingasEpicEmu.json next to the .exe"
