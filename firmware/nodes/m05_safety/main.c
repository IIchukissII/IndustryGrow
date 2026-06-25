/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * M05-SAFETY firmware — bring-up, layer 1 (board + clock + CAN-raw).
 *
 * This layer proves the board path with no Cyphal stack yet: clock to
 * 168 MHz, module-ID strap self-check, a bxCAN internal-loopback self-test,
 * then idle blinking the status LED. Layer 2 adds libcanard and the node
 * skeleton (Heartbeat + GetInfo + register) so the node enumerates on the
 * gateway. See firmware/README.md for the milestone definition.
 */

#include "e0001.h"
#include "atecc608.h"
#include "module_id.h"
#include "clock.h"
#include "cyphal.h"
#include "can.h"
#include "uart.h"
#include "sensors.h"

/* Print a byte as two uppercase hex digits over the debug UART. */
static void put_hex8(uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0xFu]);
}

/* Static Node-ID for bring-up. ADR-0005 d6 makes this register-provisioned;
 * a fixed default is fine on the closed cabinet bus until then. */
#define IGROW_NODE_ID 96u

int main(void)
{
    clock_init();
    e0001_init();
    uart_init();

    uart_puts("\r\nIndustryGrow M05-SAFETY bring-up (layer 1)\r\n");

    /* Module-ID strap self-check (ADR-0014 d6). */
    uint8_t id = e0001_read_module_id();
    uart_puts("module-id strap = 0b");
    uart_put_bin3(id);
    bool is_m05 = (id == M05_MODULE_ID);
    uart_puts(is_m05 ? " -> M05-SAFETY OK\r\n"
                     : " -> MISMATCH (expected 0b101)\r\n");
    /* NOTE: STRAP_1 (PA6, bit 1) is unrouted to the MCU on E0001-000001 and
     * reads the pull-down 0. For M05 (0b101) bit 1 is 0, so this passes; it is
     * NOT a true read of bit 1. Tracked carrier fix — see e0001.h / pin map. */
    e0001_led_status(is_m05);

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

    /* Live bus + Cyphal node: publishes Heartbeat (1 Hz) and answers GetInfo,
     * so the node enumerates on the gateway console (roadmap stage 1). */
    (void)can_init_normal();
    cyphal_init(IGROW_NODE_ID);
    sensors_init(); /* M05 personality: probe + publish the sensor set */
    uart_puts("cyphal up: node-id ");
    uart_put_u32(IGROW_NODE_ID);
    uart_puts(", M05 telemetry live\r\n");

    uint32_t last = millis();
    for (;;) {
        sensors_spin(); /* queue sensor telemetry ... */
        cyphal_spin();  /* ... and flush TX + service RX */
        if ((millis() - last) >= 500u) {
            last = millis();
            e0001_led_status_toggle(); /* liveness blink */
        }
    }
}
