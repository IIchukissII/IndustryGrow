#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Wrap an application binary in the ADR-0029 image header.

Input is the .bin objcopy emits from the application ELF -- the body, starting
at its vector table. Output is header + body, the thing that is written to a
slot and the thing a signature covers.

The header layout is firmware/common/platform/image.h; this script is its only
writer. The signature field is left zero: build-time signing is a deferred
decision of ADR-0029 and belongs with the key ceremony (ADR-0024). Nothing
today reads it -- the bootloader checks the CRC at boot (d8) and the signature
on download (d6), and the download path is not built.
"""

import argparse
import hashlib
import struct
import sys

MAGIC = 0x4947494D  # 'IGIM'
HEADER_VERSION = 1
HEADER_SIZE = 512
SIGNATURE_OFFSET = 0x48
SIGNATURE_LEN = 64


def crc32_mpeg2(data: bytes) -> int:
    """CRC-32/MPEG-2 over `data`: poly 0x04C11DB7, init 0xFFFFFFFF, no
    reflection, no final XOR."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def stm32_crc32(body: bytes) -> int:
    """What the STM32 CRC unit returns for `body`.

    The unit is fed 32-bit words and processes each word most-significant byte
    first, so a little-endian buffer reaches the polynomial in the order
    b3,b2,b1,b0 per word. Reversing each word here is what makes the host and
    the node agree (common/platform/crc32.h)."""
    swapped = b"".join(body[i:i + 4][::-1] for i in range(0, len(body), 4))
    return crc32_mpeg2(swapped)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True, help="application body (.bin)")
    ap.add_argument("--output", required=True, help="headered image (.img)")
    ap.add_argument("--version-major", type=int, required=True)
    ap.add_argument("--version-minor", type=int, required=True)
    ap.add_argument("--hardware-class", type=int, required=True)
    ap.add_argument("--vcs-revision-id", default="0",
                    help="git commit as hex, or 0 when not a released build")
    ap.add_argument("--max-body", type=int, required=True,
                    help="slot capacity less the header")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        body = f.read()

    # The node CRCs whole words, so the body is padded to a word boundary and
    # the header's length covers the padding.
    if len(body) % 4:
        body += b"\xff" * (4 - len(body) % 4)

    if not body:
        sys.stderr.write("mkimage: empty body\n")
        return 1
    if len(body) > args.max_body:
        sys.stderr.write(
            "mkimage: body is %d bytes, slot holds %d\n" % (len(body), args.max_body))
        return 1

    header = struct.pack(
        "<IIIIHHIQII32s",
        MAGIC,
        HEADER_VERSION,
        len(body),
        args.hardware_class,
        args.version_major,
        args.version_minor,
        0,                                   # reserved0
        int(args.vcs_revision_id, 16),
        stm32_crc32(body),
        0,                                   # reserved1
        hashlib.sha256(body).digest(),
    )
    assert len(header) == SIGNATURE_OFFSET, len(header)
    header += b"\x00" * SIGNATURE_LEN        # detached signature, unset
    header += b"\x00" * (HEADER_SIZE - len(header))

    with open(args.output, "wb") as f:
        f.write(header + body)

    print("mkimage: %s, %d body bytes, crc %#010x" %
          (args.output, len(body), stm32_crc32(body)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
