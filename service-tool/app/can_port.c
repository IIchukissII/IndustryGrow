/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "can_port.h"

#include "panel_mem.h"
#include "stm32h7xx_hal.h"

/* --- receive ring ---------------------------------------------------------
 * Frames are taken out of the hardware FIFO in the FDCAN interrupt and parked
 * here; the main loop drains them whenever it gets round to it.
 *
 * Without this the GUI has to return within the depth of the 64-frame hardware
 * FIFO. At 500 kbit/s a saturated bus delivers roughly 3900 frames/s, so that
 * is 16-25 ms of headroom against a 20 ms LVGL refresh period -- a heavy repaint
 * could drop frames, and the symptom would look like a bus fault rather than a
 * missed deadline. 512 entries is about 130 ms of tolerance for 8 kB.
 *
 * One producer (the ISR) and one consumer (the main loop), so head and tail
 * each have a single writer and no lock is needed. `head` is written only by
 * the ISR, `tail` only by the consumer. */
#define CAN_RING_LEN 512U /* power of two: the index wrap is a mask */

typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
} can_ring_entry_t;

static can_ring_entry_t   s_ring[CAN_RING_LEN] PANEL_AXI_BSS;
static volatile uint16_t  s_ring_head;
static volatile uint16_t  s_ring_tail;

/* --- bit timing -----------------------------------------------------------
 * FDCAN kernel clock = HSE = 25 MHz (the eval board's crystal; see
 * SystemClock_Config). 25 MHz / prescaler 2 = 12.5 MHz time-quantum clock,
 * 25 tq per bit => 500 000 bit/s exactly. Sample point (1 + 21) / 25 = 88 %,
 * which is where the rest of the bus sits.
 *
 * Take the crystal, not a PLL output: it is the one clock whose accuracy is
 * specified on the board and it does not move when the display clocks change. */
#define CAN_NOMINAL_PRESCALER 2U
#define CAN_NOMINAL_TSEG1     21U
#define CAN_NOMINAL_TSEG2     3U
#define CAN_NOMINAL_SJW       3U

typedef struct {
    const char          *name;
    FDCAN_GlobalTypeDef *instance;
    GPIO_TypeDef        *tx_port;
    uint16_t             tx_pin;
    GPIO_TypeDef        *rx_port;
    uint16_t             rx_pin;
    uint8_t              af;
} can_profile_t;

/* The FDCAN pin pairs that are ACTUALLY AVAILABLE on this board.
 *
 * The STM32H753 offers more than these, but on the MB1246 motherboard most of
 * them already belong to something, and reconfiguring a pin to FDCAN takes that
 * something away. Three pairs are excluded and must stay excluded:
 *
 *   PD0/PD1    FMC_D2 / FMC_D3 -- the SDRAM data bus. Driving these as FDCAN
 *              corrupts every access to the framebuffer and the model.
 *   PH13/PH14  also FMC (see SDRAM_MspInit in stm32h747i_eval_sdram.c).
 *   PB5/PB6    PB6 is I2C1_SCL, the bus the touch controller and the display
 *              components sit on.
 *
 * PA11/PA12 is USB OTG FS and PB12/PB13 is in the Ethernet block; neither is
 * used by this firmware, so borrowing them costs nothing.
 *
 * PA11/PA12 is candidate 0 because it is the pair that works -- verified
 * against the cabinet bus. It is where the MB1246 transceiver sits, and what
 * ST's own FDCAN example for this motherboard uses. The MCU reaches it only
 * with JP1 and JP2 fitted: those steer PA11/PA12 between USB OTG FS and the CAN
 * transceiver, and without them every candidate reads as idle pins -- no
 * frames, no errors. See BENCH.md step 0. */
static const can_profile_t s_profiles[] = {
    {"FDCAN1 PA11/PA12", FDCAN1, GPIOA, GPIO_PIN_12, GPIOA, GPIO_PIN_11, GPIO_AF9_FDCAN1},
    {"FDCAN1 PB8/PB9", FDCAN1, GPIOB, GPIO_PIN_9, GPIOB, GPIO_PIN_8, GPIO_AF9_FDCAN1},
    {"FDCAN2 PB12/PB13", FDCAN2, GPIOB, GPIO_PIN_13, GPIOB, GPIO_PIN_12, GPIO_AF9_FDCAN2},
};

