<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Mechanical development sources

Mechanical design sources under iteration, outside the document store and carrying no
identifier (ADR-0032 d8, ADR-0017 d5).

| | |
|---|---|
| Contents | One file per part, STEP plus the print-ready geometry |
| Naming | Part name, as the specification names it |
| Versioning | Git history. No version field, no serial, no E- or SP-number |
| Licence | CERN-OHL-S-2.0 (`REUSE.toml`), as `store/**` |
| Exit | At the commit a combination reaches, its parts file under the identifier that commit assigns and leave this directory |

`spec/E0012-R-specification.md` occupies it with the housing HS1 and the §5 duct set — DA1, DT1,
IT1, PL1 and SL1. A01-CLIMATE's humidity tract follows: TB1, the exchanger sections that house
`SP0011`, the wall penetrations and the condensate collector, per `spec/E0011-R-specification.md`
§9.
