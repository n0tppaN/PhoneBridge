# Installs the separate Media Foundation and DirectShow COM camera sources.
# Administrator PowerShell; close OBS/other camera clients before replacing DLLs.
param([string]$Dll = "", [string]$DirectShowDll = "", [switch]$MediaFoundationOnly)
$ErrorActionPreference = 'Stop'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Abra o PowerShell como ADMINISTRADOR.'
}
if (-not [Environment]::Is64BitProcess) { throw 'Execute este script numa PowerShell x64.' }
function Find-CameraDll([string]$Path, [string]$Name) {
    if (-not $Path) {
        $build = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
        $found = Get-ChildItem $build -Recurse -Filter $Name -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($found) { $Path = $found.FullName }
    }
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Falta $Name. Compile primeiro; veja docs/directshow-compatibility.md."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}
$Dll = Find-CameraDll $Dll 'phonebridge_mediasource.dll'
if (-not $MediaFoundationOnly) { $DirectShowDll = Find-CameraDll $DirectShowDll 'phonebridge_directshow.dll' }
$dest = Join-Path $env:ProgramFiles 'PhoneBridge'
$logs = Join-Path $env:ProgramData 'PhoneBridge\logs'
$regsvr = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$names = @('phonebridge_mediasource.dll')
$sources = @($Dll)
if (-not $MediaFoundationOnly) { $names += 'phonebridge_directshow.dll'; $sources += $DirectShowDll }
Write-Host 'Feche OBS, browsers e outras aplicações de vídeo; a DLL DirectShow fica carregada nesses processos.'
# Restart service/writer after install to create the shared mapping with its new read ACL.
try {
    Stop-Service FrameServerMonitor -Force -ErrorAction SilentlyContinue
    Stop-Service FrameServer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    for ($i = 0; $i -lt $names.Count; ++$i) {
        $target = Join-Path $dest $names[$i]
        if ($sources[$i] -ne $target) { Copy-Item -LiteralPath $sources[$i] -Destination $target -Force }
    }
    icacls $dest /grant '*S-1-5-19:(OI)(CI)(RX)' /T | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls da pasta de DLLs falhou.' }
    New-Item -ItemType Directory -Force -Path $logs | Out-Null
    icacls $logs /grant '*S-1-5-19:(OI)(CI)(M)' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'icacls dos logs falhou.' }
    foreach ($name in $names) {
        $target = Join-Path $dest $name
        $p = Start-Process -FilePath $regsvr -ArgumentList "/s `"$target`"" -Wait -PassThru
        if ($p.ExitCode -ne 0) {
            throw "regsvr32 de $name falhou ($($p.ExitCode)). Verifique exports, x64 e VC++ Runtime x64 para DirectShow. Instalação pode estar parcial."
        }
        Write-Host "Registada: $target"
    }
} finally {
    Start-Service FrameServer -ErrorAction SilentlyContinue
}
Write-Host 'PRONTO. Reinicie phonebridge_service.exe (administrador) e depois OBS x64.' -ForegroundColor Green
if (-not $MediaFoundationOnly) { Write-Host 'OBS > Video Capture Device > PhoneBridge (DirectShow), YUY2 1920x1080, 30 fps.' }
Write-Host 'Valide com tools/check_directshow_camera.ps1 e phonebridge_dshow_diagnostic.exe --run.'
