#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Build the node images and publish the release artifacts into store/ under the
# ADR-0017 (rev 1) 'F' (Firmware) document layer, rooted on the carrier E0001
# (one shared codebase across node types; decision 16):
#   store/E0001-000001-F-boot.hex    bootloader, sectors 0-3
#   store/E0001-000001-F-slot-a.hex  application linked for slot A
#   store/E0001-000001-F-slot-b.hex  application linked for slot B
#   store/E0001-000001-F-src.zip     source snapshot (firmware/ tree at HEAD)
#
# Three images because ADR-0029 d1 partitions the flash: the bootloader owns
# the reset vector, and the application is not position-independent, so it is
# built once per slot. A first flash writes the bootloader and one slot.
#
# VVVVVV here is the FIRMWARE (codebase) version (independent of the E0001 carrier
# board design version); bump FW_VER on a firmware release. Run from anywhere.
set -eu

FW_VER="000001"
ART="E0001-${FW_VER}-F"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Signing keys (ADR-0029 d6). Neither lives in the repository: pass the private
# key path and its public half in the environment. Without them the images are
# built UNSIGNED and no node will accept them over the bus -- which is a valid
# thing to release for SWD flashing, and is why this warns rather than stops.
: "${IGROW_SIGNING_KEY:=}"
: "${IGROW_VERIFY_KEY_HEX:=}"
if [ -z "$IGROW_SIGNING_KEY" ] || [ -z "$IGROW_VERIFY_KEY_HEX" ]; then
  echo "release: IGROW_SIGNING_KEY / IGROW_VERIFY_KEY_HEX unset - releasing UNSIGNED images" >&2
fi

# Cross-build (expects submodules from tools/bootstrap.sh + arm-none-eabi-gcc + nnvg).
cmake -S firmware -B firmware/build \
      -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake \
      -DIGROW_SIGNING_KEY="$IGROW_SIGNING_KEY" \
      -DIGROW_VERIFY_KEY_HEX="$IGROW_VERIFY_KEY_HEX" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build

# Every personality is in each application image; the strap selects at runtime
# (ADR-0017 d16). Which slot an image is for is a link address, not a variant.
cp "firmware/build/igrowboot.hex" "store/${ART}-boot.hex"
cp "firmware/build/igrow-a.hex"   "store/${ART}-slot-a.hex"
cp "firmware/build/igrow-b.hex"   "store/${ART}-slot-b.hex"

# Source snapshot of the firmware/ tree at HEAD (tracked files only; submodules
# are gitlinks and excluded by design — bootstrap.sh re-fetches them).
git archive --format=zip --prefix="${ART}-src/" -o "store/${ART}-src.zip" HEAD -- firmware

echo "released: store/${ART}-boot.hex  store/${ART}-slot-a.hex  store/${ART}-slot-b.hex  store/${ART}-src.zip"
echo "note: these paths are annotated AGPL-3.0-or-later in REUSE.toml."
