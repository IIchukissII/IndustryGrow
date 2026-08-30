# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Printable reports over what the ERP already owns.

A report adds no entity. It is another representation of records the API already
serves — the same relationship decision 13's parsed identifier fields have to the
identifier, and decision 7's read-through has to a blob. Nothing here queries
anything a JSON route does not.

**Rendered for monochrome.** Greys and rules only, no colour: these are printed,
filed, and photocopied, and a status a reader can only get from a colour is lost
the first time that happens. The mark is the README's mono-light logo, which is
the black-on-white variant of the brand asset (`img/`).

fpdf2 is the whole rendering dependency — pure Python, no system libraries, which
keeps ADR-0021 decision 15's "compact" single container intact. It cannot parse
the mono logo's SVG (the glyphs in it break its parser), so the PNG beside it is
the rasterised form; regenerate it with
``cairosvg.svg2png(url=..., output_width=1000)`` if the brand asset changes.
"""

from __future__ import annotations

import re
from datetime import UTC, datetime
from pathlib import Path

import markdown as md_lib
from fpdf import FPDF
from fpdf.enums import Align

from app.config import settings


def _logo() -> Path:
    """The brand mark, black on white. Relative paths resolve against erp/, as
    store_dir and registry_path do."""
    return Path(settings.report_logo)


INK = (26, 26, 26)
MUTED = (110, 110, 110)
RULE = (200, 200, 200)

MARGIN = 18.0
LOGO_MM = 16.0


# fpdf2's core fonts are latin-1. Anything outside it raises rather than degrades,
# and the text here is not all ours — an instance note or a removal reason carries
# whatever an operator typed. So every string is folded to latin-1 before it
# reaches the page: the common typography is mapped to its ASCII equivalent, and
# anything else becomes "?" rather than a 500 on a report route.
_FOLD = str.maketrans(
    {
        "\u2014": "-",
        "\u2013": "-",
        "\u2026": "...",
        "\u2018": "'",
        "\u2019": "'",
        "\u201c": '"',
        "\u201d": '"',
        "\u00a0": " ",
        "\u2011": "-",
        "\u2212": "-",
    }
)


def _safe(text: str) -> str:
    return text.translate(_FOLD).encode("latin-1", "replace").decode("latin-1")


def _fmt(value: object) -> str:
    """A cell's text. `None` prints as a dash, never as "None"."""
    if value is None or value == "":
        return "-"
    if isinstance(value, datetime):
        return value.strftime("%Y-%m-%d")
    return str(value)


