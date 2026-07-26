#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gateway ATECC608 identity provisioning — the tool `SP0004-M-atecc-provisioning` specifies.

Turns a blank TrustCUSTOM ATECC608B into a gateway identity: an on-chip P-256
keypair in slot 0 whose private half never leaves the die (ADR-0007 d1), a CSR
carrying `CN=GBOX_NNNN` signed *by that slot*, and the public anchors the
provisioning binding is recorded from (`store/SP0004-M-atecc-provisioning.md` §8).
The CSR is the only thing that can travel to the operator CA, which is why
`pki/sign-csr.sh` signs a request it did not generate.

    provision_identity.py detect
    provision_identity.py keygen  --confirm-irreversible          # writes + locks config
    provision_identity.py csr     --gbox GBOX_0001 --out GBOX_0001.csr
    # ... pki/sign-csr.sh --dir ./ca --csr GBOX_0001.csr --profile gateway ...
    provision_identity.py install --gbox GBOX_0001 --chain GBOX_0001-chain.crt
    provision_identity.py binding --gbox GBOX_0001 --vendor-serial SN123 \
                                  --cert GBOX_0001.crt --out GBOX_0001-binding.json

Every subcommand takes `--software-key PATH` instead of touching a chip. That is
the manual's §5 fallback: an exportable key on disk, which is the exact property
the ATECC exists to prevent, so it proves the pipeline and never a real unit.
`binding` refuses to emit a production record from one.

VALIDATION POSTURE — read this before provisioning a part you care about. The
ATECC code path has never run against hardware: there is no ATECC608 and no
cryptoauthlib on any machine that has executed this file, so `keygen` is
read-verified only, the same posture `erp/deploy/mtls/nginx.conf` carried before
it ran on a box. What *is* exercised (erp/tests/test_gateway_provisioning.py) is
everything downstream of the signer: the CSR's DER, the raw-to-DER signature
conversion the chip's output needs, the CA round trip, and the resulting leaf
authenticating through the ERP's mTLS seam. Confirm the §3 config words on a
sacrificial part before locking one destined for the field — the lock is
irreversible and a wrong value scraps the part.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

# ADR-0017 machine identifier, the same grammar pki/sign-csr.sh enforces. Checked
# here so a typo fails while it is still free, rather than at the CA — or worse,
# as a certificate that authenticates as nothing (ADR-0022 d2 reads identity out
# of this exact string).
GBOX_RE = re.compile(r"^GBOX_[0-9]{4}$")

IDENTITY_SLOT = 0  # ADR-0007 d9 / manual §3: the cryptoauthlib atcacert default.

# Manual §3, the ADR-0007 d9 values. Little-endian in the 128-byte config zone.
SLOT_CONFIG_0_OFFSET = 20  # SlotConfig[0]
KEY_CONFIG_0_OFFSET = 96  # KeyConfig[0]
SLOT_CONFIG_0 = 0x2087  # IsSecret=1, WriteConfig=GenKey, ECDSA sign permitted
KEY_CONFIG_0 = 0x0033  # Private=1, PubInfo=1, KeyType=4 (P-256), Lockable=1

CONFIG_ZONE_SIZE = 128

# Where a provisioned identity lands on the gateway. provision.sh owns
# /etc/industrygrow (0750 root:gateway); the chain is world-readable inside it
# because it is public material. Note what is absent: no private key file. There
# is nothing to protect on disk, which is the point of the part being on the BOM.
DEFAULT_INSTALL_DIR = Path("/etc/industrygrow/pki")


class ProvisioningError(Exception):
    """A refusal or a failure that the operator has to resolve, not a bug."""


# ---------------------------------------------------------------------------
# DER
#
# Hand-rolled rather than via `cryptography`, for two reasons. The gateway venv
# is deliberately minimal (gateway/requirements.txt: pycyphal and nothing more),
# and the chip returns a bare 64-byte R||S that has to be re-encoded as a DER
# SEQUENCE of two INTEGERs no matter which library builds the rest — so the
# fiddly part is unavoidable and is better in one tested place than split across
# a dependency boundary. cryptoauthlib's atcacert does the equivalent in C.
# ---------------------------------------------------------------------------

_SEQUENCE = 0x30
_SET = 0x31
_INTEGER = 0x02
_BIT_STRING = 0x03
_UTF8_STRING = 0x0C
_CONTEXT_0 = 0xA0

# Pre-encoded OIDs. Verified by round-tripping the CSR through `openssl req` in
# the test suite, which is the only check that matters for these.
_OID_COMMON_NAME = bytes.fromhex("0603550403")  # 2.5.4.3
_OID_ORGANIZATION = bytes.fromhex("060355040A")  # 2.5.4.10
_OID_EC_PUBLIC_KEY = bytes.fromhex("06072A8648CE3D0201")  # 1.2.840.10045.2.1
_OID_PRIME256V1 = bytes.fromhex("06082A8648CE3D030107")  # 1.2.840.10045.3.1.7
_OID_ECDSA_SHA256 = bytes.fromhex("06082A8648CE3D040302")  # 1.2.840.10045.4.3.2


