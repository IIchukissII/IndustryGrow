#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Operator-side profile signing — the authoring half of ADR-0025.

A gateway will not apply a profile it cannot verify (ADR-0015 d7), and the key
that makes a profile verifiable is the operator's, held here and never in the ERP
container or on a gateway (ADR-0025 d3). This is the tool that holds it.

    sign_profile.py keygen --dir ./profile-key --pass env:SIGNPW
    sign_profile.py sign   --dir ./profile-key --in gbox0001-v43.json --pass env:SIGNPW
    sign_profile.py verify --pubkey ./profile-key/profile-verify.pub \\
                           --in gbox0001-v43.json --sig gbox0001-v43.json.sig

**This tool never modifies the document it signs.** The signature covers the
document's exact bytes (ADR-0025 d6), so the one way to guarantee the gateway
verifies what was signed is to not rewrite the file — not to rewrite it carefully.
It reads the bytes, parses a throwaway copy to check what it is about to vouch
for, and writes a detached `.sig` beside it.

This is a *third* trust root, distinct from the operator CA in `pki/` (ADR-0024,
identity) and the firmware-signing key (ADR-0004 d12, code) — see ADR-0025 d4.
Do not reuse either of those keys here, and do not use this one for either of
them: the separation is what keeps a compromise of one from implicating the
others.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

GBOX_RE = re.compile(r"^GBOX_[0-9]{4}$")

KEY_NAME = "profile-signing.key"
PUB_NAME = "profile-verify.pub"


class SigningError(Exception):
    """A refusal or failure the operator has to resolve."""


def openssl(*args: str, stdin: bytes | None = None) -> bytes:
    if not shutil.which("openssl"):
        raise SigningError("openssl is not on PATH")
    done = subprocess.run(["openssl", *args], input=stdin, capture_output=True, check=False)
    if done.returncode != 0:
        raise SigningError(
            f"openssl {args[0]} failed: {done.stderr.decode(errors='replace').strip()}"
        )
    return done.stdout


# ---------------------------------------------------------------------------
# What the signature covers
#
# ADR-0025 d6: the exact bytes of the document, and nothing constructed. There is
# no canonical form to agree on, no field list to keep in sync, and no pre-image
# builder that could omit a field — which is why the verifier on the gateway can
# be a handful of lines that share no code with this file and still agree with it.
# ---------------------------------------------------------------------------


def sign_bytes(key_path: Path, passphrase: str | None, document: bytes) -> str:
    """A detached ECDSA-P256/SHA-256 signature over `document`, base64 DER."""
    args = ["dgst", "-sha256", "-sign", str(key_path)]
    if passphrase:
        args += ["-passin", passphrase]
    return base64.b64encode(openssl(*args, stdin=document)).decode()


def verify_bytes(pubkey_path: Path, document: bytes, signature_b64: str) -> bool:
    """True when `signature_b64` is a valid signature over `document`.

    Deliberately the whole check: no length heuristics, no "looks like base64"
    pre-screen. A malformed signature is an invalid signature.
    """
    try:
        raw = base64.b64decode(signature_b64, validate=True)
    except Exception:
        return False

    # openssl needs the signature in a file.
    fd, sig_path = tempfile.mkstemp()
    try:
        os.write(fd, raw)
        os.close(fd)
        openssl(
            "dgst", "-sha256", "-verify", str(pubkey_path), "-signature", sig_path, stdin=document
        )
    except SigningError:
        return False
    finally:
        Path(sig_path).unlink(missing_ok=True)
    return True


# ---------------------------------------------------------------------------
# What the document has to say about itself
# ---------------------------------------------------------------------------


def document_identity(document: bytes) -> tuple[str, str]:
    """The machine and version the document declares (ADR-0025 d7).

    Read from a throwaway parse purely to validate and display. The signature
    covers the bytes, so what the gateway trusts is what it finds inside them
    after verifying — never a value alongside them.
    """
    try:
        parsed = json.loads(document)
    except json.JSONDecodeError as exc:
        raise SigningError(f"not a JSON document: {exc}") from exc
    if not isinstance(parsed, dict):
        raise SigningError("a profile document is a JSON object")

    machine = parsed.get("machine_id")
    version = parsed.get("version_tag")
    if not machine or not version:
        raise SigningError(
            "the document must carry `machine_id` and `version_tag` (ADR-0025 d7): one\n"
            "       signature binds content, addressee and version together, so a document\n"
            "       that does not name its cabinet and version cannot be bound to them.\n"
            "       A community template carries neither — instantiate it for a cabinet first."
        )
    if not GBOX_RE.match(str(machine)):
        raise SigningError(f"machine_id must be an ADR-0017 machine identifier, got: {machine}")
    return str(machine), str(version)


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------