#define CAN_PROFILE_COUNT (sizeof s_profiles / sizeof s_profiles[0])

static FDCAN_HandleTypeDef s_fdcan;
static can_stats_t         s_stats;
static uint8_t             s_profile;
static can_mode_t          s_mode;
static bool                s_listen_only;
static bool                s_up;

static can_hunt_state_t s_hunt_state;
static uint8_t          s_hunt_index;
static uint32_t         s_hunt_dwell_ms;
static uint32_t         s_hunt_started_ms;
static uint32_t         s_hunt_rx_at_start;

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    } else {
        /* No other port appears in the table. */
    }
}

static void pins_init(const can_profile_t *p)
{
    GPIO_InitTypeDef g = {0};
    gpio_clock_enable(p->tx_port);
    gpio_clock_enable(p->rx_port);

    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = p->af;

    g.Pin = p->tx_pin;
    HAL_GPIO_Init(p->tx_port, &g);
    g.Pin = p->rx_pin;
    HAL_GPIO_Init(p->rx_port, &g);
}

static void pins_deinit(const can_profile_t *p)
{
    HAL_GPIO_DeInit(p->tx_port, p->tx_pin);
    HAL_GPIO_DeInit(p->rx_port, p->rx_pin);
}

bool can_init(uint8_t profile, bool listen_only)
{
    return can_init_mode(profile, listen_only ? CAN_MODE_LISTEN : CAN_MODE_NORMAL);
}

can_mode_t can_mode(void)
{
    return s_mode;
}

