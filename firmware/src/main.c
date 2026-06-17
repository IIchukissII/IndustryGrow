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

#include "board.h"
#include "clock.h"
#include "cyphal/cyphal.h"
#include "drivers/can.h"
#include "drivers/uart.h"

/* Static Node-ID for bring-up. ADR-0005 d6 makes this register-provisioned;
 * a fixed default is fine on the closed cabinet bus until then. */
#define IGROW_NODE_ID 96u

int main(void)
{
    clock_init();
    board_init();
    uart_init();

    uart_puts("\r\nIndustryGrow M05-SAFETY bring-up (layer 1)\r\n");

    /* Module-ID strap self-check (ADR-0014 d6). */
    uint8_t id = board_read_module_id();
    uart_puts("module-id strap = 0b");
    uart_put_bin3(id);
    bool is_m05 = (id == BRD_MODULE_ID_M05);
    uart_puts(is_m05 ? " -> M05-SAFETY OK\r\n"
                     : " -> MISMATCH (expected 0b101)\r\n");
    /* NOTE: STRAP_1 (PA6, bit 1) is unrouted to the MCU on E0001-000001 and
     * reads the pull-down 0. For M05 (0b101) bit 1 is 0, so this passes; it is
     * NOT a true read of bit 1. Tracked carrier fix — see board.h / pin map. */
    board_led_status(is_m05);

    /* bxCAN peripheral + 500 kbit/s bit-timing self-test (internal loopback). */
    int rc = can_selftest_loopback();
    uart_puts(rc == 0 ? "CAN loopback self-test OK\r\n"
                      : "CAN loopback self-test FAIL\r\n");

    /* Live bus + Cyphal node: publishes Heartbeat (1 Hz) and answers GetInfo,
     * so the node enumerates on the gateway console (roadmap stage 1). */
    (void)can_init_normal();
    cyphal_init(IGROW_NODE_ID);
    uart_puts("cyphal up: node-id ");
    uart_put_u32(IGROW_NODE_ID);
    uart_puts(", Heartbeat + GetInfo live\r\n");

    uint32_t last = millis();
    for (;;) {
        cyphal_spin();
        if ((millis() - last) >= 500u) {
            last = millis();
            board_led_status_toggle(); /* liveness blink */
        }
    }
}
