/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "watchdog.h"
#include "e0001.h" /* CMSIS device header */

/* IWDG key register commands (RM0090 21.4.1). */
#define IWDG_KEY_ACCESS 0x5555u
#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_START  0xCCCCu

/* Nominal LSI is 32 kHz, so /64 gives 500 counts/s and 1000 counts ~= 2 s.
 *
 * The slowest legitimate pass through the main loop is a publish cycle: three
 * INA226 reads, a TMP117 read, the leak channel's 5 ms excitation settle, and
 * the Cyphal TX flush -- tens of milliseconds. 2 s is ~100x that, so only a
 * genuine hang trips it.
 *
 * The margin also has to absorb the LSI itself, which is neither trimmed nor
 * accurate: the STM32F405 datasheet spreads it 17-47 kHz. At 47 kHz the window
 * shrinks to ~1.4 s and at 17 kHz it stretches to ~3.8 s. Both still clear the
 * worst-case loop by orders of magnitude, which is why a coarse timeout is the
 * right choice here -- a tight one would demand LSI calibration to be safe. */
#define IWDG_PRESCALER_DIV64 4u
#define IWDG_RELOAD_COUNTS   1000u

static uint8_t s_reset_cause;

void watchdog_capture_reset_cause(void)
{
    s_reset_cause = (uint8_t)(RCC->CSR >> 24);
    RCC->CSR |= RCC_CSR_RMVF; /* clear, so the next boot reports its own cause */
}

uint8_t watchdog_reset_cause(void)
{
    return s_reset_cause;
}

const char *watchdog_reset_cause_str(void)
{
    /* Several flags are set at once on a normal power-up (BOR and POR together,
     * and a debug probe adds PIN and SFT), so report the most diagnostic one
     * first rather than pretending a single cause. */
    if (s_reset_cause & (uint8_t)(RCC_CSR_IWDGRSTF >> 24)) {
        return "IWDG (firmware hang)";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_WWDGRSTF >> 24)) {
        return "WWDG";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_LPWRRSTF >> 24)) {
        return "low-power";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_SFTRSTF >> 24)) {
        return "software";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_PINRSTF >> 24)) {
        return "NRST pin (debug probe or button)";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_PORRSTF >> 24)) {
        return "power-on";
    }
    if (s_reset_cause & (uint8_t)(RCC_CSR_BORRSTF >> 24)) {
        return "brown-out";
    }
    return "unknown";
}

/* Bounded spins. A watchdog that hangs the boot it was added to protect is
 * worse than no watchdog, so nothing here waits forever. ~2.4 ms at 168 MHz,
 * against an LSI startup the datasheet bounds at 40 us. */
#define WDG_SPIN_GUARD 100000u

void watchdog_start(void)
{
    /* Freeze the counter when a debugger halts the core. Without this, every
     * breakpoint longer than the timeout resets the target -- which looks
     * exactly like the spurious resets a watchdog is meant to expose. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    /* The IWDG is clocked by the LSI, and PR/RLR writes are synchronised to it:
     * with the LSI stopped, IWDG_SR never clears. LSION is 0 out of reset and
     * nothing else on this node starts the LSI, so start it here and wait for
     * it -- writing PR/RLR first would spin on SR forever. */
    RCC->CSR |= RCC_CSR_LSION;
    for (uint32_t g = WDG_SPIN_GUARD; !(RCC->CSR & RCC_CSR_LSIRDY) && g; g--) {
    }
    if ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        return; /* no LSI: run unprotected rather than never reaching main() */
    }

    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = IWDG_PRESCALER_DIV64;
    IWDG->RLR = IWDG_RELOAD_COUNTS;
    for (uint32_t g = WDG_SPIN_GUARD; (IWDG->SR != 0u) && g; g--) {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_START;
}

void watchdog_kick(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}
