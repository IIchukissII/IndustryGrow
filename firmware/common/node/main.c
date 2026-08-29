/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * IndustryGrow node firmware -- the one image every node runs.
 *
 * Boot is identical on every node: clock, carrier BSP, debug console, module-ID
 * strap read, CAN self-test, carrier identity, Cyphal node. The strap then
 * selects the sensor-module personality (ADR-0017 d16), which is the only part
 * that differs between an E0002 and an E0006 in the socket.
 *
 * Two independent questions are answered here, and neither answers the other
 * (ADR-0027 d1): the strap says WHAT is fitted, the carrier flash store says
 * WHICH INSTANCE this node is. Telemetry needs both.
 *
 * Nothing here knows what a personality measures. That is the point: a new
 * module class adds a nodes/<type>/ directory and one registry entry, and this
 * file does not change.
 */

#include "e0001.h"
#include "atecc608.h"
#include "identity.h"
#include "node.h"
#include "clock.h"
#include "partition.h"
#include "watchdog.h"
#include "cyphal.h"
#include "can.h"
#include "uart.h"

/* This image's vector table, placed first by the linker script. Its address is
 * both what VTOR must point at and how the image learns which slot it runs
 * from -- nothing else tells it. */
extern uint32_t g_pfnVectors;

/* Print a byte as two uppercase hex digits over the debug UART. */
static void put_hex8(uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0xFu]);
}

int main(void)
{
    /* Before anything else: the image runs from a slot, not from the reset
     * vector (ADR-0029 d1), so the vector table must be pointed at this
     * image's own before any interrupt can be taken. The bootloader set it
     * already; doing it here is what makes the application correct on its own
     * terms rather than by inheritance. ST's SystemInit leaves VTOR alone. */
    SCB->VTOR = (uint32_t)&g_pfnVectors;

    /* The reset flags are sticky, so latch and clear them while they still
     * describe THIS boot rather than every boot since power-on. */
    watchdog_capture_reset_cause();

    clock_init();
    e0001_init();
    uart_init();

    uart_puts("\r\nIndustryGrow node firmware (carrier E0001) v");
    uart_put_u32(IGROW_APP_VERSION_MAJOR);
    uart_putc('.');
    uart_put_u32(IGROW_APP_VERSION_MINOR);
    /* Which slot the image is running from. An operator reading a boot log
     * through an update needs it, and it is read from the vector table
     * rather than from a build-time constant so that it reports where the
     * code actually is. */
    igrow_slot_t slot;
    if (partition_slot_of((uint32_t)&g_pfnVectors, &slot)) {
        uart_puts(", slot ");
        uart_puts(partition_slot_str(slot));
    }
    uart_puts("\r\n");
    uart_puts("last reset: ");
    uart_puts(watchdog_reset_cause_str());
    uart_puts("\r\n");

    /* Module-ID strap -> personality (ADR-0014 rev 4 d6, ADR-0017 d16).
     *
     * The strap carries 3 bits on this carrier revision, so it reaches classes
     * 0x01..0x07 -- every sensor class defined today. Actuator classes start at
     * 0x80 and need the EEPROM transport of E0001-000100; this image is built
     * for the strap carrier and does not attempt to fall back between the two. */
    uint8_t module_id = e0001_read_module_id();
    const node_personality_t *node = node_for_module_id(module_id);

    uart_puts("module-id strap = 0b");
    uart_put_bin3(module_id);
    if (node != NULL) {
        uart_puts(" -> ");
        uart_puts(node->name);
        uart_puts("\r\n");
    } else {
        uart_puts(" -> UNIDENTIFIED (no personality claims this ID)\r\n");
    }
    /* All three strap bits are read. A carrier older than E0001-000003 without
     * the hand-added STRAP_1 link reads bit 1 as the pull-down 0, so it reads
     * every bit-1 class (M02/M03/M06/M07) with that bit cleared and can select
     * only M01 (0x01) and M05 (0x05) -- see common/carrier/e0001.h. */
    e0001_led_status(node != NULL);

    /* bxCAN peripheral + 500 kbit/s bit-timing self-test (internal loopback). */
    int rc = can_selftest_loopback();
    uart_puts(rc == 0 ? "CAN loopback self-test OK\r\n"
                      : "CAN loopback self-test FAIL\r\n");

    /* Carrier identity IC: probe the ATECC608 on I2C2 (ADR-0007 identity anchor).
     * Its 9-byte serial becomes the Cyphal node unique-id; absent on a bare WeAct
     * (no carrier), in which case the STM32 factory UID is used instead. */
    atecc608_init();
    uart_puts("ATECC608 carrier ID (I2C2): ");
    if (atecc608_present()) {
        const uint8_t *sn = atecc608_serial();
        uart_puts("present, SN=");
        for (unsigned i = 0; i < ATECC608_SERIAL_LEN; i++) {
            put_hex8(sn[i]);
        }
        uart_puts(" -> node unique-id\r\n");
    } else {
        uart_puts("absent -> STM32 factory UID\r\n");
    }

    /* Instance identity: the Node-ID, read from the dedicated carrier flash
     * sector (ADR-0027 d2). Not derived from the strap -- one ID per module
     * class collides the moment a class has two instances. */
    identity_init();
    const uint8_t node_id = identity_node_id();
    uart_puts("node identity: ");
    uart_puts(identity_state_str());
    uart_puts(", node-id ");
    uart_put_u32(node_id);
    uart_puts("\r\n");

    /* Telemetry needs a provisioned identity AND a resolved personality. An
     * unprovisioned node sits at 127 and publishes no subjects (d6); a
     * provisioned node whose class resolves to nothing keeps its own Node-ID
     * and publishes nothing either, because it does not know what it would
     * publish (d10). Both stay visible: heartbeat, GetInfo, registers. */
    const bool publish = identity_provisioned() && (node != NULL);

    /* Live bus + Cyphal node: Heartbeat at 1 Hz, GetInfo, register,
     * ExecuteCommand. Brought up in every case -- a node that is visible on
     * the gateway can be diagnosed; one that stays silent cannot. */
    const char *cyphal_name = (node != NULL) ? node->cyphal_name
                                             : IGROW_NODE_NAME_UNIDENTIFIED;
    const char *description = (node != NULL) ? node->name : "unidentified module";
    (void)can_init_normal();
    cyphal_init(node_id, cyphal_name, description);
    cyphal_report_identity(identity_provisioned(), node != NULL);
    uart_puts("cyphal up: node-id ");
    uart_put_u32(node_id);
    uart_puts("\r\n");

    /* Personality bring-up. This is the slow part of boot on some modules --
     * M01's SCD41 alone blocks 500 ms for its mandatory stop, and 800 ms more
     * on the one boot that persists ASC-off to EEPROM -- which is exactly why
     * the watchdog is not running yet. */
    if (publish) {
        node->init();
    } else {
        uart_puts("skeleton only, no sensor publications: ");
        uart_puts(identity_provisioned() ? "no personality claims this strap\r\n"
                                         : "no Node-ID provisioned\r\n");
    }

    /* Last: the personality bring-up, the ATECC read and the CAN self-test are
     * slow and must not race the first timeout. Once started it cannot be
     * stopped. */
    watchdog_start();

    uint32_t last = millis();
    for (;;) {
        watchdog_kick(); /* the ONLY kick: from the foreground, never an ISR */
        if (publish) {
            node->spin(); /* queue sensor telemetry ... */
        }
        cyphal_spin();    /* ... and flush TX + service RX */
        if ((millis() - last) >= 500u) {
            last = millis();
            e0001_led_status_toggle(); /* liveness blink */
        }
    }
}