def _der_length(n: int) -> bytes:
    if n < 0x80:
        return bytes([n])
    body = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(body)]) + body


def _tlv(tag: int, body: bytes) -> bytes:
    return bytes([tag]) + _der_length(len(body)) + body


def _der_positive_integer(value: bytes) -> bytes:
    """DER INTEGER from a fixed-width unsigned big-endian field.

    Minimal encoding, plus a leading zero when the top bit is set — an ECDSA r or
    s with a high first bit is a positive number, and omitting the pad would
    encode it as negative. Roughly half of all signatures hit this, so a missing
    pad is not a rare-path bug; it is a coin flip per component.
    """
    trimmed = value.lstrip(b"\x00") or b"\x00"
    if trimmed[0] & 0x80:
        trimmed = b"\x00" + trimmed
    return _tlv(_INTEGER, trimmed)


def der_signature(raw: bytes) -> bytes:
    """Ecdsa-Sig-Value (RFC 3279) from the chip's 64-byte R||S."""
    if len(raw) != 64:
        raise ProvisioningError(f"expected a 64-byte P-256 R||S signature, got {len(raw)} bytes")
    return _tlv(_SEQUENCE, _der_positive_integer(raw[:32]) + _der_positive_integer(raw[32:]))


def raw_signature(der: bytes) -> bytes:
    """The inverse of der_signature: DER Ecdsa-Sig-Value back to 64-byte R||S.

    Only the software fallback needs this — openssl hands back DER where the chip
    hands back R||S. Keeping the signer contract at R||S means the conversion the
    *hardware* depends on is the one under test.
    """
    if not der or der[0] != _SEQUENCE:
        raise ProvisioningError("signature is not a DER SEQUENCE")
    body, rest = _read_tlv(der[1:])
    if rest:
        raise ProvisioningError("trailing bytes after the signature SEQUENCE")
    out = b""
    for _ in range(2):
        if not body or body[0] != _INTEGER:
            raise ProvisioningError("signature SEQUENCE does not hold two INTEGERs")
        value, body = _read_tlv(body[1:])
        value = value.lstrip(b"\x00")
        if len(value) > 32:
            raise ProvisioningError("signature component is wider than P-256")
        out += value.rjust(32, b"\x00")
    if body:
        raise ProvisioningError("more than two INTEGERs in the signature SEQUENCE")
    return out


def _read_tlv(after_tag: bytes) -> tuple[bytes, bytes]:
    """Split a length-prefixed value (tag already consumed) into (value, remainder)."""
    if not after_tag:
        raise ProvisioningError("truncated DER: no length byte")
    first = after_tag[0]
    if first < 0x80:
        size, offset = first, 1
    else:
        count = first & 0x7F
        if count == 0 or len(after_tag) < 1 + count:
            raise ProvisioningError("truncated or indefinite DER length")
        size = int.from_bytes(after_tag[1 : 1 + count], "big")
        offset = 1 + count
    end = offset + size
    if len(after_tag) < end:
        raise ProvisioningError("truncated DER: value shorter than its length")
    return after_tag[offset:end], after_tag[end:]


def subject_public_key_info(public_key: bytes) -> bytes:
    """SubjectPublicKeyInfo for a P-256 point given as the chip's 64-byte X||Y."""
    if len(public_key) != 64:
        raise ProvisioningError(
            f"expected a 64-byte uncompressed P-256 public key (X||Y), got {len(public_key)}"
        )
    algorithm = _tlv(_SEQUENCE, _OID_EC_PUBLIC_KEY + _OID_PRIME256V1)
    # 0x04 = uncompressed point; the leading 0x00 is the BIT STRING's unused-bit count.
    point = _tlv(_BIT_STRING, b"\x00\x04" + public_key)
    return _tlv(_SEQUENCE, algorithm + point)


def _distinguished_name(common_name: str, organization: str | None) -> bytes:
    rdns = b""
    if organization:
        # Manual §1/§5: the operator name may ride O= for estate consistency; the
        # ERP reads only the CN. O first, so RFC2253 renders CN=… leftmost, which
        # is what pki/sign-csr.sh parses.
        rdns += _tlv(
            _SET, _tlv(_SEQUENCE, _OID_ORGANIZATION + _tlv(_UTF8_STRING, organization.encode()))
        )
    rdns += _tlv(_SET, _tlv(_SEQUENCE, _OID_COMMON_NAME + _tlv(_UTF8_STRING, common_name.encode())))
    return _tlv(_SEQUENCE, rdns)


def _pem(label: str, der: bytes) -> str:
    body = base64.b64encode(der).decode()
    lines = "\n".join(body[i : i + 64] for i in range(0, len(body), 64))
    return f"-----BEGIN {label}-----\n{lines}\n-----END {label}-----\n"


