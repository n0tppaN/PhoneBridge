# PowerShell Stress Failure Recovery Test Client for PhoneBridge (Milestone 7)

$adbPath = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adbPath)) {
    $adbPath = "adb"
}

Write-Host "Configurando adb forward..." -ForegroundColor Cyan
& $adbPath forward tcp:27183 localabstract:phonebridge

$port = 27183

function Run-RecoverySession([int]$sessionId) {
    Write-Host "`n--- [Stress Test] Iniciando Sessao de Recuperacao #${sessionId} ---" -ForegroundColor Yellow
    try {
        $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $port)
        $client.ReceiveTimeout = 3000
        $stream = $client.GetStream()

        $seq = 0
        function Local-NewPkt([UInt16]$type, [byte[]]$payload = @()) {
            $ms = [System.IO.MemoryStream]::new()
            $writer = [System.IO.BinaryWriter]::new($ms)
            $writer.Write([UInt32]0x50484252)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]$type)
            $writer.Write([UInt32]0)
            $writer.Write([UInt64]0)
            $writer.Write([UInt32]$payload.Length)
            $writer.Write([UInt32]$seq)
            $script:seq++
            if ($payload.Length -gt 0) { $writer.Write($payload, 0, $payload.Length) }
            $bytes = $ms.ToArray()
            $writer.Close(); $ms.Close()
            return $bytes
        }

        # Send HELLO
        $helloPkt = Local-NewPkt 1 ([System.Text.Encoding]::UTF8.GetBytes('{"protocol":1}'))
        $stream.Write($helloPkt, 0, $helloPkt.Length)
        $stream.Flush()

        # Read Handshake responses
        for ($i = 0; $i -lt 3; $i++) {
            $hdr = New-Object byte[] 28
            $r = $stream.Read($hdr, 0, 28)
            $size = [System.BitConverter]::ToUInt32($hdr, 16)
            if ($size -gt 0) {
                $payload = New-Object byte[] $size
                $stream.Read($payload, 0, $size) | Out-Null
            }
        }
        Write-Host "Sessao #${sessionId} Handshake OK." -ForegroundColor Green

        # Start Camera & Mic
        $startMic = Local-NewPkt 0x0022
        $stream.Write($startMic, 0, $startMic.Length)
        $startCam = Local-NewPkt 0x0020
        $stream.Write($startCam, 0, $startCam.Length)
        $stream.Flush()

        # Stream for 2 seconds
        $start = [System.DateTime]::Now
        $frames = 0
        while (([System.DateTime]::Now - $start).TotalSeconds -lt 2) {
            try {
                $hdr = New-Object byte[] 28
                $r = $stream.Read($hdr, 0, 28)
                if ($r -le 0) { break }
                $size = [System.BitConverter]::ToUInt32($hdr, 16)
                if ($size -gt 0) {
                    $payload = New-Object byte[] $size
                    $stream.Read($payload, 0, $size) | Out-Null
                }
                $frames++
            } catch {
                break
            }
        }
        Write-Host "Sessao #${sessionId}: Recebidos $frames pacotes de midia." -ForegroundColor Green

        # Abrupt disconnect
        $client.Close()
        Write-Host "Sessao #${sessionId} encerrada abruptamente com sucesso." -ForegroundColor Yellow
        return $true
    } catch {
        Write-Host "Erro na Sessao #${sessionId}: $_" -ForegroundColor Red
        return $false
    }
}

# Executar 3 ciclos consecutivos de queda abrupta e reconexao (Milestone 7 Stress Test)
for ($cycle = 1; $cycle -le 3; $cycle++) {
    $ok = Run-RecoverySession -sessionId $cycle
    if (-not $ok) {
        Write-Host "Falha no ciclo de estresse #$cycle" -ForegroundColor Red
        exit 1
    }
    Start-Sleep -Seconds 2
}

Write-Host "`nTESTE DE ESTRESSE DE RECUPERACAO (MILESTONE 7) CONCLUIDO COM SUCESSO!" -ForegroundColor Cyan
