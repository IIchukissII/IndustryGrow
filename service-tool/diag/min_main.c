/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Minimal bare-metal bring-up test for the STM32H757I-EVAL + MB1063.
 *
 * No FreeRTOS, no LVGL, no CAN, no Cyphal. Console first, then SDRAM, then a
 * rectangle on the glass. Built as a second target, igrow-min.hex, so the real
 * firmware is not disturbed.
 *
 * Written because the panel firmware hangs before it prints anything, in
 *
 *     while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) { }
 *
 * with PWR_CSR1.ACTVOSRDY = 0. Every wait here has a timeout and reports, so a
 * board that cannot reach voltage Scale 1 still boots, still talks, and still
 * draws -- at the clocks Scale 3 permits. An unreachable flag becomes a line of
 * text instead of a silent hang.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "eval_board.h"
#include "stm32h7xx_hal.h"

#define PANEL_W IGROW_PANEL_W
#define PANEL_H IGROW_PANEL_H

/* RGB565. Navy ground and cyan bar, the same two the panel UI uses. */
#define RGB565_NAVY 0x08A5U
#define RGB565_CYAN 0x269DU
#define RGB565_GREEN 0x4EEDU

/* SDRAM refresh is counted in SDRAM clocks, so it has to follow the clock.
 * The BSP value is for a 100 MHz SDRAM clock (HCLK 200 MHz / 2); at half the
 * HCLK the counter has to halve too or rows are refreshed half as often. */
#define REFRESH_COUNT_100MHZ 0x0603U

static UART_HandleTypeDef s_uart;
static bool               s_uart_ok;
static bool               s_vos_ready;
static bool               s_full_clock;

/* --- console -------------------------------------------------------------- */

/* Transmit by polling the peripheral rather than through HAL_UART_Transmit:
 * this has to work before, during and after clock changes, and must never
 * depend on the tick that a clock change re-bases. */
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

/* Called again after every clock change: the baud rate divisor is computed
 * from the APB clock, so a UART left alone across a clock change talks
 * gibberish. */
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

/* --- power ---------------------------------------------------------------- */

static void pwr_dump(const char *when)
{
    uprintf("PWR %-8s CR1=%08lX CSR1=%08lX CR3=%08lX D3CR=%08lX CPUCR=%08lX\r\n", when,
            (unsigned long)PWR->CR1, (unsigned long)PWR->CSR1, (unsigned long)PWR->CR3,
            (unsigned long)PWR->D3CR, (unsigned long)PWR->CPUCR);
    uprintf("    VOS=%u VOSRDY=%u   ACTVOS=%u ACTVOSRDY=%u   RCC_CR=%08lX\r\n",
            (unsigned)((PWR->D3CR >> 14) & 3U), (unsigned)((PWR->D3CR >> 13) & 1U),
            (unsigned)((PWR->CSR1 >> 14) & 3U), (unsigned)((PWR->CSR1 >> 13) & 1U),
            (unsigned long)RCC->CR);
}

/* Ask for Scale 1 and wait a bounded time. Returns whether the regulator said
 * it got there; the caller decides what to do about a no.
 *
 * The supply configuration has to be completed first: it is the only thing
 * that sets ACTVOSRDY, and until it is set every scale request is ignored.
 * eval_board.h carries which option this board wants -- the wrong one browns
 * the core out until the next power cycle.
 *
 * And the flag polled here is ACTVOS/ACTVOSRDY in CSR1, the scale actually in
 * force, NOT VOSRDY in D3CR. VOSRDY is still set from Scale 3 for a moment
 * after the write, so polling it reports a transition that has not happened
 * and reads a board that never moved as one that arrived instantly. */
static bool vos_scale1(uint32_t ms)
{
    (void)HAL_PWREx_ConfigSupply(IGROW_SUPPLY);

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    const uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) <= ms) {
        const uint32_t csr1 = PWR->CSR1;
        if ((((csr1 >> 14) & 3U) == 3U) && (((csr1 >> 13) & 1U) == 1U)) {
            return true;
        }
    }
    return false;
}

static void supply_try(uint32_t cfg, const char *name)
{
    uprintf("supply -> %s ... ", name);
    const HAL_StatusTypeDef st = HAL_PWREx_ConfigSupply(cfg);
    uprintf("%s\r\n", (st == HAL_OK) ? "HAL_OK" : "HAL_ERROR");
    pwr_dump("after");
}

/* --- clocks --------------------------------------------------------------- */

