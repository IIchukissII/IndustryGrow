/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless board self-test. Builds for either eval board -- see eval_board.h;
 * both are the MB1246 motherboard, so one source covers both and the build
 * picks the BSP, the device header and the core supply option.
 *
 * The MCU define is not cosmetic: a CM7 image built for a dual-core part is
 * not a trustworthy instrument for judging single-core silicon, or the
 * reverse.
 *
 * No display is needed, so nothing here uses one. Every result goes to the
 * console (COM, 115200 8N1) and every wait is bounded: a peripheral that is
 * absent or wedged becomes a line of text, never a hang.
 *
 * Tests, in the order a failure stops being diagnosable:
 *
 *   1  identity     device/revision/UID/flash size straight out of the silicon
 *   2  power        voltage scaling, and whether Scale 1 is reachable
 *   3  clocks       HSE, PLL1, the four bus clocks
 *   4  LSE          32.768 kHz crystal
 *   5  internal RAM AXI D1, D2 SRAM1/2/3, SRD SRAM4, backup SRAM
 *   6  SDRAM        IS42S32800G, 32 MB: data bus, address bus, full sweep
 *   7  QSPI NOR     MT25TL01G: JEDEC ID, indirect read, memory-mapped read
 *   8  I2C1         bus scan (names the fitted daughterboards)
 *   9  MFX + LEDs   the IO expander, and all four LEDs blinked
 *  10  FDCAN1       internal loopback: the controller with no transceiver
 *  11  ADC / POT1   potentiometer
 *  12  RTC          runs and advances on the LSE
 *  13  microSD      card detect
 *
 * Nothing here writes to the QSPI flash or the SD card. Both are read only.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "eval_board.h"
#include "stm32h7xx_hal.h"

/* --- result table --------------------------------------------------------- */

typedef enum { R_PASS = 0, R_FAIL, R_INFO, R_SKIP } res_t;

typedef struct {
    const char *name;
    res_t       verdict;
    char        detail[72];
} result_t;

#define MAX_RESULTS 24

static result_t s_results[MAX_RESULTS];
static unsigned s_result_count;

static const char *verdict_text(res_t r)
{
    switch (r) {
    case R_PASS:
        return "PASS";
    case R_FAIL:
        return "FAIL";
    case R_INFO:
        return "info";
    default:
        return "skip";
    }
}

/* --- console -------------------------------------------------------------- */

static UART_HandleTypeDef s_uart;
static bool               s_uart_ok;

/* Polled transmit rather than HAL_UART_Transmit: this has to work before,
 * during and after clock changes, and must never depend on a tick that a
 * clock change re-bases. */
static void uputc(char c)
{
    while ((COM1_UART->ISR & USART_ISR_TXE_TXFNF) == 0U) {
    }
    COM1_UART->TDR = (uint8_t)c;
}

static void uputs(const char *s)
{
    while ((s != NULL) && (*s != '\0')) {
        uputc(*s++);
    }
}

static void uprintf(const char *fmt, ...)
{
    if (!s_uart_ok) {
        return;
    }
    char    line[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > 0) {
        uputs(line);
    }
}

static int ugetc(void)
{
    if ((COM1_UART->ISR & USART_ISR_RXNE_RXFNE) == 0U) {
        return -1;
    }
    return (int)(COM1_UART->RDR & 0xFFU);
}

/* The baud divisor is computed from the APB clock, so a UART left alone across
 * a clock change talks gibberish. Called again after every clock change. */
static void uart_up(void)
{
    GPIO_InitTypeDef g = {0};

    COM1_TX_GPIO_CLK_ENABLE();
    COM1_RX_GPIO_CLK_ENABLE();
    COM1_CLK_ENABLE();

    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = COM1_TX_AF;
    g.Pin       = COM1_TX_PIN;
    HAL_GPIO_Init(COM1_TX_GPIO_PORT, &g);

    g.Alternate = COM1_RX_AF;
    g.Pin       = COM1_RX_PIN;
    HAL_GPIO_Init(COM1_RX_GPIO_PORT, &g);

    if (s_uart_ok) {
        (void)HAL_UART_DeInit(&s_uart);
    }

    s_uart.Instance                    = COM1_UART;
    s_uart.Init.BaudRate               = 115200;
    s_uart.Init.WordLength             = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits               = UART_STOPBITS_1;
    s_uart.Init.Parity                 = UART_PARITY_NONE;
    s_uart.Init.Mode                   = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling           = UART_OVERSAMPLING_16;
    s_uart.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    s_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    s_uart_ok = (HAL_UART_Init(&s_uart) == HAL_OK);
}

/* Records a verdict and prints it as it happens, so an unexpected hang still
 * leaves everything that passed before it on the wire. */
static void record(const char *name, res_t verdict, const char *fmt, ...)
{
    char    detail[72];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);

    if (s_result_count < MAX_RESULTS) {
        s_results[s_result_count].name    = name;
        s_results[s_result_count].verdict = verdict;
        (void)strncpy(s_results[s_result_count].detail, detail, sizeof detail - 1U);
        s_results[s_result_count].detail[sizeof detail - 1U] = '\0';
        s_result_count++;
    }
    uprintf("[%s] %-12s %s\r\n", verdict_text(verdict), name, detail);
}

/* --- 1. identity ---------------------------------------------------------- */

