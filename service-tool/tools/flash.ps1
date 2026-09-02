# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Flash over the board's on-board ST-LINK-V3E.
#     powershell -ExecutionPolicy Bypass -File tools\flash.ps1
#     ... -Min                           the bare-metal bring-up image
#     ... -Board H757 -Image igrow-check  the headless self-test on the H757
#
# STM32_Programmer_CLI is not installed standalone on this machine; the copy
# inside STM32CubeIDE is the one used. Pass -ShowOptionBytes to print the
# option bytes instead of flashing (see BENCH.md on BOOT_CM4).
#
# THREE ST-LINKs can be attached at once -- one per eval board, plus the
# standalone V3 for the WeAct nodes -- and all are VID_0483&PID_374E. -Board
# picks the right probe by serial number; without one the programmer takes the
# first it finds, and a CM7 image flashed into the wrong MCU is not a
# diagnosis. -Serial still overrides.

param(
    [switch]$ShowOptionBytes,
    [switch]$Min,
    [ValidateSet('H753', 'H757')]
    [string]$Board = 'H753',
    [string]$Image = '',
    [string]$Serial = ''
)

$ErrorActionPreference = 'Stop'

$cli = 'C:\Program Files\STM32CubeIDE_1.16.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.400.202404281720\tools\bin\STM32_Programmer_CLI.exe'
if (-not (Test-Path $cli)) { throw "STM32_Programmer_CLI not found at $cli" }

# Each board's own probe. Mass-storage volume EVA_H753XI / EVA_H757XI, console
# COM5 / COM4.
$probe = @{ 'H753' = '003300333137510C33333639'; 'H757' = '0020002F3137511333333639' }
if ($Serial -eq '') { $Serial = $probe[$Board] }

$port = "port=SWD mode=UR reset=HWrst sn=$Serial"

if ($ShowOptionBytes) {
    & $cli -c $port.Split(' ') -ob displ
    exit $LASTEXITCODE
}

$root  = Split-Path -Parent $PSScriptRoot
$build = if ($Board -eq 'H753') { Join-Path $root 'build' } else { Join-Path $root "build-$($Board.ToLower())" }

# -Min flashes the bare-metal bring-up image instead of the panel firmware.
if ($Image -eq '') { $Image = if ($Min) { 'igrow-min' } else { 'igrow-panel' } }
$hex = Join-Path $build "$Image.hex"
if (-not (Test-Path $hex)) { throw "$hex not built - run tools\build.ps1 -Board $Board first" }

& $cli -c $port.Split(' ') -d $hex -v -rst
exit $LASTEXITCODE
