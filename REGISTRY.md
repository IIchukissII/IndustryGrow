<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow identifier registry

The authoritative map from opaque identifiers to their meaning, per ADR-0017
(E-numbers) and ADR-0019 (SP numbers). Identifiers carry no meaning in
themselves; this registry is where meaning lives. It is the repo-side, public
type registry (ADR-0017 deferred "Registry and store location"); the
instance/integration layer — serials, provisioning, integration identifiers —
is platform-side and is not recorded here.

Conventions:
- An **E-number** (`Exxxx`) identifies a buildable/documentable assembly the
  project designs (ADR-0017 decision 3). Version (`-VVVVVV`) and serial
  (`-NNNNNN`) are the design's semver and the manufactured instance; neither
  appears here — this registry is type-level.
- An **SP number** (`SPxxxx`) identifies a purchased part by a vendor-free
  characteristic spec (ADR-0019). It has no project version or serial. The
  concrete vendor SKU and price live downstream in the BOM (ADR-0000), never here.
- Numbers are assigned sequentially at design/selection commit, opaque, and not
  pre-reserved by class (ADR-0017 decision 5; ADR-0019 decision 6).

## E-numbers (designed assemblies)

| E-number | Designation | Discipline | Notes |
|----------|-------------|------------|-------|
| `E0001` | Universal carrier | electrical | One bare design, one assembly — no real variant (ADR-0017 decision 4). Hosts the WeAct core board (`SP0005`). |
| `E0002` | M01-CLIMATE sensor module | electrical | Module-ID strap `0b001`. Air environment: SHT45, BME688, SCD41, FS3000. |
| `E0003` | M02-LIGHT sensor module | electrical | Module-ID strap `0b010`. Photic: AS7341 + UV-A sensor. |
| `E0004` | M03-ANALYTICS sensor module | electrical (mixed-signal) | Module-ID strap `0b011`. Hydroponic solution: pH (LMP7721 front-end), EC (AD5933), DS18B20, ADuM isolation. |
| `E0005` | M04-PLANT sensor module | electrical | Module-ID strap `0b100`. Plant-level: MLX90640 thermal imager. |
| `E0006` | M05-SAFETY / cabinet distribution + monitoring board | electrical | Module-ID strap `0b101`. Sense-only. Single INA226 on the `+12 V` SELV bus, TMP117, reed, leak, on-board input fuse, DIN-meter S0 input (ADR-0018). |

> E0002–E0006 are proposed in M01→M05 order; set them to the actual commit
> order. The contiguity is incidental, not a reservation.

## SP numbers (purchased parts)

| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |
|-----------|-----------------------------------|-------------------|-------|
| `SP0001` | DIN-rail energy meter, S0 pulse output (DIN EN 62053-31) | no | Phase count and pulse constant (imp/kWh) chosen per deployment in the BOM (ADR-0018). The S0-counting contract is the commitment; the meter is swappable. |
| `SP0002` | DIN-rail miniature circuit breaker (MCB), cabinet mains input | no | Rating and curve per deployment in the BOM. Distinct from the board's on-board `+12 V` input fuse, which is an MPN line in E0006's BOM, not an SP device. |
| `SP0003` | DIN-rail SELV power supply, `+12 V` output, OVP/OCP | no | Output power/current per deployment in the BOM. Phase 1 is `+12 V` only; a `+24 V` supply for the power section is a separate later SP entry (ADR-0018). |
| `SP0004` | Gateway SBC — Raspberry Pi 3B+ / 4 / 5 class | yes (vendor serial / gateway identity) | The one SP part with per-instance identity: its vendor serial and the ATECC-bound gateway certificate are the instance key (ADR-0019 decision 2; ADR-0004 / ADR-0007). Specific model in the BOM. |
| `SP0005` | STM32F405RGT6 core board (WeAct-class) | no | Hosted on every carrier (E0001). Resolves ADR-0017's WeAct deferred item (ADR-0019 decision 7). |

## Governing ADRs

- ADR-0017 — component / document / instance identification (E-numbers, two-axis model).
- ADR-0019 — purchased-part (SP) identification.
- ADR-0000 — single source of truth; vendor SKU and price live in the BOM, not here.
- ADR-0014 — sensor-module taxonomy (module-ID straps, M01–M05).
- ADR-0018 — cabinet power distribution (E0006 / M05, the S0 meter, the SELV supply).
