/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IndustryGrow bench panel. Which eval board is a build option -- eval_board.h.
 *
 * Reads the cabinet CAN bus and shows it. It joins the bus in BUS MONITORING
 * mode and stays there until somebody presses a button: a panel that cannot
 * ACK cannot disturb a bus that is working, which is the property that lets it
 * be plugged into a live bench without a risk assessment.
 *
 * The image is CM7 on either host. On the dual-core H747 the CM4 boots too,
 * from bank 2 at 0x08100000, which this build never writes -- it locks up on
 * an erased vector table and holds nothing the CM7 needs. Nothing here gates
 * it or clears the BOOT_CM4 option byte.
 */
#include "main.h"

#include <stdbool.h>

#include "can_port.h"
#include "eval_board.h"
#include "console.h"
#include "cyphal_rx.h"
#include "lvgl_port.h"
#include "model.h"
#include "protocol.h"
#include "selftest.h"
#include "tasks.h"
#include "ui_main.h"

static bool SystemClock_Config(void);

/* Voltage-scaling evidence, captured before the console exists. */
static uint32_t s_vos_d3cr_pre;
static uint32_t s_vos_d3cr_req;
static uint32_t s_vos_d3cr_post;
static uint32_t s_vos_csr1_post;
static uint32_t s_vos_ms;
static uint32_t s_vos_scuen;
static HAL_StatusTypeDef s_vos_supply;
static void MPU_Config(void);
static void CPU_CACHE_Enable(void);

