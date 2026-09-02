# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Configure and build the panel firmware. Run from anywhere:
#     powershell -ExecutionPolicy Bypass -File tools\build.ps1
#
# -Board picks the eval board and with it the MCU, the BSP and the core supply
# option. Each board gets its own build tree, so switching does not reconfigure
# and rebuild the other one:
#     tools\build.ps1                              -> build\       (H753)
#     tools\build.ps1 -Board H757 -Target igrow-check.elf -> build-h757\
#
# -Target builds one executable instead of all three. The panel and min images
# still assume the H753 BSP in their sources, so on H757 only igrow-check.elf
# builds today.
#
# The toolchain file must be given as an absolute path and CMAKE_MAKE_PROGRAM
# must be named explicitly -- CMake will not find ninja.exe on its own here.

param(
    [ValidateSet('H753', 'H757')]
    [string]$Board = 'H753',
    [string]$Target = ''
)

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$build = if ($Board -eq 'H753') { Join-Path $root 'build' } else { Join-Path $root "build-$($Board.ToLower())" }

# arm-none-eabi-gcc comes from STM32CubeIDE; nnvg from the user Python scripts
# directory. Both have to be on PATH for the configure step.
$gcc = 'C:\Program Files\STM32CubeIDE_1.16.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.0.200.202406191623\tools\bin'
$nnvg = "$env:APPDATA\Python\Python312\Scripts"
$env:PATH = "$gcc;$nnvg;$env:PATH"

$toolchain = Join-Path $root 'cmake\arm-none-eabi-cm7.cmake'
$ninja     = 'C:\Users\admin\tools\ninja.exe'

# STM32CubeH7 and LVGL are large third-party checkouts and are NOT vendored in
# the repository. CMake defaults them to service-tool\deps\, which is
# gitignored; this bench keeps them elsewhere, so point at them here. One line
# to change on another machine, or drop the two checkouts into deps\ instead.
$deps = 'E:\hmi-panel'
$cube = Join-Path $deps 'cube-h7'
$lvgl = Join-Path $deps 'lvgl'

if (-not (Test-Path $build)) {
    cmake -S $root -B $build -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCUBE_DIR=$cube" `
        "-DLVGL_DIR=$lvgl" `
        "-DIGROW_BOARD=$Board" `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
}

if ($Target -ne '') {
    cmake --build $build --target $Target
} else {
    cmake --build $build
}
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Output ''
Get-ChildItem -Path $build -Filter '*.hex' | ForEach-Object { Write-Output "hex: $($_.FullName)" }
