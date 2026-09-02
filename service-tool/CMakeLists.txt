# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The E0010 service tool. Builds for either eval board -- see Core/Inc/eval_board.h.
#
#   cmake -S . -B build -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<abs path>/cmake/arm-none-eabi-cm7.cmake \
#         -DCMAKE_MAKE_PROGRAM=<abs path to ninja> \
#         -DCUBE_DIR=<abs path> -DLVGL_DIR=<abs path> \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# This directory is self-contained: nothing under firmware/ or anywhere else in
# the repository refers to it, and nothing here is built by the node firmware's
# CMake. The one thing it does reach for is the vocabulary.
#
#   CUBE_DIR   the STM32CubeH7 checkout (HAL, CMSIS, BSP, components) -- large,
#              third-party, and NOT vendored here. README.md says what to clone.
#   LVGL_DIR   the LVGL checkout, likewise.
#   IGROW_REPO this repository, one level up: libcanard, o1heap and the DSDL, so
#              the tool decodes the same vocabulary the nodes publish rather than
#              a second copy of it that can drift from the first.

cmake_minimum_required(VERSION 3.20)
project(igrow_panel C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CUBE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/deps/cube-h7" CACHE PATH "STM32CubeH7 checkout")
set(LVGL_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/deps/lvgl"    CACHE PATH "LVGL checkout")
set(IGROW_REPO "${CMAKE_CURRENT_SOURCE_DIR}/.."           CACHE PATH "IndustryGrow repo")

# --- which eval board -------------------------------------------------------
#
# Both are the MB1246 motherboard, so the BSP API, the connectors, the SDRAM,
# the QSPI, the MFX, the codec, the PHY and the CAN are the same. What differs
# is the MCU and the LCD daughterboard, which forces the BSP: the H743I-EVAL
# BSP drives MB1063 over LTDC parallel RGB, the H747I-EVAL BSP drives MB1166
# over DSI. The pairing is not a preference -- the STM32H753 has no DSI
# peripheral, so MB1166 can only ever run on the H757.
#
# Configure a second build tree rather than switching one in place:
#   cmake -S . -B build-h757 ... -DIGROW_BOARD=H757
#
# Core/Inc/eval_board.h turns the choice into headers and the supply option.
set(IGROW_BOARD "H753" CACHE STRING "eval board: H753 or H757")
set_property(CACHE IGROW_BOARD PROPERTY STRINGS H753 H757)

if(IGROW_BOARD STREQUAL "H757")
  set(IG_DEVICE  STM32H747xx)
  set(IG_STARTUP Core/Startup/startup_stm32h747xihx.s)
  set(BSP_DIR    "${CUBE_DIR}/Drivers/BSP/STM32H747I-EVAL")
  set(BSP_PFX    stm32h747i_eval)
elseif(IGROW_BOARD STREQUAL "H753")
  set(IG_DEVICE  STM32H753xx)
  set(IG_STARTUP Core/Startup/startup_stm32h753xx.s)
  set(BSP_DIR    "${CUBE_DIR}/Drivers/BSP/STM32H743I-EVAL")
  set(BSP_PFX    stm32h743i_eval)
else()
  message(FATAL_ERROR "IGROW_BOARD must be H753 or H757, not ${IGROW_BOARD}")
endif()

set(IG_DEFS USE_HAL_DRIVER ${IG_DEVICE} CORE_CM7 IGROW_BOARD_${IGROW_BOARD})
message(STATUS "board ${IGROW_BOARD}: ${IG_DEVICE}, BSP ${BSP_PFX}")

foreach(d "${CUBE_DIR}/Drivers/STM32H7xx_HAL_Driver" "${LVGL_DIR}/src"
          "${IGROW_REPO}/firmware/third_party/libcanard")
  if(NOT EXISTS "${d}")
    message(FATAL_ERROR
      "missing dependency: ${d}\n"
      "STM32CubeH7 and LVGL are not vendored in this repository. Clone them and "
      "pass -DCUBE_DIR= and -DLVGL_DIR=, or put them under service-tool/deps/ "
      "(gitignored). See service-tool/README.md.")
  endif()
endforeach()

# --- DSDL ------------------------------------------------------------------
# Same two-pass Nunavut invocation the node firmware uses. Generated code is
# header-only, so there is nothing to compile -- only an include directory.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/dsdl.cmake")

# --- sources ---------------------------------------------------------------

file(GLOB HAL_SOURCES "${CUBE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/*.c")
list(FILTER HAL_SOURCES EXCLUDE REGEX ".*_template\\.c$")

# The board's BSP, chosen above. The panel and touch components come with it:
# MB1063 is an AMPIRE 640x480 over LTDC parallel RGB with an EXC7200 at I2C
# 0x08; MB1166 is an OTM8009A 800x480 over DSI with an FT6x06 at 0x54.
set(BSP_SOURCES
  "${BSP_DIR}/${BSP_PFX}.c"
  "${BSP_DIR}/${BSP_PFX}_bus.c"
  "${BSP_DIR}/${BSP_PFX}_io.c"
  "${BSP_DIR}/${BSP_PFX}_lcd.c"
  "${BSP_DIR}/${BSP_PFX}_sdram.c"
  "${BSP_DIR}/${BSP_PFX}_ts.c"
  "${CUBE_DIR}/Drivers/BSP/Components/mfxstm32l152/mfxstm32l152.c"
  "${CUBE_DIR}/Drivers/BSP/Components/mfxstm32l152/mfxstm32l152_reg.c"
)
# The two BSPs drive the same SDRAM through different component drivers.
if(IGROW_BOARD STREQUAL "H757")
  list(APPEND BSP_SOURCES
    "${CUBE_DIR}/Drivers/BSP/Components/is42s32800j/is42s32800j.c"
    "${CUBE_DIR}/Drivers/BSP/Components/otm8009a/otm8009a.c"
    "${CUBE_DIR}/Drivers/BSP/Components/otm8009a/otm8009a_reg.c"
    "${CUBE_DIR}/Drivers/BSP/Components/adv7533/adv7533.c"
    "${CUBE_DIR}/Drivers/BSP/Components/adv7533/adv7533_reg.c"
    "${CUBE_DIR}/Drivers/BSP/Components/ft6x06/ft6x06.c"
    "${CUBE_DIR}/Drivers/BSP/Components/ft6x06/ft6x06_reg.c"
  )
else()
  list(APPEND BSP_SOURCES
    "${CUBE_DIR}/Drivers/BSP/Components/is42s32800g/is42s32800g.c"
    "${CUBE_DIR}/Drivers/BSP/Components/exc7200/exc7200.c"
    "${CUBE_DIR}/Drivers/BSP/Components/exc7200/exc7200_reg.c"
  )
endif()

file(GLOB_RECURSE LVGL_SOURCES "${LVGL_DIR}/src/*.c")

set(RTOS_DIR "${CUBE_DIR}/Middlewares/Third_Party/FreeRTOS/Source")
set(RTOS_SOURCES
  "${RTOS_DIR}/tasks.c"
  "${RTOS_DIR}/queue.c"
  "${RTOS_DIR}/list.c"
  "${RTOS_DIR}/timers.c"
  "${RTOS_DIR}/event_groups.c"
  "${RTOS_DIR}/portable/GCC/ARM_CM7/r0p1/port.c"
  "${RTOS_DIR}/portable/MemMang/heap_4.c"
)

set(APP_SOURCES
  Core/Src/main.c
  Core/Src/stm32h7xx_it.c
  Core/Src/fault.c
  Core/Src/stm32h7xx_hal_timebase_tim.c
  Core/Src/system_stm32h7xx.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  ${IG_STARTUP}
  app/can_port.c
  app/console.c
  app/cyphal_rx.c
  app/model.c
  app/protocol.c
  app/protocol_doc.c
  app/selftest.c
  app/tasks.c
  app/subjects.c
  ui/lvgl_port.c
  ui/ui_main.c
  ui/ui_theme.c
  ui/ig_mark_img.c
  "${IGROW_REPO}/firmware/third_party/libcanard/libcanard/canard.c"
  "${IGROW_REPO}/firmware/third_party/o1heap/o1heap/o1heap.c"
)

add_executable(igrow-panel.elf ${APP_SOURCES} ${HAL_SOURCES} ${BSP_SOURCES} ${LVGL_SOURCES}
                               ${RTOS_SOURCES})
add_dependencies(igrow-panel.elf dsdl_generated)

target_compile_definitions(igrow-panel.elf PRIVATE
  ${IG_DEFS}
  LV_CONF_INCLUDE_SIMPLE
)

target_include_directories(igrow-panel.elf PRIVATE
  Core/Inc
  app
  ui
  "${DSDL_GEN_DIR}"
  "${IGROW_REPO}/firmware/third_party/libcanard/libcanard"
  "${IGROW_REPO}/firmware/third_party/o1heap/o1heap"
  "${CUBE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc"
  "${CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include"
  "${CUBE_DIR}/Drivers/CMSIS/Include"
  "${BSP_DIR}"
  "${CUBE_DIR}/Drivers/BSP/Components/Common"
  "${LVGL_DIR}"
  "${LVGL_DIR}/src"
  "${RTOS_DIR}/include"
  "${RTOS_DIR}/portable/GCC/ARM_CM7/r0p1"
)

target_compile_options(igrow-panel.elf PRIVATE
  -Wall -Wextra
  -ffunction-sections -fdata-sections
  $<$<CONFIG:Release>:-Os>
  $<$<CONFIG:Debug>:-Og -g3>
)

# LVGL and the ST drivers are third-party trees; their warnings are not ours to
# fix and would bury the ones that are.
set_source_files_properties(${LVGL_SOURCES} ${HAL_SOURCES} ${BSP_SOURCES} ${RTOS_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(igrow-panel.elf PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32H753XIHX_FLASH.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_BINARY_DIR}/igrow-panel.map
  -specs=nano.specs
  -u _printf_float          # console_printf and the value tiles format floats
  -static
)
target_link_libraries(igrow-panel.elf PRIVATE c m nosys)

add_custom_command(TARGET igrow-panel.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:igrow-panel.elf> igrow-panel.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrow-panel.elf> igrow-panel.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrow-panel.elf>
  COMMENT "hex + bin + size"
)

# --- igrow-min: bare-metal bring-up test ------------------------------------
#
# A second image from the same HAL and BSP, with no FreeRTOS, no LVGL and no
# CAN. It exists because the panel firmware can hang before it prints anything:
# this one reports every step and bounds every wait, so a board that will not
# reach voltage Scale 1 still boots, still talks and still draws.

set(MIN_SOURCES
  diag/min_main.c
  Core/Src/system_stm32h7xx.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  ${IG_STARTUP}
)

add_executable(igrow-min.elf ${MIN_SOURCES} ${HAL_SOURCES} ${BSP_SOURCES})

target_compile_definitions(igrow-min.elf PRIVATE
  ${IG_DEFS}
)

target_include_directories(igrow-min.elf PRIVATE
  Core/Inc
  "${CUBE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc"
  "${CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include"
  "${CUBE_DIR}/Drivers/CMSIS/Include"
  "${BSP_DIR}"
  "${CUBE_DIR}/Drivers/BSP/Components/Common"
)

target_compile_options(igrow-min.elf PRIVATE
  -Wall -Wextra
  -ffunction-sections -fdata-sections
  $<$<CONFIG:Release>:-Os>
  $<$<CONFIG:Debug>:-Og -g3>
)

target_link_options(igrow-min.elf PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32H753XIHX_FLASH.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_BINARY_DIR}/igrow-min.map
  -specs=nano.specs
  -static
)
target_link_libraries(igrow-min.elf PRIVATE c m nosys)

add_custom_command(TARGET igrow-min.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:igrow-min.elf> igrow-min.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrow-min.elf> igrow-min.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrow-min.elf>
  COMMENT "min: hex + bin + size"
)

# --- igrow-check: headless board self-test for the STM32H753I-EVAL -----------
#
# The second eval board is a single-core STM32H753XI on the same MB1246
# motherboard, with no display daughterboard fitted. It gets its own device
# define and startup file: a CM7 image built for the dual-core H747 is not a
# trustworthy instrument for judging single-core silicon.
#
# The linker script is shared -- the CM7 memory map is identical on both parts,
# and nothing in this image is placed in the SDRAM region it declares.
#
# The LCD and touch BSP files are left out (no panel fitted); the QSPI and SD
# ones are added, because on a headless board they are most of what is left.

set(CHECK_SOURCES
  diag/board_check.c
  Core/Src/system_stm32h7xx.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  ${IG_STARTUP}
)

set(CHECK_BSP_SOURCES
  "${BSP_DIR}/${BSP_PFX}.c"
  "${BSP_DIR}/${BSP_PFX}_bus.c"
  "${BSP_DIR}/${BSP_PFX}_io.c"
  "${BSP_DIR}/${BSP_PFX}_sdram.c"
  "${BSP_DIR}/${BSP_PFX}_qspi.c"
  "${BSP_DIR}/${BSP_PFX}_sd.c"
  "${CUBE_DIR}/Drivers/BSP/Components/mfxstm32l152/mfxstm32l152.c"
  "${CUBE_DIR}/Drivers/BSP/Components/mfxstm32l152/mfxstm32l152_reg.c"
  "${CUBE_DIR}/Drivers/BSP/Components/mt25tl01g/mt25tl01g.c"
)
if(IGROW_BOARD STREQUAL "H757")
  list(APPEND CHECK_BSP_SOURCES "${CUBE_DIR}/Drivers/BSP/Components/is42s32800j/is42s32800j.c")
else()
  list(APPEND CHECK_BSP_SOURCES "${CUBE_DIR}/Drivers/BSP/Components/is42s32800g/is42s32800g.c")
endif()

add_executable(igrow-check.elf ${CHECK_SOURCES} ${HAL_SOURCES} ${CHECK_BSP_SOURCES})

target_compile_definitions(igrow-check.elf PRIVATE
  ${IG_DEFS}
)

target_include_directories(igrow-check.elf PRIVATE
  Core/Inc
  "${CUBE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc"
  "${CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include"
  "${CUBE_DIR}/Drivers/CMSIS/Include"
  "${BSP_DIR}"
  "${CUBE_DIR}/Drivers/BSP/Components/Common"
)

target_compile_options(igrow-check.elf PRIVATE
  -Wall -Wextra
  -ffunction-sections -fdata-sections
  $<$<CONFIG:Release>:-Os>
  $<$<CONFIG:Debug>:-Og -g3>
)

set_source_files_properties(${CHECK_BSP_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(igrow-check.elf PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32H753XIHX_FLASH.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_BINARY_DIR}/igrow-check.map
  -specs=nano.specs
  -static
)
target_link_libraries(igrow-check.elf PRIVATE c m nosys)

add_custom_command(TARGET igrow-check.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:igrow-check.elf> igrow-check.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrow-check.elf> igrow-check.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrow-check.elf>
  COMMENT "check: hex + bin + size"
)
