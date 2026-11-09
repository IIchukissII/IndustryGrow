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
from app.models.identifiers import StoreKey

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


_SEPARATOR_CELL = re.compile(r"^:?-+:?$")
_TRACKED_CELL = re.compile(r"^(yes|no)\b")


def _tables(lines: list[str]) -> list[list[list[str]]]:
    """The tables of a section's own body, each as rows of cells.

    The body ends at the next heading of any level, so a `###` subsection's
    tables are mentions (ADR-0023 d3). Separator rows are dropped; the first row
    of each table is its header.
    """
    tables: list[list[list[str]]] = []
    current: list[list[str]] | None = None
    for line in lines:
        if line.startswith("#"):
            break
        if not line.lstrip().startswith("|"):
            current = None
            continue
        cells = _cells(line)
        if all(_SEPARATOR_CELL.match(c) for c in cells):
            continue
        if current is None:
            current = []
            tables.append(current)
        current.append(cells)
    return tables


def _entry_rows(sections: dict[str, list[str]], heading: str) -> list[list[str]]:
    """The data rows of a registry section's table: the first table, header excluded."""
    for key, lines in sections.items():
        if key.startswith(heading):
            tables = _tables(lines)
            return tables[0][1:] if tables else []
    return []


def parse(text: str) -> Catalog:
    """The two registry tables into rows; a row that is not an entry is skipped.

    Skipping keeps a consumer reading; ``check`` reports every skipped row.
    """
    sections = _sections(text)
    modules: list[Module] = []
    parts: list[Part] = []

    for cells in _entry_rows(sections, _MODULES_HEADING):
        if len(cells) == len(_MODULE_COLUMNS) and (m := _E_CELL.match(cells[0])):
            modules.append(
                Module(
                    e_number=m.group(1),
                    designation=_strip_md(cells[1]),
                    discipline=_strip_md(cells[2]),
                    notes=_strip_md(cells[3]),
                )
            )
    for cells in _entry_rows(sections, _PARTS_HEADING):
        if len(cells) == len(_PART_COLUMNS) and (m := _SP_CELL.match(cells[0])):
            parts.append(
                Part(
                    sp_number=m.group(1),
                    role=_strip_md(cells[1]),
                    # "yes (vendor serial / gateway identity)" | "no"
                    instance_tracked=_strip_md(cells[2]).startswith("yes"),
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
    """A section's body holds one table, and its first row is the canonical header."""
    for key, lines in sections.items():
        if not key.startswith(heading):
            continue
        tables = _tables(lines)
        if not tables:
            return f"{heading}: section has no table"
        if len(tables) > 1:
            return f"{heading}: section holds {len(tables)} tables, expected one"
        found = tuple(_strip_md(c) for c in tables[0][0])
        if found != expected:
            return f"{heading}: column header is {found!r}, expected {expected!r}"
        return ""
    return f"{heading}: section missing"


def _check_rows(
    sections: dict[str, list[str]],
    heading: str,
    columns: tuple[str, ...],
    cell: re.Pattern[str],
) -> list[str]:
    """Every data row is an entry: the column count, a bare backticked identifier in
    column 1 and, on the SP table, `Instance-tracked?` beginning `yes` or `no`."""
    problems = []
    for cells in _entry_rows(sections, heading):
        first = cells[0]
        if len(cells) != len(columns):
            problems.append(
                f"{heading}: row {first!r} has {len(cells)} cells, expected {len(columns)}"
            )
            continue
        if not cell.match(first):
            problems.append(f"{heading}: first cell {first!r} is not a bare backticked identifier")
            continue
        if columns == _PART_COLUMNS and not _TRACKED_CELL.match(_strip_md(cells[2])):
            problems.append(
                f"{heading}: {first} {columns[2]} is {cells[2]!r}, expected to begin yes or no"
            )
    return problems


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
    problems += _check_rows(sections, _MODULES_HEADING, _MODULE_COLUMNS, _E_CELL)
    problems += _check_rows(sections, _PARTS_HEADING, _PART_COLUMNS, _SP_CELL)

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


# --- type-layer document kinds (ADR-0017 d9 document layers) -----------------
# Labels are the expansions of ADR-0017's code legend (dec 1), word for word.
# The layer letter in `<identifier>-<layer>-<slug>` says what a document IS, and
# the registry is where that vocabulary lives — so the mapping sits here beside
# the parser rather than in the API or the console (ADR-0023 d6: the registry
# holds meaning, the design system holds appearance).
_DOC_LAYERS = {
    "S": "Schema",
    "D": "Drawing",
    "L": "List / BOM",
    "P": "Protocol",
    "M": "Manual",
    "I": "Interface",
    "F": "Firmware",
    "R": "Requirements",
}


def layer_label(layer: str | None) -> str | None:
    """What a document-layer letter means, or ``None`` for a key that has no layer."""
    return _DOC_LAYERS.get(layer or "")


def store_doc_kind(key: StoreKey) -> str:
    """A human label for a type-layer document, from its parsed key.

    The layer letter is read *positionally* (``parse_store_key``) rather than
    hunted for anywhere in the name: `D` in the slot after the version is the
    Drawing layer, and `D` further along is a word in a slug. Falls back to the
    file extension rather than guessing — an object that carries no layer letter
    is still a real document an operator may want, and calling it "Drawing"
    because that is the commonest layer would be worse than calling it what it
    plainly is.
    """
    if key.status:
        # Not a kind of document so much as a kind of grave: one archive object
        # standing for a whole withdrawn artifact set (ADR-0017 d17).
        return f"withdrawn · {key.status.lower()}"
    if key.layer == "D" and key.slug == "fab":
        # One indivisible manufacturing deliverable, not a folder of gerbers
        # (ADR-0017 d18) — worth naming, because it is the one live object in the
        # store that stands for many files.
        return "fabrication package"
    if label := layer_label(key.layer):
        return label
    ext = key.extension.lower()
    # An EDA project is many files of one thing. Letting each extension become its
    # own kind fragments the list into near-identical groups that say nothing an
    # operator was looking for — "design source" is what they all are.
    if ext.startswith("kicad_") or ext in {"qet", "step", "stl"}:
        return "design source"
    return {"md": "document", "pdf": "document", "zip": "archive", "csv": "table"}.get(
        ext, ext or "file"
    )
