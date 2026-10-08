# PowerShell Video Streaming Test Client for PhoneBridge (Milestone 2)

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

    # 2. Solicitar inicio da câmera (START_CAMERA = 0x0020)
    Write-Host "`nSolicitando transmissao da camera H.264 (START_CAMERA)..." -ForegroundColor Cyan
    $startCamPkt = New-Packet -type 0x0020 -payload @()
    $stream.Write($startCamPkt, 0, $startCamPkt.Length)
    $stream.Flush()

    # 3. Gravar 5 segundos de pacotes VIDEO_FRAME (0x0101)
    $h264Ms = [System.IO.MemoryStream]::new()
    $recordTimeSeconds = 5
    $startTime = [System.DateTime]::Now
    $framesCount = 0
    $keyframesCount = 0

    Write-Host "Recebendo video do celular por $recordTimeSeconds segundos..." -ForegroundColor Yellow

    while (([System.DateTime]::Now - $startTime).TotalSeconds -lt $recordTimeSeconds) {
        $res = Read-Packet $stream
        if ($null -eq $res) { continue }
        if ($res.Type -eq 0x0101) { # VIDEO_FRAME
            $h264Ms.Write($res.Payload, 0, $res.Payload.Length)
            $framesCount++
            if (($res.Flags -band 0x0100) -ne 0) { # FLAG_KEYFRAME
                $keyframesCount++
            }
        } elseif ($res.Type -eq 0x0100) { # VIDEO_CONFIG
            $cfgText = [System.Text.Encoding]::UTF8.GetString($res.Payload)
            Write-Host "[VIDEO_CONFIG] $cfgText" -ForegroundColor Magenta
        } elseif ($res.Type -eq 0x0011) { # CONFIG_RESPONSE
            $cfgText = [System.Text.Encoding]::UTF8.GetString($res.Payload)
            Write-Host "[CONFIG_RESPONSE] $cfgText" -ForegroundColor Magenta
        }
    }

    $rawH264 = $h264Ms.ToArray()
    Write-Host "Recebidos $framesCount quadros de video ($keyframesCount Keyframes / $($rawH264.Length) bytes)." -ForegroundColor Green

    # 4. Enviar STOP_CAMERA (0x0021)
    Write-Host "Parando camera (STOP_CAMERA)..." -ForegroundColor Cyan
    $stopCamPkt = New-Packet -type 0x0021 -payload @()
    $stream.Write($stopCamPkt, 0, $stopCamPkt.Length)
    $stream.Flush()

    # 5. Salvar arquivo H.264
    $h264Path = Join-Path $PSScriptRoot "output_video.h264"
    [System.IO.File]::WriteAllBytes($h264Path, $rawH264)

    Write-Host "`nArquivo de video H.264 salvo com sucesso em:" -ForegroundColor Cyan
    Write-Host "$h264Path" -ForegroundColor Yellow

    # Enviar GOODBYE
    $gbPkt = New-Packet -type 0x0FFF -payload @()
    $stream.Write($gbPkt, 0, $gbPkt.Length)
    $stream.Flush()

} catch {
    Write-Host "Erro durante o teste de video: $_" -ForegroundColor Red
} finally {
    $client.Close()
}