/*
 * HSE 25 MHz, PLL1 M=5 (5 MHz reference), P=2.
 *
 *   full     N=160 -> VCO 800 MHz -> SYSCLK 400, HCLK 200, APB 100. Needs VOS1.
 *   reduced  N=80  -> VCO 400 MHz -> SYSCLK 200, HCLK 100, APB  50. Legal at VOS3.
 *
 * The reduced set exists so that a board whose regulator will not leave Scale 3
 * still runs everything, slower, instead of not running at all.
 */
static bool clock_config(bool full)
{
    RCC_ClkInitTypeDef c = {0};
    RCC_OscInitTypeDef o = {0};

    o.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    o.HSEState       = RCC_HSE_ON;
    o.HSIState       = RCC_HSI_OFF;
    o.CSIState       = RCC_CSI_OFF;
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

/* --- SDRAM ---------------------------------------------------------------- */

static bool sdram_up(void)
{
    if (BSP_SDRAM_Init(0) != BSP_ERROR_NONE) {
        return false;
    }
    if (!s_full_clock) {
        (void)HAL_SDRAM_ProgramRefreshRate(&hsdram[0], REFRESH_COUNT_100MHZ / 2U);
        uprintf("SDRAM refresh rescaled to %u for the half-speed FMC clock\r\n",
                (unsigned)(REFRESH_COUNT_100MHZ / 2U));
    }
    return true;
}

/* A framebuffer that reads back what was written is the only thing that makes
 * a blank screen mean the LCD rather than the memory behind it. */
static uint32_t sdram_check(void)
{
    volatile uint32_t *p     = (volatile uint32_t *)0xD0000000U;
    uint32_t           bad   = 0;
    const uint32_t     words = 8192U;

    for (uint32_t i = 0; i < words; i++) {
        p[i] = 0xA5A50000U ^ i;
    }
    for (uint32_t i = 0; i < words; i++) {
        if (p[i] != (0xA5A50000U ^ i)) {
            bad++;
        }
    }
    return bad;
}

/* --- display -------------------------------------------------------------- */

static void fb_fill(uint16_t colour)
{
    volatile uint16_t *fb = (volatile uint16_t *)LCD_FB_START_ADDRESS;
    for (uint32_t i = 0; i < (PANEL_W * PANEL_H); i++) {
        fb[i] = colour;
    }
}

static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour)
{
    volatile uint16_t *fb = (volatile uint16_t *)LCD_FB_START_ADDRESS;
    for (uint32_t row = y; row < (y + h); row++) {
        for (uint32_t col = x; col < (x + w); col++) {
            fb[(row * PANEL_W) + col] = colour;
        }
    }
}

/* The MB1063 is identified over I2C during the LCD init, so a wedged bus is
 * reported as an LCD component failure (-5). Free the bus before each attempt.
 * BSP_I2C1_DeInit() must never be called: it decrements its reference counter
 * twice and underflows it, after which I2C1 can never be initialised again. */
static bool i2c1_recover(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode             = GPIO_MODE_OUTPUT_OD;
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_LOW;
    g.Pin              = GPIO_PIN_6; /* I2C1_SCL */
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_7; /* I2C1_SDA */
    HAL_GPIO_Init(GPIOB, &g);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(1);

    for (unsigned i = 0; i < 9U; i++) {
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) {
            break;
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(1);

    const bool freed = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET);

    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    g.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &g);

    return freed;
}

static bool display_up(int32_t *out_rc)
{
    for (unsigned attempt = 1; attempt <= 3U; attempt++) {
        (void)i2c1_recover();
        const int32_t rc =
            BSP_LCD_InitEx(0, LCD_ORIENTATION_LANDSCAPE, LCD_PIXEL_FORMAT_RGB565, PANEL_W, PANEL_H);
        *out_rc = rc;
        if (rc == BSP_ERROR_NONE) {
            uprintf("display : ok on attempt %u\r\n", attempt);
            (void)BSP_LCD_SetActiveLayer(0, 0);
            return true;
        }
        uprintf("display : attempt %u returned %ld\r\n", attempt, (long)rc);
        (void)BSP_LCD_DeInit(0);
        HAL_Delay(120);
    }
    return false;
}

static void draw_test_pattern(void)
{
    fb_fill(RGB565_NAVY);
    fb_rect(120, 90, 400, 300, RGB565_CYAN);
    fb_rect(160, 130, 320, 220, RGB565_NAVY);
    fb_rect(280, 220, 80, 40, RGB565_GREEN);
    (void)BSP_LCD_DisplayOn(0);
}

/* --- MPU and cache -------------------------------------------------------- */

/* The framebuffer is written by the CPU and read by the LTDC, which does not
 * see the D-cache. Write-through means every store reaches the array before
 * the scanout does, with no cache maintenance on each draw. */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();
    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER0;
    r.BaseAddress      = 0xD0000000;
    r.Size             = MPU_REGION_SIZE_32MB;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable      = MPU_ACCESS_CACHEABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.TypeExtField     = MPU_TEX_LEVEL0;
    r.SubRegionDisable = 0x00;
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void CPU_CACHE_Enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

