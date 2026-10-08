<# PhoneBridge desktop UI - ASCII source to remain safe in Windows PowerShell encoding. #>
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$serviceCandidates = @(
    (Join-Path $root 'windows\build-v7-main\Release\phonebridge_service.exe'),
    (Join-Path $root 'windows\build\Release\phonebridge_service.exe'),
    (Join-Path $root 'windows\build\x64\Release\phonebridge_service.exe')
)
$servicePath = $serviceCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$adbPath = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
if (-not (Test-Path $adbPath)) { $adbPath = 'adb' }

# Unicode is constructed at runtime, so Windows PowerShell 5 does not decode the source as ANSI.
$Aacute=[char]0x00C1; $aacute=[char]0x00E1; $Atilde=[char]0x00C3; $atilde=[char]0x00E3
$Acirc=[char]0x00C2; $acirc=[char]0x00E2; $Ccedil=[char]0x00C7; $ccedil=[char]0x00E7
$Oacute=[char]0x00D3; $oacute=[char]0x00F3; $uacute=[char]0x00FA; $acute=[char]0x00E9
$eacute=[char]0x00E9; $iacute=[char]0x00ED; $auml=[char]0x00E4
$cameraText = '  C' + $acirc + 'mara virtual'
$modulesText = 'M' + $oacute + 'dulos a transmitir'
$phoneNotFound = 'Telem' + $oacute + 'vel n' + $atilde + 'o ligado'
$usbHint = 'Ligue um dispositivo por USB e ative a depura' + $ccedil + $atilde + 'o USB'
$waiting = 'A aguardar dispositivo'
$available = 'Dispositivo dispon' + $iacute + 'vel'
$startText = 'Iniciar ponte'; $stopText = 'Parar ponte'
$activeText = 'Ativa'; $inactiveText = 'Desligada'; $micActiveText='Ativo'; $micInactiveText='Desligado'

$bg=[Drawing.Color]::FromArgb(10,12,16); $surface=[Drawing.Color]::FromArgb(18,22,28); $surfaceHigh=[Drawing.Color]::FromArgb(26,32,41)
$border=[Drawing.Color]::FromArgb(43,52,68); $text=[Drawing.Color]::FromArgb(243,246,250); $muted=[Drawing.Color]::FromArgb(163,174,189)
$disabled=[Drawing.Color]::FromArgb(107,118,134); $blue=[Drawing.Color]::FromArgb(91,140,255); $green=[Drawing.Color]::FromArgb(61,220,151)
$orange=[Drawing.Color]::FromArgb(255,181,71); $red=[Drawing.Color]::FromArgb(255,92,108)

function New-Label($value,$size,$color) { $l=New-Object Windows.Forms.Label; $l.Text=$value; $l.Font=New-Object Drawing.Font('Segoe UI',$size); $l.ForeColor=$color; $l.BackColor=[Drawing.Color]::Transparent; return $l }
function Set-Rounded($control,[int]$radius) {
    $path=New-Object -TypeName System.Drawing.Drawing2D.GraphicsPath; $d=$radius*2
    $path.AddArc(0,0,$d,$d,180,90); $path.AddArc($control.Width-$d,0,$d,$d,270,90); $path.AddArc($control.Width-$d,$control.Height-$d,$d,$d,0,90); $path.AddArc(0,$control.Height-$d,$d,$d,90,90); $path.CloseFigure(); $control.Region=New-Object -TypeName System.Drawing.Region -ArgumentList $path; $path.Dispose()
}
function Add-RoundedBorder($panel,[int]$radius=22) {
    # Use only the native Region. Custom Paint handlers are intentionally
    # avoided because Windows PowerShell can render them as red-X controls.
    Set-Rounded $panel $radius
}
function Get-DeviceInfo {
    try { $lines=& $adbPath devices 2>$null; $row=$lines | Where-Object { $_ -match '^\S+\s+device\s*$' } | Select-Object -First 1; if($row){$serial=($row -split '\s+')[0]; $model=(& $adbPath -s $serial shell getprop ro.product.model 2>$null | Out-String).Trim(); if(!$model){$model=$serial}; return @{Connected=$true;Name=$model;Serial=$serial} } } catch {}
    return @{Connected=$false;Name=$phoneNotFound;Serial=''}
}

