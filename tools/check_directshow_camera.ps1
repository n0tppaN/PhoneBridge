# Read-only registration report plus actual ICreateDevEnum/pin diagnostic.
# Run in PowerShell x64. --run is optional and creates a temporary Null Renderer graph.
param([string]$DiagnosticExe = '', [switch]$RunGraph)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use uma PowerShell x64 para a câmara DirectShow x64.' }
$mfClsid = '{E6B65C58-4D2A-4C20-9F16-368798135CC4}'
$dsClsid = '{497D53E0-1D46-4E4B-A959-88A413139466}'
$category = '{860BB310-5D01-11D0-BD3B-00A0C911CE86}'
foreach ($source in @(@('Media Foundation', $mfClsid), @('DirectShow', $dsClsid))) {
    $path = "HKLM:\SOFTWARE\Classes\CLSID\$($source[1])\InprocServer32"
    if (Test-Path $path) {
        $dll = (Get-ItemProperty $path).'(default)'
        $exists = $false
        if ($dll) { $exists = Test-Path -LiteralPath $dll -PathType Leaf }
        Write-Host "$($source[0]) COM: $dll (ficheiro existe: $exists)"
    } else { Write-Warning "$($source[0]) COM ausente" }
}
$instance = "HKLM:\SOFTWARE\Classes\CLSID\$category\Instance\$dsClsid"
Write-Host "DirectShow VideoInputDeviceCategory: $(Test-Path $instance)"
Write-Host 'Chaves de registo não provam enumeração/ativação: o teste abaixo usa ICreateDevEnum.'
if (-not $DiagnosticExe) {
    $root = Split-Path -Parent $PSScriptRoot
    $found = Get-ChildItem (Join-Path $root 'windows\build') -Recurse -Filter 'phonebridge_dshow_diagnostic.exe' -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($found) { $DiagnosticExe = $found.FullName }
}
if (-not $DiagnosticExe -or -not (Test-Path -LiteralPath $DiagnosticExe)) {
    Write-Warning 'Falta phonebridge_dshow_diagnostic.exe. Compile o target e/ou passe -DiagnosticExe; enumeração COM NÃO foi testada.'
    exit 3
}
if ($RunGraph) { & $DiagnosticExe --run } else { & $DiagnosticExe }
$code = $LASTEXITCODE
Write-Host "Resultado: $code (0=enumeração/interfaces OK; 1=falha COM/contrato; 2=PhoneBridge ausente; 3=teste indisponível)."
if ($RunGraph) { Write-Host 'Null Renderer verifica conexão/lifecycle, não pixels nem compatibilidade final OBS.' }
exit $code
