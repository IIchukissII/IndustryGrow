/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "can.h"
#include "board.h"

/* Bit-timing fields for 500 kbit/s at APB1 = 42 MHz (see can.h). */
#define CAN_BRP 6u
#define CAN_BS1 11u
#define CAN_BS2 2u
#define CAN_SJW 1u

static void can_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    const uint32_t rx = BRD_CAN_RX_PIN, tx = BRD_CAN_TX_PIN; /* 8, 9 */
    BRD_CAN_GPIO->MODER &= ~((3u << (rx * 2u)) | (3u << (tx * 2u)));
    BRD_CAN_GPIO->MODER |= (2u << (rx * 2u)) | (2u << (tx * 2u)); /* AF */
    /* High speed, push-pull; pull-up on RX is harmless and helps when idle. */
    BRD_CAN_GPIO->OSPEEDR |= (3u << (rx * 2u)) | (3u << (tx * 2u));
    BRD_CAN_GPIO->AFR[1] &= ~((0xFu << ((rx - 8u) * 4u)) | (0xFu << ((tx - 8u) * 4u)));
    BRD_CAN_GPIO->AFR[1] |= (BRD_CAN_AF << ((rx - 8u) * 4u)) |
                            (BRD_CAN_AF << ((tx - 8u) * 4u));
}

int can_init(bool loopback)
{
    can_gpio_init();

    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;

    /* Enter initialization mode. */
    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    uint32_t guard = 1000000u;
    while (!(CAN1->MSR & CAN_MSR_INAK) && --guard) {
    }
    if (guard == 0u) {
        return 1;
    }

    /* Automatic bus-off recovery; no time-triggered / auto-retransmit changes. */
    CAN1->MCR |= CAN_MCR_ABOM;

    CAN1->BTR = ((CAN_SJW - 1u) << CAN_BTR_SJW_Pos) |
                ((CAN_BS1 - 1u) << CAN_BTR_TS1_Pos) |
                ((CAN_BS2 - 1u) << CAN_BTR_TS2_Pos) |
                ((CAN_BRP - 1u) << CAN_BTR_BRP_Pos) |
                (loopback ? (CAN_BTR_LBKM | CAN_BTR_SILM) : 0u);

    /* Filter 0: 32-bit mask mode, accept all, route to FIFO0. */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1u;
    CAN1->FS1R |= 1u;  /* single 32-bit filter */
    CAN1->FM1R &= ~1u; /* mask mode */
    CAN1->sFilterRegister[0].FR1 = 0u;
    CAN1->sFilterRegister[0].FR2 = 0u; /* mask 0 -> all bits don't-care */
    CAN1->FFA1R &= ~1u;                /* filter 0 -> FIFO0 */
    CAN1->FA1R |= 1u;                  /* activate */
    CAN1->FMR &= ~CAN_FMR_FINIT;

    /* Leave initialization mode. */
    CAN1->MCR &= ~CAN_MCR_INRQ;
    guard = 1000000u;
    while ((CAN1->MSR & CAN_MSR_INAK) && --guard) {
    }
    return (guard == 0u) ? 2 : 0;
}

int can_send(uint16_t id, const uint8_t *data, uint8_t len)
{
    if (len > 8u) {
        return -1;
    }
    /* Use mailbox 0 if free. */
    if (!(CAN1->TSR & CAN_TSR_TME0)) {
        return -2;
    }
    uint32_t dl = 0u, dh = 0u;
    for (uint8_t i = 0u; i < len; i++) {
        if (i < 4u) {
            dl |= (uint32_t)data[i] << (8u * i);
        } else {
            dh |= (uint32_t)data[i] << (8u * (i - 4u));
        }
    }
    CAN1->sTxMailBox[0].TDTR = len; /* DLC */
    CAN1->sTxMailBox[0].TDLR = dl;
    CAN1->sTxMailBox[0].TDHR = dh;
    CAN1->sTxMailBox[0].TIR = ((uint32_t)id << 21u) | CAN_TI0R_TXRQ; /* STID, IDE/RTR=0 */
    return 0;
}

