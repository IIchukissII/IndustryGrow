# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Printable reports over what the ERP already owns.

A report adds no entity. It is another representation of records the API already
serves — the same relationship decision 13's parsed identifier fields have to the
identifier, and decision 7's read-through has to a blob. Nothing here queries
anything a JSON route does not.

**Rendered for monochrome.** Greys and rules only, no colour: these are printed,
filed, and photocopied, and a status a reader can only get from a colour is lost
the first time that happens.

Both report kinds are markdown or record data turned into HTML and laid out by
WeasyPrint against the stylesheet below, so they share one page frame: masthead,
type scale, rules, footer. The layout the frame depends on is CSS paged media —
page margin boxes, running headers, repeated table headers, widow, orphan and
break control — and WeasyPrint is the engine that implements it. It draws text
through Pango and HarfBuzz, which the image installs (see erp/Dockerfile), and
takes UTF-8 through to the font: an operator's em dash or umlaut in a removal
reason prints as typed.
"""

from __future__ import annotations

import base64
import logging
import re
from datetime import UTC, datetime
from functools import lru_cache
from html import escape
from pathlib import Path

import markdown as md_lib
import weasyprint

from app.config import settings

log = logging.getLogger(__name__)

# Monochrome. Ink for anything read, muted for anything that frames it, and two
# rule weights: a heavy one under the masthead, a hairline everywhere else.
INK = "#141414"
MUTED = "#6b6b6b"
FAINT = "#9a9a9a"
RULE = "#c9c9c9"

# Liberation and DejaVu, in that order, everywhere: both ship as Debian packages
# the image installs, so a page rendered on a developer's machine and one
# rendered in the container use the same faces. DejaVu is the fallback because
# it covers what Liberation does not.
SANS = '"Liberation Sans", "DejaVu Sans", sans-serif'
SERIF = '"Liberation Serif", "DejaVu Serif", serif'
MONO = '"Liberation Mono", "DejaVu Sans Mono", monospace'


def _logo() -> Path:
    """The brand mark. Relative paths resolve against erp/, as store_dir and
    registry_path do."""
    return Path(settings.report_logo)


# The mark's SVG carries a `prefers-color-scheme` rule that flips its fill to the
# dark-theme green. Inlined into an HTML document that rule is live and would put
# a colour on a page defined as monochrome, so the style block is dropped and the
# fill is set from this stylesheet instead.
_SVG_STYLE = re.compile(r"<style\b.*?</style>", re.S | re.I)


@lru_cache(maxsize=1)
def _mark_html() -> str:
    """The mark as markup for the masthead, or nothing.

    An SVG is inlined rather than referenced: it stays vector at any size, and it
    is the one external resource the page is allowed, since the fetcher below
    refuses everything the document itself asks for. A missing file prints the
    report without the mark rather than failing it.
    """
    path = _logo()
    if not path.is_file():
        return ""
    if path.suffix.lower() == ".svg":
        return f'<div class="mark">{_SVG_STYLE.sub("", path.read_text())}</div>'
    data = base64.b64encode(path.read_bytes()).decode("ascii")
    return f'<div class="mark"><img src="data:image/png;base64,{data}" alt=""></div>'


def _no_remote_resources(url: str, timeout: int = 10, ssl_context: object = None) -> dict:
    """The only resources a document may load are the ones inlined into it.

    The markdown comes out of the warehouse, and an image or stylesheet reference
    in it would otherwise be fetched by the ERP — from the network, or from the
    container's filesystem via `file:`. Refusing the fetch drops that one element
    and renders the rest; WeasyPrint logs it and carries on.
    """
    if url.startswith("data:"):
        return weasyprint.default_url_fetcher(url, timeout, ssl_context)
    raise ValueError(f"refused external resource: {url}")


# The page frame. A4, a masthead on the first page and a one-line running header
# after it, and a footer that carries provenance — a printed page outlives the
# screen it came from and is read without one.
_CSS = f"""
@page {{
  size: A4;
  margin: 24mm 18mm 20mm 18mm;
  /* The header boxes are bottom-aligned in the top margin, so the padding under
     them is what separates their rule from the first line of the page. */
  @top-left {{
    content: element(runhead);
    width: 100%;
    vertical-align: bottom;
    padding-bottom: 4mm;
  }}
  @bottom-left {{
    /* The provenance line is per document; _document() appends it. */
    width: 60%;
    font-family: {SANS}; font-size: 7pt; color: {FAINT};
    border-top: 0.4pt solid {RULE}; padding-top: 2.5mm;
    vertical-align: top; text-align: left;
  }}
  @bottom-right {{
    content: "Page " counter(page) " of " counter(pages);
    width: 40%;
    font-family: {SANS}; font-size: 7pt; color: {FAINT};
    border-top: 0.4pt solid {RULE}; padding-top: 2.5mm;
    vertical-align: top; text-align: right;
  }}
}}
/* The first page carries the full masthead and gives it the room to sit in. */
@page :first {{
  margin-top: 44mm;
  @top-left {{
    content: element(masthead);
    width: 100%;
    vertical-align: bottom;
    padding-bottom: 6mm;
  }}
}}

