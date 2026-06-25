# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# IndustryGrow node firmware — M05-SAFETY bring-up.
# Cross build:
#   cmake -S firmware -B firmware/build \
#         -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
#   cmake --build firmware/build
#
# Requires the CMSIS submodules under third_party/ (run tools/bootstrap.sh):
#   third_party/cmsis_device_f4   (ST: stm32f405xx.h, system + gcc startup)
#   third_party/cmsis_core        (ARM CMSIS-Core: core_cm4.h)

cmake_minimum_required(VERSION 3.20)
project(igrow_m05_firmware C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMSIS_DEVICE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis_device_f4")
set(CMSIS_CORE   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis_core")
set(STARTUP "${CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f405xx.s")
set(SYSTEM  "${CMSIS_DEVICE}/Source/Templates/system_stm32f4xx.c")

if(NOT EXISTS "${CMSIS_DEVICE}/Include/stm32f405xx.h")
  message(FATAL_ERROR
    "CMSIS device headers missing. Run firmware/tools/bootstrap.sh "
    "(git submodule update --init) before configuring.")
endif()

set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32F405RGTx_FLASH.ld")

set(LIBCANARD "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcanard")
set(O1HEAP    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/o1heap")

# Nunavut-generated DSDL headers (header-only C) -> ${DSDL_GEN_DIR}.
include(cmake/dsdl.cmake)

# common/ = shared node skeleton + HW drivers (identical for every node type);
# nodes/<type>/ = the per-node personality (ADR-0002 d5). This image is M05.
add_executable(m05.elf
  # --- node personality: M05-SAFETY ---
  nodes/m05_safety/main.c
  nodes/m05_safety/sensors.c
  nodes/m05_safety/drivers/ina226.c
  nodes/m05_safety/drivers/tmp117.c
  nodes/m05_safety/drivers/s0.c
  nodes/m05_safety/drivers/leak.c
  # --- common: shared across all node types ---
  common/carrier/e0001.c
  common/carrier/atecc608.c
  common/platform/clock.c
  common/drivers/uart.c
  common/drivers/can.c
  common/drivers/i2c.c
  common/cyphal/cyphal.c
  common/cyphal/registers.c
  # --- vendored runtimes + device support ---
  "${LIBCANARD}/libcanard/canard.c"
  "${O1HEAP}/o1heap/o1heap.c"
  ${SYSTEM}
  ${STARTUP}
)

add_dependencies(m05.elf dsdl_generated)

target_include_directories(m05.elf PRIVATE
  common/carrier
  common/platform
  common/drivers
  common/cyphal
  nodes/m05_safety
  nodes/m05_safety/drivers
  "${LIBCANARD}/libcanard"
  "${O1HEAP}/o1heap"
  "${DSDL_GEN_DIR}"
  "${CMSIS_DEVICE}/Include"
  "${CMSIS_CORE}/CMSIS/Core/Include"
)

target_compile_definitions(m05.elf PRIVATE STM32F405xx)

target_compile_options(m05.elf PRIVATE
  -Wall -Wextra -Wundef -ffunction-sections -fdata-sections
  $<$<CONFIG:Debug>:-Og -g3>
  $<$<CONFIG:Release>:-Os>
)

target_link_options(m05.elf PRIVATE
  -T${LINKER_SCRIPT}
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,-Map=m05.map,--cref
  -Wl,--print-memory-usage
)

# Emit .hex/.bin and report size.
add_custom_command(TARGET m05.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:m05.elf> m05.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:m05.elf> m05.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:m05.elf>
  VERBATIM
)
