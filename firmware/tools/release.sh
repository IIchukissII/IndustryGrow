#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build the node image and publish the release artifacts into store/ under the
# ADR-0017 (rev 1) 'F' (Firmware) document layer, rooted on the carrier E0001
# (one shared codebase across node types; decision 16):
#   store/E0001-000001-F.hex       built image
#   store/E0001-000001-F-src.zip   source snapshot (firmware/ tree at HEAD)
#
# VVVVVV here is the FIRMWARE (codebase) version (independent of the E0001 carrier
# board design version); bump FW_VER on a firmware release. Run from anywhere.
set -eu

FW_VER="000001"
ART="E0001-${FW_VER}-F"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Cross-build (expects submodules from tools/bootstrap.sh + arm-none-eabi-gcc + nnvg).
cmake -S firmware -B firmware/build \
      -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build

cp "firmware/build/m05.hex" "store/${ART}.hex"

# Source snapshot of the firmware/ tree at HEAD (tracked files only; submodules
# are gitlinks and excluded by design — bootstrap.sh re-fetches them).
git archive --format=zip --prefix="${ART}-src/" -o "store/${ART}-src.zip" HEAD -- firmware

echo "released: store/${ART}.hex  store/${ART}-src.zip"
echo "note: these paths are annotated AGPL-3.0-or-later in REUSE.toml."
