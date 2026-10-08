# Removes the PhoneBridge camera media source. Run from an ADMINISTRATOR PowerShell.
$ErrorActionPreference = "Continue"
$dest = Join-Path $env:ProgramFiles "PhoneBridge"
Stop-Service -Name FrameServer -Force -ErrorAction SilentlyContinue
Stop-Service -Name FrameServerMonitor -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
$dll = Join-Path $dest "phonebridge_mediasource.dll"
if (Test-Path $dll) { Start-Process -FilePath "regsvr32.exe" -ArgumentList "/u /s `"$dll`"" -Wait }
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
Start-Service -Name FrameServer -ErrorAction SilentlyContinue
Write-Host "Removido." -ForegroundColor Green