static void test_identity(void)
{
    const uint32_t idcode = DBGMCU->IDCODE;
    const uint32_t devid  = idcode & 0x00000FFFU;
    const uint32_t revid  = (idcode >> 16) & 0xFFFFU;

    const uint16_t flash_kb = *(volatile uint16_t *)0x1FF1E880U;
    const uint32_t uid0     = *(volatile uint32_t *)0x1FF1E800U;
    const uint32_t uid1     = *(volatile uint32_t *)0x1FF1E804U;
    const uint32_t uid2     = *(volatile uint32_t *)0x1FF1E808U;

    const char *rev = (revid == 0x2003U)   ? "V"
                      : (revid == 0x2001U) ? "X"
                      : (revid == 0x1003U) ? "Y"
                      : (revid == 0x1001U) ? "Z"
                                           : "?";

    uprintf("CPUID   : %08lX   IDCODE %08lX\r\n", (unsigned long)SCB->CPUID,
            (unsigned long)idcode);
    uprintf("UID     : %08lX %08lX %08lX\r\n", (unsigned long)uid0, (unsigned long)uid1,
            (unsigned long)uid2);

    /* 0x450 covers H742/H743/H750/H753. The build is for an H753XI; if the
     * silicon disagrees with that, nothing below this line is trustworthy. */
    if (devid == 0x450U) {
        record("identity", R_PASS, "dev 0x450 rev %s, %u KB flash", rev, (unsigned)flash_kb);
    } else {
        record("identity", R_FAIL, "unexpected dev 0x%03lX rev %s", (unsigned long)devid, rev);
    }
}

/* --- 2. power ------------------------------------------------------------- */

static bool s_vos1;

/* The VOS field is not the scale number: 0b11 is Scale 1, 0b10 Scale 2, 0b01
 * Scale 3, and 0b00 is the reset state, which also runs at Scale 3. Printing
 * the raw field reads as the opposite of what it means. */
static const char *vos_name(uint32_t field)
{
    switch (field & 3U) {
    case 3U:
        return "Scale 1";
    case 2U:
        return "Scale 2";
    case 1U:
        return "Scale 3";
    default:
        return "Scale 3 (reset)";
    }
}

static void pwr_dump(const char *when)
{
    uprintf("PWR %-6s CR1=%08lX CSR1=%08lX CR3=%08lX D3CR=%08lX\r\n", when,
            (unsigned long)PWR->CR1, (unsigned long)PWR->CSR1, (unsigned long)PWR->CR3,
            (unsigned long)PWR->D3CR);
    uprintf("          selected %s VOSRDY=%u   active %s ACTVOSRDY=%u\r\n",
            vos_name(PWR->D3CR >> 14), (unsigned)((PWR->D3CR >> 13) & 1U),
            vos_name(PWR->CSR1 >> 14), (unsigned)((PWR->CSR1 >> 13) & 1U));
}

/* Ask for Scale 1 and wait a bounded time. The other eval board never gets
 * there; whether this one does is a real difference between the two, so it is
 * measured rather than assumed either way. */
/* Cycle-accurate stamps, so a regulator transition that takes microseconds is
 * not reported as "instant" by a 1 ms tick. */
static void dwt_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Bit 2 is SCUEN on the H753 and SMPSEN on the H747, so it is named for the
 * part this image is built for rather than for one of them. */
static void cr3_dump(const char *when)
{
    const uint32_t cr3 = PWR->CR3;
#if defined(IGROW_BOARD_H757)
    const char *bit2 = "SMPSEN";
#else
    const char *bit2 = "SCUEN ";
#endif
    uprintf("CR3 %-6s %08lX  LDOEN=%u %s=%u BYPASS=%u SMPSLEVEL=%u\r\n", when,
            (unsigned long)cr3, (unsigned)((cr3 >> 1) & 1U), bit2, (unsigned)((cr3 >> 2) & 1U),
            (unsigned)(cr3 & 1U), (unsigned)((cr3 >> 4) & 3U));
}

/* Why this test looks the way it does.
 *
 * The regulator would not leave Scale 3, on BOTH eval boards. The cause is not
 * the regulator: it is that the supply configuration was never completed.
 *
 * HAL_PWREx_ConfigSupply is the ONLY place in the HAL that waits for
 * ACTVOSRDY, and the clock config never called it. Worse, it cannot help once
 * SCUEN is clear: with the configuration locked and the requested supply equal
 * to the current one, the HAL returns HAL_OK having done nothing at all -- no
 * write, and no wait. A success return that did nothing is why this went
 * unnoticed.
 *
 * SCUEN is the single-write enable for the supply configuration and it reloads
 * ONLY on a power-on reset. A programmer reset, NRST and a software reset all
 * leave it as the last boot left it. So this test reports the reset cause and
 * SCUEN alongside the result: on a system reset the outcome says nothing about
 * the hardware, and only a POR boot is evidence either way.
 *
 * The supply option is per board and asking for the wrong one browns the core
 * out until the next power cycle -- eval_board.h carries which and why.
 *
 * On the dual-core part there is no SCUEN flag and the HAL infers the lock a
 * different way: it treats CR3 != (SMPSEN|LDOEN) as already configured. The
 * reset value has both set, so the first call after a power cycle applies and
 * every later one is refused unless it matches. Bit 2 is reported either way,
 * so a boot that could not have changed anything says so. */
/* The dual-core part reports a software and a low-power reset per core, so the
 * flags are named CM7-side there and unnumbered on the single-core part. */
#if !defined(RCC_RSR_SFTRSTF)
#define RCC_RSR_SFTRSTF  RCC_RSR_SFT1RSTF
#define RCC_RSR_LPWRRSTF RCC_RSR_LPWR1RSTF
#endif

