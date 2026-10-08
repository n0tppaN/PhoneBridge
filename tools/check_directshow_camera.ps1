# Diagnóstico de enumeração DirectShow/Media Foundation.
# Não altera o sistema. Executar numa PowerShell x64.
$ErrorActionPreference = 'Stop'

$mfClsid = '{E6B65C58-4D2A-4C20-9F16-368798135CC4}'
$dsCategory = '{860BB310-5D01-11D0-BD3B-00A0C911CE86}' # CLSID_VideoInputDeviceCategory
$mfCategory = '{E5323777-F976-4F5B-9B55-B94699C46E44}' # KSCATEGORY_VIDEO_CAMERA

Write-Host "=== PhoneBridge camera registration ==="
$clsidPath = "HKLM:\SOFTWARE\Classes\CLSID\$mfClsid\InprocServer32"
if (Test-Path $clsidPath) {
    $p = Get-ItemProperty $clsidPath
    Write-Host "Media Foundation COM CLSID: OK"
    Write-Host "  DLL: $($p.'(default)')"
} else {
    Write-Warning "Media Foundation COM CLSID não está registado."
}

$instancePath = "HKLM:\SOFTWARE\Classes\CLSID\$dsCategory\Instance\$mfClsid"
if (Test-Path $instancePath) {
    Write-Host "DirectShow video-input category: PRESENT"
} else {
    Write-Host "DirectShow video-input category: AUSENTE"
    Write-Host "Isto é esperado enquanto o filtro DirectShow não estiver implementado."
}

$mfInstancePath = "HKLM:\SOFTWARE\Classes\CLSID\$mfCategory\Instance\$mfClsid"
if (Test-Path $mfInstancePath) {
    Write-Host "KSCATEGORY_VIDEO_CAMERA registration: PRESENT"
} else {
    Write-Host "KSCATEGORY_VIDEO_CAMERA registration: not represented as a legacy DirectShow filter"
}

Write-Host ""
Write-Host "Interpretação: se a Câmara do Windows funciona e a categoria DirectShow está AUSENTE, OBS não terá PhoneBridge no dropdown."
