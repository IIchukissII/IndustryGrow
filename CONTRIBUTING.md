<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Contributing to IndustryGrow

Thanks for your interest in contributing. This document covers how contributions
are licensed and the Developer Certificate of Origin (DCO) you agree to when you
submit one.

The design is driven by Architecture Decision Records in [`ADR/`](ADR/) — read
the relevant ADR before proposing a change to that area. Changes land through a
pull request against `main`; the [REUSE Compliance](.github/workflows/reuse.yml)
check must pass before merge.

## Licensing of contributions

IndustryGrow is **open-core**: different parts of the repository carry different
licenses (see [`LICENSE.md`](LICENSE.md) for the authoritative mapping and
[`LICENSES/`](LICENSES/) for the full texts).

Contributions are **inbound = outbound**: by submitting a change you agree that
your contribution is licensed under the **same license as the part of the
repository it touches**, namely:

| Part of the repository | License | SPDX ID |
|------------------------|---------|---------|
| Hardware designs (`store/`) | CERN Open Hardware Licence v2 – Strongly Reciprocal | `CERN-OHL-S-2.0` |
| ADRs & documentation (`ADR/`, `README.md`, `REGISTRY.md`, etc.) | Creative Commons Attribution-ShareAlike 4.0 | `CC-BY-SA-4.0` |
| Reference firmware *(when added)* | GNU Affero GPL v3 or later | `AGPL-3.0-or-later` |
| DSDL / protocol layer *(when added)* | Apache License 2.0 | `Apache-2.0` |

You retain copyright to your contributions; this is not a copyright assignment.

### SPDX / REUSE headers

The repository is [REUSE](https://reuse.software) compliant and CI enforces it.
Every file you add must carry licensing information:

- **Files that support comments** (Markdown, YAML, source code) — add an inline
  header at the top, e.g. for documentation:

  ```
  <!--
  SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
  -->
  ```

- **Files that cannot carry a comment** (KiCad sources, generated fab outputs,
  binaries) — add an entry to [`REUSE.toml`](REUSE.toml) instead.

Use `The IndustryGrow contributors` as the `SPDX-FileCopyrightText` holder unless
you have a specific reason to use your own name. You can check locally with:

```
pip install reuse
reuse lint
```

## Developer Certificate of Origin (DCO)

We use the [Developer Certificate of Origin](https://developercertificate.org/)
instead of a CLA. It is a lightweight way for you to certify that you wrote, or
otherwise have the right to submit, the code or content you contribute.

**Every commit must be signed off.** Add a `Signed-off-by` line with your real
name and email by committing with the `-s` flag:

```
git commit -s -m "Your commit message"
```

This appends:

```
Signed-off-by: Your Name <your.email@example.com>
```

The name and email must match a real identity and your git author information.
Commits without a valid `Signed-off-by` line will not be accepted. If you forget,
amend the last commit with `git commit --amend -s`, or for multiple commits use
`git rebase --signoff <base>`.

By signing off, you certify the following:

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```