def cmd_keygen(args: argparse.Namespace) -> int:
    """Generate the operator's profile-signing key.

    Encrypted at rest, which ADR-0025 d5 makes the baseline: this key is used
    every time anyone changes a setpoint, so it gets custody proportionate to
    that cadence rather than the CA root's ceremony. An operator who wants it on
    a token or an offline host loses nothing by doing that.
    """
    directory = Path(args.dir)
    key = directory / KEY_NAME
    pub = directory / PUB_NAME
    if key.exists() and not args.force:
        raise SigningError(
            f"{key} exists. Generating over it would orphan every profile signed with it —\n"
            "       gateways would reject the lot. Pass --force only if that is what you want."
        )
    if not args.passphrase:
        raise SigningError(
            "--pass is required: an unencrypted profile-signing key on a workstation is the\n"
            "       whole authority over what a cabinet does, sitting in a file (ADR-0025 d5)."
        )

    directory.mkdir(parents=True, exist_ok=True)
    openssl(
        "genpkey",
        "-algorithm",
        "EC",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-aes-256-cbc",
        "-pass",
        args.passphrase,
        "-out",
        str(key),
    )
    key.chmod(0o600)
    pub.write_bytes(openssl("pkey", "-in", str(key), "-passin", args.passphrase, "-pubout"))
    pub.chmod(0o644)

    print(f"profile-signing key: {key}      (encrypted; back it up, losing it means re-signing)")
    print(f"verification key:    {pub}      (public — provision this onto each gateway)")
    print(
        "\nthis is a third trust root, separate from pki/'s operator CA and the firmware-signing\n"
        "key (ADR-0025 d4). Keep the three apart, and record which is which.",
        file=sys.stderr,
    )
    return 0


def _key_and_pub(args: argparse.Namespace) -> tuple[Path, Path]:
    if args.dir:
        return Path(args.dir) / KEY_NAME, Path(args.dir) / PUB_NAME
    if not args.key:
        raise SigningError("pass --dir (the keygen directory) or --key")
    return Path(args.key), Path(args.key).with_name(PUB_NAME)


def cmd_sign(args: argparse.Namespace) -> int:
    """Sign a profile document, leaving the document itself untouched."""
    key, pub = _key_and_pub(args)
    if not key.exists():
        raise SigningError(f"no signing key at {key} — run keygen first")

    source = Path(args.input)
    document = source.read_bytes()
    machine, version = document_identity(document)
    if args.gbox and machine != args.gbox:
        raise SigningError(f"the document names {machine}, not the {args.gbox} you asked for")

    signature = sign_bytes(key, args.passphrase, document)

    out = Path(args.out) if args.out else source.with_name(source.name + ".sig")
    out.write_text(signature + "\n")

    # Verify what was just produced, against the public half rather than the
    # private one. A signature that does not verify is worse than none: it fails
    # at a cabinet, hours later, looking like a delivery problem.
    if pub.exists() and not verify_bytes(pub, document, signature):
        out.unlink(missing_ok=True)
        raise SigningError("the signature just produced does not verify — refusing to emit it")

    print(f"signed {machine} {version}")
    print(f"  document:  {source}   (unmodified — the signature covers these exact bytes)")
    print(f"  signature: {out}")
    print(
        "\nupload both to the ERP. The gateway will refuse a version that is not newer than\n"
        "the one it runs (ADR-0025 d8), so a rollback is a new higher version, not this one\n"
        "re-activated.",
        file=sys.stderr,
    )
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    """Pre-flight the same check the gateway will make."""
    document = Path(args.input).read_bytes()
    signature = Path(args.sig).read_text().strip()
    pubkey = Path(args.pubkey)
    if not pubkey.exists():
        raise SigningError(f"no verification key at {pubkey}")

    if not verify_bytes(pubkey, document, signature):
        raise SigningError(
            "signature does NOT verify. A gateway would reject this and keep running its\n"
            "       previous profile (ADR-0015 d7). Common cause: the document was edited,\n"
            "       reformatted, or re-serialised after signing — the signature covers bytes."
        )
    machine, version = document_identity(document)
    print(f"signature verifies: {machine} {version}")
    if args.gbox and machine != args.gbox:
        raise SigningError(
            f"...but it names {machine}, so {args.gbox} would reject it (ADR-0025 d7)"
        )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sign_profile.py",
        description="Operator-side cultivation-profile signing (ADR-0025).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("keygen", help="generate the operator's profile-signing key")
    p.add_argument("--dir", required=True, help="directory to hold the key pair")
    p.add_argument(
        "--pass",
        dest="passphrase",
        metavar="SPEC",
        help="openssl passphrase spec, e.g. env:SIGNPW or file:pw.txt",
    )
    p.add_argument(
        "--force",
        action="store_true",
        help="overwrite an existing key, orphaning everything signed with it",
    )
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("sign", help="emit a detached signature for a profile document")
    p.add_argument("--dir", help="the keygen directory")
    p.add_argument("--key", help="the signing key, if not using --dir")
    p.add_argument("--in", dest="input", required=True, help="the profile document")
    p.add_argument("--out", help="signature path; default <document>.sig")
    p.add_argument("--gbox", help="assert the document names this machine")
    p.add_argument("--pass", dest="passphrase", metavar="SPEC")
    p.set_defaults(func=cmd_sign)

    p = sub.add_parser("verify", help="check a signature the way the gateway will")
    p.add_argument("--pubkey", required=True)
    p.add_argument("--in", dest="input", required=True)
    p.add_argument("--sig", required=True)
    p.add_argument("--gbox", help="assert the document names this machine")
    p.set_defaults(func=cmd_verify)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except SigningError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