/* --- console commands ----------------------------------------------------- */

static void help(void)
{
    uputs("\r\ncommands: p PWR dump   v retry Scale 1\r\n");
#if defined(SMPS)
    uputs("          1 SMPS1V8->LDO   2 direct SMPS (this board's)\r\n");
    uputs("          3 LDO only (BROWNS THIS BOARD OUT - the SMPS feeds the core)\r\n");
#else
    uputs("          3 LDO only (this board's - the part has no SMPS)\r\n");
#endif
    uputs("          d redraw   o display off   O display on   r reset   h help\r\n");
}

static void handle(int c)
{
    switch (c) {
    case 'p':
        pwr_dump("now");
        break;
    case 'v':
        uprintf("Scale 1 retry: %s\r\n", vos_scale1(200U) ? "READY" : "timeout");
        pwr_dump("after");
        break;
/* The SMPS options exist only on a part that has an SMPS. On the H753 the LDO
 * is the sole core supply and there is nothing to choose between. */
#if defined(SMPS)
    case '1':
        supply_try(PWR_SMPS_1V8_SUPPLIES_LDO, "SMPS 1V8 supplies LDO");
        break;
    case '2':
        supply_try(PWR_DIRECT_SMPS_SUPPLY, "direct SMPS");
        break;
#endif
    case '3':
        supply_try(PWR_LDO_SUPPLY, "LDO only");
        break;
    case 'd':
        draw_test_pattern();
        uputs("redrawn\r\n");
        break;
    case 'o':
        (void)BSP_LCD_DisplayOff(0);
        uputs("display off\r\n");
        break;
    case 'O':
        (void)BSP_LCD_DisplayOn(0);
        uputs("display on\r\n");
        break;
    case 'r':
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

/* --- main ----------------------------------------------------------------- */

int main(void)
{
    MPU_Config();
    CPU_CACHE_Enable();
    HAL_Init();

    /* Console before anything that can fail, on the reset clock (HSI 64 MHz).
     * Everything after this point is reported rather than assumed. */
    uart_up();
    uputs("\r\n\r\n=== igrow-min: bare-metal bring-up (no RTOS, no LVGL, no CAN) ===\r\n");
    pwr_dump("at boot");

    s_vos_ready = vos_scale1(100U);
    if (s_vos_ready) {
        uputs("VOS     : Scale 1 ready\r\n");
    } else {
        uputs("VOS     : Scale 1 NOT ready after 100 ms - continuing at reduced clocks\r\n");
    }
    pwr_dump("after VOS");

    s_full_clock = s_vos_ready;
    if (!clock_config(s_full_clock)) {
        /* The only unrecoverable step: without a clock there is nothing to say
         * it with. Fall back to the reset clock and keep the console. */
        s_full_clock = false;
        uart_up();
        uputs("CLOCK   : PLL config FAILED - staying on HSI\r\n");
    } else {
        uart_up();
        uprintf("CLOCK   : %s - SYSCLK %lu Hz, HCLK %lu Hz\r\n",
                s_full_clock ? "full" : "reduced (Scale 3 limits)",
                (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HAL_RCC_GetHCLKFreq());
    }

    const bool sdram_ok = sdram_up();
    uprintf("SDRAM   : %s\r\n", sdram_ok ? "ok" : "FAILED");
    if (sdram_ok) {
        const uint32_t bad = sdram_check();
        uprintf("SDRAM   : %lu of 8192 words read back wrong\r\n", (unsigned long)bad);
    }

    int32_t    lcd_rc     = 0;
    const bool display_ok = sdram_ok && display_up(&lcd_rc);
    if (display_ok) {
        draw_test_pattern();
        uputs("drawn   : cyan frame on navy, green block in the middle\r\n");
    } else {
        uprintf("display : FAILED, last rc %ld (-4 peripheral, -5 component, -7 unknown)\r\n",
                (long)lcd_rc);
    }

    help();

    uint32_t last = HAL_GetTick();
    for (;;) {
        const int c = ugetc();
        if (c >= 0) {
            handle(c);
        }
        const uint32_t now = HAL_GetTick();
        if ((now - last) >= 2000U) {
            last = now;
            uprintf("alive %lus  VOSRDY=%u ACTVOS=%u display=%s\r\n", (unsigned long)(now / 1000U),
                    (unsigned)((PWR->D3CR >> 13) & 1U), (unsigned)((PWR->CSR1 >> 14) & 3U),
                    display_ok ? "up" : "down");
        }
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
