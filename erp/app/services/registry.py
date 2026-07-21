# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The type registry, read (never restated) — ADR-0021 d11.

Identifiers carry no meaning in themselves; ``REGISTRY.md`` is where the meaning
lives (ADR-0017 d3, ADR-0019). The ERP is the *instance* layer: it references the
type registry and must not become a second copy of it, so this module parses the
repo's registry rather than holding a table of its own. Anything that needs a
human label for an ``Exxxx``/``SPxxxx`` — the console included — reads it from
here, and a type added to ``REGISTRY.md`` shows up without a code change.

The registry is a document, not a database: it is parsed on demand from the two
canonical tables and never written into Mongo, which would recreate exactly the
duplication this avoids.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from app.config import settings

# A table row: leading/trailing pipes optional-ish, cells split on "|".
_E_CELL = re.compile(r"^`(E\d{4})`$")
_SP_CELL = re.compile(r"^`(SP\d{4})`$")

# The two authoritative sections. Rows outside them (the document-layer and
# withdrawn-artifact tables, which key on *versioned* identifiers) are not the
# type registry and must not leak into the catalog.
_MODULES_HEADING = "## E-numbers"
_PARTS_HEADING = "## SP numbers"


@dataclass(frozen=True)
class Module:
    """A designed assembly (ADR-0017 d3)."""

    e_number: str
    designation: str
    discipline: str
    notes: str


@dataclass(frozen=True)
class Part:
    """A purchased part, identified by vendor-free spec (ADR-0019). The SKU and
    price stay in the BOM — never here."""

    sp_number: str
    role: str
    instance_tracked: bool
    notes: str


@dataclass(frozen=True)
class Catalog:
    modules: tuple[Module, ...]
    parts: tuple[Part, ...]


def registry_path() -> Path:
    path = Path(settings.registry_path)
    return path if path.is_absolute() else (Path(__file__).parent.parent.parent / path).resolve()


def _cells(line: str) -> list[str]:
    return [c.strip() for c in line.strip().strip("|").split("|")]


def _sections(text: str) -> dict[str, list[str]]:
    """Split the document into `##` sections, keyed by heading line."""
    out: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in text.splitlines():
        if line.startswith("## "):
            current = out.setdefault(line.strip(), [])
        elif current is not None:
            current.append(line)
    return out


def _strip_md(value: str) -> str:
    """Backticks and bold markers are table typography, not part of the value."""
    return value.replace("`", "").replace("**", "").strip()


def parse(text: str) -> Catalog:
    sections = _sections(text)
    modules: list[Module] = []
    parts: list[Part] = []

    for heading, lines in sections.items():
        is_modules = heading.startswith(_MODULES_HEADING)
        is_parts = heading.startswith(_PARTS_HEADING)
        if not (is_modules or is_parts):
            continue
        for line in lines:
            if not line.lstrip().startswith("|"):
                continue
            cells = _cells(line)
            if len(cells) < 4:
                continue
            if is_modules and (m := _E_CELL.match(cells[0])):
                modules.append(
                    Module(
                        e_number=m.group(1),
                        designation=_strip_md(cells[1]),
                        discipline=_strip_md(cells[2]),
                        notes=_strip_md(cells[3]),
                    )
                )
            elif is_parts and (m := _SP_CELL.match(cells[0])):
                parts.append(
                    Part(
                        sp_number=m.group(1),
                        role=_strip_md(cells[1]),
                        # "yes (vendor serial / gateway identity)" | "no"
                        instance_tracked=_strip_md(cells[2]).lower().startswith("yes"),
                        notes=_strip_md(cells[3]),
                    )
                )

    return Catalog(modules=tuple(modules), parts=tuple(parts))


@lru_cache(maxsize=1)
def _load(path: str, mtime: float) -> Catalog:
    return parse(Path(path).read_text(encoding="utf-8"))


def catalog() -> Catalog:
    """The parsed registry, re-read when the file changes.

    Keyed on mtime so an edited registry is picked up without a restart — the
    registry is a git-tracked document that moves with the repo, not with a
    deploy.
    """
    path = registry_path()
    if not path.is_file():
        return Catalog(modules=(), parts=())
    return _load(str(path), path.stat().st_mtime)


# --------------------------------------------------------------------------
# Conformance check (ADR-0023 decision 7)
# --------------------------------------------------------------------------
# Consumers stopped carrying their own copy of the registry, so a registry that
# no longer parses must fail loudly at edit time rather than quietly yielding an
# empty catalog at runtime. Run by CI over the same parser consumers use — a
# check against a second implementation would only prove the two agree.

_MODULE_COLUMNS = ("E-number", "Designation", "Discipline", "Notes")
_PART_COLUMNS = ("SP-number", "Role / generic spec (vendor-free)", "Instance-tracked?", "Notes")


def _check_header(sections: dict[str, list[str]], heading: str, expected: tuple[str, ...]) -> str:
    """The first table row of a section must be the canonical column header."""
    for key, lines in sections.items():
        if not key.startswith(heading):
            continue
        for line in lines:
            if line.lstrip().startswith("|"):
                found = tuple(_strip_md(c) for c in _cells(line))
                if found != expected:
                    return f"{heading}: column header is {found!r}, expected {expected!r}"
                return ""
        return f"{heading}: section has no table"
    return f"{heading}: section missing"


def check(path: Path | None = None) -> list[str]:
    """Return the canonical-form violations in the registry; empty means valid."""
    path = path or registry_path()
    if not path.is_file():
        return [f"registry not found: {path}"]

    text = path.read_text(encoding="utf-8")
    sections = _sections(text)
    problems = [
        p
        for p in (
            _check_header(sections, _MODULES_HEADING, _MODULE_COLUMNS),
            _check_header(sections, _PARTS_HEADING, _PART_COLUMNS),
        )
        if p
    ]

    cat = parse(text)
    if not cat.modules:
        problems.append(f"{_MODULES_HEADING}: no entries parsed")
    if not cat.parts:
        problems.append(f"{_PARTS_HEADING}: no entries parsed")

    for label, ids in (
        ("E-number", [m.e_number for m in cat.modules]),
        ("SP-number", [p.sp_number for p in cat.parts]),
    ):
        duplicates = sorted({i for i in ids if ids.count(i) > 1})
        if duplicates:
            problems.append(f"duplicate {label}(s): {', '.join(duplicates)}")

    for m in cat.modules:
        if not m.designation:
            problems.append(f"{m.e_number}: empty designation")
    for p in cat.parts:
        if not p.role:
            problems.append(f"{p.sp_number}: empty role")

    return problems


def main() -> int:
    path = registry_path()
    problems = check(path)
    if problems:
        print(f"{path}: does not conform to the canonical form (ADR-0023 decision 2):")
        for p in problems:
            print(f"  - {p}")
        print("\nConsumers read this document directly; a malformed table empties their catalog.")
        return 1

    cat = catalog()
    print(f"{path}: OK — {len(cat.modules)} E-number(s), {len(cat.parts)} SP-number(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