static void test_power(void)
{
    const uint32_t rsr = RCC->RSR;
    const bool     por = (rsr & RCC_RSR_PORRSTF) != 0U;
    uprintf("reset   : RSR %08lX ->%s%s%s%s%s\r\n", (unsigned long)rsr,
            por ? " POR" : "", ((rsr & RCC_RSR_PINRSTF) != 0U) ? " PIN" : "",
            ((rsr & RCC_RSR_SFTRSTF) != 0U) ? " SOFT" : "",
            ((rsr & RCC_RSR_BORRSTF) != 0U) ? " BOR" : "",
            ((rsr & RCC_RSR_LPWRRSTF) != 0U) ? " LPWR" : "");
    __HAL_RCC_CLEAR_RESET_FLAGS();

    cr3_dump("boot");
    pwr_dump("boot");

    const uint32_t          scuen  = (PWR->CR3 >> 2) & 1U;
    const HAL_StatusTypeDef supply = HAL_PWREx_ConfigSupply(IGROW_SUPPLY);
    uprintf("supply  : ConfigSupply(%s) -> %s\r\n", IGROW_SUPPLY_NAME,
            (supply == HAL_OK) ? "HAL_OK" : "HAL_ERROR (locked, and the request did not match)");
    cr3_dump("supply");
    pwr_dump("supply");

    /* Now request Scale 1 and log every change of D3CR/CSR1 with the cycle it
     * happened on, rather than sampling once and guessing what the regulator
     * did in between. */
    dwt_start();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    uint32_t       last_d3cr = 0xFFFFFFFFU;
    uint32_t       last_csr1 = 0xFFFFFFFFU;
    unsigned       logged    = 0;
    const uint32_t t0        = HAL_GetTick();

    s_vos1 = false;
    while ((HAL_GetTick() - t0) <= 100U) {
        const uint32_t d3cr = PWR->D3CR;
        const uint32_t csr1 = PWR->CSR1;
        if (((d3cr != last_d3cr) || (csr1 != last_csr1)) && (logged < 12U)) {
            uprintf("  +%8lu cyc  D3CR %08lX  CSR1 %08lX  active %s ACTVOSRDY=%u\r\n",
                    (unsigned long)DWT->CYCCNT, (unsigned long)d3cr, (unsigned long)csr1,
                    vos_name(csr1 >> 14), (unsigned)((csr1 >> 13) & 1U));
            last_d3cr = d3cr;
            last_csr1 = csr1;
            logged++;
        }
        if ((((csr1 >> 14) & 3U) == 3U) && (((csr1 >> 13) & 1U) == 1U)) {
            s_vos1 = true;
            break;
        }
    }

    if (s_vos1) {
        record("power", R_PASS, "Scale 1 active in %lu cyc (%s boot)",
               (unsigned long)DWT->CYCCNT, por ? "POR" : "reset");
    } else if (!por) {
        record("power", R_INFO, "Scale 1 refused, but SCUEN=%u on a non-POR boot",
               (unsigned)scuen);
    } else {
        record("power", R_FAIL, "Scale 1 refused on a POR boot, SCUEN was %u",
               (unsigned)scuen);
    }
}

/* --- 3. clocks ------------------------------------------------------------ */

static bool s_full_clock;

/* HSE 25 MHz, PLL1 M=5 (5 MHz reference), P=2.
 *   full     N=160 -> VCO 800 -> SYSCLK 400, HCLK 200, APB 100.  Needs VOS1.
 *   reduced  N=80  -> VCO 400 -> SYSCLK 200, HCLK 100, APB  50.  Legal at VOS3. */
static bool clock_config(bool full)
{
    RCC_ClkInitTypeDef c = {0};
    RCC_OscInitTypeDef o = {0};

    o.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    o.HSEState       = RCC_HSE_ON;
    o.PLL.PLLState   = RCC_PLL_ON;
    o.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    o.PLL.PLLM       = 5;
    o.PLL.PLLN       = full ? 160 : 80;
    o.PLL.PLLFRACN   = 0;
    o.PLL.PLLP       = 2;
    o.PLL.PLLR       = 2;
    o.PLL.PLLQ       = 4;
    o.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    o.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) {
        return false;
    }

    c.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 |
                   RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1);
    c.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    c.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    c.AHBCLKDivider  = RCC_HCLK_DIV2;
    c.APB3CLKDivider = RCC_APB3_DIV2;
    c.APB1CLKDivider = RCC_APB1_DIV2;
    c.APB2CLKDivider = RCC_APB2_DIV2;
    c.APB4CLKDivider = RCC_APB4_DIV2;

    /* More wait states than the frequency needs is always safe; fewer is not,
     * and this runs at two different frequencies. */
    return HAL_RCC_ClockConfig(&c, FLASH_LATENCY_4) == HAL_OK;
}

static void test_clocks(void)
{
    s_full_clock = s_vos1;

    if (!clock_config(s_full_clock)) {
        /* The one step with no fallback worth taking: without a clock there is
         * nothing to report a clock failure with. Stay on HSI and say so. */
        s_full_clock = false;
        uart_up();
        record("clocks", R_FAIL, "PLL1 would not lock, running on HSI");
        return;
    }
    uart_up();

    const unsigned long sysclk = HAL_RCC_GetSysClockFreq();
    const unsigned long hclk   = HAL_RCC_GetHCLKFreq();

    uprintf("clocks  : SYSCLK %lu  HCLK %lu  PCLK1 %lu  PCLK2 %lu  PCLK4 %lu\r\n", sysclk, hclk,
            (unsigned long)HAL_RCC_GetPCLK1Freq(), (unsigned long)HAL_RCC_GetPCLK2Freq(),
            (unsigned long)HAL_RCCEx_GetD3PCLK1Freq());

    /* The PLL locking off HSE is also the proof that the 25 MHz crystal runs;
     * a dead crystal fails HAL_RCC_OscConfig above, not here. */
    record("clocks", R_PASS, "HSE+PLL1 ok, SYSCLK %lu MHz (%s)", sysclk / 1000000UL,
           s_full_clock ? "full" : "Scale 3 limited");
}

/* --- 4. LSE --------------------------------------------------------------- */

static bool s_lse_ok;

static void test_lse(void)
{
    RCC_OscInitTypeDef o = {0};

    HAL_PWR_EnableBkUpAccess();

    o.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    o.LSEState       = RCC_LSE_ON;
    o.PLL.PLLState   = RCC_PLL_NONE;

    /* HAL bounds this itself at 5 s. A board with no 32.768 kHz crystal takes
     * that long to say so once, and then the RTC test is skipped. */
    s_lse_ok = (HAL_RCC_OscConfig(&o) == HAL_OK);

    if (s_lse_ok) {
        record("LSE", R_PASS, "32.768 kHz crystal running");
    } else {
        record("LSE", R_FAIL, "no LSE after 5 s");
    }
}

/* --- 5. internal RAM ------------------------------------------------------ */

/* Writes an address-dependent pattern and reads it back. Address-dependent so
 * that aliased address lines show up as mismatches rather than as a region
 * that appears to work because every cell holds the same constant. */