def build_csr(signer: Signer, common_name: str, organization: str | None = None) -> str:
    """A PKCS#10 CSR whose signature is produced by the signer's private key.

    The CN is a UTF8String, not a PrintableString: `GBOX_0001` contains an
    underscore, which PrintableString's alphabet does not include.
    """
    if not GBOX_RE.match(common_name):
        raise ProvisioningError(
            f"CN must be an ADR-0017 machine identifier (GBOX_NNNN), got: {common_name}"
        )

    request_info = _tlv(
        _SEQUENCE,
        _tlv(_INTEGER, b"\x00")  # version v1
        + _distinguished_name(common_name, organization)
        + subject_public_key_info(signer.public_key())
        + _tlv(_CONTEXT_0, b""),  # attributes: [0] IMPLICIT SET, empty
    )

    # The chip signs a digest, never a message — so the digest is computed here
    # and atcab_sign is handed 32 bytes. The software fallback signs the same 32
    # bytes rather than re-hashing, so both backends see an identical interface.
    digest = hashlib.sha256(request_info).digest()
    signature = der_signature(signer.sign_digest(digest))

    return _pem(
        "CERTIFICATE REQUEST",
        _tlv(
            _SEQUENCE,
            request_info
            + _tlv(_SEQUENCE, _OID_ECDSA_SHA256)  # parameters absent (RFC 5758)
            + _tlv(_BIT_STRING, b"\x00" + signature),
        ),
    )


# ---------------------------------------------------------------------------
# Signers
#
# The seam. Both expose the chip's contract — a public key as 64-byte X||Y and a
# signature as 64-byte R||S over a caller-supplied digest — so build_csr above is
# the same code in the field and in CI, and only the thing holding the key
# differs. Same shape as the ERP's FakeWarehouse: the fake exists to make the
# real path testable, not to substitute for it.
# ---------------------------------------------------------------------------


class Signer:
    """Something holding the gateway's P-256 identity key."""

    provenance: str  # "atecc" or "software"; the binding refuses the latter

    def public_key(self) -> bytes:
        raise NotImplementedError

    def sign_digest(self, digest: bytes) -> bytes:
        raise NotImplementedError

    def device_serial(self) -> str | None:
        """The ATECC die serial (manual §7), or None when there is no chip."""
        raise NotImplementedError


def _openssl(*args: str, stdin: bytes | None = None) -> bytes:
    if not shutil.which("openssl"):
        raise ProvisioningError("openssl is not on PATH")
    done = subprocess.run(["openssl", *args], input=stdin, capture_output=True, check=False)
    if done.returncode != 0:
        raise ProvisioningError(
            f"openssl {' '.join(args)} failed: {done.stderr.decode(errors='replace').strip()}"
        )
    return done.stdout


class SoftwareSigner(Signer):
    """A P-256 key in a file — the manual §5 fallback, for CI and laptops.

    A fixture in the same sense as erp/deploy/mtls/make-test-ca.sh: it exercises
    CSR to issuance to install to renewal without a chip, and it is not a
    provisioning procedure. The key is exportable and on disk, which is precisely
    what the ATECC prevents.
    """

    provenance = "software"

    def __init__(self, key_path: Path):
        self.key_path = Path(key_path)
        if not self.key_path.exists():
            raise ProvisioningError(f"no such key: {self.key_path}")

    @classmethod
    def generate(cls, key_path: Path) -> SoftwareSigner:
        key_path = Path(key_path)
        key_path.parent.mkdir(parents=True, exist_ok=True)
        _openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key_path))
        key_path.chmod(0o600)
        return cls(key_path)

    def public_key(self) -> bytes:
        spki = _openssl("ec", "-in", str(self.key_path), "-pubout", "-outform", "DER")
        # A P-256 SPKI is a fixed 26-byte header then 0x04 || X || Y.
        if len(spki) != 91 or spki[26] != 0x04:
            raise ProvisioningError("not a P-256 public key — the fallback key is the wrong curve")
        return spki[27:]

    def sign_digest(self, digest: bytes) -> bytes:
        # pkeyutl -sign over an EC key signs its input *as* the digest without
        # re-hashing, which is what makes this comparable to atcab_sign.
        return raw_signature(
            _openssl("pkeyutl", "-sign", "-inkey", str(self.key_path), stdin=digest)
        )

    def device_serial(self) -> str | None:
        return None