$form=New-Object Windows.Forms.Form; $form.Text='PhoneBridge'; $form.ClientSize=New-Object Drawing.Size(620,720); $form.StartPosition='CenterScreen'; $form.BackColor=$bg; $form.ForeColor=$text; $form.FormBorderStyle='FixedSingle'; $form.MaximizeBox=$false
$title=New-Label 'PhoneBridge' 22 $text; $title.Font=New-Object Drawing.Font('Segoe UI',22,[Drawing.FontStyle]::Bold); $title.Location=New-Object Drawing.Point(28,24); $title.AutoSize=$true; $form.Controls.Add($title)
$statusPanel=New-Object Windows.Forms.Panel; $statusPanel.Location=New-Object Drawing.Point(420,24); $statusPanel.Size=New-Object Drawing.Size(170,40); $statusPanel.BackColor=$surface; Add-RoundedBorder $statusPanel 14; $form.Controls.Add($statusPanel)
$statusDot=New-Label '●' 12 $disabled; $statusDot.Location=New-Object Drawing.Point(12,10); $statusDot.AutoSize=$true; $statusPanel.Controls.Add($statusDot); $statusText=New-Label 'Parado' 10 $muted; $statusText.Location=New-Object Drawing.Point(34,11); $statusText.AutoSize=$true; $statusPanel.Controls.Add($statusText)
$card=New-Object Windows.Forms.Panel; $card.Location=New-Object Drawing.Point(28,88); $card.Size=New-Object Drawing.Size(562,255); $card.BackColor=$surface; Add-RoundedBorder $card 24; $form.Controls.Add($card)
$deviceIcon=New-Label '▣' 44 $disabled; $deviceIcon.Location=New-Object Drawing.Point(245,28); $deviceIcon.AutoSize=$true; $card.Controls.Add($deviceIcon)
$deviceTitle=New-Label $phoneNotFound 16 $text; $deviceTitle.Font=New-Object Drawing.Font('Segoe UI',16,[Drawing.FontStyle]::Bold); $deviceTitle.AutoSize=$true; $deviceTitle.Location=New-Object Drawing.Point(181,92); $card.Controls.Add($deviceTitle)
$deviceDetail=New-Label $usbHint 10 $muted; $deviceDetail.AutoSize=$true; $deviceDetail.Location=New-Object Drawing.Point(128,132); $card.Controls.Add($deviceDetail)
$deviceState=New-Label $waiting 10 $orange; $deviceState.AutoSize=$true; $deviceState.Location=New-Object Drawing.Point(225,178); $card.Controls.Add($deviceState)
$selectTitle=New-Label $modulesText 12 $text; $selectTitle.Font=New-Object Drawing.Font('Segoe UI',12,[Drawing.FontStyle]::Bold); $selectTitle.Location=New-Object Drawing.Point(28,373); $selectTitle.AutoSize=$true; $form.Controls.Add($selectTitle)
$mediaPanel=New-Object Windows.Forms.Panel; $mediaPanel.Location=New-Object Drawing.Point(28,410); $mediaPanel.Size=New-Object Drawing.Size(562,112); $mediaPanel.BackColor=$surface; Add-RoundedBorder $mediaPanel 18; $form.Controls.Add($mediaPanel)
$camera=New-Object Windows.Forms.CheckBox; $camera.Text=$cameraText; $camera.Font=New-Object Drawing.Font('Segoe UI',11); $camera.ForeColor=$text; $camera.BackColor=$surface; $camera.Location=New-Object Drawing.Point(28,20); $camera.AutoSize=$true; $camera.Checked=$true; $mediaPanel.Controls.Add($camera)
$mic=New-Object Windows.Forms.CheckBox; $mic.Text='  Microfone virtual'; $mic.Font=New-Object Drawing.Font('Segoe UI',11); $mic.ForeColor=$text; $mic.BackColor=$surface; $mic.Location=New-Object Drawing.Point(300,20); $mic.AutoSize=$true; $mic.Checked=$true; $mediaPanel.Controls.Add($mic)
$cameraState=New-Label $inactiveText 9 $disabled; $cameraState.Location=New-Object Drawing.Point(55,58); $cameraState.AutoSize=$true; $mediaPanel.Controls.Add($cameraState); $micState=New-Label $micInactiveText 9 $disabled; $micState.Location=New-Object Drawing.Point(327,58); $micState.AutoSize=$true; $mediaPanel.Controls.Add($micState)
$button=New-Object Windows.Forms.Button; $button.Text=$startText; $button.Font=New-Object Drawing.Font('Segoe UI',12,[Drawing.FontStyle]::Bold); $button.FlatStyle='Flat'; $button.FlatAppearance.BorderSize=0; $button.BackColor=$blue; $button.ForeColor=[Drawing.Color]::FromArgb(4,18,26); $button.Location=New-Object Drawing.Point(28,566); $button.Size=New-Object Drawing.Size(562,62); $button.Enabled=$false; $form.Controls.Add($button); Add-RoundedBorder $button 16
$hint=New-Label ('Ligue o telem' + $oacute + 'vel para ativar os controlos.') 9 $muted; $hint.Location=New-Object Drawing.Point(28,646); $hint.AutoSize=$true; $form.Controls.Add($hint)