int main(void)
{
    MPU_Config();
    CPU_CACHE_Enable();

    HAL_Init();
    const bool full_clock = SystemClock_Config();

    (void)console_init();
    console_printf("\r\n\r\nboot\r\n");
    if (!full_clock) {
        console_printf("CLOCK   : REDUCED - Scale 1 needs a POWER CYCLE, not a reset "
                       "(the supply configuration is consumed once per power-on)\r\n");
    }
    /* CR3 bit 2 is SCUEN on the H753 and SMPSEN on the H747, so it is printed
     * raw and named for the part rather than for one of them. A HAL_ERROR is
     * not a fault on a warm boot: it means the configuration was already set
     * by the boot that followed the last power cycle. */
    console_printf("SUPPLY  : ConfigSupply(%s) %s, CR3 bit 2 was %lu\r\n", IGROW_SUPPLY_NAME,
                   (s_vos_supply == HAL_OK) ? "HAL_OK" : "HAL_ERROR (locked since the last "
                                                         "power-on, so this did nothing)",
                   (unsigned long)s_vos_scuen);
    console_printf("VOS     : D3CR %08lX -> %08lX -> %08lX  CSR1 %08lX  waited %lu ms\r\n",
                   (unsigned long)s_vos_d3cr_pre, (unsigned long)s_vos_d3cr_req,
                   (unsigned long)s_vos_d3cr_post, (unsigned long)s_vos_csr1_post,
                   (unsigned long)s_vos_ms);
    console_printf("CLOCK   : SYSCLK %lu Hz, HCLK %lu Hz\r\n",
                   (unsigned long)HAL_RCC_GetSysClockFreq(),
                   (unsigned long)HAL_RCC_GetHCLKFreq());

    /* SDRAM first: the model and the LVGL heap both live there, and both must
     * work whether or not the display does. */
    const bool sdram_ok = panel_sdram_init();
    model_init();
    /* The carried procedure is parsed once, from the bytes this build carries.
     * It lives in SDRAM, so it has to come after the SDRAM is up. */
    if (sdram_ok && protocol_load_embedded()) {
        console_printf("protocol: %s, %u steps\r\n", protocol_get()->identity,
                       (unsigned)protocol_get()->count);
    }

    /* Display and touch BEFORE anything touches CAN.
     *
     * Order is load-bearing, not tidiness. Bringing FDCAN up reassigns two
     * GPIOs, and on this board the wrong pair belongs to the FMC that drives
     * the SDRAM the framebuffer lives in. Claiming the glass first means a
     * display failure is a display failure, and not something CAN did to it. */
    /* The LCD does not always come up first time: 2 boots in 5 returned -5.
     *
     * That is NOT a display fault. BSP_LCD_InitEx identifies the daughterboard by
     * reading the touch controller's ID over I2C, so a wedged I2C bus is reported
     * as an LCD component failure. Retrying alone never helped, because nothing
     * in the retry freed the bus; a reset always did, because it re-initialised
     * I2C1. So each attempt is preceded by an explicit bus recovery.
     *
     * The retries are reported. A panel that quietly needed three goes is a fault
     * being hidden, and it should be visible before it becomes a field problem. */
    
    bool    display_ok = false;
    bool    touch_ok   = false;
    bool    joy_ok     = false;
    int32_t lcd_rc     = 0;
    uint8_t lcd_tries  = 0;
    if (sdram_ok) {
        for (lcd_tries = 1; lcd_tries <= 3U; lcd_tries++) {
            if (!panel_i2c_recover()) {
                console_printf("I2C1 still held low after a recovery burst\r\n");
            }
            display_ok = panel_display_init_rc(&lcd_rc);
            if (display_ok) {
                break;
            }
            panel_display_deinit();
            HAL_Delay(120);
        }
        if (display_ok) {
            touch_ok = panel_touch_init();
            joy_ok   = panel_joy_init();
            lvgl_port_init();
            ui_build();
        }
    }
    console_report_boot(display_ok, touch_ok, joy_ok, sdram_ok);
    if (display_ok && (lcd_tries > 1U)) {
        console_printf("WARNING: the LCD needed %u attempts - I2C1 was wedged and was recovered.\r\n",
                       (unsigned)lcd_tries);
    }
    if (!display_ok) {
        console_printf("LCD init returned %ld after %u attempts "
                       "(-4 peripheral, -5 component, -7 unknown component)\r\n",
                       (long)lcd_rc, (unsigned)lcd_tries - 1U);
        console_printf("-5 here means the touch controller did not answer on I2C, which is how "
                       "a wedged I2C bus reports itself through the LCD init.\r\n");
        console_i2c_scan();
    }

    /* Listen-only, on the first candidate pair. The sweep below decides which
     * pins are real; until it does, nothing is driven onto the bus. */
    /* Board controls: either push-button blanks or restores the display.
     * BUTTON_USER and BUTTON_TAMPER are the same pin (PC13) on this board, so
     * there are two distinct buttons: PA0 (wakeup) and PC13 (tamper, the one
     * marked B2). Neither navigates -- the joystick does all of that on its
     * own. No brightness control -- see panel_display_set(). */
    (void)BSP_PB_Init(BUTTON_WAKEUP, BUTTON_MODE_GPIO);
    (void)BSP_PB_Init(BUTTON_USER, BUTTON_MODE_GPIO);
    console_printf("controls: PA0 or PC13 (B2) blanks the display\r\n");

    (void)can_init(0, true);
    cyphal_rx_init(PANEL_DEFAULT_NODE_ID);
    console_report_can();

    /* Which pins the board's transceiver is on is not known from the BSP, so
     * find out. Three seconds per candidate, in monitoring mode throughout. */
    can_hunt_start(3000U);
    console_printf("hunting for the CAN pins (listen-only, 3 s per candidate)\r\n");

    /* Everything above is single-threaded bring-up. From here the scheduler
     * owns the CPU: one task owns the interface, one does the work that
     * blocks, one runs LVGL. tasks_start() does not return. */
    tasks_start();
}

/*
 * System clock. HSE 25 MHz, PLL1 M=5 (5 MHz reference), P=2:
 *
 *   full     N=160 -> VCO 800 MHz -> SYSCLK 400, HCLK 200, APB 100. Needs Scale 1.
 *   reduced  N=80  -> VCO 400 MHz -> SYSCLK 200, HCLK 100, APB  50. Legal at Scale 3.
 *
 * The wait for the regulator is bounded, and the reduced set exists because a
 * warm boot legitimately cannot reach Scale 1: the supply configuration that
 * starts the voltage-scaling machinery is applied once per power-on, so an
 * image flashed and reset arrives at Scale 3 and stays there until the board
 * is power-cycled. The ST template's unbounded wait turns that into a silent
 * hang before the console exists -- no banner, no display, and nothing to tell
 * it from a dead board. Everything runs at the reduced set instead; the panel
 * says which it got and carries on.
 *
 * FDCAN does NOT run off this PLL -- can_port.c puts it on HSE directly, so
 * neither the bit rate nor the sample point moves with the clock chosen here.
 *
 * Returns whether the full set was reached.
 */
