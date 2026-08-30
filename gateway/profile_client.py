#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gateway profile-pull client — the consumer ADR-0015 decision 5 describes.

The ERP serves a machine its active profile over the mTLS channel (ADR-0022 d8)
and nothing consumed it; `/etc/industrygrow/active-profile.json` was written by
nothing. This is that consumer: pull, verify, apply atomically, keep the last
known good, and back off when the ERP is unreachable.

    profile_client.py once   # one pull; what the systemd timer runs
    profile_client.py show   # what is installed right now
    profile_client.py serve  # poll on an interval, for a host without timers

Every step that could apply the wrong thing refuses instead:

  * the signature must verify against the provisioned key (ADR-0015 d7, the
    requirement; ADR-0025, the scheme). No key configured means no pull is
    applied — this is fail-closed, like the ERP's own gateway routes;
  * the document must name *this* machine, read from the client certificate's CN
    rather than from configuration, so the two ends cannot drift (ADR-0025 d7);
  * the version must be newer than the running one, because a signature does not
    expire and an old artifact is otherwise indistinguishable from a current one
    (ADR-0025 d8);
  * everything authoritative is read from *inside* the signed bytes. The envelope
    the ERP wraps them in is unsigned and is used only to locate them.

On failure the previous profile stays active and the gateway keeps controlling
the cabinet (ADR-0015 d7; ADR-0020 d11). Nothing here ever deletes the active
profile: there is no failure mode whose correct response is leaving the cabinet
with no setpoints.
"""

from __future__ import annotations

import argparse
import base64
import contextlib
import json
import os
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

GBOX_RE = re.compile(r"^GBOX_[0-9]{4}$")

# ADR-0015 d4 fixes the active-profile path; the rest sit beside it. The
# last-known-good copy is what ADR-0020 d11's cold boot resumes from when the
# current file is unreadable.
CONFIG_DIR = Path(os.environ.get("IGROW_CONFIG_DIR", "/etc/industrygrow"))
ACTIVE_PROFILE = CONFIG_DIR / "active-profile.json"
LAST_KNOWN_GOOD = CONFIG_DIR / "active-profile.last-known-good.json"
PKI_DIR = CONFIG_DIR / "pki"

# ADR-0015 d5: poll periodically, default 60s. The value lives here and in the
# systemd timer; the timer is what actually runs in production.
DEFAULT_INTERVAL = 60
DEFAULT_TIMEOUT = 20


class ProfileError(Exception):
    """A refusal. The previous profile stays active and the loop keeps running."""


def log(message: str) -> None:
    # journald captures stdout for the unit, as it does for gateway_selftest.
    print(f"[profile-client] {message}", flush=True)


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Config:
    url: str
    chain: Path  # the gateway's leaf + issuing intermediate (ADR-0024 d1/d3)
    anchor: Path  # the operator root the ERP's server cert is checked against
    verify_key: Path | None  # the ADR-0025 profile-verification public key
    # The private key, when it is not inside the chain file. Defaulted so a
    # caller that predates the split still constructs.
    key: Path | None = None
    timeout: int = DEFAULT_TIMEOUT
    interval: int = DEFAULT_INTERVAL

    @classmethod
    def from_env(cls) -> Config:
        url = os.environ.get("IGROW_ERP_URL", "").rstrip("/")
        if not url:
            raise ProfileError(
                "IGROW_ERP_URL is not set — there is no ERP to pull from. Set it in\n"
                f"       {CONFIG_DIR}/gateway.env."
            )
        verify_key = os.environ.get("IGROW_PROFILE_VERIFY_KEY") or str(
            PKI_DIR / "profile-verify.pub"
        )
        return cls(
            url=f"{url}/api/v1/gateway/active-profile",
            chain=Path(os.environ.get("IGROW_GATEWAY_CHAIN", PKI_DIR / "gateway-chain.crt")),
            key=Path(os.environ.get("IGROW_GATEWAY_KEY", PKI_DIR / "gateway.key")),
            anchor=Path(os.environ.get("IGROW_OPERATOR_ROOT", PKI_DIR / "operator-root.crt")),
            verify_key=Path(verify_key) if verify_key else None,
            timeout=int(os.environ.get("IGROW_PROFILE_TIMEOUT", DEFAULT_TIMEOUT)),
            interval=int(os.environ.get("IGROW_PROFILE_INTERVAL", DEFAULT_INTERVAL)),
        )


def machine_identity(chain: Path) -> str:
    """This gateway's `GBOX_NNNN`, read from its own certificate's CN.

    ADR-0007 rev 1 d10b puts the machine identifier in the CN verbatim, and
    ADR-0022 d2 has the ERP authorise the pull by that same string. Reading it
    here rather than from a config variable means the identity the gateway checks
    a profile against cannot drift from the identity it authenticated as — there
    is no second place to set it wrong.
    """
    if not chain.exists():
        raise ProfileError(
            f"no client certificate at {chain}. The gateway has no identity yet — provision\n"
            "       one with gateway/provision_identity.py "
            "(store/SP0004-M-atecc-provisioning.md)."
        )
    if not shutil.which("openssl"):
        raise ProfileError("openssl is not on PATH")
    done = subprocess.run(
        ["openssl", "x509", "-in", str(chain), "-noout", "-subject", "-nameopt", "RFC2253"],
        capture_output=True,
        text=True,
        check=False,
    )
    if done.returncode != 0:
        raise ProfileError(f"cannot read {chain}: {done.stderr.strip()}")
    # `[^,\n]+` and a fullmatch, not `[^,]+` with `$`: openssl ends the line with a
    # newline, `[^,]+` happily swallows it, and `$` matches *before* a trailing
    # newline — so a sloppy pattern yields "GBOX_0001\n", passes the grammar check,
    # and then fails to equal the identifier inside the profile for reasons nothing
    # in the message explains.
    match = re.search(r"\bCN=([^,\n]+)", done.stdout)
    if not match or not GBOX_RE.fullmatch(match.group(1).strip()):
        raise ProfileError(
            f"the certificate's CN is not a machine identifier: {done.stdout.strip()}"
        )
    return match.group(1).strip()


# ---------------------------------------------------------------------------
# Verification
#
# The signature covers the document's exact bytes (ADR-0025 d6), so this shares
# no code with signing/sign_profile.py and needs none: there is no canonical form
# or pre-image construction for the two ends to agree on. openssl does the maths;
# the gateway venv gains no dependency for it.
# ---------------------------------------------------------------------------


def verify_signature(verify_key: Path, document: bytes, signature_b64: str) -> bool:
    if not verify_key.exists():
        raise ProfileError(
            f"no profile-verification key at {verify_key}. ADR-0015 d7 requires verification\n"
            "       before a profile is applied, so without the key nothing can be applied —\n"
            "       provision the operator's public key (ADR-0025 d10). This is fail-closed on\n"
            "       purpose: the alternative is a gateway that runs whatever it is handed."
        )
    try:
        raw = base64.b64decode(signature_b64, validate=True)
    except Exception:
        return False

    # openssl wants the signature in a file. PrivateTmp=yes on the unit keeps this
    # out of any directory another user can reach.
    fd, sig_path = tempfile.mkstemp()
    try:
        os.write(fd, raw)
        os.close(fd)
        done = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(verify_key), "-signature", sig_path],
            input=document,
            capture_output=True,
            check=False,
        )
        return done.returncode == 0
    finally:
        Path(sig_path).unlink(missing_ok=True)


def version_of(document: bytes) -> tuple[str, str]:
    """The machine and version a *verified* document declares (ADR-0025 d7)."""
    parsed = json.loads(document)
    if not isinstance(parsed, dict):
        raise ProfileError("a profile document is a JSON object")
    machine, version = parsed.get("machine_id"), parsed.get("version_tag")
    if not machine or not version:
        raise ProfileError(
            "the signed document does not carry machine_id and version_tag, so it cannot be "
            "bound to this cabinet or ordered against what is running (ADR-0025 d7)"
        )
    return str(machine), str(version)


def version_ordinal(version_tag: str) -> int:
    """A comparable ordinal for the ADR-0025 d8 monotonicity check.

    ADR-0025 leaves *what* carries the total order to the schema (deferred), so
    this reads the leading integer of a tag like `v43` or `43`. A tag that has no
    such ordinal cannot be ordered, and an unorderable version is refused rather
    than guessed at: guessing here would silently accept a downgrade, which is
    the single thing decision 8 exists to prevent.
    """
    match = re.match(r"^v?([0-9]+)", version_tag)
    if not match:
        raise ProfileError(
            f"cannot order version {version_tag!r} against the running one. ADR-0025 d8 needs a\n"
            "       comparable version; use a tag whose leading component is an integer "
            "(e.g. v43)."
        )
    return int(match.group(1))


def installed_version() -> str | None:
    """The version currently applied, or None when nothing is.

    A corrupt active profile is treated as "nothing installed" rather than as a
    hard error: the point of the pull is to replace it, and refusing to pull
    because the thing being replaced is broken would strand the cabinet.
    """
    for candidate in (ACTIVE_PROFILE, LAST_KNOWN_GOOD):
        if not candidate.exists():
            continue
        try:
            return version_of(candidate.read_bytes())[1]
        except (ProfileError, json.JSONDecodeError):
            log(f"{candidate} is not a readable profile; ignoring it for ordering")
    return None


# ---------------------------------------------------------------------------
# Transport
# ---------------------------------------------------------------------------


def pull(config: Config) -> dict:
    """Fetch the active-profile envelope over the mTLS channel.

    The chain is presented, not the bare leaf: a peer anchored on the operator
    root cannot build a path from the leaf alone (ADR-0024 d1/d3).
    """
    for path, what in ((config.chain, "client chain"), (config.anchor, "operator root")):
        if not path.exists():
            raise ProfileError(f"no {what} at {path}")

    context = ssl.create_default_context(cafile=str(config.anchor))
    # The key is a separate file when one exists, and inside the chain otherwise.
    # Passing the chain as its own keyfile — which this did — works only for a
    # combined PEM and fails with an opaque "PEM lib" error for the ordinary
    # layout of a certificate beside its key, which is what the CA tooling emits.
    keyfile = str(config.key) if config.key and config.key.exists() else None
    context.load_cert_chain(str(config.chain), keyfile)
    try:
        with urllib.request.urlopen(config.url, timeout=config.timeout, context=context) as reply:
            body = reply.read()
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            raise ProfileError("the ERP has no active profile recorded for this machine") from exc
        if exc.code in (401, 403):
            raise ProfileError(
                f"the ERP rejected this gateway's certificate (HTTP {exc.code}). Its identity is "
                "valid TLS but not an accepted caller."
            ) from exc
        if exc.code == 503:
            raise ProfileError(
                "the ERP's gateway channel is closed (HTTP 503) — its trusted-proxy list is "
                "empty, so no client certificate would be accepted (ADR-0022 d2)."
            ) from exc
        raise ProfileError(f"the ERP returned HTTP {exc.code}") from exc
    except (urllib.error.URLError, ssl.SSLError, OSError) as exc:
        raise ProfileError(f"cannot reach the ERP at {config.url}: {exc}") from exc

    try:
        envelope = json.loads(body)
    except json.JSONDecodeError as exc:
        raise ProfileError(f"the ERP's reply is not JSON: {exc}") from exc
    if not isinstance(envelope, dict):
        raise ProfileError("the ERP's reply is not an object")
    return envelope


def document_and_signature(envelope: dict) -> tuple[bytes, str]:
    """Pull the signed bytes and their signature out of the unsigned envelope.

    `document_b64` carries the artifact byte-for-byte, which is what lets it
    survive a JSON hop without re-serialisation (ADR-0025 d6). Everything else in
    the envelope is unsigned and is deliberately not read: the machine and version
    this client acts on come from inside the verified bytes.
    """
    encoded = envelope.get("document_b64")
    signature = envelope.get("signature")
    if not encoded:
        raise ProfileError(
            "the ERP returned no `document_b64`. A profile version must travel as the exact\n"
            "       bytes that were signed (ADR-0025 d6); an ERP that returns a re-serialised\n"
            "       object cannot serve a verifiable profile."
        )
    if not signature:
        raise ProfileError(
            "the ERP returned a profile with no signature. ADR-0025 d11 makes a signature\n"
            "       non-optional for anything a gateway can pull, and ADR-0015 d7 forbids\n"
            "       applying what cannot be verified."
        )
    try:
        return base64.b64decode(encoded, validate=True), str(signature)
    except Exception as exc:
        raise ProfileError(f"`document_b64` is not valid base64: {exc}") from exc


# ---------------------------------------------------------------------------
# Apply
# ---------------------------------------------------------------------------


def apply_profile(document: bytes) -> None:
    """Install the profile with an atomic rename (ADR-0015 d6).

    The control loop re-reads the file each iteration, and `rename()` is atomic on
    POSIX, so the loop never sees a half-written profile and there is no partial
    application. The previous file becomes the last-known-good *before* the
    replacement lands, so a crash between the two leaves a recoverable pair rather
    than one file and a gap.
    """
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    if ACTIVE_PROFILE.exists():
        shutil.copy2(ACTIVE_PROFILE, LAST_KNOWN_GOOD)

    # Same directory as the target: rename() is only atomic within a filesystem.
    fd, staged = tempfile.mkstemp(dir=CONFIG_DIR, suffix=".tmp")
    try:
        os.write(fd, document)
        # fsync before the rename: without it a power loss can leave the rename
        # durable and the contents not, which is a zero-length active profile —
        # exactly the state the atomic switch exists to make impossible.
        os.fsync(fd)
        os.close(fd)
        os.chmod(staged, 0o644)
        os.replace(staged, ACTIVE_PROFILE)
    except BaseException:
        with contextlib.suppress(OSError):
            os.close(fd)
        Path(staged).unlink(missing_ok=True)
        raise


def pull_once(config: Config) -> bool:
    """One pull-verify-apply cycle. True when a new profile was applied.

    Returns False for "nothing to do"; raises ProfileError for a refusal. Both
    leave the running profile alone.
    """
    machine = machine_identity(config.chain)
    envelope = pull(config)
    document, signature = document_and_signature(envelope)

    # Verify BEFORE parsing for anything that drives a decision. Until the
    # signature checks out these are untrusted bytes off the network, and
    # ADR-0015 d7 puts verification before application, not beside it.
    if not verify_signature(config.verify_key, document, signature):
        raise ProfileError(
            "signature does NOT verify — not applying (ADR-0015 d7). The running profile stays\n"
            "       active. Either the artifact was altered after signing, or this gateway holds\n"
            "       the wrong verification key."
        )

    declared_machine, version = version_of(document)
    if declared_machine != machine:
        raise ProfileError(
            f"this profile is signed for {declared_machine}, and this gateway is {machine}.\n"
            "       Correctly signed, and not for this cabinet (ADR-0025 d7) — refusing."
        )

    running = installed_version()
    if running is not None and version_ordinal(version) <= version_ordinal(running):
        # Staying put is not an error: the steady state of a 60-second poll is "no
        # change", and logging that every minute would bury what matters. Only a
        # genuine downgrade attempt is worth a line.
        if version_ordinal(version) < version_ordinal(running):
            log(
                f"refusing {version}: older than the running {running}. A signature does not "
                f"expire, so an old artifact stays valid forever (ADR-0025 d8); a rollback is "
                f"issued as a new higher version."
            )
        return False

    apply_profile(document)
    log(f"applied {version} for {machine} (was {running or 'nothing'})")
    return True


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------


def cmd_once(args: argparse.Namespace) -> int:
    config = Config.from_env()
    try:
        pull_once(config)
    except ProfileError as exc:
        log(f"pull failed: {exc}")
        return 1
    return 0


def cmd_show(args: argparse.Namespace) -> int:
    for path, label in ((ACTIVE_PROFILE, "active"), (LAST_KNOWN_GOOD, "last known good")):
        if not path.exists():
            log(f"{label}: none at {path}")
            continue
        try:
            machine, version = version_of(path.read_bytes())
            log(f"{label}: {machine} {version} ({path})")
        except (ProfileError, json.JSONDecodeError) as exc:
            log(f"{label}: unreadable ({path}): {exc}")
    return 0


def cmd_serve(args: argparse.Namespace) -> int:
    """Poll on an interval, backing off while the ERP is unreachable.

    The systemd timer is the production path (see `industrygrow-profile-pull.timer`);
    this exists for a host without timers and for watching the loop by hand. Backoff
    is exponential to a ceiling and resets on success, so a long ERP outage costs a
    handful of attempts an hour rather than one a minute — while a transient failure
    still recovers quickly. ADR-0015 leaves "what happens on extended sync failure"
    deferred, and this does the one thing that decision cannot contradict: keeps
    running the last known good and keeps trying.
    """
    config = Config.from_env()
    delay = config.interval
    ceiling = max(config.interval, args.max_interval)
    while True:
        try:
            pull_once(config)
            delay = config.interval
        except ProfileError as exc:
            log(f"pull failed: {exc}")
            delay = min(delay * 2, ceiling)
            log(f"next attempt in {delay}s")
        except KeyboardInterrupt:
            return 0
        time.sleep(delay)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="profile_client.py",
        description="Pull, verify and apply this gateway's active cultivation profile "
        "(ADR-0015 d5-7, ADR-0025).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("once", help="one pull-verify-apply cycle; what the timer runs")
    p.set_defaults(func=cmd_once)

    p = sub.add_parser("show", help="report the installed profile and last known good")
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("serve", help="poll on an interval with backoff, for a host without timers")
    p.add_argument(
        "--max-interval", type=int, default=3600, help="backoff ceiling in seconds; default 3600"
    )
    p.set_defaults(func=cmd_serve)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except ProfileError as exc:
        log(f"error: {exc}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
