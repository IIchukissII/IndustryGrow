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
#include "drivers/can.h"
#include "drivers/uart.h"

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

    /* Re-init for the live bus; layer 2 hands this off to libcanard. */
    (void)can_init_normal();

    /* Idle: ~1 Hz status blink + mirror any RX activity onto the CAN LED. */
    uint8_t buf[8];
    uint16_t rid;
    uint8_t rlen;
    uint32_t last = millis();
    for (;;) {
        if (can_recv(&rid, buf, &rlen) == 1) {
            board_led_can(true);
        }
        if ((millis() - last) >= 500u) {
            last = millis();
            board_led_status_toggle();
            board_led_can(false);
        }
    }
}
