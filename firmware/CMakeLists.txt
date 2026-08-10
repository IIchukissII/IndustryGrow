# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# IndustryGrow node firmware. ONE codebase, ONE carrier (E0001), ONE image:
# every sensor-module personality under nodes/ is compiled in, and the module-ID
# strap selects among them at runtime (ADR-0017 d16, ADR-0014 d6). That is why
# the firmware document layer `F` roots on the carrier and not on the module --
# there is one versioned codebase, not one per node type.
#
# Cross build:
#   cmake -S firmware -B firmware/build \
#         -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
#   cmake --build firmware/build
#
# Requires the CMSIS submodules under third_party/ (run tools/bootstrap.sh):
#   third_party/cmsis_device_f4   (ST: stm32f405xx.h, system + gcc startup)
#   third_party/cmsis_core        (ARM CMSIS-Core: core_cm4.h)

cmake_minimum_required(VERSION 3.20)
project(igrow_node_firmware C ASM)

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

# common/ = the carrier support and node skeleton, identical whichever module is
# fitted; nodes/<type>/ = the personalities, plus registry.c which maps a strap
# value to one of them (ADR-0002 d5).
add_executable(igrow.elf
  # --- common: boot, carrier, drivers, Cyphal skeleton ---
  common/node/main.c
  common/carrier/e0001.c
  common/carrier/atecc608.c
  common/platform/clock.c
  common/platform/watchdog.c
  common/drivers/uart.c
  common/drivers/can.c
  common/drivers/i2c.c
  common/cyphal/cyphal.c
  common/cyphal/registers.c
  # --- personalities, selected at runtime by the module-ID strap ---
  nodes/registry.c
  nodes/m05_safety/sensors.c
  nodes/m05_safety/drivers/ina226.c
  nodes/m05_safety/drivers/tmp117.c
  nodes/m05_safety/drivers/s0.c
  nodes/m05_safety/drivers/leak.c
  nodes/m01_climate/sensors.c
  nodes/m01_climate/drivers/sensirion.c
  nodes/m01_climate/drivers/sht4x.c
  nodes/m01_climate/drivers/bme68x.c
  nodes/m01_climate/drivers/scd4x.c
  # --- vendored runtimes + device support ---
  "${LIBCANARD}/libcanard/canard.c"
  "${O1HEAP}/o1heap/o1heap.c"
  ${SYSTEM}
  ${STARTUP}
)

add_dependencies(igrow.elf dsdl_generated)

# nodes/ is on the path so registry.c can name a personality's header
# unambiguously (m01_climate/sensors.h); each personality's own quoted includes
# still resolve against its own directory first.
target_include_directories(igrow.elf PRIVATE
  common/node
  common/carrier
  common/platform
  common/drivers
  common/cyphal
  nodes
  nodes/m05_safety
  nodes/m05_safety/drivers
  nodes/m01_climate
  nodes/m01_climate/drivers
  "${LIBCANARD}/libcanard"
  "${O1HEAP}/o1heap"
  "${DSDL_GEN_DIR}"
  "${CMSIS_DEVICE}/Include"
  "${CMSIS_CORE}/CMSIS/Core/Include"
)

target_compile_definitions(igrow.elf PRIVATE STM32F405xx)

target_compile_options(igrow.elf PRIVATE
  -Wall -Wextra -Wundef -ffunction-sections -fdata-sections
  $<$<CONFIG:Debug>:-Og -g3>
  $<$<CONFIG:Release>:-Os>
)

# libm: M01's VPD needs expf() for the saturation-vapour-pressure curve (spec 6.1).
target_link_libraries(igrow.elf PRIVATE m)

target_link_options(igrow.elf PRIVATE
  -T${LINKER_SCRIPT}
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,-Map=igrow.map,--cref
  -Wl,--print-memory-usage
)

# Emit .hex/.bin and report size.
add_custom_command(TARGET igrow.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:igrow.elf> igrow.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrow.elf> igrow.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrow.elf>
  VERBATIM
)