static uint32_t ram_pattern(volatile uint32_t *base, uint32_t bytes)
{
    const uint32_t words = bytes / 4U;
    uint32_t       bad   = 0;

    for (uint32_t i = 0; i < words; i++) {
        base[i] = (uint32_t)(uintptr_t)&base[i] ^ 0xA5A5A5A5U;
    }
    for (uint32_t i = 0; i < words; i++) {
        if (base[i] != ((uint32_t)(uintptr_t)&base[i] ^ 0xA5A5A5A5U)) {
            bad++;
        }
    }
    return bad;
}

static void test_internal_ram(void)
{
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    /* D3 SRAM4 has no run-mode clock gate on this part -- AHB4ENR carries no
     * D3SRAM1EN bit, only the sleep-mode one in AHB4LPENR -- so it is already
     * clocked and there is nothing to enable. */
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_Delay(1);

    uint32_t bad = 0;

    /* DTCM is not tested: this program's stack and globals live in it, so it
     * is already proven by the fact that there is anything to print. The AXI
     * region is tested from +256 KB up, clear of anything the linker placed. */
    bad += ram_pattern((volatile uint32_t *)0x24040000U, 256U * 1024U); /* AXI D1  */
    bad += ram_pattern((volatile uint32_t *)0x30000000U, 128U * 1024U); /* D2 SRAM1 */
    bad += ram_pattern((volatile uint32_t *)0x30020000U, 128U * 1024U); /* D2 SRAM2 */
    bad += ram_pattern((volatile uint32_t *)0x30040000U, 32U * 1024U);  /* D2 SRAM3 */
    bad += ram_pattern((volatile uint32_t *)0x38000000U, 64U * 1024U);  /* SRD SRAM4 */
    bad += ram_pattern((volatile uint32_t *)0x38800000U, 4U * 1024U);   /* backup   */

    if (bad == 0U) {
        record("int RAM", R_PASS, "AXI+D2x3+SRD+backup, 612 KB, 0 errors");
    } else {
        record("int RAM", R_FAIL, "%lu words wrong", (unsigned long)bad);
    }
}

/* --- 6. SDRAM ------------------------------------------------------------- */

/* The BSP refresh constant is for a 100 MHz SDRAM clock (HCLK 200 / 2). At
 * half the HCLK the counter has to halve too, or rows are refreshed half as
 * often -- which corrupts slowly rather than failing to initialise. */
#define REFRESH_COUNT_100MHZ 0x0603U

#define SDRAM_BASE_ADDR 0xD0000000U
#define SDRAM_BYTES     0x02000000U /* 32 MB */

static bool s_sdram_ok;

/* Walking ones on the data bus at one address: separates a stuck or shorted
 * data line from a memory-array fault. */
static uint32_t sdram_data_bus(void)
{
    volatile uint32_t *p   = (volatile uint32_t *)SDRAM_BASE_ADDR;
    uint32_t           bad = 0;

    for (unsigned bit = 0; bit < 32U; bit++) {
        const uint32_t v = 1UL << bit;
        *p               = v;
        if (*p != v) {
            bad++;
        }
    }
    return bad;
}

/* Walking ones on the address bus: writes a unique value at each power-of-two
 * offset, then checks none of them aliased onto another. */
static uint32_t sdram_addr_bus(void)
{
    volatile uint32_t *base = (volatile uint32_t *)SDRAM_BASE_ADDR;
    uint32_t           bad  = 0;

    base[0] = 0xDEADBEEFU;
    for (uint32_t off = 4U; off < SDRAM_BYTES; off <<= 1) {
        base[off / 4U] = off;
    }
    if (base[0] != 0xDEADBEEFU) {
        bad++; /* something aliased onto offset 0 */
    }
    for (uint32_t off = 4U; off < SDRAM_BYTES; off <<= 1) {
        if (base[off / 4U] != off) {
            bad++;
        }
    }
    return bad;
}

static void test_sdram(void)
{
    if (BSP_SDRAM_Init(0) != BSP_ERROR_NONE) {
        record("SDRAM", R_FAIL, "BSP_SDRAM_Init failed");
        return;
    }
    if (!s_full_clock) {
        (void)HAL_SDRAM_ProgramRefreshRate(&hsdram[0], REFRESH_COUNT_100MHZ / 2U);
        uprintf("SDRAM   : refresh rescaled to %u for the half-speed FMC clock\r\n",
                (unsigned)(REFRESH_COUNT_100MHZ / 2U));
    }

    const uint32_t db = sdram_data_bus();
    const uint32_t ab = sdram_addr_bus();
    if ((db != 0U) || (ab != 0U)) {
        record("SDRAM", R_FAIL, "bus test: %lu data, %lu address", (unsigned long)db,
               (unsigned long)ab);
        return;
    }

    /* Full 32 MB, address-dependent pattern. This is the test that catches a
     * refresh that is too slow -- a region that reads back correctly the
     * moment it is written but not once the sweep has moved on. */
    volatile uint32_t *p     = (volatile uint32_t *)SDRAM_BASE_ADDR;
    const uint32_t     words = SDRAM_BYTES / 4U;
    const uint32_t     t0    = HAL_GetTick();

    for (uint32_t i = 0; i < words; i++) {
        p[i] = i ^ 0xA5A5A5A5U;
    }
    uint32_t bad = 0;
    for (uint32_t i = 0; i < words; i++) {
        if (p[i] != (i ^ 0xA5A5A5A5U)) {
            bad++;
        }
    }
    const uint32_t ms = HAL_GetTick() - t0;

    s_sdram_ok = (bad == 0U);
    if (s_sdram_ok) {
        record("SDRAM", R_PASS, "32 MB swept in %lu ms, 0 errors", (unsigned long)ms);
    } else {
        record("SDRAM", R_FAIL, "%lu of %lu words wrong", (unsigned long)bad,
               (unsigned long)words);
    }
}

/* --- 7. QSPI NOR ---------------------------------------------------------- */