html {{ color: {INK}; }}
body {{ font-family: {SERIF}; font-size: 10pt; line-height: 1.5; margin: 0; }}

/* ── Masthead ───────────────────────────────────────────────────────────────
   Mark, then what the page is and which record it is about. The identifier is
   set in the monospace face throughout the document because it is an object key
   (ADR-0017 d15), not a name — the character positions carry meaning and are
   read one at a time. The double rule under it is the page's one ornament. */
.masthead {{ position: running(masthead); width: 100%; }}
.masthead-row {{ display: flex; align-items: flex-end; gap: 6mm; }}
.mark {{ flex: 0 0 14mm; }}
.mark svg, .mark img {{ width: 14mm; height: auto; display: block; }}
.mark path {{ fill: {INK}; }}
.masthead-id {{ flex: 1 1 auto; }}
.masthead-org {{ flex: 0 0 auto; text-align: right; }}
.doc-class {{
  font-family: {SANS}; font-size: 7pt; font-weight: 700;
  letter-spacing: 0.16em; text-transform: uppercase; color: {MUTED};
}}
.doc-subject {{
  font-family: {MONO}; font-size: 13pt; letter-spacing: 0.02em;
  color: {INK}; margin-top: 1.5mm; word-break: break-all;
}}
.doc-operator {{ font-family: {SANS}; font-size: 8pt; color: {MUTED}; }}
.doc-scope {{
  font-family: {SANS}; font-size: 6.5pt; font-weight: 700;
  letter-spacing: 0.14em; text-transform: uppercase; color: {MUTED};
  border: 0.5pt solid {RULE}; padding: 0.8mm 1.6mm; display: inline-block;
  margin-top: 1.5mm;
}}
.masthead-rule {{ border-top: 1.6pt solid {INK}; margin-top: 3mm; }}
.masthead-rule-thin {{ border-top: 0.4pt solid {RULE}; margin-top: 0.9mm; }}

/* Every page after the first: the same two facts, one line, out of the way. */
.runhead {{
  position: running(runhead); width: 100%;
  font-family: {SANS}; font-size: 7.5pt; color: {MUTED};
  border-bottom: 0.4pt solid {RULE}; padding-bottom: 2mm;
  display: flex; justify-content: space-between; gap: 6mm;
}}
.runhead .rh-id {{ font-family: {MONO}; color: {INK}; }}
.runhead .rh-class {{ letter-spacing: 0.14em; text-transform: uppercase; }}

/* ── Prose ──────────────────────────────────────────────────────────────────
   Headings are sans against a serif body, which is what separates a heading
   from a bold sentence when both are printed in black. Sizes stay close to the
   body: these are procedures and records, not covers. */
h1, h2, h3, h4, h5, h6 {{
  font-family: {SANS}; color: {INK}; font-weight: 700;
  line-height: 1.25; break-after: avoid; margin: 0;
}}
h1 {{ font-size: 15pt; letter-spacing: -0.01em; margin: 0 0 3mm; }}
h2 {{
  font-size: 11.5pt; margin: 7mm 0 2mm;
  border-bottom: 0.4pt solid {RULE}; padding-bottom: 1.2mm;
}}
h3 {{ font-size: 10pt; margin: 5mm 0 1.5mm; }}
h4, h5, h6 {{
  font-size: 8.5pt; letter-spacing: 0.08em; text-transform: uppercase;
  color: {MUTED}; margin: 4mm 0 1.5mm;
}}
p {{ margin: 0 0 2.4mm; orphans: 2; widows: 2; }}
ul, ol {{ margin: 0 0 2.4mm; padding-left: 6mm; }}
li {{ margin-bottom: 1mm; }}
li::marker {{ color: {MUTED}; }}
a {{ color: {INK}; text-decoration: none; border-bottom: 0.4pt solid {RULE}; }}
strong {{ font-weight: 700; }}
hr {{ border: 0; border-top: 0.4pt solid {RULE}; margin: 5mm 0; }}
blockquote {{
  margin: 3mm 0; padding-left: 4mm; border-left: 1.5pt solid {RULE}; color: {MUTED};
}}
img {{ max-width: 100%; }}

