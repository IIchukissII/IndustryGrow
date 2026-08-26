/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "flash.h"
#include "watchdog.h"
#include "e0001.h" /* CMSIS device header */

/* FLASH_KEYR unlock sequence (RM0090 3.5.1). */
#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

/* Program/erase parallelism x32. Valid because the carrier runs the MCU at
 * 3.3 V and x32 requires VDD >= 2.7 V (RM0090 3.5.2); it is also the fastest,
 * which is what keeps the stall inside the watchdog window. */
#define FLASH_PSIZE_X32 FLASH_CR_PSIZE_1

/* FLASH_SR error flags, all write-1-to-clear. SOP is the operation error. */
#define FLASH_SR_ERRORS (FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_PGPERR | FLASH_SR_PGSERR)

/*
 * Resident in RAM, and called through a pointer.
 *
 * `.RamFunc` is copied to SRAM by the startup code alongside `.data` (see the
 * linker script), so these routines keep executing while the flash controller
 * stalls every flash read. `long_call` is required, not cosmetic: SRAM is
 * 0x18000000 from the flash base and a Thumb-2 BL reaches +/-16 MB.
 * `noinline` keeps a body from being pulled back into a flash-resident caller.
 * GCC emits each function's literal pool inside its own section, so the
 * constants below travel to RAM with the code.
 */
#define RAMFUNC __attribute__((section(".RamFunc"), noinline, long_call))

/* Spin bound for a stuck controller. The loop runs from RAM at 168 MHz, so
 * this is seconds, not milliseconds -- a 128 KB sector erase is of the order
 * of one second and must not be cut short. BSY that never clears is dead
 * silicon; returning is better than reloading the watchdog forever. */
#define FLASH_BSY_GUARD 300000000u

static RAMFUNC int wait_busy(void)
{
    uint32_t guard = FLASH_BSY_GUARD;
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        /* The one reason this routine lives in RAM. watchdog.h owns the key. */
        IWDG->KR = IGROW_IWDG_KEY_RELOAD;
        if (--guard == 0u) {
            return -1;
        }
    }
    return ((FLASH->SR & FLASH_SR_ERRORS) != 0u) ? -1 : 0;
}

static RAMFUNC int erase_sector_unlocked(uint32_t sector)
{
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_SNB_Msk | FLASH_CR_PSIZE_Msk)) |
                FLASH_CR_SER | FLASH_PSIZE_X32 | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    const int rc = wait_busy();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB_Msk);
    return rc;
}

static RAMFUNC int program_words_unlocked(uint32_t addr, const uint32_t *words, uint32_t count)
{
    FLASH->SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE_Msk) | FLASH_CR_PG | FLASH_PSIZE_X32;
    int rc = 0;
    for (uint32_t i = 0u; (i < count) && (rc == 0); i++) {
        /* The source word is read before the store starts the operation, so it
         * may live in flash; the identity record is built on the stack. */
        *(volatile uint32_t *)(addr + (i * 4u)) = words[i];
        rc = wait_busy();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return rc;
}

/* Interrupts are masked for the whole operation. That is what makes the RAM
 * residency worth anything: an interrupt taken mid-erase would fetch its
 * handler from flash and stall there, and the watchdog reload in wait_busy()
 * would never run. */
static int unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return ((FLASH->CR & FLASH_CR_LOCK) != 0u) ? -1 : 0;
}

int flash_erase_sector(uint8_t sector)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int rc = unlock();
    if (rc == 0) {
        rc = erase_sector_unlocked(sector);
    }
    FLASH->CR |= FLASH_CR_LOCK;
    __set_PRIMASK(primask);
    return rc;
}

int flash_program_words(uint32_t addr, const uint32_t *words, size_t count)
{
    if (((addr & 3u) != 0u) || (words == NULL) || (count == 0u)) {
        return -1;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int rc = unlock();
    if (rc == 0) {
        rc = program_words_unlocked(addr, words, (uint32_t)count);
    }
    FLASH->CR |= FLASH_CR_LOCK;
    __set_PRIMASK(primask);
    return rc;
}