$script:service=$null; $script:running=$false
function Update-Ui {
    $d=Get-DeviceInfo
    if($d.Connected){$deviceIcon.ForeColor=$green;$deviceTitle.Text=$d.Name;$deviceDetail.Text='Ligado por USB  ·  '+$d.Serial;$deviceState.Text=$available;$deviceState.ForeColor=$green;if(!$script:running){$button.Enabled=($camera.Checked -or $mic.Checked);$hint.Text='Escolha os m' + $oacute + 'dulos e inicie a ponte.'}}
    else{$deviceIcon.ForeColor=$disabled;$deviceTitle.Text=$phoneNotFound;$deviceDetail.Text=$usbHint;$deviceState.Text=$waiting;$deviceState.ForeColor=$orange;if(!$script:running){$button.Enabled=$false;$hint.Text='Ligue o telem' + $oacute + 'vel para ativar os controlos.'}}
    if($script:running -and $script:service -and $script:service.HasExited){$script:running=$false;$script:service=$null}
    if($script:running){$statusDot.ForeColor=$green;$statusText.Text='Ativo';$button.Text=$stopText;$button.BackColor=$surfaceHigh;$button.ForeColor=$red;$camera.Enabled=$false;$mic.Enabled=$false;$cameraState.Text=if($camera.Checked){$activeText}else{$inactiveText};$cameraState.ForeColor=if($camera.Checked){$green}else{$disabled};$micState.Text=if($mic.Checked){$micActiveText}else{$micInactiveText};$micState.ForeColor=if($mic.Checked){$green}else{$disabled}}
    else{$statusDot.ForeColor=$disabled;$statusText.Text='Parado';$button.Text=$startText;$button.BackColor=$blue;$button.ForeColor=[Drawing.Color]::FromArgb(4,18,26);$camera.Enabled=$true;$mic.Enabled=$true;$cameraState.Text=$inactiveText;$cameraState.ForeColor=$disabled;$micState.Text=$micInactiveText;$micState.ForeColor=$disabled}
}
$timer=New-Object Windows.Forms.Timer; $timer.Interval=1000; $timer.Add_Tick({Update-Ui}); $timer.Start()
$camera.Add_CheckedChanged({if(!$script:running){$button.Enabled=(($camera.Checked -or $mic.Checked) -and (Get-DeviceInfo).Connected)}}); $mic.Add_CheckedChanged({if(!$script:running){$button.Enabled=(($camera.Checked -or $mic.Checked) -and (Get-DeviceInfo).Connected)}})
$button.Add_Click({if($script:running){if($script:service){$script:service.CloseMainWindow();Start-Sleep -Milliseconds 400;if(!$script:service.HasExited){$script:service.Kill()}};$script:running=$false;return};$d=Get-DeviceInfo;if(!$d.Connected){[Windows.Forms.MessageBox]::Show('Ligue primeiro um telem' + $oacute + 'vel Android por USB.','PhoneBridge');return};if(!$servicePath){[Windows.Forms.MessageBox]::Show('N' + $atilde + 'o encontrei phonebridge_service.exe. Compile primeiro a vers' + $atilde + 'o Windows.','PhoneBridge');return};$mode=if($camera.Checked -and $mic.Checked){'--both'}elseif($camera.Checked){'--camera'}else{'--microphone'};$script:service=Start-Process -FilePath $servicePath -ArgumentList $mode -WorkingDirectory (Split-Path $servicePath) -PassThru;$script:running=$true;Update-Ui})
$form.Add_FormClosing({if($script:service -and !$script:service.HasExited){$script:service.CloseMainWindow();Start-Sleep -Milliseconds 300;if(!$script:service.HasExited){$script:service.Kill()}}})
Update-Ui; [void]$form.ShowDialog()
