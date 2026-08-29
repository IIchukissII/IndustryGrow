/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * IndustryGrow node bootloader -- sectors 0-3, ADR-0029.
 *
 * The second image every node carries. It owns the flash: the application
 * never writes a slot (d3), so everything that erases or programs an image
 * lives here, in one component that is signed as a whole and replaced only
 * over SWD (d10).
 *
 * What it does on every boot: read the update-state block, act on a pending
 * update request, settle the A/B decision including a failed trial's revert
 * (d8), check that the chosen slot holds an image this hardware may run, and
 * hand over. The bus half lives in update.c -- it is reached only when there is
 * an update to run or nothing to boot, so an ordinary boot costs milliseconds
 * and never joins the bus.
 *
 * It runs before the watchdog: IWDG cannot be stopped once started, and an
 * application that inherited a running watchdog would have to service it
 * through its own slow bring-up. The application starts it (common/node/main.c)
 * once it is ready to.
 */

#include "e0001.h"
#include "clock.h"
#include "uart.h"
#include "image.h"
#include "partition.h"
#include "update.h"
#include "update_state.h"

static void put_hex8(uint8_t b)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0xFu]);
}

static void put_hex32(uint32_t v)
{
    uart_puts("0x");
    for (int i = 24; i >= 0; i -= 8) {
        put_hex8((uint8_t)(v >> i));
    }
}

static void report_slot(igrow_slot_t slot)
{
    const igrow_image_header_t *h = image_header(slot);
    uart_puts("slot ");
    uart_puts(partition_slot_str(slot));
    uart_puts(" @ ");
    put_hex32(partition_slot_addr(slot));
    if (!image_slot_bootable(slot)) {
        uart_puts(": no bootable image\r\n");
        return;
    }
    uart_puts(": v");
    uart_put_u32(h->version_major);
    uart_putc('.');
    uart_put_u32(h->version_minor);
    uart_puts(", ");
    uart_put_u32(h->image_length);
    uart_puts(" bytes\r\n");
}

/* Hand over to the image in `slot`. Never returns.
 *
 * The application is entitled to the reset state of everything the bootloader
 * touched: interrupts disabled at the NVIC and none pending, the clock tree
 * back on the HSI, and VTOR on the image's own table. Its startup code then
 * runs as if it had been reset into. */
static void jump_to_image(igrow_slot_t slot)
{
    const uint32_t base = image_body_addr(slot);
    const uint32_t sp = *(const volatile uint32_t *)base;
    const uint32_t pc = *(const volatile uint32_t *)(base + 4u);

    uart_puts("boot -> slot ");
    uart_puts(partition_slot_str(slot));
    uart_puts("\r\n");
    uart_flush();

    for (uint32_t i = 0u; i < 8u; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    clock_deinit();

    SCB->VTOR = base;
    __DSB();
    __ISB();
    __set_MSP(sp);
    ((void (*)(void))pc)();

    for (;;) {
    }
}

int main(void)
{
    clock_init();
    e0001_init();
    uart_init();

    uart_puts("\r\nIndustryGrow bootloader v");
    uart_put_u32(IGROW_IMAGE_VERSION_MAJOR);
    uart_putc('.');
    uart_put_u32(IGROW_IMAGE_VERSION_MINOR);
    uart_puts(" (carrier E0001)\r\n");

    update_state_load();
    report_slot(IGROW_SLOT_A);
    report_slot(IGROW_SLOT_B);

    /* An update request is answered before anything else, and by restarting
     * rather than by booting the new slot directly: the slot then goes through
     * the same checks as any other boot, on a node whose peripherals are in
     * their reset state rather than half way through a file transfer. */
    if (update_state()->request_pending) {
        if (update_run_request()) {
            uart_puts("restarting into the new image\r\n");
            uart_flush();
            NVIC_SystemReset();
        }
        uart_puts("update abandoned; booting what was already here\r\n");
    }

    update_state_t st = *update_state();

    /* A trial slot spends one attempt per boot, and the spend is committed
     * BEFORE the image runs (d8). An image that hangs or faults on entry
     * therefore still costs an attempt, which is what bounds the loop: were
     * the counter written after a successful start, an image that never gets
     * that far would retry forever. */
    if (st.state == IGROW_BOOT_TRIAL) {
        if (st.attempts == 0u) {
            uart_puts("trial slot ");
            uart_puts(partition_slot_str(st.boot_slot));
            uart_puts(" did not confirm -> revert to slot ");
            uart_puts(partition_slot_str(st.prev_slot));
            uart_puts("\r\n");
            st.boot_slot = st.prev_slot;
            st.state = IGROW_BOOT_CONFIRMED;
        } else {
            st.attempts--;
            uart_puts("trial boot, attempts left after this one: ");
            uart_put_u32(st.attempts);
            uart_puts("\r\n");
        }
        (void)update_state_store(&st);
    }

    /* A slot can fail its check without ever having been on trial: an
     * interrupted SWD flash, or flash that has lost a bit. Falling back to the
     * other slot is the same rollback by a different route. */
    if (!image_slot_bootable(st.boot_slot)) {
        const igrow_slot_t other = partition_other_slot(st.boot_slot);
        uart_puts("slot ");
        uart_puts(partition_slot_str(st.boot_slot));
        uart_puts(" failed its check\r\n");
        if (!image_slot_bootable(other)) {
            update_await_image(); /* never returns */
        }
        st.boot_slot = other;
        st.prev_slot = other;
        st.state = IGROW_BOOT_CONFIRMED;
        st.attempts = 0u;
        (void)update_state_store(&st);
    }

    e0001_led_status(true);
    jump_to_image(st.boot_slot);
    return 0;
}
