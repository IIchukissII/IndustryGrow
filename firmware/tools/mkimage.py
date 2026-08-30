#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Wrap an application binary in the ADR-0029 image header, and sign it.

Input is the .bin objcopy emits from the application ELF -- the body, starting
at its vector table. Output is header + body: what a slot holds, what the
gateway serves, and what a signature covers. The header layout is
firmware/common/platform/image.h and this script is its only writer.

With --key the header is signed: ECDSA P-256 over SHA-256 of the header up to
the signature field (d6), emitted as raw r||s because that is what the node
verifies. Without one the signature stays zero and the bootloader refuses the
image on download -- the intended behaviour of an unsigned build, not a way
around signing.

Where the private key lives belongs to the key ceremony (ADR-0024), so this
script only reads a path it is given. --new-key and --public-key exist so that
creating a key and telling the build about its public half is one documented
step instead of an improvised openssl line.
"""

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

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
            crc = (
                ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
                if crc & 0x80000000
                else (crc << 1) & 0xFFFFFFFF
            )
    return crc


def stm32_crc32(body: bytes) -> int:
    """What the STM32 CRC unit returns for `body`.

    The unit is fed 32-bit words and processes each word most-significant byte
    first, so a little-endian buffer reaches the polynomial in the order
    b3,b2,b1,b0 per word. Reversing each word here is what makes the host and
    the node agree (common/platform/crc32.h)."""
    swapped = b"".join(body[i : i + 4][::-1] for i in range(0, len(body), 4))
    return crc32_mpeg2(swapped)


def key_passphrase(spec):
    """Resolve an OpenSSL-style passphrase spec, or None for an unencrypted key.

    `env:VAR`, `file:PATH` or `pass:VALUE`, matching pki/*.sh so one convention
    covers both trust roots. `pass:` is accepted for a scripted rehearsal and is
    visible in the process table; the release path uses `env:`.
    """
    if not spec:
        return None
    kind, _, rest = spec.partition(":")
    if kind == "env":
        value = os.environ.get(rest)
        if value is None:
            raise SystemExit(f"mkimage: {rest} is not set")
        return value.encode()
    if kind == "file":
        return Path(rest).read_text().strip().encode()
    if kind == "pass":
        return rest.encode()
    raise SystemExit(f"mkimage: unsupported passphrase spec {spec!r} (env:, file:, pass:)")


def load_signer(path: str, passphrase=None):
    """The private key at `path`, as a P-256 signing key.

    The signing key is held encrypted at rest, as the operator root is
    (ADR-0024 d5): it is the one key whose compromise cannot be recovered over
    the wire, because its public half is compiled into a bootloader that only SWD
    can replace (ADR-0029 d10).
    """
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    with open(path, "rb") as f:
        raw = f.read()
    try:
        key = serialization.load_pem_private_key(raw, password=passphrase)
    except TypeError as exc:  # encrypted key, no passphrase given
        raise SystemExit(
            f"mkimage: {path} is encrypted; pass --key-pass env:VAR (or file:/pass:)"
        ) from exc
    except ValueError as exc:
        raise SystemExit(f"mkimage: {path} could not be read: {exc}") from exc
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise SystemExit(f"mkimage: {path} is not a P-256 private key")
    return key


def sign(path: str, data: bytes, passphrase=None) -> bytes:
    """ECDSA P-256 over SHA-256 of `data`, as raw r||s big-endian. micro-ecc
    takes no DER, so the DER the library returns is unpacked here."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils

    der = load_signer(path, passphrase).sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def new_key(path: str, passphrase=None, dev: bool = False) -> int:
    """Write a fresh P-256 signing key, encrypted.

    Refuses to overwrite one: replacing a signing key means re-flashing every node
    in service over SWD (ADR-0024 d14). Refuses to write one unencrypted for the
    same reason — it is the one key whose loss cannot be repaired over the wire,
    and d5's encryption-at-rest applies to it with more force than to the operator
    root, not less.
    """
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec

    if os.path.exists(path):
        sys.stderr.write(f"mkimage: {path} exists; refusing to overwrite a signing key\n")
        return 1
    if not passphrase and not dev:
        sys.stderr.write(
            "mkimage: --new-key needs --key-pass; a signing key is not written in the "
            "clear (ADR-0024 d5, d14). For a bench key, pass --dev.\n"
        )
        return 1
    pem = ec.generate_private_key(ec.SECP256R1()).private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=(
            serialization.BestAvailableEncryption(passphrase)
            if passphrase
            else serialization.NoEncryption()
        ),
    )
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    if dev and not passphrase:
        print(
            f"mkimage: wrote {path} as a DEVELOPMENT key -- unencrypted, not custodied,\n"
            "         not for any deployment (ADR-0024 d14). A node flashed with a\n"
            "         bootloader holding this key refuses production images, and vice versa."
        )
    else:
        print(f"mkimage: wrote {path} (encrypted, mode 600) -- keep it out of the repository")
    return 0


def public_key_hex(path: str, passphrase=None) -> int:
    """Print the public half as 128 hex characters: the uncompressed point
    X||Y without its 0x04 prefix, which is what -DIGROW_VERIFY_KEY_HEX takes."""
    from cryptography.hazmat.primitives import serialization

    point = (
        load_signer(path, passphrase)
        .public_key()
        .public_bytes(
            encoding=serialization.Encoding.X962,
            format=serialization.PublicFormat.UncompressedPoint,
        )
    )
    if (len(point) != 65) or (point[0] != 0x04):
        raise SystemExit("mkimage: unexpected public point encoding")
    print(point[1:].hex())
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", help="application body (.bin)")
    ap.add_argument("--output", help="headered image (.img)")
    ap.add_argument("--version-major", type=int)
    ap.add_argument("--version-minor", type=int)
    ap.add_argument("--hardware-class", type=int)
    ap.add_argument(
        "--vcs-revision-id", default="0", help="git commit as hex, or 0 when not a released build"
    )
    ap.add_argument("--max-body", type=int, help="slot capacity less the header")
    ap.add_argument("--key", help="P-256 private key (PEM) to sign the header with")
    ap.add_argument(
        "--key-pass",
        metavar="SPEC",
        help="passphrase for an encrypted --key: env:VAR, file:PATH or pass:VALUE",
    )
    ap.add_argument("--new-key", metavar="PATH", help="write a new signing key and exit")
    ap.add_argument(
        "--dev",
        action="store_true",
        help="with --new-key: a bench key, unencrypted and not custodied",
    )
    ap.add_argument(
        "--public-key", metavar="PATH", help="print a key's public half as hex and exit"
    )
    args = ap.parse_args()

    if args.new_key:
        return new_key(args.new_key, key_passphrase(args.key_pass), args.dev)
    if args.public_key:
        return public_key_hex(args.public_key, key_passphrase(args.key_pass))
    for required in (
        "input",
        "output",
        "version_major",
        "version_minor",
        "hardware_class",
        "max_body",
    ):
        if getattr(args, required) is None:
            ap.error("--{} is required to build an image".format(required.replace("_", "-")))

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
        sys.stderr.write(f"mkimage: body is {len(body)} bytes, slot holds {args.max_body}\n")
        return 1

    header = struct.pack(
        "<IIIIHHIQII32s",
        MAGIC,
        HEADER_VERSION,
        len(body),
        args.hardware_class,
        args.version_major,
        args.version_minor,
        0,  # reserved0
        int(args.vcs_revision_id, 16),
        stm32_crc32(body),
        0,  # reserved1
        hashlib.sha256(body).digest(),
    )
    assert len(header) == SIGNATURE_OFFSET, len(header)

    # The signature covers the header up to itself; the body reaches it through
    # the digest already in there, so the header never signs itself.
    signature = (
        sign(args.key, header, key_passphrase(args.key_pass))
        if args.key
        else b"\x00" * SIGNATURE_LEN
    )
    header += signature
    header += b"\x00" * (HEADER_SIZE - len(header))

    with open(args.output, "wb") as f:
        f.write(header + body)

    state = "signed" if args.key else "UNSIGNED"
    print(f"mkimage: {args.output}, {len(body)} body bytes, crc {stm32_crc32(body):#010x}, {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