bool can_init_mode(uint8_t profile, can_mode_t mode)
{
    if (profile >= CAN_PROFILE_COUNT) {
        return false;
    }
    can_deinit();

    const can_profile_t *p = &s_profiles[profile];

    /* Kernel clock: HSE. The reset value already selects it, but say so --
     * a later peripheral clock change must not silently move the bit rate. */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection      = RCC_PERIPHCLK_FDCAN;
    pclk.FdcanClockSelection       = RCC_FDCANCLKSOURCE_HSE;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        return false;
    }
    __HAL_RCC_FDCAN_CLK_ENABLE();
    pins_init(p);

    s_fdcan.Instance                  = p->instance;
    s_fdcan.Init.FrameFormat          = FDCAN_FRAME_CLASSIC;
    s_fdcan.Init.Mode                 = (mode == CAN_MODE_LISTEN)   ? FDCAN_MODE_BUS_MONITORING
                                        : (mode == CAN_MODE_LOOPBACK) ? FDCAN_MODE_INTERNAL_LOOPBACK
                                                                      : FDCAN_MODE_NORMAL;
    s_fdcan.Init.AutoRetransmission   = ENABLE;
    s_fdcan.Init.TransmitPause        = DISABLE;
    s_fdcan.Init.ProtocolException    = DISABLE;
    s_fdcan.Init.NominalPrescaler     = CAN_NOMINAL_PRESCALER;
    s_fdcan.Init.NominalSyncJumpWidth = CAN_NOMINAL_SJW;
    s_fdcan.Init.NominalTimeSeg1      = CAN_NOMINAL_TSEG1;
    s_fdcan.Init.NominalTimeSeg2      = CAN_NOMINAL_TSEG2;
    /* Data phase is never used in classic mode; give it legal values anyway. */
    s_fdcan.Init.DataPrescaler        = CAN_NOMINAL_PRESCALER;
    s_fdcan.Init.DataSyncJumpWidth    = CAN_NOMINAL_SJW;
    s_fdcan.Init.DataTimeSeg1         = CAN_NOMINAL_TSEG1;
    s_fdcan.Init.DataTimeSeg2         = CAN_NOMINAL_TSEG2;

    /* The H7 shares one 10 kB message RAM between both FDCAN instances and
     * makes the application lay it out. Only one instance is ever up here, so
     * it starts at offset 0. */
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
        pins_deinit(p);
        return false;
    }

    /* No filters configured, so tell the acceptance logic to take everything
     * into FIFO 0 rather than reject it. */
    if (HAL_FDCAN_ConfigGlobalFilter(&s_fdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK) {
        return false;
    }
    if (HAL_FDCAN_Start(&s_fdcan) != HAL_OK) {
        return false;
    }

    /* Empty the ring before the first interrupt can fill it: a profile switch
     * must not deliver frames captured on the previous pins. */
    s_ring_head = 0;
    s_ring_tail = 0;

    if (HAL_FDCAN_ActivateNotification(&s_fdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        return false;
    }
    const IRQn_Type irq = (p->instance == FDCAN1) ? FDCAN1_IT0_IRQn : FDCAN2_IT0_IRQn;
    /* Above the SysTick priority so a repaint cannot delay servicing, but
     * preemptible so it never holds off the tick for long. */
    HAL_NVIC_SetPriority(irq, 5, 0);
    HAL_NVIC_EnableIRQ(irq);

    s_profile = profile;
    s_mode    = mode;
    /* Loopback must be able to transmit -- that is the whole point of it -- so
     * only bus monitoring blocks can_tx(). */
    s_listen_only = (mode == CAN_MODE_LISTEN);
    s_up          = true;
    return true;
}

void can_deinit(void)
{
    if (!s_up) {
        return;
    }
    HAL_NVIC_DisableIRQ((s_profiles[s_profile].instance == FDCAN1) ? FDCAN1_IT0_IRQn
                                                                   : FDCAN2_IT0_IRQn);
    (void)HAL_FDCAN_Stop(&s_fdcan);
    (void)HAL_FDCAN_DeInit(&s_fdcan);
    s_ring_head = 0;
    s_ring_tail = 0;
    pins_deinit(&s_profiles[s_profile]);
    s_up = false;
}

/* Called from the FDCAN interrupt. Drains the hardware FIFO into the ring so
 * the peripheral never has to wait for the GUI. */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U) {
        return;
    }
    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U) {
        FDCAN_RxHeaderTypeDef h;
        uint8_t               buf[8];
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &h, buf) != HAL_OK) {
            break;
        }
        s_stats.rx_frames++;

        /* Nothing on a Cyphal bus uses 11-bit IDs; count it, do not carry it. */
        if (h.IdType != FDCAN_EXTENDED_ID) {
            continue;
        }

        const uint16_t head = s_ring_head;
        const uint16_t next = (uint16_t)((head + 1U) & (CAN_RING_LEN - 1U));
        if (next == s_ring_tail) {
            /* Full. Drop the newest and say so -- a silently short stream is
             * the failure that looks like a sensor fault. */
            s_stats.rx_lost_ring++;
            continue;
        }
        uint8_t len = (uint8_t)h.DataLength; /* classic: DLC == byte count for 0..8 */
        if (len > 8U) {
            len = 8U;
        }
        s_ring[head].id  = h.Identifier;
        s_ring[head].len = len;
        for (uint8_t i = 0; i < len; i++) {
            s_ring[head].data[i] = buf[i];
        }
        s_ring_head = next; /* publish only after the entry is complete */
    }
}

void FDCAN1_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&s_fdcan);
}

void FDCAN2_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&s_fdcan);
}

bool can_rx(uint32_t *out_ext_id, uint8_t *out_data, uint8_t *out_len)
{
    const uint16_t tail = s_ring_tail;
    if (tail == s_ring_head) {
        return false;
    }
    *out_ext_id = s_ring[tail].id;
    *out_len    = s_ring[tail].len;
    for (uint8_t i = 0; i < s_ring[tail].len; i++) {
        out_data[i] = s_ring[tail].data[i];
    }
    s_ring_tail = (uint16_t)((tail + 1U) & (CAN_RING_LEN - 1U));
    return true;
}

uint16_t can_ring_capacity(void)
{
    return (uint16_t)CAN_RING_LEN;
}

uint16_t can_ring_depth(void)
{
    return (uint16_t)((s_ring_head - s_ring_tail) & (CAN_RING_LEN - 1U));
}