static void test_qspi(void)
{
    BSP_QSPI_Init_t init;
    init.InterfaceMode = MT25TL01G_QPI_MODE;
    init.TransferRate  = MT25TL01G_STR_TRANSFER;
    init.DualFlashMode = MT25TL01G_DUALFLASH_ENABLE;

    const int32_t rc = BSP_QSPI_Init(0, &init);
    if (rc != BSP_ERROR_NONE) {
        record("QSPI NOR", R_FAIL, "BSP_QSPI_Init rc %ld", (long)rc);
        return;
    }

    uint8_t id[8] = {0};
    (void)BSP_QSPI_ReadID(0, id);
    uprintf("QSPI    : ID %02X %02X %02X %02X %02X %02X\r\n", id[0], id[1], id[2], id[3], id[4],
            id[5]);

    BSP_QSPI_Info_t info;
    memset(&info, 0, sizeof info);
    (void)BSP_QSPI_GetInfo(0, &info);

    /* Read the same 256 bytes twice, once through the indirect command path
     * and once through the memory-mapped window. Agreement exercises both
     * paths; a difference would mean the mapping, not the flash. Read only --
     * nothing here erases or programs. */
    static uint8_t a[256];
    static uint8_t b[256];

    if (BSP_QSPI_Read(0, a, 0, sizeof a) != BSP_ERROR_NONE) {
        record("QSPI NOR", R_FAIL, "indirect read failed");
        return;
    }
    if (BSP_QSPI_EnableMemoryMappedMode(0) != BSP_ERROR_NONE) {
        record("QSPI NOR", R_FAIL, "memory-mapped mode refused");
        return;
    }
    memcpy(b, (const void *)0x90000000U, sizeof b);
    (void)BSP_QSPI_DisableMemoryMappedMode(0);

    if (memcmp(a, b, sizeof a) != 0) {
        record("QSPI NOR", R_FAIL, "indirect and mapped reads disagree");
        return;
    }

    /* Dual-flash interleaves the two dies byte by byte, so a Micron
     * manufacturer/type pair 0x20 0xBA arrives as 20 20 BA BA -- reading the
     * first three bytes as one die's JEDEC ID is what makes a healthy part
     * look unknown. */
    const bool micron = (id[0] == 0x20U) && (id[1] == 0x20U) && (id[2] == 0xBAU) &&
                        (id[3] == 0xBAU);
    record("QSPI NOR", R_PASS, "%s %lu MB, ID %02X%02X%02X%02X, both paths agree",
           micron ? "Micron dual" : "unknown", (unsigned long)(info.FlashSize >> 20), id[0], id[1],
           id[2], id[3]);
}

/* --- 8. I2C1 scan --------------------------------------------------------- */

/* Addresses are 8-bit here, the way the BSP and the schematics write them.
 * 0x84 is the MFX IO expander, 0x34 the WM8994 audio codec, 0xA0 the EEPROM,
 * 0x08 the EXC7200 touch controller on the MB1063 display daughterboard --
 * so this scan is also how the board says which daughterboards are fitted. */
static void test_i2c_scan(void)
{
    if (BSP_I2C1_Init() != BSP_ERROR_NONE) {
        record("I2C1", R_FAIL, "BSP_I2C1_Init failed");
        return;
    }

    unsigned found = 0;
    char     list[64];
    int      used = 0;

    list[0] = '\0';
    uputs("I2C1    : ");
    for (uint16_t a = 2; a < 0xFFU; a += 2U) {
        if (HAL_I2C_IsDeviceReady(&hbus_i2c1, a, 2, 5) == HAL_OK) {
            uprintf("0x%02X ", (unsigned)a);
            found++;
            if (used < (int)(sizeof list) - 6) {
                used += snprintf(&list[used], sizeof list - (size_t)used, "%02X ", (unsigned)a);
            }
        }
    }
    uputs("\r\n");

    if (found == 0U) {
        record("I2C1", R_FAIL, "no device answered - bus dead or wedged");
    } else {
        record("I2C1", R_PASS, "%u devices: %s", found, list);
    }
}

/* --- 9. MFX IO expander and LEDs ------------------------------------------ */

/* LED1 (PF10) and LED3 (PA4) are wired to GPIO; LED2 and LED4 hang off the MFX
 * IO expander over I2C. So all four lighting is a test of the expander too --
 * and on a board with no display it is the only output the user can see. */
static void test_leds(void)
{
    const Led_TypeDef leds[4] = {LED1, LED2, LED3, LED4};
    const char       *names[4] = {"LED1 green", "LED2 orange", "LED3 red", "LED4 blue"};
    unsigned          ok       = 0;

    for (unsigned i = 0; i < 4U; i++) {
        if (BSP_LED_Init(leds[i]) == BSP_ERROR_NONE) {
            ok++;
        } else {
            uprintf("LED     : %s would not initialise\r\n", names[i]);
        }
        (void)BSP_LED_Off(leds[i]);
    }

    /* Three passes so a watching pair of eyes cannot miss it. */
    for (unsigned pass = 0; pass < 3U; pass++) {
        for (unsigned i = 0; i < 4U; i++) {
            (void)BSP_LED_On(leds[i]);
            HAL_Delay(120);
            (void)BSP_LED_Off(leds[i]);
        }
    }

    if (ok == 4U) {
        record("MFX+LEDs", R_PASS, "4 LEDs blinked (2 of them via the MFX)");
    } else {
        record("MFX+LEDs", R_FAIL, "only %u of 4 LEDs initialised", ok);
    }
}

/* --- 10. FDCAN1 internal loopback ----------------------------------------- */

/* FDCAN kernel clock = HSE = 25 MHz. 25 / prescaler 2 = 12.5 MHz tq clock,
 * 25 tq per bit => 500 kbit/s exactly, sample point (1 + 21) / 25 = 88 %.
 * The same numbers the panel firmware uses, so a pass here means the timing
 * the cabinet bus needs is reachable on this silicon. */
#define CAN_PRESCALER 2U
#define CAN_TSEG1     21U
#define CAN_TSEG2     3U
#define CAN_SJW       3U