int can_recv(uint16_t *id, uint8_t *data, uint8_t *len)
{
    if (!(CAN1->RF0R & CAN_RF0R_FMP0)) {
        return 0;
    }
    uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
    uint32_t rdt = CAN1->sFIFOMailBox[0].RDTR;
    uint32_t dl = CAN1->sFIFOMailBox[0].RDLR;
    uint32_t dh = CAN1->sFIFOMailBox[0].RDHR;

    if (id) {
        *id = (uint16_t)(rir >> 21u);
    }
    uint8_t n = (uint8_t)(rdt & 0xFu);
    if (n > 8u) {
        n = 8u;
    }
    if (len) {
        *len = n;
    }
    if (data) {
        for (uint8_t i = 0u; i < n; i++) {
            data[i] = (uint8_t)((i < 4u) ? (dl >> (8u * i)) : (dh >> (8u * (i - 4u))));
        }
    }
    CAN1->RF0R |= CAN_RF0R_RFOM0; /* release the FIFO entry */
    return 1;
}

int can_send_ext(uint32_t ext_id, const uint8_t *data, uint8_t len)
{
    if (len > 8u) {
        return -1;
    }
    if (!(CAN1->TSR & CAN_TSR_TME0)) {
        return -2; /* no free mailbox */
    }
    uint32_t dl = 0u, dh = 0u;
    for (uint8_t i = 0u; i < len; i++) {
        if (i < 4u) {
            dl |= (uint32_t)data[i] << (8u * i);
        } else {
            dh |= (uint32_t)data[i] << (8u * (i - 4u));
        }
    }
    CAN1->sTxMailBox[0].TDTR = len;
    CAN1->sTxMailBox[0].TDLR = dl;
    CAN1->sTxMailBox[0].TDHR = dh;
    /* EXID in bits[31:3], IDE=1, RTR=0, then request transmit. */
    CAN1->sTxMailBox[0].TIR = (ext_id << 3u) | CAN_TI0R_IDE | CAN_TI0R_TXRQ;
    return 0;
}

int can_recv_ext(uint32_t *ext_id, uint8_t *data, uint8_t *len)
{
    if (!(CAN1->RF0R & CAN_RF0R_FMP0)) {
        return 0;
    }
    uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
    uint32_t rdt = CAN1->sFIFOMailBox[0].RDTR;
    uint32_t dl = CAN1->sFIFOMailBox[0].RDLR;
    uint32_t dh = CAN1->sFIFOMailBox[0].RDHR;
    int is_ext = (rir & CAN_RI0R_IDE) ? 1 : 0;
    uint8_t n = (uint8_t)(rdt & 0xFu);
    if (n > 8u) {
        n = 8u;
    }
    if (is_ext) {
        if (ext_id) {
            *ext_id = rir >> 3u; /* 29-bit EXID */
        }
        if (len) {
            *len = n;
        }
        if (data) {
            for (uint8_t i = 0u; i < n; i++) {
                data[i] = (uint8_t)((i < 4u) ? (dl >> (8u * i)) : (dh >> (8u * (i - 4u))));
            }
        }
    }
    CAN1->RF0R |= CAN_RF0R_RFOM0; /* release entry regardless */
    return is_ext;                /* 0 = not an extended (Cyphal) frame */
}

int can_selftest_loopback(void)
{
    if (can_init(true) != 0) {
        return 1;
    }
    const uint8_t tx[2] = {0xA5u, 0x5Au};
    if (can_send(0x123u, tx, sizeof(tx)) != 0) {
        return 2;
    }
    uint32_t guard = 1000000u;
    while (!(CAN1->RF0R & CAN_RF0R_FMP0) && --guard) {
    }
    if (guard == 0u) {
        return 3;
    }
    uint16_t id = 0u;
    uint8_t rx[8] = {0};
    uint8_t len = 0u;
    if (can_recv(&id, rx, &len) != 1) {
        return 4;
    }
    if (id != 0x123u || len != 2u || rx[0] != tx[0] || rx[1] != tx[1]) {
        return 5;
    }
    return 0;
}
