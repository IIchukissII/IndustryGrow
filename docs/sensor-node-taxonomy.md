<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Sensor node classes in the node firmware

The manual of [ADR-0014](../ADR/ADR-0014-sensor-node-taxonomy-and-module-decomposition.md): how the one node image resolves a module class to a personality and publishes only the sensors that respond.

## ADR-0014 dec 1

One image serves every class; each class is one personality in one table. An instance differs only in its provisioned Node-ID.

| Code | Role |
|---|---|
| `firmware/nodes/registry.c` | `s_personalities`: one entry per built class; `node_for_module_id` selects one |
| `firmware/common/node/node.h` | `node_personality_t`: class ID, names, `init`, `spin` |
| `firmware/common/node/main.c` | Common boot path; runs the selected personality |
| `firmware/common/carrier/identity.c` | Instance Node-ID, independent of the class |

## ADR-0014 dec 2, 8

Each personality probes its devices at boot, publishes the subjects of those that respond, and re-probes on a fixed period.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | Probe and re-probe of the three I²C sensors |
| `firmware/nodes/m02_light/sensors.c` | Probe of the switch, then each sensor behind its own switch channel; re-probe |
| `firmware/nodes/m04_plant/sensors.c` | Probe of the thermal imager and the module EEPROM; re-probe when the imager is idle |
| `firmware/nodes/m05_safety/sensors.c` | Probe and re-probe of the I²C sensors; GPIO and ADC inputs always present |
| `firmware/common/drivers/i2c.c` | `i2c_probe` |

## ADR-0014 dec 3

A hardware provision of the module specifications; no code distinguishes a sensor on a lead from one on the board.

## ADR-0014 dec 4

Personalities and drivers exist for M01, M02, M04 and M05. M03-ANALYTICS, M06-VENTILATION and M07-AMBIENT have no firmware.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/drivers/sht4x.c` | SHT45 |
| `firmware/nodes/m01_climate/drivers/bme68x.c` | BME688 |
| `firmware/nodes/m01_climate/drivers/scd4x.c` | SCD41 |
| `firmware/nodes/m02_light/drivers/as7343.c` | AS7343 |
| `firmware/nodes/m02_light/drivers/tsl2585.c` | TSL2585 |
| `firmware/nodes/m02_light/drivers/tca9543a.c` | TCA9543A channel select |
| `firmware/nodes/m04_plant/drivers/mlx90640.c` | MLX90640 |
| `firmware/nodes/m04_plant/frame.c` | Full thermal frames for file transfer |
| `firmware/nodes/m05_safety/drivers/ina226.c` | INA226 |
| `firmware/nodes/m05_safety/drivers/s0.c` | S0 pulse input |
| `firmware/nodes/m05_safety/drivers/tmp117.c` | TMP117 |
| `firmware/nodes/m05_safety/drivers/leak.c` | Leak strip |

## ADR-0014 dec 5

The carrier BSP restates the header pin assignment from the carrier pin map; drivers exist for I²C, 1-Wire and PWM. No SPI driver exists.

| Code | Role |
|---|---|
| `firmware/common/carrier/e0001.h` | Header pins and carrier revision selection |
| `firmware/common/carrier/e0001.c` | Carrier BSP initialisation |
| `firmware/common/drivers/i2c.c` | Header I²C |
| `firmware/common/drivers/onewire.c` | 1-Wire line |
| `firmware/common/drivers/pwm.c` | PWM outputs |

## ADR-0014 dec 6

| Code | Role |
|---|---|
| `firmware/common/carrier/e0001.c` | `e0001_read_module_id` reads the strap; `e0001_read_class_id` reads byte 0 of the module EEPROM |
| `firmware/common/carrier/e0001.h` | `E0001_MODULE_EEPROM_ADDR`; `IGROW_CARRIER_000100` selects the transport at build time |
| `firmware/common/node/main.c` | Reads the class ID for the built carrier revision; an EEPROM that answers but fails to read is unidentified |
| `firmware/nodes/registry.c` | `node_for_module_id` never matches the reserved or erased value |
| `firmware/common/node/node.h` | `IGROW_NODE_NAME_UNIDENTIFIED`: the node-info name of an unresolved class |
| `firmware/nodes/m01_climate/module_id.h` | `M01_MODULE_ID` |
| `firmware/nodes/m02_light/module_id.h` | `M02_MODULE_ID` |
| `firmware/nodes/m04_plant/module_id.h` | `M04_MODULE_ID` |
| `firmware/nodes/m05_safety/module_id.h` | `M05_MODULE_ID` |
| `firmware/nodes/m04_plant/drivers/m24c64.h` | M04's module EEPROM, holding the class ID at byte 0 |

## ADR-0014 dec 7

The node carries no role or zone: `node_personality_t` has neither field. The gateway's Node-ID to `(module_class, node_role, zone)` map is not yet realised; the telemetry store keys samples on Node-ID and subject-ID only.

| Code | Role |
|---|---|
| `firmware/common/node/node.h` | Personality without role or zone |
| `gateway/files/app/gateway_telemetry.py` | Stores samples by Node-ID and subject-ID |

## ADR-0014 dec 9

A scope boundary; it has no form in this repository's code.

Links: [firmware/README.md](../firmware/README.md#identity-resolution), [firmware/README.md subject map](../firmware/README.md#default-subject-id-map), [the carrier specification](../spec/E0001-R-specification.md).
