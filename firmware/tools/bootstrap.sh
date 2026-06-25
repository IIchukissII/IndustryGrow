#!/usr/bin/env sh
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Fetch the firmware build dependencies as pinned git submodules. Run once
# from the repository root after cloning. Pins are tags; bump deliberately.
set -eu

cd "$(git rev-parse --show-toplevel)"

add() { # url path tag
  if [ ! -e "$2/.git" ] && [ ! -f "$2/.git" ]; then
    git submodule add "$1" "$2" || true
  fi
  git -C "$2" fetch --tags --quiet
  git -C "$2" checkout --quiet "$3"
}

# CMSIS-Core (Cortex-M4 intrinsics + core_cm4.h)
add https://github.com/ARM-software/CMSIS_5.git           firmware/third_party/cmsis_core       5.9.0
# ST CMSIS device pack for STM32F4 (stm32f405xx.h, system + gcc startup)
add https://github.com/STMicroelectronics/cmsis_device_f4.git firmware/third_party/cmsis_device_f4 v2.6.10

# Cyphal stack (used from layer 2 onward).
# libcanard pinned to v3.x: cyphal.c targets the canardTxInit/canardRxAccept
# API of that major. libcanard v4 reworked the memory-resource API — if you
# bump to v4, update src/cyphal/cyphal.c accordingly.
add https://github.com/OpenCyphal/libcanard.git          firmware/third_party/libcanard        3.0.0
add https://github.com/pavel-kirienko/o1heap.git         firmware/third_party/o1heap            2.0.0
# public_regulated_data_types publishes no release tags; pin to a commit.
add https://github.com/OpenCyphal/public_regulated_data_types.git \
                                                          firmware/third_party/public_regulated_data_types  a229bb78e76c48a3082be163bc240b2c13ff2d89

git submodule update --init --recursive
echo "bootstrap: submodules ready under firmware/third_party/"