class AteccSigner(Signer):
    """The real thing: slot 0 of an ATECC608B over I2C, via cryptoauthlib.

    NOT EXERCISED — see the module docstring. Needs the `cryptoauthlib` Python
    package (a ctypes wrapper over libcryptoauth), which is not in
    gateway/requirements.txt because a gateway without a provisioned chip has no
    use for it; install it in the venv on the provisioning pass. Imported lazily
    so the software fallback works on a machine that has neither.
    """

    provenance = "atecc"

    def __init__(self, bus: int = 1, address: int = 0x6C, devtype: str = "ATECC608"):
        self.bus, self.address, self.devtype = bus, address, devtype
        self._cal = None

    @property
    def cal(self):
        if self._cal is None:
            try:
                import cryptoauthlib as cal
            except ImportError as exc:  # pragma: no cover - no chip on any test host
                raise ProvisioningError(
                    "cryptoauthlib is not installed. Either install it for the ATECC path\n"
                    "       (pip install cryptoauthlib, plus libcryptoauth on the host), or pass\n"
                    "       --software-key to exercise the flow without a chip (a fixture, not a\n"
                    "       provisioning run)."
                ) from exc
            cfg = cal.cfg_ateccx08a_i2c_default()
            cfg.devtype = cal.get_device_type_id(self.devtype)
            # atcai2c.address is the 8-bit form: 0x6C for a part answering at
            # 7-bit 0x36 (manual §1/§5).
            cfg.cfg.atcai2c.address = self.address
            cfg.cfg.atcai2c.bus = self.bus
            self._check(cal.atcab_init(cfg), "atcab_init")
            self._cal = cal
        return self._cal

    def _check(self, status: int, what: str) -> None:
        import cryptoauthlib as cal

        if status != cal.Status.ATCA_SUCCESS:
            raise ProvisioningError(f"{what} failed: cryptoauthlib status 0x{status:02X}")

    def public_key(self) -> bytes:
        """The slot-0 public key, derived on demand.

        `atcab_get_pubkey` recomputes it from the private key (PubInfo=1 in the
        §3 KeyConfig) rather than reading a stored copy — so renewal re-certifies
        the same key with no stored public half to drift (manual §9).
        """
        cal = self.cal
        buf = bytearray(64)
        self._check(cal.atcab_get_pubkey(IDENTITY_SLOT, buf), "atcab_get_pubkey")
        return bytes(buf)

    def sign_digest(self, digest: bytes) -> bytes:
        if len(digest) != 32:
            raise ProvisioningError("the ATECC signs a 32-byte digest")
        cal = self.cal
        buf = bytearray(64)
        self._check(cal.atcab_sign(IDENTITY_SLOT, digest, buf), "atcab_sign")
        return bytes(buf)

    def device_serial(self) -> str | None:
        cal = self.cal
        buf = bytearray(9)
        self._check(cal.atcab_read_serial_number(buf), "atcab_read_serial_number")
        return bytes(buf).hex().upper()

    # -- the irreversible part (manual §4) ---------------------------------

    def read_config_zone(self) -> bytes:
        cal = self.cal
        buf = bytearray(CONFIG_ZONE_SIZE)
        self._check(cal.atcab_read_config_zone(buf), "atcab_read_config_zone")
        return bytes(buf)

    def is_config_locked(self) -> bool:
        cal = self.cal
        locked = cal.AtcaReference(False)
        self._check(cal.atcab_is_config_locked(locked), "atcab_is_config_locked")
        return bool(locked.value)

    def is_data_locked(self) -> bool:
        cal = self.cal
        locked = cal.AtcaReference(False)
        self._check(cal.atcab_is_data_locked(locked), "atcab_is_data_locked")
        return bool(locked.value)

    def write_config_zone(self, config: bytes) -> None:
        self._check(self.cal.atcab_write_config_zone(bytes(config)), "atcab_write_config_zone")

    def lock_config_zone(self) -> None:
        self._check(self.cal.atcab_lock_config_zone(), "atcab_lock_config_zone")

    def lock_data_zone(self) -> None:
        self._check(self.cal.atcab_lock_data_zone(), "atcab_lock_data_zone")

    def lock_slot(self, slot: int) -> None:
        self._check(self.cal.atcab_lock_data_slot(slot), "atcab_lock_data_slot")

    def genkey(self) -> bytes:
        cal = self.cal
        buf = bytearray(64)
        self._check(cal.atcab_genkey(IDENTITY_SLOT, buf), "atcab_genkey")
        return bytes(buf)


# ---------------------------------------------------------------------------
# Config zone
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ConfigDiff:
    offset: int
    name: str
    present: bytes
    wanted: bytes

    def __str__(self) -> str:
        return (
            f"  byte {self.offset:>3}  {self.name:<14} "
            f"{self.present.hex(' ')} -> {self.wanted.hex(' ')}"
        )


def config_diffs(present: bytes, wanted: bytes) -> list[ConfigDiff]:
    """Every byte that a write would change on *this* part, one entry per byte.

    Separate from patch_config_zone because with `--config-template` the two
    questions differ: patch_config_zone says what the §3 words do to the template,
    while the operator about to lock a part needs what changes on the part in front
    of them. Showing the former would understate a template that disagrees with the
    part anywhere else — and every one of those bytes locks just as permanently.
    """
    # Both bytes of each pinned word are labelled: a word is two bytes and either
    # of them changing is that word changing.
    names = {
        SLOT_CONFIG_0_OFFSET: "SlotConfig[0]",
        SLOT_CONFIG_0_OFFSET + 1: "SlotConfig[0]",
        KEY_CONFIG_0_OFFSET: "KeyConfig[0]",
        KEY_CONFIG_0_OFFSET + 1: "KeyConfig[0]",
    }
    return [
        ConfigDiff(i, names.get(i, ""), present[i : i + 1], wanted[i : i + 1])
        for i in range(CONFIG_ZONE_SIZE)
        if present[i] != wanted[i]
    ]