bool can_tx(uint32_t ext_id, const uint8_t *data, uint8_t len)
{
    if (!s_up || s_listen_only || (len > 8U)) {
        return false;
    }
    if (HAL_FDCAN_GetTxFifoFreeLevel(&s_fdcan) == 0U) {
        s_stats.tx_dropped++;
        return false;
    }
    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier            = ext_id;
    h.IdType                = FDCAN_EXTENDED_ID;
    h.TxFrameType           = FDCAN_DATA_FRAME;
    h.DataLength            = len;
    h.ErrorStateIndicator   = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch         = FDCAN_BRS_OFF;
    h.FDFormat              = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl    = FDCAN_NO_TX_EVENTS;
    h.MessageMarker         = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&s_fdcan, &h, (uint8_t *)data) != HAL_OK) {
        s_stats.tx_dropped++;
        return false;
    }
    s_stats.tx_frames++;
    return true;
}

void can_stats_poll(void)
{
    if (!s_up) {
        return;
    }
    FDCAN_ProtocolStatusTypeDef ps;
    FDCAN_ErrorCountersTypeDef  ec;
    HAL_FDCAN_GetProtocolStatus(&s_fdcan, &ps);
    HAL_FDCAN_GetErrorCounters(&s_fdcan, &ec);

    s_stats.last_error    = (uint8_t)ps.LastErrorCode;
    s_stats.bus_off       = (ps.BusOff != 0U);
    s_stats.error_passive = (ps.ErrorPassive != 0U);
    s_stats.error_warning = (ps.Warning != 0U);
    s_stats.tx_errors     = (uint8_t)ec.TxErrorCnt;
    s_stats.rx_errors     = (uint8_t)ec.RxErrorCnt;
    /* The peripheral's own count of errored frames since the last read; the
     * closest thing it offers to "frames the panel did not get". */
    s_stats.rx_lost       = ec.ErrorLogging;
}

const can_stats_t *can_stats(void)
{
    return &s_stats;
}

uint8_t can_profile_count(void)
{
    return (uint8_t)CAN_PROFILE_COUNT;
}

const char *can_profile_name(uint8_t profile)
{
    return (profile < CAN_PROFILE_COUNT) ? s_profiles[profile].name : "?";
}

uint8_t can_current_profile(void)
{
    return s_profile;
}

bool can_is_listen_only(void)
{
    return s_listen_only;
}

bool can_is_up(void)
{
    return s_up;
}

/* --- the pin sweep -------------------------------------------------------- */

void can_hunt_start(uint32_t dwell_ms)
{
    s_hunt_dwell_ms   = dwell_ms;
    s_hunt_index      = 0;
    s_hunt_state      = CAN_HUNT_RUNNING;
    s_hunt_started_ms = HAL_GetTick();
    s_hunt_rx_at_start = s_stats.rx_frames;
    (void)can_init(s_hunt_index, true);
}

can_hunt_state_t can_hunt_step(void)
{
    if (s_hunt_state != CAN_HUNT_RUNNING) {
        return s_hunt_state;
    }
    if (s_stats.rx_frames != s_hunt_rx_at_start) {
        s_hunt_state = CAN_HUNT_FOUND; /* frames on these pins: stop here */
        return s_hunt_state;
    }
    if ((HAL_GetTick() - s_hunt_started_ms) < s_hunt_dwell_ms) {
        return s_hunt_state;
    }
    s_hunt_index++;
    if (s_hunt_index >= CAN_PROFILE_COUNT) {
        s_hunt_state = CAN_HUNT_EXHAUSTED;
        return s_hunt_state;
    }
    s_hunt_started_ms  = HAL_GetTick();
    s_hunt_rx_at_start = s_stats.rx_frames;
    (void)can_init(s_hunt_index, true);
    return s_hunt_state;
}

void can_hunt_abort(void)
{
    s_hunt_state = CAN_HUNT_IDLE;
}

can_hunt_state_t can_hunt_state(void)
{
    return s_hunt_state;
}

uint8_t can_hunt_current(void)
{
    return s_hunt_index;
}