/* ── Code ───────────────────────────────────────────────────────────────────
   A block is set on a tint rather than in a box: photocopied, a light grey
   survives and a hairline box around a page-wide block does not. */
code {{ font-family: {MONO}; font-size: 8.5pt; }}
pre {{
  font-family: {MONO}; font-size: 7.5pt; line-height: 1.4;
  background: #f4f4f4; border-left: 1.5pt solid {RULE};
  padding: 2.5mm 3mm; margin: 2.5mm 0; white-space: pre-wrap; word-break: break-word;
}}
pre code {{ font-size: inherit; }}

/* ── Tables ─────────────────────────────────────────────────────────────────
   Reference material, read down a column: ranged left, rules between rows, no
   fill. A header repeated on every page, because a table that breaks across a
   page break otherwise loses its column names. */
table {{
  width: 100%; border-collapse: collapse; margin: 3mm 0;
  font-family: {MONO}; font-size: 8pt;
}}
thead {{ display: table-header-group; }}
th {{
  font-family: {SANS}; font-size: 7pt; font-weight: 700;
  letter-spacing: 0.1em; text-transform: uppercase; color: {MUTED};
  text-align: left; padding: 0 3mm 1.5mm 0;
  border-bottom: 0.8pt solid {INK};
}}
td {{
  text-align: left; vertical-align: top; padding: 1.4mm 3mm 1.4mm 0;
  border-bottom: 0.4pt solid {RULE}; word-break: break-word;
}}
tr {{ break-inside: avoid; }}
th:last-child, td:last-child {{ padding-right: 0; }}

/* ── Record blocks ──────────────────────────────────────────────────────────
   The dossier's own furniture: a section head, a label/value list, and a note. */
.section {{ margin-top: 7mm; break-inside: auto; }}
.section > h2 {{ margin-top: 0; }}
.fields {{ margin: 0; display: grid; grid-template-columns: 42mm 1fr; row-gap: 1.4mm; }}
.fields dt {{
  font-family: {SANS}; font-size: 7.5pt; letter-spacing: 0.06em;
  text-transform: uppercase; color: {MUTED}; padding-top: 0.4mm;
}}
.fields dd {{ font-family: {MONO}; font-size: 9pt; margin: 0; word-break: break-all; }}
.note {{
  font-family: {SERIF}; font-style: italic; font-size: 8pt; color: {MUTED};
  margin: 2.5mm 0 0; line-height: 1.45;
}}
.absent {{ font-family: {SERIF}; font-style: italic; font-size: 9pt; color: {MUTED}; }}
"""


def _css_string(text: str) -> str:
    """A CSS string literal. The provenance line reaches the page through the
    footer margin box, which takes a string rather than an element."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _document(*, doc_class: str, subject: str, footer: str, body: str) -> bytes:
    """One report: the page frame, the masthead, and a body already in HTML."""
    mark = _mark_html()
    doc = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>{escape(doc_class)} {escape(subject)}</title>
</head><body>
<div class="masthead">
  <div class="masthead-row">
    {mark}
    <div class="masthead-id">
      <div class="doc-class">{escape(doc_class)}</div>
      <div class="doc-subject">{escape(subject)}</div>
    </div>
    <div class="masthead-org">
      <div class="doc-operator">{escape(settings.operator_name)}</div>
      <div class="doc-scope">Operator-private</div>
    </div>
  </div>
  <div class="masthead-rule"></div>
  <div class="masthead-rule-thin"></div>
</div>
<div class="runhead">
  <span class="rh-id">{escape(subject)}</span>
  <span class="rh-class">{escape(doc_class)}</span>
</div>
{body}
</body></html>"""
    provenance = f"@page {{ @bottom-left {{ content: {_css_string(footer)}; }} }}"
    rendered = weasyprint.HTML(string=doc, url_fetcher=_no_remote_resources)
    return rendered.write_pdf(
        stylesheets=[weasyprint.CSS(string=_CSS), weasyprint.CSS(string=provenance)]
    )


def _stamp() -> str:
    return datetime.now(UTC).strftime("%Y-%m-%d %H:%M UTC")


def _fmt(value: object) -> str:
    """A cell's text. `None` prints as a dash, never as "None"."""
    if value is None or value == "":
        return "-"
    if isinstance(value, datetime):
        return value.strftime("%Y-%m-%d")
    return str(value)


def _fields(pairs: list[tuple[str, object]]) -> str:
    rows = "".join(
        f"<dt>{escape(label)}</dt><dd>{escape(_fmt(value))}</dd>" for label, value in pairs
    )
    return f'<dl class="fields">{rows}</dl>'


