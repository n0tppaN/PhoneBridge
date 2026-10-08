# PowerShell test client for PhoneBridge (Milestone 0)

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
    $stream = $client.GetStream()
} catch {
    Write-Host "Erro ao conectar. Verifique se o aplicativo PhoneBridge esta rodando no dispositivo e se voce clicou em 'Start'." -ForegroundColor Red
    exit 1
}

$global:seq = 0

function New-Packet([UInt16]$type, [byte[]]$payload) {
    $ms = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($ms)

    $writer.Write([UInt32]0x50484252) # Magic 'PHBR'
    $writer.Write([UInt16]1)          # Version
    $writer.Write([UInt16]$type)      # Type
    $writer.Write([UInt32]0)          # Flags
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
        $r = $st.Read($buf, $read, $count - $read)
        if ($r -le 0) { throw "Conexao fechada pelo dispositivo." }
        $read += $r
    }
    return $buf
}

function Read-Packet([System.IO.Stream]$st) {
    $hdr = Read-Exact $st 28
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
    }
    return @{ Type = $type; Payload = $payload }
}

try {
    # 1. Enviar HELLO
    $helloPayload = [System.Text.Encoding]::UTF8.GetBytes('{"protocol":1}')
    $helloPkt = New-Packet -type 1 -payload $helloPayload
    $stream.Write($helloPkt, 0, $helloPkt.Length)
    $stream.Flush()

    # 2. Ler HELLO_ACK, DEVICE_INFO, CAPABILITIES
    $names = @{ 1="HELLO"; 2="HELLO_ACK"; 3="DEVICE_INFO"; 4="CAPABILITIES"; 0x0F00="HEARTBEAT"; 0x0F01="HEARTBEAT_ACK"; 0x0FFF="GOODBYE" }

    for ($i = 0; $i -lt 3; $i++) {
        $res = Read-Packet $stream
        $typeName = if ($names.ContainsKey($res.Type)) { $names[$res.Type] } else { "0x{0:X}" -f $res.Type }
        $text = [System.Text.Encoding]::UTF8.GetString($res.Payload)
        Write-Host "[$typeName] $text" -ForegroundColor Green
    }

    # 3. Enviar Heartbeats
    for ($hb = 0; $hb -lt 5; $hb++) {
        Start-Sleep -Seconds 1
        $hbPayload = [System.Text.Encoding]::UTF8.GetBytes("$hb")
        $hbPkt = New-Packet -type 0x0F00 -payload $hbPayload
        $stream.Write($hbPkt, 0, $hbPkt.Length)
        $stream.Flush()

        $res = Read-Packet $stream
        $typeName = if ($names.ContainsKey($res.Type)) { $names[$res.Type] } else { "0x{0:X}" -f $res.Type }
        $text = [System.Text.Encoding]::UTF8.GetString($res.Payload)
        Write-Host "[$typeName] $text" -ForegroundColor Yellow
    }

    # 4. Enviar GOODBYE e fechar
    $gbPkt = New-Packet -type 0x0FFF -payload @()
    $stream.Write($gbPkt, 0, $gbPkt.Length)
    $stream.Flush()

    Write-Host "`nTeste concluido com sucesso! O dispositivo respondeu corretamente." -ForegroundColor Cyan
} catch {
    Write-Host "Erro durante a comunicacao: $_" -ForegroundColor Red
} finally {
    $client.Close()
}