class Report(FPDF):
    """A4 portrait with a running header and footer.

    The header carries what the page *is* and who it belongs to; the footer
    carries provenance — when it was produced and from which record — because a
    printed page outlives the screen it came from and is read without one.
    """

    def __init__(self, title: str, subject: str) -> None:
        super().__init__(orientation="P", unit="mm", format="A4")
        self.title_text = title
        self.subject_text = subject
        self.set_auto_page_break(auto=True, margin=22)
        self.set_margins(MARGIN, MARGIN, MARGIN)
        self.set_title(_safe(f"{title} - {subject}"))

    def header(self) -> None:
        logo = _logo()
        if logo.is_file():
            self.image(str(logo), x=MARGIN, y=12, w=LOGO_MM)
        left = MARGIN + LOGO_MM + 5

        self.set_xy(left, 13)
        self.set_font("Helvetica", "B", 13)
        self.set_text_color(*INK)
        self.cell(0, 6, _safe(self.title_text), new_x="LMARGIN", new_y="NEXT")

        self.set_x(left)
        self.set_font("Helvetica", "", 10)
        self.set_text_color(*MUTED)
        self.cell(0, 5, _safe(self.subject_text), new_x="LMARGIN", new_y="NEXT")

        self.set_x(left)
        self.set_font("Helvetica", "", 8)
        self.cell(
            0, 4, f"{settings.operator_name} · operator-private", new_x="LMARGIN", new_y="NEXT"
        )

        self.set_draw_color(*RULE)
        self.set_line_width(0.3)
        self.line(MARGIN, 32, self.w - MARGIN, 32)
        self.set_y(38)

    def footer(self) -> None:
        self.set_y(-16)
        self.set_draw_color(*RULE)
        self.set_line_width(0.2)
        self.line(MARGIN, self.get_y(), self.w - MARGIN, self.get_y())
        self.set_y(-13)
        self.set_font("Helvetica", "", 7.5)
        self.set_text_color(*MUTED)
        stamp = datetime.now(UTC).strftime("%Y-%m-%d %H:%M UTC")
        self.cell(
            0,
            4,
            f"Generated {stamp} from the instance-and-integration record (ADR-0021).",
            align=Align.L,
        )
        self.set_y(-13)
        # `{nb}` is substituted with the final page count by fpdf2.
        self.cell(0, 4, f"Page {self.page_no()} of {{nb}}", align=Align.R)

    # ---- building blocks ---------------------------------------------------

    def section(self, heading: str) -> None:
        self.ln(3)
        self.set_font("Helvetica", "B", 10)
        self.set_text_color(*INK)
        self.cell(0, 6, _safe(heading), new_x="LMARGIN", new_y="NEXT")
        self.set_draw_color(*RULE)
        self.line(MARGIN, self.get_y(), self.w - MARGIN, self.get_y())
        self.ln(2)

    def field(self, label: str, value: object) -> None:
        self.set_font("Helvetica", "", 9)
        self.set_text_color(*MUTED)
        self.cell(46, 5.5, _safe(label))
        self.set_text_color(*INK)
        self.set_font("Courier", "", 9)
        self.multi_cell(0, 5.5, _safe(_fmt(value)), new_x="LMARGIN", new_y="NEXT")

    def note(self, text: str) -> None:
        self.ln(1)
        self.set_font("Helvetica", "I", 8)
        self.set_text_color(*MUTED)
        self.multi_cell(0, 4, _safe(text), new_x="LMARGIN", new_y="NEXT")
        self.set_text_color(*INK)

    def table(self, columns: list[tuple[str, float]], rows: list[list[object]]) -> None:
        """A table, or the reason there is not one.

        An empty section prints a stated absence rather than a blank: on paper a
        gap is indistinguishable from a rendering fault.
        """
        if not rows:
            self.set_font("Helvetica", "I", 9)
            self.set_text_color(*MUTED)
            self.cell(0, 5.5, "None on record.", new_x="LMARGIN", new_y="NEXT")
            self.set_text_color(*INK)
            return

        self.set_font("Helvetica", "B", 8.5)
        self.set_text_color(*MUTED)
        for name, width in columns:
            self.cell(width, 5.5, _safe(name))
        self.ln(5.5)
        self.set_draw_color(*RULE)
        self.line(MARGIN, self.get_y(), self.w - MARGIN, self.get_y())
        self.ln(1)

        self.set_text_color(*INK)
        for row in rows:
            self.set_font("Courier", "", 8.5)
            for (_name, width), cell in zip(columns, row, strict=False):
                text = _safe(_fmt(cell))
                # Truncate rather than wrap: a row that wraps mid-identifier is
                # harder to read than one that is visibly cut.
                budget = int(width / 1.75)
                if len(text) > budget:
                    text = text[: budget - 3] + "..."
                self.cell(width, 5, text)
            self.ln(5)