def patch_config_zone(present: bytes) -> tuple[bytes, list[ConfigDiff]]:
    """Set the two §3 words in a config zone read off the part, and nothing else.

    Only slot 0's policy is pinned by ADR-0007 d9 and the manual. The 608B's full
    config map is under NDA, so the other 126 bytes are the part's own factory
    default: patching what we know in place beats writing 128 invented bytes,
    where a wrong byte outside slot 0 is locked in just as permanently as a wrong
    byte inside it. `--config-template` is there for an operator who has a
    hardware-validated template of their own.
    """
    if len(present) != CONFIG_ZONE_SIZE:
        raise ProvisioningError(f"a config zone is {CONFIG_ZONE_SIZE} bytes, got {len(present)}")

    wanted = bytearray(present)
    diffs: list[ConfigDiff] = []
    for offset, value, name in (
        (SLOT_CONFIG_0_OFFSET, SLOT_CONFIG_0, "SlotConfig[0]"),
        (KEY_CONFIG_0_OFFSET, KEY_CONFIG_0, "KeyConfig[0]"),
    ):
        encoded = struct.pack("<H", value)  # little-endian in the config zone
        if bytes(present[offset : offset + 2]) != encoded:
            diffs.append(ConfigDiff(offset, name, bytes(present[offset : offset + 2]), encoded))
        wanted[offset : offset + 2] = encoded
    return bytes(wanted), diffs


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------


def _signer_from(args: argparse.Namespace) -> Signer:
    if getattr(args, "software_key", None):
        path = Path(args.software_key)
        if not path.exists():
            print(
                f"generating a software fallback key at {path} (a fixture, not an identity)",
                file=sys.stderr,
            )
            return SoftwareSigner.generate(path)
        return SoftwareSigner(path)
    return AteccSigner(bus=args.bus, address=args.address)


def cmd_detect(args: argparse.Namespace) -> int:
    """Manual §4 step 1 — is there a part on the bus, and what state is it in?"""
    if not shutil.which("i2cdetect"):
        raise ProvisioningError("i2cdetect is not on PATH (apt install i2c-tools)")
    done = subprocess.run(
        ["i2cdetect", "-y", str(args.bus)], capture_output=True, text=True, check=False
    )
    if done.returncode != 0:
        raise ProvisioningError(f"i2cdetect failed: {done.stderr.strip()}")
    print(done.stdout, end="")
    found = [addr for addr in ("60", "36") if re.search(rf"\b{addr}\b", done.stdout)]
    if not found:
        print(
            f"no ATECC on bus {args.bus}: expected 7-bit 0x60 (blank TrustCUSTOM) "
            "or 0x36 (configured)",
            file=sys.stderr,
        )
        return 1
    for addr in found:
        state = "blank TrustCUSTOM" if addr == "60" else "configured/TFLXTLS"
        print(
            f"ATECC at 7-bit 0x{addr} ({state}); cryptoauthlib wants 8-bit "
            f"0x{int(addr, 16) << 1:02X}"
        )
    return 0


def cmd_keygen(args: argparse.Namespace) -> int:
    """Manual §4 steps 2-5: write config, lock it, generate the key, read anchors.

    Both locks here are one-way. The order is load-bearing — GenKey into a private
    slot is rejected while the config zone is unlocked, so the freeze has to come
    first — and the sequence must survive power loss between steps, which is why
    the optional slot lock is a separate subcommand run after the CA has returned
    a working certificate.
    """
    if getattr(args, "software_key", None):
        raise ProvisioningError(
            "keygen is the on-chip sequence; there is nothing to configure or lock in a\n"
            "       software fixture. `csr --software-key PATH` generates the key it needs."
        )

    signer = AteccSigner(bus=args.bus, address=args.address)

    if args.config_template:
        template = Path(args.config_template).read_bytes()
        if len(template) != CONFIG_ZONE_SIZE:
            raise ProvisioningError(
                f"{args.config_template} is {len(template)} bytes, not {CONFIG_ZONE_SIZE}"
            )
        wanted, _ = patch_config_zone(template)
        present = signer.read_config_zone()
        # The first 16 bytes are read-only (serial, revision); write_config_zone
        # skips them, so a template disagreeing there is a template for a
        # different part, not a policy to apply.
        if template[:16] != present[:16]:
            raise ProvisioningError(
                "the template's read-only header does not match this part — it was captured "
                "from a different chip"
            )
    else:
        present = signer.read_config_zone()
        wanted, _ = patch_config_zone(present)

    # Always against the part in front of the operator, never against the template.
    diffs = [d for d in config_diffs(present, wanted) if d.offset >= 16]

    locked = signer.is_config_locked()
    print(f"config zone: {'LOCKED' if locked else 'unlocked'}")

    if locked:
        # A locked part is either already ours or unusable for this policy. Saying
        # which one, instead of failing at genkey, is the difference between "resume
        # provisioning" and "this part cannot hold our identity".
        if present[SLOT_CONFIG_0_OFFSET : SLOT_CONFIG_0_OFFSET + 2] != struct.pack(
            "<H", SLOT_CONFIG_0
        ) or present[KEY_CONFIG_0_OFFSET : KEY_CONFIG_0_OFFSET + 2] != struct.pack(
            "<H", KEY_CONFIG_0
        ):
            raise ProvisioningError(
                "this part's config zone is locked with a slot-0 policy that is not the\n"
                "       ADR-0007 d9 one (manual §3). Locked config cannot be changed: the part\n"
                "       cannot hold a gateway identity. A TrustFLEX part looks like this."
            )
        print("slot 0 already carries the ADR-0007 d9 policy; skipping write and lock")
    else:
        if diffs:
            print("config-zone changes to be written and then locked FOREVER:")
            for diff in diffs:
                print(diff)
        else:
            print("the part already reads back the §3 words; the lock is still required")
        if args.dry_run:
            print(
                "\ndry run: nothing written, nothing locked. Do this on a sacrificial part "
                "first\n(manual §3) and diff the read-back before you lock a field unit."
            )
            return 0
        if not args.confirm_irreversible:
            raise ProvisioningError(
                "refusing to write and lock the config zone without --confirm-irreversible.\n"
                "       There is no unlock and no factory reset: a wrong value scraps the part.\n"
                "       Run with --dry-run first, and prove the whole flow on a sacrificial part."
            )
        signer.write_config_zone(wanted)

        # Manual §3: read back and diff byte-for-byte *before* the lock. This is
        # the last moment at which a bad write is still recoverable.
        readback = signer.read_config_zone()
        if readback[16:] != wanted[16:]:
            raise ProvisioningError(
                "the config zone did not read back as written — NOT locking. Compare the "
                "read-back against the template before retrying."
            )
        print("config zone read back byte-for-byte; locking")
        signer.lock_config_zone()

    public_key = signer.genkey() if not args.reuse_key else signer.public_key()
    serial = signer.device_serial()

    anchors = {
        "atecc_serial": serial,
        "public_key": public_key.hex().upper(),
        "public_key_sha256": public_key_fingerprint(public_key),
        "provenance": "atecc",
    }
    print(json.dumps(anchors, indent=2), flush=True)
    if args.out:
        Path(args.out).write_text(json.dumps(anchors, indent=2) + "\n")
        print(f"anchors written to {args.out}", file=sys.stderr)
    print(
        "\nnext: csr --gbox GBOX_NNNN, then pki/sign-csr.sh, then install. Leave slot 0\n"
        "unlocked until the CA has returned a working certificate (lock-slot).",
        file=sys.stderr,
    )
    return 0


