<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Security Policy

IndustryGrow is an early-stage, open-core project (Phase 1 — hardware and
firmware bring-up). Security is taken seriously across the stack: hardware
reference designs, reference firmware, gateway edge software, and the Cyphal/CAN
field bus. This document explains how to report a vulnerability and what to
expect.

## Supported versions

There are no tagged releases yet; development happens on `main`.

| Version | Supported |
|---------|-----------|
| `main` (development) | ✅ Security fixes land here |
| Tagged releases | n/a — none yet; this table will be updated when releases begin |

Until releases begin, always evaluate and patch against the latest `main`.

## Reporting a vulnerability

**Please do not open a public issue, pull request, or discussion for a security
vulnerability**, and do not disclose it publicly until it has been addressed.

Report privately through GitHub's **private vulnerability reporting**:

1. Go to the repository's **Security** tab → **Report a vulnerability** (this
   opens a private GitHub Security Advisory visible only to you and the
   maintainers).
2. If that is unavailable to you, contact the maintainer privately via their
   GitHub profile: <https://github.com/IIchukissII>.

Please include, as far as you can:

- the affected component (hardware design, firmware, gateway software, DSDL/protocol);
- the version / commit (`main` SHA) and environment;
- a description of the issue and its impact;
- steps to reproduce or a proof of concept;
- any suggested mitigation.

## What to expect

This is a small, early-stage project, so responses are **best effort**:

- **Acknowledgement** of your report within a few days.
- An assessment of validity and severity, and a fix or mitigation plan for
  confirmed issues, coordinated with you.
- **Coordinated disclosure:** we will agree on a disclosure timeline with you;
  please allow reasonable time to release a fix before any public disclosure.
- Credit for the reporter in the advisory, unless you prefer to remain anonymous.

## Scope and security architecture

The security architecture is described in the Architecture Decision Records
([`ADR/`](ADR/)) and is the best context for a report:

- **ADR-0004 (rev 1)** — gateway host hardening, stateless-edge operation,
  firmware signing, and the gateway↔IndustryFlow trust boundary.
- **ADR-0002 (rev 3)** — field bus architecture and the per-node ATECC608 secure
  element.
- **ADR-0007 (planned)** — PKI: ATECC608 binding, certificate provisioning, and
  gateway-to-platform authentication.

In-scope: the hardware designs, reference firmware, gateway software, and
protocol/DSDL definitions in this repository. Operational deployments and the
IndustryFlow platform are governed separately.
