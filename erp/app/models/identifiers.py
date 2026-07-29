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
from dataclasses import dataclass
from typing import NamedTuple

# Field patterns (ADR-0017 decision 1).
E_MODULE = r"E\d{4}"  # E + 4 digits
SP_PART = r"SP\d{4}"  # SP + 4 digits (ADR-0019 d1)
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

# Withdrawal status tokens (ADR-0017 d17). Full uppercase words, deliberately
# neither a layer letter nor a lifecycle suffix, so they can never be mistaken
# for one — which is what makes the token itself the machine-readable status.
STATUS_TOKENS = frozenset({"BLOCKED", "SUPERSEDED"})

ROOT_RE = re.compile(rf"^(?:{E_MODULE}|{SP_PART})$")


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


# --- type-layer object keys -------------------------------------------------
# The store is flat and an identifier *is* the object key (ADR-0017 d15), so the
# only structure a type-layer document has is the one its key spells out. Reading
# that structure is a parse, not a match: the fields are positional, and which
# field a segment lands in is what gives it meaning. `D` after the version is the
# design layer; `D` further along is a word in a slug.


@dataclass(frozen=True)
class StoreKey:
    """One type-layer object key, read into its ADR-0017 / ADR-0019 fields.

    ``root`` is ``None`` for a file in ``store/`` that carries no identifier at
    all (an EDA library table, say). That is a real state and is reported as one
    rather than guessed at — an unfiled object is worth seeing.
    """

    object_key: str
    root: str | None = None  # E0001 (designed) or SP0004 (purchased)
    root_kind: str | None = None  # "E" | "SP"
    version: str | None = None  # VVVVVV — E roots only (ADR-0019 d2)
    layer: str | None = None  # S D L P M I F  (ADR-0017 d9/d16)
    slug: str | None = None  # fab, pinmap, rp5-case-src …
    status: str | None = None  # BLOCKED | SUPERSEDED  (ADR-0017 d17)
    extension: str = ""

    @property
    def prefix(self) -> str:
        """What this key shares with its siblings — the row of the filing plate.

        Root plus version for an E document, root alone for an SP one. The rest
        of the key hangs off it, so ``prefix + tail`` is the key again.
        """
        if not self.root:
            return ""
        return f"{self.root}-{self.version}" if self.version else self.root

    @property
    def tail(self) -> str:
        """The part of the key that is not the prefix, separator included."""
        return self.object_key[len(self.prefix) :] if self.prefix else self.object_key


def parse_store_key(object_key: str) -> StoreKey:
    """Read a type-layer object key into its fields; never raises.

    A key that does not fit the grammar comes back with the fields it did fill
    and ``root=None``. The store holds what the repository put there, and a
    listing that dropped or mislabelled an unrecognised object would hide it.
    """
    stem, dot, extension = object_key.rpartition(".")
    if not dot:  # rpartition puts an extension-less name in the last field
        stem, extension = object_key, ""
    parts = stem.split("-")
    if not ROOT_RE.match(parts[0]):
        return StoreKey(object_key=object_key, extension=extension)

    root = parts[0]
    kind = "E" if root.startswith("E") else "SP"
    rest = parts[1:]

    # Only an E root carries a version: the supplier owns an SP part's versioning
    # (ADR-0019 d2), so an SP document is type-level like the SP number itself.
    version = None
    if kind == "E" and rest and re.fullmatch(VERSION, rest[0]):
        version, rest = rest[0], rest[1:]

    status = layer = None
    if rest and rest[0] in STATUS_TOKENS:
        status, rest = rest[0], rest[1:]
    elif rest and rest[0] in DOCUMENT_LAYERS:
        layer, rest = rest[0], rest[1:]

    return StoreKey(
        object_key=object_key,
        root=root,
        root_kind=kind,
        version=version,
        layer=layer,
        slug="-".join(rest) or None,
        status=status,
        extension=extension,
    )
