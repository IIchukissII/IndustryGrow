# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Capture the panel console (COM4, 115200 8N1) for a fixed window.
#
#     powershell -ExecutionPolicy Bypass -File tools\mon.ps1 -Seconds 45
#     ... -Send "d" -SendAfter 30 -Log run.txt
#
# Open this BEFORE resetting the board: the boot banner is printed once, and it
# is the only thing that says which of SDRAM, display, touch and the clock came
# up. Reset the board from a second shell with
#
#     powershell -ExecutionPolicy Bypass -File tools\flash.ps1        # or -Min
#
# -Send delivers one console key part way through the window, so a run can flash
# or reset, wait, and then ask for a dump without a human at the keyboard:
#   s self-test   d dump   h hunt   c cpu idle   b display on/off
#
# COM4 is this eval board; COM3 is the standalone ST-Link V3 for the WeAct
# nodes. Both are VID_0483&PID_374E -- tell them apart by the mass-storage
# volume, not the VID/PID.

param(
    [string]$Port = 'COM4',
    [int]$Seconds = 30,
    [string]$Log = '',
    [string]$Send = '',
    [double]$SendAfter = 3.0,
    # A whole run in one argument: "5:w,70:d,75:x,80:d" sends w at 5 s, d at
    # 70 s, and so on. Only one process can hold the port, so a test that needs
    # several keys at known times has to schedule them rather than reopen it.
    [string]$Keys = ''
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 200
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()

$buf = New-Object System.Text.StringBuilder
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sent = ($Send -eq '')

$schedule = @()
if ($Keys -ne '') {
    foreach ($pair in $Keys.Split(',')) {
        $bits = $pair.Split(':')
        # A key may be a whole string, so a line of input goes in one entry. The
        # token CR stands for carriage return, which cannot be written literally
        # in a command-line argument.
        $k = $bits[1]
        if ($k -eq 'CR') { $k = "`r" }
        $schedule += [pscustomobject]@{ At = [double]$bits[0]; Key = $k; Done = $false }
    }
}

try {
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        foreach ($s in $schedule) {
            if (-not $s.Done -and $sw.Elapsed.TotalSeconds -ge $s.At) {
                $sp.Write($s.Key)
                $null = $buf.Append("`r`n[t+$([int]$sw.Elapsed.TotalSeconds)s sent: $($s.Key)]`r`n")
                $s.Done = $true
            }
        }
        if (-not $sent -and $sw.Elapsed.TotalSeconds -ge $SendAfter) {
            $sp.Write($Send)
            $null = $buf.Append("[sent: $Send]`r`n")
            $sent = $true
        }
        $chunk = ''
        try { $chunk = $sp.ReadExisting() } catch {}
        if ($chunk -ne '') { $null = $buf.Append($chunk) }
        Start-Sleep -Milliseconds 100
    }
}
finally {
    $sp.Close()
    $sp.Dispose()
}

$text = $buf.ToString()
if ($Log -ne '') { Set-Content -Path $Log -Value $text -Encoding utf8 }
Write-Output $text
