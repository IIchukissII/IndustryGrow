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

# Two images, two scripts (ADR-0029 d1). The bootloader has one fixed home at
# the reset vector; the application is built once per slot from a template,
# because it is not position-independent and links to the slot it runs from.
set(BOOT_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32F405RGTx_BOOT.ld")
set(APP_LINKER_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/STM32F405RGTx_APP.ld.in")

# The image header the bootloader reads, and the slot map it reads it at
# (common/platform/image.h, common/platform/partition.h). Kept here as well
# because the build addresses the artifacts: CMake places them, mkimage.py
# describes them, and the two must agree.
set(IGROW_IMAGE_HEADER_SIZE 512)
set(IGROW_SLOT_SIZE 393216)          # 384 KB
set(IGROW_HW_CLASS 1)                # carrier E0001
set(IGROW_SLOT_A_BASE 0x08020000)
set(IGROW_SLOT_B_BASE 0x08080000)

# The application version, reported by GetInfo and stamped into every image
# header. One definition: the C code takes it as a macro and mkimage.py as an
# argument, so a released image cannot claim a version its node denies.
set(IGROW_APP_VERSION_MAJOR 0)
set(IGROW_APP_VERSION_MINOR 1)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(LIBCANARD "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcanard")
set(O1HEAP    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/o1heap")

# Nunavut-generated DSDL headers (header-only C) -> ${DSDL_GEN_DIR}.
include(cmake/dsdl.cmake)

# Sources shared by both images: the carrier BSP, the clock, the debug console
# and the flash side of ADR-0029 -- the partition, the image header and the
# update-state block. The bootloader writes slots and the application does not
# (d3), but both read the same structures, so there is one implementation of
# them and not two.
set(SHARED_SOURCES
  common/carrier/e0001.c
  common/platform/clock.c
  common/platform/crc32.c
  common/platform/flash.c
  common/platform/image.c
  common/platform/update_state.c
  common/drivers/uart.c
  ${SYSTEM}
  ${STARTUP}
)

# common/ = the carrier support and node skeleton, identical whichever module is
# fitted; nodes/<type>/ = the personalities, plus registry.c which maps a strap
# value to one of them (ADR-0002 d5).
set(APP_SOURCES
  ${SHARED_SOURCES}
  # --- common: boot, carrier, drivers, Cyphal skeleton ---
  common/node/main.c
  common/carrier/atecc608.c
  common/carrier/identity.c
  common/platform/watchdog.c
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
)

# nodes/ is on the path so registry.c can name a personality's header
# unambiguously (m01_climate/sensors.h); each personality's own quoted includes
# still resolve against its own directory first.
function(igrow_target_settings target)
  target_include_directories(${target} PRIVATE
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
  target_compile_definitions(${target} PRIVATE STM32F405xx)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wundef -ffunction-sections -fdata-sections
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-Os>
  )
endfunction()

# --- bootloader: sectors 0-3, no Cyphal stack yet, no personalities ----------
add_executable(igrowboot.elf boot/main.c ${SHARED_SOURCES})
add_dependencies(igrowboot.elf dsdl_generated)
igrow_target_settings(igrowboot.elf)
target_link_options(igrowboot.elf PRIVATE
  -T${BOOT_LINKER_SCRIPT}
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,-Map=igrowboot.map,--cref
  -Wl,--print-memory-usage
)
add_custom_command(TARGET igrowboot.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:igrowboot.elf> igrowboot.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrowboot.elf> igrowboot.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrowboot.elf>
  VERBATIM
)

# --- application: one build per slot ----------------------------------------
#
# Same sources, same personalities, two link addresses. Each build is wrapped
# in its image header by mkimage.py and then addressed into a .hex for SWD:
# the .img is what a slot holds and what a signature will cover, the .hex is
# what a programmer writes.
math(EXPR APP_FLASH_LENGTH "${IGROW_SLOT_SIZE} - ${IGROW_IMAGE_HEADER_SIZE}")

foreach(slot a b)
  string(TOUPPER ${slot} SLOT_UPPER)
  set(IGROW_SLOT_NAME ${SLOT_UPPER})
  set(SLOT_BASE ${IGROW_SLOT_${SLOT_UPPER}_BASE})
  math(EXPR SLOT_FLASH_ORIGIN "${SLOT_BASE} + ${IGROW_IMAGE_HEADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
  set(IGROW_SLOT_FLASH_ORIGIN ${SLOT_FLASH_ORIGIN})
  set(IGROW_SLOT_FLASH_LENGTH ${APP_FLASH_LENGTH})

  set(SLOT_LD "${CMAKE_CURRENT_BINARY_DIR}/igrow-${slot}.ld")
  configure_file("${APP_LINKER_TEMPLATE}" "${SLOT_LD}" @ONLY)

  add_executable(igrow-${slot}.elf ${APP_SOURCES})
  add_dependencies(igrow-${slot}.elf dsdl_generated)
  igrow_target_settings(igrow-${slot}.elf)
  target_compile_definitions(igrow-${slot}.elf PRIVATE
    IGROW_APP_VERSION_MAJOR=${IGROW_APP_VERSION_MAJOR}
    IGROW_APP_VERSION_MINOR=${IGROW_APP_VERSION_MINOR}
  )
  # libm: M01's VPD needs expf() for the saturation-vapour-pressure curve (spec 6.1).
  target_link_libraries(igrow-${slot}.elf PRIVATE m)
  target_link_options(igrow-${slot}.elf PRIVATE
    -T${SLOT_LD}
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=igrow-${slot}.map,--cref
    -Wl,--print-memory-usage
  )
  add_custom_command(TARGET igrow-${slot}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:igrow-${slot}.elf> igrow-${slot}.bin
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --input igrow-${slot}.bin
            --output igrow-${slot}.img
            --version-major ${IGROW_APP_VERSION_MAJOR}
            --version-minor ${IGROW_APP_VERSION_MINOR}
            --hardware-class ${IGROW_HW_CLASS}
            --max-body ${APP_FLASH_LENGTH}
    COMMAND ${CMAKE_OBJCOPY} -I binary -O ihex --change-addresses=${SLOT_BASE}
            igrow-${slot}.img igrow-${slot}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:igrow-${slot}.elf>
    VERBATIM
  )
endforeach()