static FDCAN_HandleTypeDef s_fdcan;

/* Internal loopback needs no transceiver, no jumpers and no wiring: the
 * controller feeds its own transmitter back into its own receiver. That is
 * exactly the right test for a board whose CAN connector is not hooked up. */
static void test_fdcan(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection     = RCC_PERIPHCLK_FDCAN;
    pclk.FdcanClockSelection      = RCC_FDCANCLKSOURCE_HSE;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        record("FDCAN1", R_FAIL, "kernel clock would not select HSE");
        return;
    }
    __HAL_RCC_FDCAN_CLK_ENABLE();

    s_fdcan.Instance                  = FDCAN1;
    s_fdcan.Init.FrameFormat          = FDCAN_FRAME_CLASSIC;
    s_fdcan.Init.Mode                 = FDCAN_MODE_INTERNAL_LOOPBACK;
    s_fdcan.Init.AutoRetransmission   = ENABLE;
    s_fdcan.Init.TransmitPause        = DISABLE;
    s_fdcan.Init.ProtocolException    = DISABLE;
    s_fdcan.Init.NominalPrescaler     = CAN_PRESCALER;
    s_fdcan.Init.NominalSyncJumpWidth = CAN_SJW;
    s_fdcan.Init.NominalTimeSeg1      = CAN_TSEG1;
    s_fdcan.Init.NominalTimeSeg2      = CAN_TSEG2;
    s_fdcan.Init.DataPrescaler        = CAN_PRESCALER;
    s_fdcan.Init.DataSyncJumpWidth    = CAN_SJW;
    s_fdcan.Init.DataTimeSeg1         = CAN_TSEG1;
    s_fdcan.Init.DataTimeSeg2         = CAN_TSEG2;
    s_fdcan.Init.MessageRAMOffset     = 0;
    s_fdcan.Init.StdFiltersNbr        = 0;
    s_fdcan.Init.ExtFiltersNbr        = 0;
    s_fdcan.Init.RxFifo0ElmtsNbr      = 64;
    s_fdcan.Init.RxFifo0ElmtSize      = FDCAN_DATA_BYTES_8;
    s_fdcan.Init.RxFifo1ElmtsNbr      = 0;
    s_fdcan.Init.RxFifo1ElmtSize      = FDCAN_DATA_BYTES_8;
    s_fdcan.Init.RxBuffersNbr         = 0;
    s_fdcan.Init.RxBufferSize         = FDCAN_DATA_BYTES_8;
    s_fdcan.Init.TxEventsNbr          = 0;
    s_fdcan.Init.TxBuffersNbr         = 0;
    s_fdcan.Init.TxFifoQueueElmtsNbr  = 16;
    s_fdcan.Init.TxFifoQueueMode      = FDCAN_TX_FIFO_OPERATION;
    s_fdcan.Init.TxElmtSize           = FDCAN_DATA_BYTES_8;

    if (HAL_FDCAN_Init(&s_fdcan) != HAL_OK) {
        record("FDCAN1", R_FAIL, "HAL_FDCAN_Init failed");
        return;
    }
    if (HAL_FDCAN_ConfigGlobalFilter(&s_fdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK) {
        record("FDCAN1", R_FAIL, "global filter config failed");
        return;
    }
    if (HAL_FDCAN_Start(&s_fdcan) != HAL_OK) {
        record("FDCAN1", R_FAIL, "HAL_FDCAN_Start failed");
        return;
    }

    unsigned sent = 0, got = 0, wrong = 0;

    for (unsigned n = 0; n < 32U; n++) {
        FDCAN_TxHeaderTypeDef h = {0};
        uint8_t               tx[8];

        for (unsigned i = 0; i < 8U; i++) {
            tx[i] = (uint8_t)(n * 8U + i);
        }
        h.Identifier          = 0x1234500U + n;
        h.IdType              = FDCAN_EXTENDED_ID;
        h.TxFrameType         = FDCAN_DATA_FRAME;
        h.DataLength          = FDCAN_DLC_BYTES_8;
        h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        h.BitRateSwitch       = FDCAN_BRS_OFF;
        h.FDFormat            = FDCAN_CLASSIC_CAN;
        h.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
        h.MessageMarker       = 0;

        if (HAL_FDCAN_AddMessageToTxFifoQ(&s_fdcan, &h, tx) != HAL_OK) {
            break;
        }
        sent++;

        /* Bounded: a controller that never loops back must not spin here. */
        const uint32_t t0 = HAL_GetTick();
        while ((HAL_FDCAN_GetRxFifoFillLevel(&s_fdcan, FDCAN_RX_FIFO0) == 0U) &&
               ((HAL_GetTick() - t0) < 10U)) {
        }

        FDCAN_RxHeaderTypeDef rh;
        uint8_t               rx[8];
        if (HAL_FDCAN_GetRxMessage(&s_fdcan, FDCAN_RX_FIFO0, &rh, rx) != HAL_OK) {
            continue;
        }
        got++;
        if ((rh.Identifier != h.Identifier) || (memcmp(rx, tx, 8) != 0)) {
            wrong++;
        }
    }

    FDCAN_ProtocolStatusTypeDef ps;
    FDCAN_ErrorCountersTypeDef  ec;
    (void)HAL_FDCAN_GetProtocolStatus(&s_fdcan, &ps);
    (void)HAL_FDCAN_GetErrorCounters(&s_fdcan, &ec);
    uprintf("FDCAN1  : LEC %lu  TEC %lu  REC %lu\r\n", (unsigned long)ps.LastErrorCode,
            (unsigned long)ec.TxErrorCnt, (unsigned long)ec.RxErrorCnt);

    (void)HAL_FDCAN_Stop(&s_fdcan);
    (void)HAL_FDCAN_DeInit(&s_fdcan);

    if ((sent == 32U) && (got == 32U) && (wrong == 0U)) {
        record("FDCAN1", R_PASS, "32/32 looped back at 500 kbit/s, 0 corrupt");
    } else {
        record("FDCAN1", R_FAIL, "sent %u, got %u, %u corrupt", sent, got, wrong);
    }
}

