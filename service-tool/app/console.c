/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "can_port.h"
#include "cyphal_rx.h"
#include "model.h"
#include "protocol.h"
#include "eval_board.h"
#include "stm32h7xx_hal.h"
#include "lvgl_port.h"
#include "subjects.h"

static UART_HandleTypeDef s_uart;
static bool               s_ready;

/* --- receive ring ---------------------------------------------------------
 *
 * Polling the receiver from a task cannot keep up with a line of input. At
 * 115200 a character lands every 87 us and a pasted identifier is 19 of them,
 * while the polling task runs every 5 ms -- and a poll that finds the register
 * momentarily empty stops looking, so the rest of the line arrives during the
 * sleep and overruns. The receiver then refuses further characters until ORE is
 * cleared, which cost the whole console until the next reset.
 *
 * The interrupt takes every character as it lands. It calls no kernel API, so
 * it may sit above the syscall ceiling and is never masked by a critical
 * section. */
#define CONSOLE_RX_LEN 256U

static volatile uint8_t  s_rx[CONSOLE_RX_LEN];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

void USART1_IRQHandler(void)
{
    /* Clear an overrun and keep the character that caused it; losing one is
     * better than losing the receiver. */
    if ((COM1_UART->ISR & USART_ISR_ORE) != 0U) {
        COM1_UART->ICR = USART_ICR_ORECF;
    }
    while ((COM1_UART->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        const uint8_t  c    = (uint8_t)(COM1_UART->RDR & 0xFFU);
        const uint16_t next = (uint16_t)((s_rx_head + 1U) % CONSOLE_RX_LEN);
        if (next != s_rx_tail) {
            s_rx[s_rx_head] = c;
            s_rx_head       = next;
        }
    }
}

bool console_init(void)
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

    s_uart.Instance            = COM1_UART;
    s_uart.Init.BaudRate       = 115200;
    s_uart.Init.WordLength     = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits       = UART_STOPBITS_1;
    s_uart.Init.Parity         = UART_PARITY_NONE;
    s_uart.Init.Mode           = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling   = UART_OVERSAMPLING_16;
    s_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    s_ready = (HAL_UART_Init(&s_uart) == HAL_OK);
    if (s_ready) {
        s_rx_head = 0;
        s_rx_tail = 0;
        __HAL_UART_ENABLE_IT(&s_uart, UART_IT_RXNE);
        /* Above configMAX_SYSCALL_INTERRUPT_PRIORITY: the handler calls no
         * kernel API, so it must not be delayed by a critical section. */
        HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    return s_ready;
}

void console_write(const char *s)
{
    if (!s_ready || (s == NULL)) {
        return;
    }
    const size_t n = strlen(s);
    (void)HAL_UART_Transmit(&s_uart, (const uint8_t *)s, (uint16_t)n, 200U);
}

void console_printf(const char *fmt, ...)
{
    if (!s_ready) {
        return;
    }
    char    line[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > 0) {
        (void)HAL_UART_Transmit(&s_uart, (const uint8_t *)line,
                                (uint16_t)((n < (int)sizeof line) ? n : (int)sizeof line - 1),
                                200U);
    }
}

void console_report_boot(bool display_ok, bool touch_ok, bool joy_ok, bool sdram_ok)
{
    console_printf("\r\n=== IndustryGrow panel (" IGROW_BOARD_NAME " + " IGROW_LCD_NAME
                   ") ===\r\n");
    console_printf("SDRAM   : %s\r\n", sdram_ok ? "ok" : "FAILED");
    if (display_ok) {
        console_printf("display : ok (%ux%u)\r\n", (unsigned)PANEL_W, (unsigned)PANEL_H);
    } else {
        console_printf("display : FAILED - text mode only\r\n");
    }
    /* "ok" here means the controller answered on I2C, which is not the same as
     * the glass registering a finger -- press counts settle that, and the
     * joystick works regardless of which daughterboard is fitted. */
    console_printf("touch   : %s (controller answered)\r\n", touch_ok ? "ok" : "FAILED");
    console_printf("joystick: %s - arrows move, click SEL to enter, HOLD SEL to go back\r\n",
                   joy_ok ? "ok" : "FAILED");
}

/* Separate from the banner because it can only be true after can_init() has
 * run, and the display has to come up before CAN touches any GPIO. */
void console_report_can(void)
{
    console_printf("CAN     : classic, 500 kbit/s, %s on %s\r\n",
                   can_is_listen_only() ? "LISTEN-ONLY" : "normal",
                   can_is_up() ? can_profile_name(can_current_profile()) : "DOWN");
}

static void emit_raw(const char *s)
{
    while ((s != NULL) && (*s != '\0')) {
        while ((COM1_UART->ISR & USART_ISR_TXE_TXFNF) == 0U) {
        }
        COM1_UART->TDR = (uint8_t)*s++;
    }
}

void console_emergency(const char *msg, const char *file, unsigned line)
{
    char digits[12];
    int  i = 0;
    if (line == 0U) {
        digits[i++] = '0';
    }
    while ((line > 0U) && (i < 10)) {
        digits[i++] = (char)('0' + (line % 10U));
        line /= 10U;
    }
    char rev[12];
    int  j = 0;
    while (i > 0) { /* the digits came out least-significant first */
        rev[j++] = digits[--i];
    }
    rev[j] = '\0';

    emit_raw("\r\n");
    emit_raw(msg);
    emit_raw(" at ");
    emit_raw(file);
    emit_raw(":");
    emit_raw(rev);
    emit_raw("\r\n");
}

char console_poll_command(void)
{
    if (!s_ready || (s_rx_tail == s_rx_head)) {
        return 0;
    }
    const char c = (char)s_rx[s_rx_tail];
    s_rx_tail    = (uint16_t)((s_rx_tail + 1U) % CONSOLE_RX_LEN);
    return c;
}

void console_i2c_scan(void)
{
    if (BSP_I2C1_Init() != BSP_ERROR_NONE) {
        console_printf("I2C1: init failed\r\n");
        return;
    }
    console_printf("I2C1 scan (8-bit addresses):");
    unsigned found = 0;
    /* The BSP speaks 8-bit addresses (TS_I2C_ADDRESS is 0x54), so step by two
     * and report what the board actually answers with. */
    for (uint16_t addr = 0x02; addr < 0xFEU; addr += 2U) {
        if (BSP_I2C1_IsReady(addr, 2) == BSP_ERROR_NONE) {
            console_printf(" 0x%02X", (unsigned)addr);
            found++;
        }
    }
    console_printf("%s\r\n", (found == 0U) ? " nothing answered" : "");
    console_printf("  (" IGROW_TS_NAME " = touch on " IGROW_LCD_NAME
                   "; 0x84 = MFX IO expander, 0x34 = audio codec)\r\n");
}

/* --- protocol execution (SERVICE-TOOL F3, F4) ------------------------------ */

/* The instance is typed, not derived: serials are issued by the ERP and a tool
 * that invented one would be issuing identity it has no authority over. Entry
 * on a panel is the realization's problem (O-76); on the bench it is a line. */
static bool    s_line_mode;
static char    s_line[32];
static uint8_t s_line_len;

bool console_feed_line_input(char c)
{
    if (!s_line_mode) {
        return false;
    }
    if ((c == '\r') || (c == '\n')) {
        s_line[s_line_len] = '\0';
        s_line_mode        = false;
        protocol_set_instance(s_line);
        console_printf("\r\ninstance set to '%s'\r\n", protocol_instance());
        return true;
    }
    if ((c == '\b') || (c == 0x7F)) {
        if (s_line_len > 0U) {
            s_line_len--;
        }
        return true;
    }
    if ((size_t)(s_line_len + 1U) < sizeof s_line) {
        s_line[s_line_len++] = c;
    }
    return true;
}

static void emit_line(const char *line)
{
    console_printf("%s\r\n", line);
}

static void protocol_show(void)
{
    const protocol_t *p = protocol_get();
    if (!p->loaded) {
        console_printf("no protocol loaded\r\n");
        return;
    }
    console_printf("\r\n-- protocol: %s\r\n", p->identity);
    console_printf("-- %u steps, %u passed, %u failed%s\r\n", (unsigned)p->count,
                   (unsigned)protocol_passed_count(), (unsigned)protocol_failed_count(),
                   p->finished ? ", RUN OVER" : "");

    const proto_step_t *cur = protocol_current();
    if (cur == NULL) {
        console_printf("-- no step due\r\n");
        return;
    }
    console_printf("-- due: step %u  [%s]\r\n", (unsigned)cur->number, cur->section);
    console_printf("     %s\r\n", cur->text);
    console_printf("     pass when: %s\r\n", cur->criterion);
}

/* n and f record the step that is due. o and e are the two attempts V4 makes:
 * a step out of order, and an edit of a result already recorded. Both go
 * through the same call the buttons use, so what refuses them is the executor,
 * not the absence of a button. */
void console_protocol_command(char c)
{
    const protocol_t *p = protocol_get();
    const char       *why = NULL;
    uint16_t          target;

    switch (c) {
    case 'p':
        protocol_show();
        return;
    case 'i':
        s_line_mode = true;
        s_line_len  = 0;
        console_printf("instance (Exxxx-VVVVVV-NNNNNN), then Enter: ");
        return;
    case 'Q':
        if (!protocol_emit(emit_line, &why)) {
            console_printf("REFUSED: %s\r\n", (why != NULL) ? why : "unknown");
        }
        return;
    case 'R':
        protocol_restart();
        console_printf("protocol restarted\r\n");
        protocol_show();
        return;
    case 'n':
    case 'f':
        target = p->cursor;
        break;
    case 'o':
        /* Two steps ahead: the one after the one that is due. */
        target = (uint16_t)(p->cursor + 2U);
        console_printf("attempting step index %u out of order...\r\n", (unsigned)target);
        break;
    case 'e':
        if (p->cursor == 0U) {
            console_printf("nothing recorded yet; pass a step first, then try e\r\n");
            return;
        }
        target = (uint16_t)(p->cursor - 1U);
        console_printf("attempting to revise recorded step index %u...\r\n", (unsigned)target);
        break;
    default:
        return;
    }

    if (protocol_record(target, (c != 'f'), &why)) {
        console_printf("step index %u recorded %s\r\n", (unsigned)target,
                       (c == 'f') ? "FAIL" : "PASS");
        protocol_show();
    } else {
        console_printf("REFUSED: %s\r\n", (why != NULL) ? why : "unknown");
    }
}

void console_dump_model(void)
{
    const can_stats_t *st = can_stats();
    /* The mode belongs on this line: a non-zero tx count means either a defect
     * or an operator action, and only the mode tells the two apart. */
    console_printf("\r\n-- mode: %s on %s\r\n", can_is_listen_only() ? "LISTEN-ONLY" : "normal",
                   can_is_up() ? can_profile_name(can_current_profile()) : "DOWN");
    console_printf("-- bus: rx %lu tx %lu drop %lu | TEC %u REC %u LEC %u%s%s%s\r\n",
                   (unsigned long)st->rx_frames, (unsigned long)st->tx_frames,
                   (unsigned long)st->tx_dropped, (unsigned)st->tx_errors,
                   (unsigned)st->rx_errors, (unsigned)st->last_error,
                   st->bus_off ? " BUS-OFF" : "", st->error_passive ? " PASSIVE" : "",
                   st->error_warning ? " WARN" : "");
    /* rx_lost_ring is what S1 and S2 are about: frames the ISR had to throw
     * away because the drain fell behind. It is a count, never a flag. */
    console_printf("-- ring: depth %u/%u, rx lost %lu\r\n", (unsigned)can_ring_depth(),
                   (unsigned)can_ring_capacity(), (unsigned long)st->rx_lost_ring);
    console_printf("-- transfers: %lu accepted, %lu unknown, %u nodes\r\n",
                   (unsigned long)cyphal_rx_accepted(), (unsigned long)cyphal_rx_unknown(),
                   (unsigned)model_node_count());

    for (uint8_t i = 0;; i++) {
        model_node_t *n = model_node_at(i);
        if (n == NULL) {
            break;
        }
        console_printf("node %-3u  %-24s up %lus  health %u mode %u vssc %u  hb %lu\r\n",
                       (unsigned)n->node_id, n->have_name ? n->name : "(name not requested)",
                       (unsigned long)n->uptime, (unsigned)n->health, (unsigned)n->mode,
                       (unsigned)n->vssc, (unsigned long)n->hb_count);
        for (uint8_t k = 0; k < n->n_signals; k++) {
            const model_signal_t  *s   = &n->sig[k];
            const igrow_subject_t *sub = igrow_subject_by_id(s->subject_id);
            if (sub == NULL) {
                continue;
            }
            /* Liveness and the observed period are printed so a verification
             * run leaves its own evidence, rather than needing someone to have
             * been watching the glass at the time. */
            const model_liveness_t lv = model_liveness(s, HAL_GetTick());
            console_printf("    %-4u %-3s %-14s %10s %-10s n=%-4lu age %6lums period %5lums %-5s%s\r\n",
                           (unsigned)s->subject_id, igrow_subject_module(s->subject_id), sub->name,
                           s->last.text, sub->unit, (unsigned long)s->count,
                           (unsigned long)(HAL_GetTick() - s->last_ms),
                           (unsigned long)s->period_ms,
                           (lv == MODEL_LIVE) ? "live" : ((lv == MODEL_LATE) ? "LATE" : "STALE"),
                           s->last.valid ? "" : "  INVALID");
        }
    }
}