def cmd_csr(args: argparse.Namespace) -> int:
    """Manual §5 — and §9: renewal is this same command over the same key.

    Nothing here regenerates or touches the key. A renewal is a re-certification:
    derive the public half, build a fresh CSR, have the CA sign it, install the
    new chain.
    """
    signer = _signer_from(args)
    csr = build_csr(signer, args.gbox, args.organization)
    if args.out:
        Path(args.out).write_text(csr)
        print(
            f"CSR for {args.gbox} written to {args.out} (key provenance: {signer.provenance})",
            file=sys.stderr,
        )
    else:
        print(csr, end="")
    return 0


def public_key_fingerprint(public_key: bytes) -> str:
    """SHA-256 over the SubjectPublicKeyInfo — the stable anchor.

    ADR-0007 rev 1 d10d and ADR-0024's deferred deny-list note both key on this
    rather than on a certificate serial, because it survives renewal and
    re-certification under another operator: the key is generated once and never
    regenerated (manual §9).
    """
    return hashlib.sha256(subject_public_key_info(public_key)).hexdigest().upper()


def _certificates_in(pem: str) -> list[str]:
    return re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.DOTALL)


def _cert_field(path: Path, *args: str) -> str:
    return _openssl("x509", "-in", str(path), "-noout", *args).decode().strip()


def _openssl_date(field: str) -> str:
    """openssl's `notAfter=Jul 26 11:20:02 2026 GMT` as an ISO-8601 instant.

    The API takes a datetime, and openssl's default format is not one. Parsed
    rather than reformatted with `-dateopt iso_8601`, which OpenSSL 1.1 does not
    have and the Pi may still be carrying.
    """
    value = field.split("=", 1)[-1].strip()
    for fmt in ("%b %d %H:%M:%S %Y %Z", "%b %d %H:%M:%S %Y"):
        try:
            return datetime.strptime(value, fmt).replace(tzinfo=UTC).isoformat()
        except ValueError:
            continue
    raise ProvisioningError(f"cannot read the certificate date {value!r}")


def _cert_common_name(path: Path) -> str:
    subject = _cert_field(path, "-subject", "-nameopt", "RFC2253")
    match = re.search(r"\bCN=([^,]+)", subject)
    if not match:
        raise ProvisioningError(f"certificate has no CN: {subject}")
    return match.group(1)