/* --- 11. ADC / potentiometer ---------------------------------------------- */

static void test_pot(void)
{
    if (BSP_POT_Init(POT1) != BSP_ERROR_NONE) {
        record("ADC/POT1", R_FAIL, "BSP_POT_Init failed");
        return;
    }

    int32_t lo = 1000, hi = -1, last = 0;
    for (unsigned i = 0; i < 16U; i++) {
        last = BSP_POT_GetLevel(POT1);
        if (last < lo) {
            lo = last;
        }
        if (last > hi) {
            hi = last;
        }
        HAL_Delay(5);
    }

    /* A converter that is not running reads a hard 0 or a hard full scale on
     * every sample; a live one wanders by a count or two. Either way the
     * reading is reported, because only the user knows where the knob is. */
    if ((lo < 0) || (hi < 0)) {
        record("ADC/POT1", R_FAIL, "no conversion");
    } else {
        record("ADC/POT1", R_PASS, "reads %ld%% (span %ld-%ld over 16 samples)", (long)last,
               (long)lo, (long)hi);
    }
}

/* --- 12. RTC -------------------------------------------------------------- */

static RTC_HandleTypeDef s_rtc;

static void test_rtc(void)
{
    if (!s_lse_ok) {
        record("RTC", R_SKIP, "no LSE to clock it");
        return;
    }

    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection     = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection        = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        record("RTC", R_FAIL, "clock source would not select LSE");
        return;
    }
    __HAL_RCC_RTC_ENABLE();

    s_rtc.Instance            = RTC;
    s_rtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    s_rtc.Init.AsynchPrediv   = 127;
    s_rtc.Init.SynchPrediv    = 255;
    s_rtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    s_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_rtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&s_rtc) != HAL_OK) {
        record("RTC", R_FAIL, "HAL_RTC_Init failed");
        return;
    }

    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    t.Hours           = 12;
    t.Minutes         = 0;
    t.Seconds         = 0;
    d.Date            = 1;
    d.Month           = RTC_MONTH_JANUARY;
    d.Year            = 26;
    d.WeekDay         = RTC_WEEKDAY_THURSDAY;
    (void)HAL_RTC_SetTime(&s_rtc, &t, RTC_FORMAT_BIN);
    (void)HAL_RTC_SetDate(&s_rtc, &d, RTC_FORMAT_BIN);

    /* Reading the shadow registers requires reading TIME then DATE, and the
     * counter has to be given time to actually move. */
    HAL_Delay(2200);
    (void)HAL_RTC_GetTime(&s_rtc, &t, RTC_FORMAT_BIN);
    (void)HAL_RTC_GetDate(&s_rtc, &d, RTC_FORMAT_BIN);

    if ((t.Seconds >= 2U) && (t.Seconds <= 3U)) {
        record("RTC", R_PASS, "advanced to %02u:%02u:%02u over 2.2 s", t.Hours, t.Minutes,
               t.Seconds);
    } else {
        record("RTC", R_FAIL, "read back %02u:%02u:%02u, expected ~2 s", t.Hours, t.Minutes,
               t.Seconds);
    }
}

/* --- 13. microSD ---------------------------------------------------------- */

static void test_sd(void)
{
    const int32_t rc = BSP_SD_Init(0);
    if (rc == BSP_ERROR_NONE) {
        BSP_SD_CardInfo ci;
        memset(&ci, 0, sizeof ci);
        (void)BSP_SD_GetCardInfo(0, &ci);
        record("microSD", R_PASS, "card present, %lu MB",
               (unsigned long)((ci.BlockNbr / 2048U)));
    } else if (BSP_SD_IsDetected(0) != SD_PRESENT) {
        /* No card is the expected state on a bench board and says nothing bad
         * about the socket, so it is not a failure. */
        record("microSD", R_SKIP, "no card in the socket");
    } else {
        record("microSD", R_FAIL, "card detected but init rc %ld", (long)rc);
    }
}

/* --- MPU and cache -------------------------------------------------------- */

/* The SDRAM region is mapped write-through so that the full-sweep test is
 * measuring the memory rather than the D-cache in front of it. */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();
    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER0;
    r.BaseAddress      = SDRAM_BASE_ADDR;
    r.Size             = MPU_REGION_SIZE_32MB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable      = MPU_ACCESS_CACHEABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.TypeExtField     = MPU_TEX_LEVEL0;
    r.SubRegionDisable = 0x00;
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);

    /* The QSPI memory-mapped window. Left uncached so that the comparison
     * against the indirect read is a real second fetch from the part. */
    r.Number       = MPU_REGION_NUMBER1;
    r.BaseAddress  = 0x90000000U;
    r.Size         = MPU_REGION_SIZE_128MB;
    r.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void CPU_CACHE_Enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

/* --- summary and console loop --------------------------------------------- */

static void summary(void)
{
    unsigned pass = 0, fail = 0;

    uputs("\r\n=== summary ===============================================\r\n");
    for (unsigned i = 0; i < s_result_count; i++) {
        uprintf("  %-4s  %-10s  %s\r\n", verdict_text(s_results[i].verdict), s_results[i].name,
                s_results[i].detail);
        if (s_results[i].verdict == R_PASS) {
            pass++;
        } else if (s_results[i].verdict == R_FAIL) {
            fail++;
        }
    }
    uprintf("===========================================================\r\n");
    uprintf("  %u passed, %u failed, %u recorded\r\n\r\n", pass, fail, s_result_count);
}

/* Defined with the console loop below, next to the button polling it shares a
 * section with. */
static void test_eth_phy(void);

static void run_all(void)
{
    s_result_count = 0;

    test_identity();
    test_power();
    test_clocks();
    test_lse();
    test_internal_ram();
    test_sdram();
    test_qspi();
    test_i2c_scan();
    test_leds();
    test_fdcan();
    test_pot();
    test_rtc();
    test_sd();
    test_eth_phy();

    summary();
}