static bool SystemClock_Config(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_OscInitTypeDef RCC_OscInitStruct;

    /* THE SUPPLY CONFIGURATION MUST BE APPLIED BEFORE THE VOLTAGE SCALE.
     *
     * Without this call ACTVOSRDY stays 0 out of reset, the voltage-scaling
     * machinery never starts, and every request for Scale 1 is ignored -- which
     * for a long time read as "the regulator will not leave Scale 3" on both
     * eval boards. It is not a regulator fault and not a board fault: this is
     * the only HAL call that completes the supply handshake and waits for
     * ACTVOSRDY. With it, Scale 1 arrives in about 6.5 ms via Scale 2.
     *
     * WHICH SUPPLY IS PER BOARD AND THE WRONG ONE BROWNS THE CORE OUT until
     * the next power cycle -- eval_board.h carries which and why. Requesting
     * the LDO on the H757, whose core is fed by the SMPS, switches off the
     * supply that is running the CPU; asking for it here once cost a locked
     * board. PWR_EXTERNAL_SOURCE_SUPPLY is never right on either.
     *
     * The configuration is single-write and reloads ONLY on a power-on reset.
     * After a programmer reset, NRST or a software reset the request is
     * refused or does nothing, and the board legitimately runs at whatever
     * scale the last power cycle established. That is why the outcome is
     * reported rather than assumed -- a 200 MHz boot after flashing is
     * expected, not a fault. */
    s_vos_scuen  = (PWR->CR3 >> 2) & 1U;
    s_vos_supply = HAL_PWREx_ConfigSupply(IGROW_SUPPLY);

    s_vos_d3cr_pre = PWR->D3CR;
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    s_vos_d3cr_req = PWR->D3CR;

    bool           vos_ready = false;
    const uint32_t t0        = HAL_GetTick();
    while ((HAL_GetTick() - t0) <= 100U) {
        /* ACTVOS is the scale actually in force. VOSRDY is still set from
         * Scale 3 for a moment after the write, so polling it reports a
         * transition that has not happened yet. */
        if ((((PWR->CSR1 >> 14) & 3U) == 3U) && (((PWR->CSR1 >> 13) & 1U) == 1U)) {
            vos_ready = true;
            break;
        }
    }
    s_vos_ms        = HAL_GetTick() - t0;
    s_vos_d3cr_post = PWR->D3CR;
    s_vos_csr1_post = PWR->CSR1;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSIState       = RCC_HSI_OFF;
    RCC_OscInitStruct.CSIState       = RCC_CSI_OFF;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 5;
    RCC_OscInitStruct.PLL.PLLN       = vos_ready ? 160 : 80;
    RCC_OscInitStruct.PLL.PLLFRACN   = 0;
    RCC_OscInitStruct.PLL.PLLP       = 2;
    RCC_OscInitStruct.PLL.PLLR       = 2;
    RCC_OscInitStruct.PLL.PLLQ       = 4;
    RCC_OscInitStruct.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                   RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                                   RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1);
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
    return vos_ready;
}

/*
 * The framebuffer is written by the CPU and read by the LTDC, which does not
 * see the D-cache. Mapping the SDRAM write-through means every store reaches
 * the array before the scanout does, without having to clean the cache by hand
 * on each flush. Not bufferable, no write-allocate: TEX0 / C=1 / B=0.
 */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    HAL_MPU_Disable();

    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0xD0000000;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_32MB;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void CPU_CACHE_Enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

void Error_Handler(void)
{
    /* Nothing to report to and nothing safe to do: stop where the fault is so
     * a debugger sees the call stack. */
    __disable_irq();
    for (;;) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    console_printf("assert %s:%lu\r\n", (const char *)file, (unsigned long)line);
    for (;;) {
    }
}
#endif