def cmd_install(args: argparse.Namespace) -> int:
    """Manual §6 — install the chain, not the bare leaf and not the root."""
    chain_path = Path(args.chain)
    certs = _certificates_in(chain_path.read_text())
    if not certs:
        raise ProvisioningError(f"{chain_path} holds no PEM certificate")
    if len(certs) < 2:
        # The two-tier fallout ADR-0024 turned up the hard way: a peer anchored on
        # the operator root cannot build a path to a leaf issued by the
        # intermediate unless the intermediate travels with it (d1/d3).
        raise ProvisioningError(
            f"{chain_path} holds one certificate. Install the chain — leaf plus issuing\n"
            "       intermediate, which is what sign-csr.sh writes as *-chain.crt. A peer\n"
            "       anchored on the operator root cannot build a path from the bare leaf\n"
            "       (ADR-0024 d1/d3)."
        )

    common_name = _cert_common_name(chain_path)  # openssl reads the first cert: the leaf
    if common_name != args.gbox:
        raise ProvisioningError(
            f"the leaf's CN is {common_name}, not {args.gbox}. The ERP derives the caller's\n"
            "       identity from this string (ADR-0022 d2), so this certificate would "
            "authenticate as something else."
        )

    install_dir = Path(args.dir)
    install_dir.mkdir(parents=True, exist_ok=True)
    target = install_dir / "gateway-chain.crt"
    target.write_text(chain_path.read_text())
    target.chmod(0o644)
    print(f"installed {target} ({len(certs)} certificates, leaf CN={common_name})")

    if args.anchor:
        anchor = Path(args.anchor)
        if not _certificates_in(anchor.read_text()):
            raise ProvisioningError(f"{anchor} holds no PEM certificate")
        # The anchor is the operator root (ADR-0024 d3) — self-issued, so subject
        # equals issuer. Catching a chain file passed here keeps the gateway from
        # anchoring on an intermediate, which would trust whatever the root signs
        # next rather than the root itself.
        if _cert_field(anchor, "-subject", "-nameopt", "RFC2253").removeprefix("subject=") != (
            _cert_field(anchor, "-issuer", "-nameopt", "RFC2253").removeprefix("issuer=")
        ):
            raise ProvisioningError(
                f"{anchor} is not self-issued, so it is not the operator root. The trust "
                "anchor is the root (ADR-0024 d3), not the issuing intermediate."
            )
        anchor_target = install_dir / "operator-root.crt"
        anchor_target.write_text(anchor.read_text())
        anchor_target.chmod(0o644)
        print(f"installed trust anchor {anchor_target}")

    print(
        f"not installed: a private key. It is in slot {IDENTITY_SLOT} of the ATECC and "
        "nowhere else\n(ADR-0007 d1) — there is no key file on this host to protect."
    )
    expiry = _cert_field(chain_path, "-enddate").removeprefix("notAfter=")
    print(f"expires {expiry} — renew with `csr` over the same key, which never changes")
    return 0


def cmd_lock_slot(args: argparse.Namespace) -> int:
    """Manual §4 step 8 — the optional one-way burn-in of this key."""
    if not args.confirm_irreversible:
        raise ProvisioningError(
            "refusing to lock slot 0 without --confirm-irreversible. This permanently blocks\n"
            "       any future GenKey on the slot. Do it only once a certificate issued from\n"
            "       this key is installed and working — before that, it removes your ability\n"
            "       to regenerate the key if issuance failed."
        )
    signer = AteccSigner(bus=args.bus, address=args.address)
    if not args.skip_data_zone_lock:
        signer.lock_data_zone()
        print("data zone locked")
    signer.lock_slot(IDENTITY_SLOT)
    print(f"slot {IDENTITY_SLOT} locked: this key is now permanent on this part")
    return 0