def instance_dossier(
    *,
    instance: dict,
    identity: dict | None,
    history: list[dict],
    documents: list[dict],
) -> bytes:
    """Everything the record holds about one manufactured instance.

    What this serial is, the certificate bound to it, where it has been installed,
    and which lifecycle documents exist for it.
    """
    instance_id = instance["_id"]
    pdf = Report("Instance", instance_id)
    pdf.alias_nb_pages()
    pdf.add_page()

    pdf.section("Identity")
    pdf.field("Instance", instance_id)
    pdf.field("Module", instance.get("e_number"))
    pdf.field("Design version", instance.get("version"))
    pdf.field("Serial", instance.get("serial"))
    pdf.field("Status", instance.get("status"))
    pdf.field("Produced", instance.get("produced_at"))
    pdf.note(
        "The identifier is the object key (ADR-0017 d15). Module, version and serial are "
        "its parts, not separate facts."
    )

    pdf.section("Provisioning binding")
    if identity is None:
        pdf.set_font("Helvetica", "I", 9)
        pdf.set_text_color(*MUTED)
        pdf.cell(0, 5.5, "No certificate bound to this serial.", new_x="LMARGIN", new_y="NEXT")
        pdf.set_text_color(*INK)
    else:
        pdf.field("Certificate serial", identity.get("cert_serial"))
        pdf.field("Public key", identity.get("public_key_fingerprint"))
        pdf.field("Valid from", identity.get("cert_not_before"))
        pdf.field("Valid until", identity.get("cert_not_after"))
        pdf.field("Provisioned", identity.get("provisioned_at"))
        pdf.note("Public certificate material only. A private key is not representable here.")

    pdf.section("Integration history")
    pdf.table(
        [("Machine", 34), ("Position", 26), ("Installed", 28), ("Removed", 28), ("Reason", 46)],
        [
            [
                h.get("machine_id"),
                h.get("depth_code"),
                h.get("installed_at"),
                h.get("removed_at"),
                h.get("removal_reason"),
            ]
            for h in history
        ],
    )
    pdf.note(
        "Position is assigned at integration and is never written onto the instance "
        "(ADR-0017 d7). An open 'Removed' is the current placement."
    )

    pdf.section("Lifecycle documents")
    pdf.table(
        [("Type", 16), ("Object key", 88), ("Valid until", 28), ("Status", 24)],
        [
            [d.get("doc_type"), d.get("object_key"), d.get("valid_until"), d.get("status")]
            for d in documents
        ],
    )
    pdf.note(
        "The ERP indexes these; the documents themselves are in the object store under "
        "the keys above (ADR-0021 d7)."
    )

    return bytes(pdf.output())


# fpdf2's write_html understands a useful subset. Tables and fenced code are the
# two that matter here: the store's `-M` manuals and bring-up protocols use both,
# and a table flattened to prose is unreadable.
_MD_EXTENSIONS = ["tables", "fenced_code", "sane_lists"]

# fpdf2 raises on any element nested inside a table cell — a `code` span or a bold
# run in a cell is enough, and the store's manuals are full of both. Flattening the
# cell to its text keeps the table, which is the part that carries the meaning; the
# alternative fpdf2 leaves open is no table at all.
_CELL = re.compile(r"(<(?:td|th)\b[^>]*>)(.*?)(</(?:td|th)>)", re.S | re.I)
_TAG = re.compile(r"<[^>]+>")
# An in-document link records a named destination that nothing sets, and fpdf2 then
# refuses to emit the file at all. The heading it points at is on the page anyway,
# so the link becomes its own text.
_FRAGMENT_LINK = re.compile(r'<a\b[^>]*href="#[^"]*"[^>]*>(.*?)</a>', re.S | re.I)


def _flatten_cells(html: str) -> str:
    return _CELL.sub(lambda m: m.group(1) + _TAG.sub("", m.group(2)).strip() + m.group(3), html)


def _drop_fragment_links(html: str) -> str:
    return _FRAGMENT_LINK.sub(lambda m: m.group(1), html)


def markdown_document(*, object_key: str, text: str, subject: str | None = None) -> bytes:
    """One markdown document as a printable PDF.

    A rendering of a document the API already serves, not a new artifact: the
    bytes come from the object store and nothing is stored back. The page frame is
    the instance report's, so a printed manual and a printed instance file together.

    The text is folded to latin-1 like everything else on the page — the core
    fonts cannot show more, and a glyph lost beats a document lost.
    """

    def page() -> Report:
        pdf = Report("Document", subject or object_key)
        pdf.alias_nb_pages()
        pdf.add_page()
        pdf.set_font("Helvetica", "", 10)
        pdf.set_text_color(*INK)
        return pdf

    html = _drop_fragment_links(
        _flatten_cells(md_lib.markdown(_safe(text), extensions=_MD_EXTENSIONS))
    )
    try:
        pdf = page()
        pdf.write_html(html)
        return bytes(pdf.output())
    except Exception as exc:
        # fpdf2's HTML support is a subset, and it fails in two places: at
        # write_html for markup it cannot lay out, and at output() for a link
        # whose destination was never set. Both are caught, and both fall back to
        # the text — an operator holding a plain page is better served than one
        # holding a 500, and the reason goes on the page rather than only a log.
        pdf = page()
        pdf.note(f"Rendered as plain text — the layout engine refused the markup: {exc}")
        pdf.set_font("Courier", "", 8.5)
        pdf.set_text_color(*INK)
        pdf.multi_cell(0, 4, _safe(text), new_x="LMARGIN", new_y="NEXT")
        return bytes(pdf.output())