def _table(columns: list[str], rows: list[list[object]]) -> str:
    """A table, or the reason there is not one.

    An empty section prints a stated absence rather than a blank: on paper a gap
    is indistinguishable from a rendering fault.
    """
    if not rows:
        return '<p class="absent">None on record.</p>'
    head = "".join(f"<th>{escape(c)}</th>" for c in columns)
    body = "".join(
        "<tr>" + "".join(f"<td>{escape(_fmt(cell))}</td>" for cell in row) + "</tr>" for row in rows
    )
    return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"


def _section(heading: str, *parts: str) -> str:
    return f'<div class="section"><h2>{escape(heading)}</h2>{"".join(parts)}</div>'


def _note(text: str) -> str:
    return f'<p class="note">{escape(text)}</p>'


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

    identity_block = (
        '<p class="absent">No certificate bound to this serial.</p>'
        if identity is None
        else _fields(
            [
                ("Certificate serial", identity.get("cert_serial")),
                ("Public key", identity.get("public_key_fingerprint")),
                ("Valid from", identity.get("cert_not_before")),
                ("Valid until", identity.get("cert_not_after")),
                ("Provisioned", identity.get("provisioned_at")),
            ]
        )
        + _note("Public certificate material only. A private key is not representable here.")
    )

    body = "".join(
        [
            _section(
                "Identity",
                _fields(
                    [
                        ("Instance", instance_id),
                        ("Module", instance.get("e_number")),
                        ("Design version", instance.get("version")),
                        ("Serial", instance.get("serial")),
                        ("Status", instance.get("status")),
                        ("Produced", instance.get("produced_at")),
                    ]
                ),
                _note(
                    "The identifier is the object key (ADR-0017 d15). Module, version and serial "
                    "are its parts, not separate facts."
                ),
            ),
            _section("Provisioning binding", identity_block),
            _section(
                "Integration history",
                _table(
                    ["Machine", "Position", "Installed", "Removed", "Reason"],
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
                ),
                _note(
                    "Position is assigned at integration and is never written onto the instance "
                    "(ADR-0017 d7). An open 'Removed' is the current placement."
                ),
            ),
            _section(
                "Lifecycle documents",
                _table(
                    ["Type", "Object key", "Valid until", "Status"],
                    [
                        [
                            d.get("doc_type"),
                            d.get("object_key"),
                            d.get("valid_until"),
                            d.get("status"),
                        ]
                        for d in documents
                    ],
                ),
                _note(
                    "The ERP indexes these; the documents themselves are in the object store "
                    "under the keys above (ADR-0021 d7)."
                ),
            ),
        ]
    )

    return _document(
        doc_class="Instance record",
        subject=instance_id,
        footer=f"Generated {_stamp()} from the instance-and-integration record (ADR-0021).",
        body=body,
    )


# Tables and fenced code are the two extensions that matter: the store's `-M`
# manuals and bring-up protocols use both, and a table flattened to prose is
# unreadable. `sane_lists` keeps a numbered list from swallowing the paragraph
# under it. `toc` gives every heading the slug id its own cross-references point
# at, so a "see §6" link in a manual is a live link in the PDF.
_MD_EXTENSIONS = ["tables", "fenced_code", "sane_lists", "toc"]


# Where the bytes came from, for the footer. A printed page is read away from
# the system that produced it, and "which record is this from" is the question a
# filed document has to answer on its own — the two origins are different homes
# under different decisions, so the page names the one it came from.
STORE_ORIGIN = "the repository store (ADR-0017 d15)"
LIFECYCLE_ORIGIN = "the lifecycle-document index (ADR-0021 d7)"


def markdown_document(
    *,
    object_key: str,
    text: str,
    subject: str | None = None,
    origin: str = STORE_ORIGIN,
) -> bytes:
    """One markdown document as a printable PDF.

    A rendering of a document the API already serves, not a new artifact: the
    bytes come from the object store and nothing is stored back. The page frame
    is the instance dossier's, so a printed manual and a printed instance record
    file together.
    """
    body = md_lib.markdown(text, extensions=_MD_EXTENSIONS)
    subject = subject or object_key
    footer = f"Generated {_stamp()} from {origin}."
    try:
        return _document(doc_class="Document", subject=subject, footer=footer, body=body)
    except Exception as exc:
        # A layout engine that raises still has to produce the document: an
        # operator holding a plain page is better served than one holding a 500.
        # The reason goes on the page and into the log — a fallback that only
        # shows on paper is a defect nobody is told about.
        log.exception("%s rendered as plain text", object_key)
        return _document(
            doc_class="Document",
            subject=subject,
            footer=footer,
            body=(
                f'<p class="note">Rendered as plain text — the layout engine refused '
                f"the markup: {escape(str(exc))}</p><pre>{escape(text)}</pre>"
            ),
        )
