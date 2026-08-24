# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generate C bindings from the DSDL namespaces with Nunavut at build time.
# Nunavut C output is header-only (static inline (de)serialization), so there
# is nothing extra to compile — only an include directory and a build-order
# dependency. Generated code is NOT committed (ADR-0005 d10); install Nunavut:
#   pip install "nunavut>=2,<3"
#
# NOTE: exact nnvg flags vary across Nunavut releases — adjust here if your
# pinned version differs.

find_program(NNVG nnvg REQUIRED)

set(DSDL_GEN_DIR "${CMAKE_BINARY_DIR}/dsdl" CACHE INTERNAL "Nunavut output dir")
set(PUBLIC_TYPES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/public_regulated_data_types")
set(PROJECT_DSDL "${CMAKE_CURRENT_SOURCE_DIR}/dsdl")

if(NOT EXISTS "${PUBLIC_TYPES}/uavcan")
  message(FATAL_ERROR
    "public_regulated_data_types missing. Run firmware/tools/bootstrap.sh first.")
endif()

set(DSDL_STAMP "${DSDL_GEN_DIR}/.stamp")

# The stamp has to depend on the DSDL sources themselves, or adding a type
# regenerates nothing and the build fails on a missing header while the .dsdl
# sits right there. Configure-time glob: a new namespace file needs a re-run of
# cmake, an edited one does not.
file(GLOB_RECURSE PROJECT_DSDL_SOURCES CONFIGURE_DEPENDS "${PROJECT_DSDL}/*.dsdl")

# Standard uavcan namespace (Heartbeat, GetInfo, register, si/sample, ...).
# The project industryflow.greenhouse namespace (ADR-0005) is generated too once
# its types are used. The project industryflow.greenhouse namespace (ADR-0005:
# safety DoorStatus/LeakStatus) is generated in a second pass that
# looks up uavcan for its dependencies (e.g. uavcan.time.SynchronizedTimestamp).
add_custom_command(
  OUTPUT "${DSDL_STAMP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DSDL_GEN_DIR}"
  COMMAND "${NNVG}" --target-language c --target-endianness little
          --outdir "${DSDL_GEN_DIR}"
          --lookup-dir "${PUBLIC_TYPES}/uavcan"
          "${PUBLIC_TYPES}/uavcan"
  COMMAND "${NNVG}" --target-language c --target-endianness little
          --outdir "${DSDL_GEN_DIR}"
          --lookup-dir "${PUBLIC_TYPES}/uavcan"
          "${PROJECT_DSDL}/industryflow"
  COMMAND "${CMAKE_COMMAND}" -E touch "${DSDL_STAMP}"
  DEPENDS ${PROJECT_DSDL_SOURCES}
  COMMENT "Nunavut: generating C bindings from DSDL (uavcan + industryflow)"
  VERBATIM
)

add_custom_target(dsdl_generated DEPENDS "${DSDL_STAMP}")