static void help(void)
{
    uputs("commands: a run everything again   m SDRAM   q QSPI   i I2C scan\r\n");
    uputs("          c FDCAN loopback   l LED sweep   t pot+buttons   e ETH PHY\r\n");
    uputs("          s summary   p PWR dump   R reset   h help\r\n");
}

/* The two buttons are the one test that needs a person, so they are polled
 * continuously rather than run once and reported.
 *
 * BSP_PB_GetState returns the raw pin level and the BSP normalises no
 * polarity, so the level alone does not say "pressed" -- an idle pin sitting
 * high would be reported as a press. What proves the button is the CHANGE, so
 * that is what is printed. */
static void buttons_poll(bool announce)
{
    static int32_t last_wk = -1, last_tp = -1;

    const int32_t wk = BSP_PB_GetState(BUTTON_WAKEUP);
    const int32_t tp = BSP_PB_GetState(BUTTON_TAMPER);

    if (announce) {
        uprintf("buttons : WAKEUP level %ld, TAMPER level %ld (idle reference)\r\n", (long)wk,
                (long)tp);
    } else {
        if (wk != last_wk) {
            uprintf("button  : WAKEUP changed to %ld  <-- press seen\r\n", (long)wk);
        }
        if (tp != last_tp) {
            uprintf("button  : TAMPER changed to %ld  <-- press seen\r\n", (long)tp);
        }
    }
    last_wk = wk;
    last_tp = tp;
}

/* --- 14. Ethernet PHY ------------------------------------------------------ */

/* Only the management interface is touched: MDC and MDIO, and a read of the
 * PHY's two identifier registers. That is enough to say the PHY is fitted,
 * powered and answering, without bringing up RMII, descriptors or buffers --
 * none of which a service tool needs to know about to judge the hardware. */
static bool mdio_read(uint32_t phy, uint32_t reg, uint16_t *out)
{
    const uint32_t hclk = HAL_RCC_GetHCLKFreq();
    /* MDC divider: the field selects an HCLK range, and MDC must land under
     * 2.5 MHz whichever clock the board settled on. */
    const uint32_t cr = (hclk > 150000000UL) ? 4UL : (hclk > 100000000UL) ? 1UL : 0UL;

    uint32_t v = (phy << ETH_MACMDIOAR_PA_Pos) | (reg << ETH_MACMDIOAR_RDA_Pos) |
                 (cr << ETH_MACMDIOAR_CR_Pos) | ETH_MACMDIOAR_MOC_RD | ETH_MACMDIOAR_MB;
    ETH->MACMDIOAR = v;

    const uint32_t t0 = HAL_GetTick();
    while ((ETH->MACMDIOAR & ETH_MACMDIOAR_MB) != 0U) {
        if ((HAL_GetTick() - t0) > 10U) {
            return false;
        }
    }
    *out = (uint16_t)(ETH->MACMDIODR & 0xFFFFU);
    return true;
}

static void test_eth_phy(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();

    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF11_ETH;
    g.Pin       = GPIO_PIN_2; /* ETH_MDIO */
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_1; /* ETH_MDC */
    HAL_GPIO_Init(GPIOC, &g);

    /* The PHY address is a strap, so it is found rather than assumed. */
    for (uint32_t phy = 0; phy < 32U; phy++) {
        uint16_t id1 = 0, id2 = 0;
        if (!mdio_read(phy, 2, &id1) || !mdio_read(phy, 3, &id2)) {
            continue;
        }
        if ((id1 == 0xFFFFU) || ((id1 == 0U) && (id2 == 0U))) {
            continue; /* nothing driving the bus at this address */
        }
        uint16_t bmsr = 0;
        (void)mdio_read(phy, 1, &bmsr);
        record("ETH PHY", R_PASS, "addr %lu, ID %04X%04X, link %s", (unsigned long)phy, id1, id2,
               ((bmsr & 0x0004U) != 0U) ? "up" : "down");
        return;
    }

    /* Not a failure of the MAC: with no cable and no RMII reference clock a
     * fitted PHY can legitimately stay silent on MDIO. */
    record("ETH PHY", R_INFO, "no PHY answered on MDIO addresses 0-31");
}

static void handle(int c)
{
    switch (c) {
    case 'a':
        run_all();
        break;
    case 'm':
        test_sdram();
        break;
    case 'q':
        test_qspi();
        break;
    case 'i':
        test_i2c_scan();
        break;
    case 'c':
        test_fdcan();
        break;
    case 'l':
        test_leds();
        break;
    case 't':
        test_pot();
        buttons_poll(true);
        break;
    case 'e':
        test_eth_phy();
        break;
    case 's':
        summary();
        break;
    case 'p':
        pwr_dump("now");
        break;
    case 'R':
        uputs("resetting\r\n");
        HAL_Delay(20);
        NVIC_SystemReset();
        break;
    case 'h':
        help();
        break;
    default:
        break;
    }
}

int main(void)
{
    MPU_Config();
    CPU_CACHE_Enable();
    HAL_Init();

    /* Console before anything that can fail, on the reset clock (HSI 64 MHz).
     * Everything after this point is reported rather than assumed. */
    uart_up();
    uputs("\r\n\r\n=== igrow board check: " IGROW_BOARD_NAME " (MB1246), headless ===\r\n");

    run_all();

    (void)BSP_PB_Init(BUTTON_WAKEUP, BUTTON_MODE_GPIO);
    (void)BSP_PB_Init(BUTTON_TAMPER, BUTTON_MODE_GPIO);
    /* Seed the idle levels before the loop starts, or the first poll compares
     * against the sentinel and reports a press nobody made. */
    buttons_poll(true);
    uputs("Press WAKEUP or TAMPER on the board - changes are reported below.\r\n");
    help();

    uint32_t last = HAL_GetTick();
    for (;;) {
        const int c = ugetc();
        if (c >= 0) {
            handle(c);
        }
        buttons_poll(false);

        const uint32_t now = HAL_GetTick();
        if ((now - last) >= 5000U) {
            last = now;
            uprintf("alive %lus\r\n", (unsigned long)(now / 1000U));
        }
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
