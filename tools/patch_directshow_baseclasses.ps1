# Corrige duas declarações antigas das DirectShow BaseClasses para MSVC/VS 2022+.
# Executar numa PowerShell normal, antes de configurar o CMake.
param(
    [Parameter(Mandatory = $true)]
    [string]$BaseClasses
)
$ErrorActionPreference = 'Stop'
$BaseClasses = (Resolve-Path -LiteralPath $BaseClasses).Path
$transip = Join-Path $BaseClasses 'transip.h'
$videoctl = Join-Path $BaseClasses 'videoctl.h'
foreach ($path in @($transip, $videoctl)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Falta: $path" }
}

$text = [IO.File]::ReadAllText($transip)
$old = '__out_opt IMediaSample * CTransInPlaceFilter::Copy(IMediaSample *pSource);'
$new = '__out_opt IMediaSample * Copy(IMediaSample *pSource);'
if ($text.Contains($old)) {
    [IO.File]::WriteAllText($transip, $text.Replace($old, $new), [Text.UTF8Encoding]::new($false))
    Write-Host "Corrigido transip.h"
} elseif (-not $text.Contains($new)) {
    throw "Não encontrei a declaração esperada em $transip"
} else { Write-Host "transip.h já estava corrigido" }

$text = [IO.File]::ReadAllText($videoctl)
$old = 'virtual CAggDirectDraw::~CAggDirectDraw() { };'
$new = 'virtual ~CAggDirectDraw() { };'
if ($text.Contains($old)) {
    [IO.File]::WriteAllText($videoctl, $text.Replace($old, $new), [Text.UTF8Encoding]::new($false))
    Write-Host "Corrigido videoctl.h"
} elseif (-not $text.Contains($new)) {
    throw "Não encontrei a declaração esperada em $videoctl"
} else { Write-Host "videoctl.h já estava corrigido" }

Write-Host 'BaseClasses preparadas para VS 2022+/MSVC atual.' -ForegroundColor Green
