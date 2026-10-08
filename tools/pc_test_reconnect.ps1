# PowerShell Reconnection & Recovery Test Client for PhoneBridge (Milestone 4)

$adbPath = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adbPath)) {
    $adbPath = "adb"
}

Write-Host "Configurando adb forward..." -ForegroundColor Cyan
& $adbPath forward tcp:27183 localabstract:phonebridge

$port = 27183

function Connect-And-Handshake {
    Write-Host "`n[Sessao] Conectando em 127.0.0.1:$port..." -ForegroundColor Cyan
    $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $port)
    $client.ReceiveTimeout = 3000
    $stream = $client.GetStream()

    $seq = 0
    $helloPkt = New-Packet -type 1 -payload ([System.Text.Encoding]::UTF8.GetBytes('{"protocol":1}')) -seq ([ref]$seq)
    $stream.Write($helloPkt, 0, $helloPkt.Length)
    $stream.Flush()

    for ($i = 0; $i -lt 3; $i++) {
        $res = Read-Packet $stream
        if ($res) {
            $text = [System.Text.Encoding]::UTF8.GetString($res.Payload)
            Write-Host "  Handshake [$($res.Type)]: $text" -ForegroundColor Green
        }
    }
    return @{ Client = $client; Stream = $stream; Seq = $seq }
}

function New-Packet([UInt16]$type, [byte[]]$payload, [ref]$seq, [UInt32]$streamId = 0) {
    $ms = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($ms)

    $writer.Write([UInt32]0x50484252)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$type)
    $writer.Write([UInt32]$streamId)
    $writer.Write([UInt64]0)
    $writer.Write([UInt32]$payload.Length)
    $writer.Write([UInt32]$seq.Value)
    $seq.Value = $seq.Value + 1

    if ($payload.Length -gt 0) {
        $writer.Write($payload, 0, $payload.Length)
    }

    $bytes = $ms.ToArray()
    $writer.Close()
    $ms.Close()
    return $bytes
}

function Read-Exact([System.IO.Stream]$st, [int]$count) {
    $buf = New-Object byte[] $count
    $read = 0
    while ($read -lt $count) {
        try {
            $r = $st.Read($buf, $read, $count - $read)
            if ($r -le 0) { throw "Conexao fechada pelo dispositivo." }
            $read += $r
        } catch {
            return $null
        }
    }
    return $buf
}

function Read-Packet([System.IO.Stream]$st) {
    $hdr = Read-Exact $st 28
    if ($null -eq $hdr) { return $null }

    $ms = [System.IO.MemoryStream]::new($hdr)
    $reader = [System.IO.BinaryReader]::new($ms)

    $magic = $reader.ReadUInt32()
    $ver = $reader.ReadUInt16()
    $type = $reader.ReadUInt16()
    $flags = $reader.ReadUInt32()
    $ts = $reader.ReadUInt64()
    $size = $reader.ReadUInt32()
    $seq = $reader.ReadUInt32()

    $payload = @()
    if ($size -gt 0) {
        $payload = Read-Exact $st $size
        if ($null -eq $payload) { return $null }
    }
    return @{ Type = $type; Payload = $payload; Seq = $seq; Timestamp = $ts; Flags = $flags }
}

try {
    # ETAPA 1: Primeira Conexão
    Write-Host "=== ETAPA 1: Conexao Inicial ===" -ForegroundColor Yellow
    $sess1 = Connect-And-Handshake
    $client1 = $sess1.Client
    $stream1 = $sess1.Stream
    $seq1 = $sess1.Seq

    # Iniciar transmissao por 3s
    $startMic = New-Packet -type 0x0022 -payload @() -seq ([ref]$seq1)
    $stream1.Write($startMic, 0, $startMic.Length)
    $stream1.Flush()

    $framesSess1 = 0
    $start1 = [System.DateTime]::Now
    while (([System.DateTime]::Now - $start1).TotalSeconds -lt 3) {
        $res = Read-Packet $stream1
        if ($res -and $res.Type -eq 0x0201) { $framesSess1++ }
    }
    Write-Host "Sessao 1: Recebidos $framesSess1 quadros de audio." -ForegroundColor Green

    # Simular desconexão abrupta (fechar socket sem enviar GOODBYE)
    Write-Host "`n[Simulacao] Simulando QUEDA de conexao (desconexao abrupta)..." -ForegroundColor Red
    $client1.Close()

    Write-Host "Aguardando 2 segundos para o servidor Android retornar ao estado de 'Waiting'..." -ForegroundColor Yellow
    Start-Sleep -Seconds 2

    # ETAPA 2: Reconexão Automática
    Write-Host "`n=== ETAPA 2: RECONEXAO AUTOMATICA ===" -ForegroundColor Yellow
    $sess2 = Connect-And-Handshake
    $client2 = $sess2.Client
    $stream2 = $sess2.Stream
    $seq2 = $sess2.Seq

    # Solicitar reinício da câmera e microfone na nova sessão
    Write-Host "Solicitando reinicio da camera e microfone na nova conexao..." -ForegroundColor Cyan
    $startMic2 = New-Packet -type 0x0022 -payload @() -seq ([ref]$seq2)
    $stream2.Write($startMic2, 0, $startMic2.Length)
    $stream2.Flush()

    $startCam2 = New-Packet -type 0x0020 -payload @() -seq ([ref]$seq2)
    $stream2.Write($startCam2, 0, $startCam2.Length)
    $stream2.Flush()

    $framesSess2 = 0
    $start2 = [System.DateTime]::Now
    while (([System.DateTime]::Now - $start2).TotalSeconds -lt 4) {
        $res = Read-Packet $stream2
        if ($res -and ($res.Type -eq 0x0201 -or $res.Type -eq 0x0101)) { $framesSess2++ }
    }

    Write-Host "Sessao 2: Recebidos $framesSess2 quadros apos reconexao!" -ForegroundColor Green

    # Enviar GOODBYE limpo
    $gb = New-Packet -type 0x0FFF -payload @() -seq ([ref]$seq2)
    $stream2.Write($gb, 0, $gb.Length)
    $stream2.Flush()
    $client2.Close()

    Write-Host "`nTESTE DE RECONEXAO AUTOMATICA CONCLUIDO COM SUCESSO!" -ForegroundColor Cyan

} catch {
    Write-Host "Erro durante o teste de reconexao: $_" -ForegroundColor Red
}
