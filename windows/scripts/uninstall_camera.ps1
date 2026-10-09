# Removes both user-mode camera COM sources, not the service/app directory.
$ErrorActionPreference = 'Stop'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Execute como ADMINISTRADOR.' }
if (-not [Environment]::Is64BitProcess) { throw 'Execute numa PowerShell x64.' }
$dest = Join-Path $env:ProgramFiles 'PhoneBridge'
$regsvr = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
Write-Host 'Feche OBS e todas as aplicações de vídeo antes de desinstalar.'
try {
    Stop-Service FrameServerMonitor -Force -ErrorAction SilentlyContinue
    Stop-Service FrameServer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    foreach ($name in @('phonebridge_directshow.dll', 'phonebridge_mediasource.dll')) {
        $dll = Join-Path $dest $name
        if (Test-Path -LiteralPath $dll) {
            $p = Start-Process -FilePath $regsvr -ArgumentList "/u /s `"$dll`"" -Wait -PassThru
            if ($p.ExitCode -ne 0) { throw "Falhou unregister de $name ($($p.ExitCode)); DLL preservada." }
            Remove-Item -LiteralPath $dll -Force
        }
    }
} finally {
    Start-Service FrameServer -ErrorAction SilentlyContinue
}
Write-Host 'Fontes de câmara removidas; serviço, aplicação e logs preservados.' -ForegroundColor Green
