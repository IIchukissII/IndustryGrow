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
#   store/E0001-000001-F-slot-a.img  the same image without a load address
#   store/E0001-000001-F-slot-b.img  -- what a signature covers, what the bus serves
#   store/E0001-000001-F-src.zip     source snapshot (firmware/ tree at HEAD)
#
# Three images because ADR-0029 d1 partitions the flash: the bootloader owns
# the reset vector, and the application is not position-independent, so it is
# built once per slot. A first flash writes the bootloader and one slot.
#
# Usage:
#   tools/release.sh --key <p256-private-key.pem>   signed release
#   tools/release.sh --unsigned                     deliberately unsigned
#
# Signing is not optional by omission (ADR-0029 d6): one of the two has to be
# said out loud, because the difference is invisible in the artifacts and
# decides whether any node will ever accept them over the bus.
#
# VVVVVV here is the FIRMWARE (codebase) version (independent of the E0001 carrier
# board design version); bump FW_VER on a firmware release. Run from anywhere.
set -eu

FW_VER="000001"
ART="E0001-${FW_VER}-F"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Its OWN build directory, never the one a developer works in. A release used
# to configure firmware/build in place, which reset that directory's cached
# signing keys: the images left behind were unsigned and the bootloader among
# them keyless, and flashing one disarmed the node it was flashed to.
BUILD="firmware/build-release"

SIGNING_KEY=""
UNSIGNED=0
while [ $# -gt 0 ]; do
  case "$1" in
    --key) SIGNING_KEY="${2:-}"; shift 2 ;;
    --unsigned) UNSIGNED=1; shift ;;
    *) echo "release: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

if [ -n "$SIGNING_KEY" ] && [ "$UNSIGNED" -eq 1 ]; then
  echo "release: --key and --unsigned are contradictory" >&2
  exit 2
fi
if [ -z "$SIGNING_KEY" ] && [ "$UNSIGNED" -eq 0 ]; then
  echo "release: say which -- --key <pem> to sign, or --unsigned to publish" >&2
  echo "         images no node will accept over the bus (ADR-0029 d6)." >&2
  exit 2
fi

VERIFY_KEY_HEX=""
if [ -n "$SIGNING_KEY" ]; then
  [ -r "$SIGNING_KEY" ] || { echo "release: cannot read $SIGNING_KEY" >&2; exit 2; }
  # The public half is derived from the private one rather than passed
  # separately: two inputs can disagree, and a bootloader holding the wrong key
  # rejects every image its own release signed.
  PYTHON="${PYTHON:-}"
  if [ -z "$PYTHON" ]; then
    if command -v python3 > /dev/null 2>&1; then PYTHON="python3"
    elif command -v py > /dev/null 2>&1; then PYTHON="py -3"
    else echo "release: no python3 to derive the public key" >&2; exit 2; fi
  fi
  VERIFY_KEY_HEX="$($PYTHON firmware/tools/mkimage.py --public-key "$SIGNING_KEY")"
fi

# Cross-build (expects submodules from tools/bootstrap.sh + arm-none-eabi-gcc + nnvg).
# Configured from scratch every time: a release must not inherit anything from a
# previous run, least of all a key.
rm -rf "$BUILD"
GENERATOR="${IGROW_CMAKE_GENERATOR:-Ninja}"
MAKE_PROGRAM=""
if [ "$GENERATOR" = "Ninja" ]; then
  NINJA="$(command -v ninja || true)"
  [ -n "$NINJA" ] || { echo "release: ninja not on PATH; set IGROW_CMAKE_GENERATOR" >&2; exit 2; }
  MAKE_PROGRAM="-DCMAKE_MAKE_PROGRAM=$NINJA"
fi

# shellcheck disable=SC2086 # MAKE_PROGRAM is one optional argument
cmake -S firmware -B "$BUILD" -G "$GENERATOR" $MAKE_PROGRAM \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/firmware/cmake/arm-none-eabi.cmake" \
      -DIGROW_SIGNING_KEY="$SIGNING_KEY" \
      -DIGROW_VERIFY_KEY_HEX="$VERIFY_KEY_HEX" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

# Every personality is in each application image; the strap selects at runtime
# (ADR-0017 d16). Which slot an image is for is a link address, not a variant.
cp "$BUILD/igrowboot.hex" "store/${ART}-boot.hex"
cp "$BUILD/igrow-a.hex"   "store/${ART}-slot-a.hex"
cp "$BUILD/igrow-b.hex"   "store/${ART}-slot-b.hex"

# The bus artifact (ADR-0029 d13). Same bytes as the .hex without the load
# address: the unit a signature covers, and the only form a node can be given
# over the bus. The bootloader has none -- it is never served (d10).
cp "$BUILD/igrow-a.img"   "store/${ART}-slot-a.img"
cp "$BUILD/igrow-b.img"   "store/${ART}-slot-b.img"

# Source snapshot of the firmware/ tree at HEAD (tracked files only; submodules
# are gitlinks and excluded by design — bootstrap.sh re-fetches them).
git archive --format=zip --prefix="${ART}-src/" -o "store/${ART}-src.zip" HEAD -- firmware

if [ -n "$SIGNING_KEY" ]; then
  echo "released SIGNED: store/${ART}-{boot,slot-a,slot-b}.hex  store/${ART}-slot-{a,b}.img  store/${ART}-src.zip"
else
  echo "released UNSIGNED: store/${ART}-{boot,slot-a,slot-b}.hex  store/${ART}-slot-{a,b}.img  store/${ART}-src.zip"
  echo "         these flash over SWD and refuse every update over the bus."
fi
echo "note: these paths are annotated AGPL-3.0-or-later in REUSE.toml."
