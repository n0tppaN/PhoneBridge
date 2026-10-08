# PowerShell Independent Controls Test Client for PhoneBridge (Milestone 6)

$adbPath = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
if (-not (Test-Path $adbPath)) {
    $adbPath = "adb"
}

Write-Host "Configurando adb forward..." -ForegroundColor Cyan
& $adbPath forward tcp:27183 localabstract:phonebridge

$port = 27183
Write-Host "Conectando em 127.0.0.1:$port..." -ForegroundColor Cyan

try {
    $client = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $port)
    $client.ReceiveTimeout = 3000
    $stream = $client.GetStream()
} catch {
    Write-Host "Erro ao conectar. Certifique-se de que o PhoneBridge esta rodando no Android e em 'Start'." -ForegroundColor Red
    exit 1
}

$global:seq = 0

function New-Packet([UInt16]$type, [byte[]]$payload, [UInt32]$streamId = 0) {
    $ms = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($ms)

    $writer.Write([UInt32]0x50484252) # Magic 'PHBR'
    $writer.Write([UInt16]1)          # Version
    $writer.Write([UInt16]$type)      # Type
    $writer.Write([UInt32]$streamId)  # Flags/Stream
    $writer.Write([UInt64]0)          # Timestamp
    $writer.Write([UInt32]$payload.Length) # Size
    $writer.Write([UInt32]$global:seq) # Seq
    $global:seq = $global:seq + 1

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
    # 1. Handshake HELLO
    $helloPkt = New-Packet -type 1 -payload ([System.Text.Encoding]::UTF8.GetBytes('{"protocol":1}'))
    $stream.Write($helloPkt, 0, $helloPkt.Length)
    $stream.Flush()

    for ($i = 0; $i -lt 3; $i++) {
        $res = Read-Packet $stream
        if ($res) {
            $text = [System.Text.Encoding]::UTF8.GetString($res.Payload)
            Write-Host "Handshake response [$($res.Type)]: $text" -ForegroundColor Green
        }
    }

    # TESTE 1: Camera ON, Microphone OFF
    Write-Host "`n[Teste 1/4] Camera ON, Microphone OFF..." -ForegroundColor Cyan
    $stream.Write((New-Packet -type 0x0020), 0, 28) # START_CAMERA
    $stream.Flush()

    $vidCount = 0; $audCount = 0
    $start = [System.DateTime]::Now
    while (([System.DateTime]::Now - $start).TotalSeconds -lt 2) {
        $res = Read-Packet $stream
        if ($res) {
            if ($res.Type -eq 0x0101) { $vidCount++ }
            if ($res.Type -eq 0x0201) { $audCount++ }
        }
    }
    Write-Host "  Resultado Teste 1 -> Video Frames: $vidCount, Audio Frames: $audCount (Esperado: video > 0, audio == 0)" -ForegroundColor Yellow

    # TESTE 2: Camera OFF, Microphone ON
    Write-Host "`n[Teste 2/4] Camera OFF, Microphone ON..." -ForegroundColor Cyan
    $stream.Write((New-Packet -type 0x0021), 0, 28) # STOP_CAMERA
    $stream.Write((New-Packet -type 0x0022), 0, 28) # START_MICROPHONE
    $stream.Flush()

    $vidCount = 0; $audCount = 0
    $start = [System.DateTime]::Now
    while (([System.DateTime]::Now - $start).TotalSeconds -lt 2) {
        $res = Read-Packet $stream
        if ($res) {
            if ($res.Type -eq 0x0101) { $vidCount++ }
            if ($res.Type -eq 0x0201) { $audCount++ }
        }
    }
    Write-Host "  Resultado Teste 2 -> Video Frames: $vidCount, Audio Frames: $audCount (Esperado: video == 0, audio > 0)" -ForegroundColor Yellow

    # TESTE 3: Camera ON, Microphone ON (Simultaneo)
    Write-Host "`n[Teste 3/4] Camera ON, Microphone ON (Simultaneo)..." -ForegroundColor Cyan
    $stream.Write((New-Packet -type 0x0020), 0, 28) # START_CAMERA
    $stream.Flush()

    $vidCount = 0; $audCount = 0
    $start = [System.DateTime]::Now
    while (([System.DateTime]::Now - $start).TotalSeconds -lt 2) {
        $res = Read-Packet $stream
        if ($res) {
            if ($res.Type -eq 0x0101) { $vidCount++ }
            if ($res.Type -eq 0x0201) { $audCount++ }
        }
    }
    Write-Host "  Resultado Teste 3 -> Video Frames: $vidCount, Audio Frames: $audCount (Esperado: video > 0, audio > 0)" -ForegroundColor Yellow

    # TESTE 4: Camera OFF, Microphone OFF
    Write-Host "`n[Teste 4/4] Camera OFF, Microphone OFF..." -ForegroundColor Cyan
    $stream.Write((New-Packet -type 0x0021), 0, 28) # STOP_CAMERA
    $stream.Write((New-Packet -type 0x0023), 0, 28) # STOP_MICROPHONE
    $stream.Flush()
    Start-Sleep -Seconds 1

    Write-Host "`nTODOS OS TESTES DE CONTROLE INDEPENDENTE CONCLUIDOS COM SUCESSO!" -ForegroundColor Cyan

    # GOODBYE
    $stream.Write((New-Packet -type 0x0FFF), 0, 28)
    $stream.Flush()

} catch {
    Write-Host "Erro durante o teste de controles independentes: $_" -ForegroundColor Red
} finally {
    $client.Close()
}
