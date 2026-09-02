# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generate C bindings from the IndustryGrow DSDL with Nunavut, the same way
# firmware/cmake/dsdl.cmake does it. Nunavut's C output is header-only, so
# there is nothing to compile -- an include directory and a build-order
# dependency is the whole result.
#
# The point of pointing at the repo rather than copying the .dsdl files here is
# that the panel then cannot decode a stale vocabulary: change a type in the
# repo and the next build of the panel picks it up.

find_program(NNVG nnvg REQUIRED)

set(DSDL_GEN_DIR "${CMAKE_BINARY_DIR}/dsdl" CACHE INTERNAL "Nunavut output dir")
set(PUBLIC_TYPES "${IGROW_REPO}/firmware/third_party/public_regulated_data_types")
set(PROJECT_DSDL "${IGROW_REPO}/firmware/dsdl")

if(NOT EXISTS "${PUBLIC_TYPES}/uavcan")
  message(FATAL_ERROR
    "public_regulated_data_types missing under ${PUBLIC_TYPES}. "
    "Run firmware/tools/bootstrap.sh in the IndustryGrow repo first.")
endif()

file(GLOB_RECURSE PROJECT_DSDL_SOURCES CONFIGURE_DEPENDS "${PROJECT_DSDL}/*.dsdl")
set(DSDL_STAMP "${DSDL_GEN_DIR}/.stamp")

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
  COMMENT "Nunavut: generating C bindings (uavcan + industryflow)"
  VERBATIM
)

add_custom_target(dsdl_generated DEPENDS "${DSDL_STAMP}")