def cmd_binding(args: argparse.Namespace) -> int:
    """Manual §8 — the handoff, not a schema.

    These are the local values §8 says this step captures, in a file the operator
    hands to the ERP. It is deliberately NOT the provisioning record's schema:
    that is ADR-deferred (ADR-0007 / ADR-0024, board card 17), and whether a
    gateway's machine-scoped binding reuses ADR-0022 d5's route or needs its own
    is not settled in any ADR. Naming the inputs is not deciding the record.
    """
    signer = _signer_from(args)
    public_key = signer.public_key()

    if signer.provenance != "atecc" and not args.fixture:
        # Manual §5: "Do not let a software-key CSR reach a production binding."
        # An exportable key recorded as a hardware anchor makes the whole binding
        # a claim about a property the key does not have.
        raise ProvisioningError(
            "refusing to write a binding for a software key. The binding asserts a hardware\n"
            "       anchor (ADR-0007 d1/d6) that an exportable file key does not have. Pass\n"
            "       --fixture to mark the output as a pipeline exercise, never a real unit."
        )

    # Shaped as the ERP's machine-binding request body (ADR-0022 rev 1 d12), so
    # this file is submittable as-is rather than hand-translated:
    #   curl --cert … --key … -H 'Content-Type: application/json' \
    #        -d @GBOX_0001-binding.json  https://erp/api/v1/machines/GBOX_0001/provisioning
    # When the manual was written the record's schema was still deferred and this
    # emitted its own names; d12 settled it for machines, so it follows the API.
    record = {
        "machine_id": args.gbox,
        "vendor_serial": args.vendor_serial,
        "atecc_serial": signer.device_serial(),
        "public_key_fingerprint": public_key_fingerprint(public_key),
        "provenance": signer.provenance,
    }
    if args.cert:
        cert = Path(args.cert)
        named = _cert_common_name(cert)
        if named != args.gbox:
            raise ProvisioningError(f"the certificate names {named}, not {args.gbox}")
        # Flat, because that is the request body's shape (ADR-0022 rev 1 d12).
        # These describe one issuance, not the unit: the serial and validity are
        # replaced at every renewal, while machine_id and the fingerprint above
        # are what survive it (ADR-0007 rev 1 d10d). The API upserts on that
        # basis, so re-certification submits this file again.
        record["cert_serial"] = _cert_field(cert, "-serial").removeprefix("serial=")
        record["cert_not_before"] = _openssl_date(_cert_field(cert, "-startdate"))
        record["cert_not_after"] = _openssl_date(_cert_field(cert, "-enddate"))
        record["issuer"] = _cert_field(cert, "-issuer", "-nameopt", "RFC2253").removeprefix(
            "issuer="
        )
    if signer.provenance != "atecc":
        record["fixture"] = "software key — exercises the pipeline, not a provisioned unit"

    # ensure_ascii=False so the record reads as written; it is handed to a person
    # before it is handed to the ERP.
    text = json.dumps(record, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(f"binding inputs written to {args.out}", file=sys.stderr)
    else:
        print(text, end="", flush=True)  # before the stderr note, not interleaved
    print(
        "public material only — no private key is in here and none can be (ADR-0007 d1/d6).\n"
        f"Submit to the ERP as machine {args.gbox}'s provisioning binding; the record's own\n"
        "schema is ADR-deferred, so this is its inputs (manual §8). The top-level values are\n"
        "the stable anchors; everything under `certificate` is one issuance's envelope and is\n"
        "not an identity to key on (ADR-0007 d10d).",
        file=sys.stderr,
    )
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _add_chip_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--bus", type=int, default=1, help="I2C bus (/dev/i2c-N); default 1")
    parser.add_argument(
        "--address",
        type=lambda v: int(v, 0),
        default=0x6C,
        help="ATECC I2C address, 8-bit form as cryptoauthlib wants it; default 0x6C",
    )


def _add_software_arg(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--software-key",
        metavar="PATH",
        help="use a P-256 key file instead of a chip, generating it if absent. The manual "
        "§5 fallback: a fixture for CI and laptops, never a provisioned unit.",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="provision_identity.py",
        description="Gateway ATECC608 identity provisioning "
        "(store/SP0004-M-atecc-provisioning.md).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("detect", help="find the ATECC on the I2C bus (manual §4.1)")
    _add_chip_args(p)
    p.set_defaults(func=cmd_detect)

    p = sub.add_parser(
        "keygen",
        help="write and lock the slot-0 policy, then generate the on-chip key "
        "(manual §4.2-5) — IRREVERSIBLE",
    )
    _add_chip_args(p)
    _add_software_arg(p)
    p.add_argument(
        "--confirm-irreversible",
        action="store_true",
        help="acknowledge that the config lock cannot be undone",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="show the config-zone changes without writing or locking",
    )
    p.add_argument(
        "--config-template",
        metavar="PATH",
        help="128-byte config zone captured from a hardware-validated part, "
        "patched with the §3 words before writing",
    )
    p.add_argument(
        "--reuse-key",
        action="store_true",
        help="do not GenKey; read the existing slot-0 public key (resume a "
        "provisioning run that was interrupted after keygen)",
    )
    p.add_argument("--out", metavar="PATH", help="write the public anchors as JSON")
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("csr", help="build a CSR signed by slot 0 (manual §5; also §9 renewal)")
    _add_chip_args(p)
    _add_software_arg(p)
    p.add_argument("--gbox", required=True, help="the machine identifier, e.g. GBOX_0001")
    p.add_argument("--organization", help="optional subject O=; the ERP reads only the CN")
    p.add_argument("--out", metavar="PATH", help="write the CSR here instead of stdout")
    p.set_defaults(func=cmd_csr)

    p = sub.add_parser("install", help="install the issued chain (manual §6)")
    p.add_argument("--gbox", required=True, help="the machine identifier the leaf must name")
    p.add_argument("--chain", required=True, help="the *-chain.crt sign-csr.sh wrote")
    p.add_argument("--anchor", help="the operator root to install as the trust anchor")
    p.add_argument(
        "--dir",
        default=str(DEFAULT_INSTALL_DIR),
        help=f"install directory; default {DEFAULT_INSTALL_DIR}",
    )
    p.set_defaults(func=cmd_install)

    p = sub.add_parser(
        "lock-slot", help="burn in this key permanently (manual §4.8) — IRREVERSIBLE"
    )
    _add_chip_args(p)
    p.add_argument("--confirm-irreversible", action="store_true")
    p.add_argument(
        "--skip-data-zone-lock",
        action="store_true",
        help="lock only slot 0, leaving the data zone writable",
    )
    p.set_defaults(func=cmd_lock_slot)

    p = sub.add_parser("binding", help="emit the provisioning-binding inputs (manual §8)")
    _add_chip_args(p)
    _add_software_arg(p)
    p.add_argument("--gbox", required=True)
    p.add_argument("--vendor-serial", required=True, help="the SP0004 vendor serial (ADR-0019 d2)")
    p.add_argument("--cert", help="the issued leaf, for its serial and validity")
    p.add_argument(
        "--fixture",
        action="store_true",
        help="permit a software key, marking the output as a pipeline exercise",
    )
    p.add_argument("--out", metavar="PATH")
    p.set_defaults(func=cmd_binding)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except ProvisioningError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
