# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The ADR-0017 identifier grammar — the load-bearing keys.

Two orthogonal axes that never fuse (ADR-0017 conceptual model):

  identity axis   Exxxx-VVVVVV-NNNNNN     what a thing is / which copy
  position axis   GBOX_NNNN-DDDDDD        where it physically sits

They meet only in the mutable integration identifier
``GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN``. This module parses and formats these
keys and nothing else; type *meaning* lives in REGISTRY.md, not here (ADR-0021 d11).
"""

from __future__ import annotations

import re
from typing import NamedTuple

# Field patterns (ADR-0017 decision 1).
E_MODULE = r"E\d{4}"  # E + 4 digits
VERSION = r"\d{6}"  # major.minor.patch, 2 digits each
SERIAL = r"\d{6}"  # per module + version
MACHINE = r"[A-Z]+_\d{4}"  # <prefix>_NNNN, e.g. GBOX_0001
DEPTH = r"\d{6}"  # main.sub1.sub2, 2 digits each

INSTANCE_RE = re.compile(rf"^(?P<e>{E_MODULE})-(?P<v>{VERSION})-(?P<n>{SERIAL})$")
INTEGRATION_RE = re.compile(
    rf"^(?P<m>{MACHINE})-(?P<d>{DEPTH})-(?P<e>{E_MODULE})-(?P<v>{VERSION})-(?P<n>{SERIAL})$"
)
MACHINE_RE = re.compile(rf"^{MACHINE}$")
DEPTH_RE = re.compile(rf"^{DEPTH}$")

# Document layers (ADR-0017 d9/d16) and lifecycle suffixes (d10-14).
DOCUMENT_LAYERS = frozenset("SDLPMIF")
LIFECYCLE_SUFFIXES = frozenset({"QP", "QR", "CP", "CC", "PR"})


class Version(NamedTuple):
    major: int
    minor: int
    patch: int

    def encode(self) -> str:
        return f"{self.major:02d}{self.minor:02d}{self.patch:02d}"

    def __str__(self) -> str:
        return f"v{self.major}.{self.minor}.{self.patch}"


class Depth(NamedTuple):
    main: int
    sub1: int
    sub2: int

    def encode(self) -> str:
        return f"{self.main:02d}{self.sub1:02d}{self.sub2:02d}"


def encode_version(major: int, minor: int, patch: int) -> str:
    return Version(major, minor, patch).encode()


def decode_version(code: str) -> Version:
    if not re.fullmatch(VERSION, code):
        raise ValueError(f"not a 6-digit version code: {code!r}")
    return Version(int(code[0:2]), int(code[2:4]), int(code[4:6]))


def decode_depth(code: str) -> Depth:
    if not DEPTH_RE.match(code):
        raise ValueError(f"not a 6-digit depth code: {code!r}")
    return Depth(int(code[0:2]), int(code[2:4]), int(code[4:6]))


def instance_id(e_number: str, version: str, serial: str) -> str:
    """Format an identity-axis instance key ``Exxxx-VVVVVV-NNNNNN``."""
    key = f"{e_number}-{version}-{serial}"
    if not INSTANCE_RE.match(key):
        raise ValueError(f"invalid instance identifier: {key!r}")
    return key


def integration_id(machine: str, depth: str, e_number: str, version: str, serial: str) -> str:
    """Format the mutable cross-reference ``GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN``."""
    key = f"{machine}-{depth}-{e_number}-{version}-{serial}"
    if not INTEGRATION_RE.match(key):
        raise ValueError(f"invalid integration identifier: {key!r}")
    return key


def counter_id(e_number: str, version: str) -> str:
    """The serial-counter key: one gap-free sequence per module + version."""
    if not re.fullmatch(E_MODULE, e_number) or not re.fullmatch(VERSION, version):
        raise ValueError(f"invalid module/version: {e_number!r}/{version!r}")
    return f"{e_number}-{version}"


def parse_instance(key: str) -> dict[str, str]:
    m = INSTANCE_RE.match(key)
    if not m:
        raise ValueError(f"not an instance identifier: {key!r}")
    return {"e_number": m["e"], "version": m["v"], "serial": m["n"]}
