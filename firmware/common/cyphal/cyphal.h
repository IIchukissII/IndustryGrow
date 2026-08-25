/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_CYPHAL_CYPHAL_H
#define IGROW_CYPHAL_CYPHAL_H

#include <stddef.h>
#include <stdint.h>

/* Cyphal node skeleton over libcanard (ADR-0005 d5): publishes
 * uavcan.node.Heartbeat at 1 Hz, answers uavcan.node.GetInfo + the register
 * interface + ExecuteCommand. That makes the node enumerate and be configurable
 * on the gateway. The per-node personality (sensor publications) sits on top
 * via cyphal_publish().
 *
 * `node_id` is a static bring-up default. ADR-0027 makes it an instance value
 * provisioned into a carrier flash sector -- not derived from the module class --
 * which this image does not yet do. Call cyphal_init() once after can_init_normal(), then cyphal_spin()
 * as often as possible from the main loop.
 *
 * `node_name` is the reverse-DNS uavcan.node.GetInfo name and `description` the
 * uavcan.node.description register. Both belong to the strap-selected
 * personality, not to the image -- one image serves every module class
 * (ADR-0017 d16), so these are how the gateway tells an E0002 from an E0006 on
 * the wire. Both must outlive the call; string literals are the intended source. */
void cyphal_init(uint8_t node_id, const char *node_name, const char *description);
void cyphal_spin(void);

/* Publish a pre-serialized message on `subject_id` (a Cyphal port-ID). The
 * caller owns `transfer_id` (one counter per subject) — it is post-incremented
 * and wrapped. Used by the node personality to emit sensor telemetry. */
void cyphal_publish(uint16_t subject_id, uint8_t *transfer_id,
                    const uint8_t *payload, size_t size);

/* The value to put in every uavcan.time.SynchronizedTimestamp field: the
 * network time base if this node is synchronized, otherwise 0 = UNKNOWN.
 *
 * That field is a NETWORK-wide time base, not a per-node one. The gateway is
 * the bus time-synchronization master (ADR-0002 d11) and publishes
 * uavcan.time.Synchronization on subject 7168 at least once per second; this node
 * is a slave and
 * tracks it as an OFFSET against its own monotonic clock. The local clock is
 * never stepped -- libcanard's transmission deadlines and transfer-ID timeouts
 * are denominated in it.
 *
 * Returns 0 whenever the offset is not currently trustworthy: before the first
 * pair of sync messages, and again once the master has been silent for three
 * publication periods. A stale offset reported as truth is worse than an absent
 * timestamp, because a consumer cannot detect it. Without a master, two nodes'
 * stamps share no origin at all -- node 96 at 1378 s and node 97 at 62 s were
 * describing the same instant -- and a consumer that aligns samples across
 * nodes, which is what the state estimator of ADR-0016 does, would be silently
 * wrong. That is what this exists to prevent.
 *
 * Accuracy is milliseconds, not microseconds (ADR-0002 d11): reception is
 * timestamped in the polled main loop, so the error is bounded by however long
 * a personality's blocking sensor read keeps the loop away from pump_rx().
 */
uint64_t cyphal_timestamp_usec(void);

/* Report the personality's own health for the next heartbeat. The heartbeat
 * carries the WORSE of this and the skeleton's own assessment, so a personality
 * cannot mask an identity fault and the skeleton cannot mask a sensor fault.
 * Values are uavcan.node.Health constants. Never reported means NOMINAL. */
void cyphal_set_health(uint8_t health);

/* Publish uavcan.diagnostic.Record (subject 8184) at OPTIONAL priority.
 *
 * The node is the only thing that knows why a reading failed, and until now it
 * said so on a UART that no deployment reads. Event-driven only -- a periodic
 * diagnostic is a log, and the bus is not the place for one. `severity` takes
 * uavcan.diagnostic.Severity values; text is truncated to fit the type. */
void cyphal_diagnostic(uint8_t severity, const char *text);

/* As above with an unsigned decimal appended after a space. */
void cyphal_diagnostic_u32(uint8_t severity, const char *text, uint32_t value);

/* Vendor-specific uavcan.node.ExecuteCommand handling. The standard commands
 * stay with the skeleton; anything the vendor range defines (from zero upward)
 * is offered to this handler, which returns a uavcan.node.ExecuteCommand
 * Response STATUS_* value. Unset means every vendor command is rejected.
 *
 * A command must return promptly: the response is sent from the same pass of
 * the loop, and the watchdog window is ~1.4 s at worst-case LSI. Anything
 * slower accepts the command and does the work from the personality's spin. */
typedef uint8_t (*cyphal_command_fn)(uint16_t command);
void cyphal_set_command_handler(cyphal_command_fn fn);

/* Declare the subjects this personality publishes, for uavcan.node.port.List
 * (subject 7510). The array must outlive the call. Heartbeat and port.List
 * itself are added by the skeleton. Without this a consumer cannot discover
 * what the node emits — ADR-0005 requires the capability. */
void cyphal_declare_publishers(const uint16_t *subject_ids, uint8_t count);

#endif /* IGROW_CYPHAL_CYPHAL_H */
