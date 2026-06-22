# Activate ESP-IDF environment for xiaozhi-esp32 (Windows PowerShell).
# Usage: . .\scripts\activate-idf.ps1

$IdfPath = "C:\Espressif\frameworks\esp-idf-v5.5.4"
if (-not (Test-Path "$IdfPath\export.ps1")) {
    Write-Error "ESP-IDF not found at $IdfPath. Install ESP-IDF 5.4+ via Espressif IDE or https://docs.espressif.com/projects/esp-idf/"
    return
}

. "$IdfPath\export.ps1"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\xiaozhi-esp32-2.2.4")
Set-Location $ProjectRoot
Write-Host "ESP-IDF ready. Project: $ProjectRoot"
Write-Host "Commands: idf.py build | idf.py flash monitor | idf.py menuconfig"
