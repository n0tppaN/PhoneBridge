# PowerShell Simultaneous Camera + Microphone Test Client for PhoneBridge (Milestone 3)

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
    $client.ReceiveTimeout = 3000 # 3 segundos de timeout maximo para nao travar
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

    # 2. Iniciar Câmera E Microfone simultaneamente
    Write-Host "`nSolicitando transmissao SIMULTANEA de Camera H.264 e Microfone PCM..." -ForegroundColor Cyan

    $startMicPkt = New-Packet -type 0x0022 -payload @() # START_MICROPHONE
    $stream.Write($startMicPkt, 0, $startMicPkt.Length)
    $stream.Flush()

    $startCamPkt = New-Packet -type 0x0020 -payload @() # START_CAMERA
    $stream.Write($startCamPkt, 0, $startCamPkt.Length)
    $stream.Flush()

    # 3. Gravar por 10 segundos
    $pcmMs = [System.IO.MemoryStream]::new()
    $h264Ms = [System.IO.MemoryStream]::new()
    $recordTimeSeconds = 10
    $startTime = [System.DateTime]::Now

    $audioFramesCount = 0
    $videoFramesCount = 0
    $keyframesCount = 0

    Write-Host "Recebendo audio e video simultaneos por $recordTimeSeconds segundos..." -ForegroundColor Yellow

    while (([System.DateTime]::Now - $startTime).TotalSeconds -lt $recordTimeSeconds) {
        $res = Read-Packet $stream
        if ($null -eq $res) {
            continue
        }
        if ($res.Type -eq 0x0201) { # AUDIO_FRAME
            $pcmMs.Write($res.Payload, 0, $res.Payload.Length)
            $audioFramesCount++
        } elseif ($res.Type -eq 0x0101) { # VIDEO_FRAME
            $h264Ms.Write($res.Payload, 0, $res.Payload.Length)
            $videoFramesCount++
            if (($res.Flags -band 0x0100) -ne 0) { # FLAG_KEYFRAME
                $keyframesCount++
            }
        } elseif ($res.Type -eq 0x0200 -or $res.Type -eq 0x0100 -or $res.Type -eq 0x0011) {
            $cfgText = [System.Text.Encoding]::UTF8.GetString($res.Payload)
            Write-Host "[CONFIG] $cfgText" -ForegroundColor Magenta
        }
    }

    $elapsedSec = ([System.DateTime]::Now - $startTime).TotalSeconds
    $rawPcm = $pcmMs.ToArray()
    $rawH264 = $h264Ms.ToArray()

    $avgVideoFps = if ($elapsedSec -gt 0) { [Math]::Round($videoFramesCount / $elapsedSec, 1) } else { 0 }
    $avgVideoBitrateKbps = if ($elapsedSec -gt 0) { [Math]::Round(($rawH264.Length * 8 / $elapsedSec) / 1000, 1) } else { 0 }
    $avgAudioBitrateKbps = if ($elapsedSec -gt 0) { [Math]::Round(($rawPcm.Length * 8 / $elapsedSec) / 1000, 1) } else { 0 }

    Write-Host "`nEstatísticas da Transmissao Simultanea ($([Math]::Round($elapsedSec, 1))s):" -ForegroundColor Cyan
    Write-Host "  VIDEO: $videoFramesCount quadros ($avgVideoFps FPS, $keyframesCount Keyframes) - $avgVideoBitrateKbps kbps" -ForegroundColor Green
    Write-Host "  AUDIO: $audioFramesCount quadros ($($rawPcm.Length) bytes PCM) - $avgAudioBitrateKbps kbps" -ForegroundColor Green

    # 4. Parar transmissao
    Write-Host "`nParando transmissao de camera e microfone..." -ForegroundColor Cyan
    $stopCamPkt = New-Packet -type 0x0021 -payload @()
    $stream.Write($stopCamPkt, 0, $stopCamPkt.Length)
    $stream.Flush()

    $stopMicPkt = New-Packet -type 0x0023 -payload @()
    $stream.Write($stopMicPkt, 0, $stopMicPkt.Length)
    $stream.Flush()

    # 5. Salvar arquivos
    $wavPath = Join-Path $PSScriptRoot "output_simultaneous_audio.wav"
    $h264Path = Join-Path $PSScriptRoot "output_simultaneous_video.h264"

    # Salvar WAV Header
    $wavMs = [System.IO.MemoryStream]::new()
    $w = [System.IO.BinaryWriter]::new($wavMs)
    $sampleRate = 48000
    $channels = 1
    $bitsPerSample = 16
    $byteRate = $sampleRate * $channels * ($bitsPerSample / 8)
    $blockAlign = $channels * ($bitsPerSample / 8)

    $w.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
    $w.Write([UInt32](36 + $rawPcm.Length))
    $w.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
    $w.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
    $w.Write([UInt32]16)
    $w.Write([UInt16]1) # PCM
    $w.Write([UInt16]$channels)
    $w.Write([UInt32]$sampleRate)
    $w.Write([UInt32]$byteRate)
    $w.Write([UInt16]$blockAlign)
    $w.Write([UInt16]$bitsPerSample)
    $w.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
    $w.Write([UInt32]$rawPcm.Length)
    $w.Write($rawPcm, 0, $rawPcm.Length)

    [System.IO.File]::WriteAllBytes($wavPath, $wavMs.ToArray())
    [System.IO.File]::WriteAllBytes($h264Path, $rawH264)
    $w.Close()

    Write-Host "Arquivos salvos com sucesso:" -ForegroundColor Cyan
    Write-Host "  Audio: $wavPath" -ForegroundColor Yellow
    Write-Host "  Video: $h264Path" -ForegroundColor Yellow

    # Enviar GOODBYE
    $gbPkt = New-Packet -type 0x0FFF -payload @()
    $stream.Write($gbPkt, 0, $gbPkt.Length)
    $stream.Flush()

} catch {
    Write-Host "Erro durante o teste de transmissao simultanea: $_" -ForegroundColor Red
} finally {
    $client.Close()
}
