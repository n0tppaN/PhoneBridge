# Installs the PhoneBridge camera media source for the Windows Frame Server.
# Run from an ADMINISTRATOR PowerShell:   .\install_camera.ps1
# Optional:  .\install_camera.ps1 -Dll "C:\path\to\phonebridge_mediasource.dll"
param([string]$Dll = "")

$ErrorActionPreference = "Stop"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERRO: abra o PowerShell como ADMINISTRADOR (botao direito > Executar como administrador)." -ForegroundColor Red
    exit 1
}

# 1) find the DLL
if (-not $Dll) {
    $windowsDir = Split-Path -Parent $PSScriptRoot
    $found = Get-ChildItem -Path (Join-Path $windowsDir "build") -Recurse -Filter "phonebridge_mediasource.dll" -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { Write-Host "ERRO: nao encontrei phonebridge_mediasource.dll. Compile primeiro (veja COMO_APLICAR.md)." -ForegroundColor Red; exit 1 }
    $Dll = $found.FullName
}
if (-not (Test-Path $Dll)) { Write-Host "ERRO: ficheiro nao existe: $Dll" -ForegroundColor Red; exit 1 }
Write-Host "DLL: $Dll"

$dest = Join-Path $env:ProgramFiles "PhoneBridge"
$logs = Join-Path $env:ProgramData "PhoneBridge\logs"

# 2) stop Frame Server so it lets go of the OLD dll
Write-Host "A parar o Frame Server..."
Stop-Service -Name FrameServer -Force -ErrorAction SilentlyContinue
Stop-Service -Name FrameServerMonitor -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# 3) copy DLL to Program Files and let LOCAL SERVICE (S-1-5-19) read+execute it
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Force $Dll (Join-Path $dest "phonebridge_mediasource.dll")
icacls $dest /grant "*S-1-5-19:(OI)(CI)(RX)" /T | Out-Null

# 4) log folder that LOCAL SERVICE may write to
New-Item -ItemType Directory -Force -Path $logs | Out-Null
icacls $logs /grant "*S-1-5-19:(OI)(CI)(M)" | Out-Null

# 5) register the COM server (writes HKLM)
$target = Join-Path $dest "phonebridge_mediasource.dll"
$p = Start-Process -FilePath "regsvr32.exe" -ArgumentList "/s `"$target`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Host "ERRO: regsvr32 falhou (codigo $($p.ExitCode)). Provavelmente a DLL nao exporta DllRegisterServer." -ForegroundColor Red; exit 1 }

$key = "HKLM:\SOFTWARE\Classes\CLSID\{E6B65C58-4D2A-4C20-9F16-368798135CC4}\InprocServer32"
$val = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue)."(default)"
Write-Host "Registo HKLM -> $val"

Start-Service -Name FrameServer -ErrorAction SilentlyContinue
Write-Host "PRONTO. Agora corra o phonebridge_service.exe como administrador." -ForegroundColor Green
Write-Host "Log: $logs\phonebridge_camera.log"
