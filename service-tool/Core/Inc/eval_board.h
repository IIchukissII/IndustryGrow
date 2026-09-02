/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Which eval board this image is built for.
 *
 * Both boards are the MB1246 motherboard, so the BSP APIs are identical --
 * same LED enum, same buttons, same POT, same COM1 on USART1/PB14/PB15. What
 * differs is the MCU and the LCD daughterboard, and the two BSPs are named
 * after their MCU. This header is the one place that knows which:
 *
 *   IGROW_BOARD_H753   STM32H753I-EVAL, single core, MB1063 (LTDC RGB, EXC7200)
 *   IGROW_BOARD_H757   STM32H757I-EVAL, dual core,   MB1166 (DSI, OTM8009A, FT6x06)
 *
 * Include this instead of a board-specific BSP header, and code that only uses
 * the common API builds for either board unchanged.
 */
#ifndef IGROW_EVAL_BOARD_H
#define IGROW_EVAL_BOARD_H

#if defined(IGROW_BOARD_H757)

#define IGROW_BOARD_NAME "STM32H757I-EVAL"
#define IGROW_LCD_NAME   "MB1166"
/* 0x54, or 0x70 on the A02 revision of the daughterboard. */
#define IGROW_TS_NAME    "FT6x06 at 0x54/0x70"

#include "stm32h747i_eval.h"
#include "stm32h747i_eval_bus.h"
#include "stm32h747i_eval_io.h"
#include "stm32h747i_eval_lcd.h"
#include "stm32h747i_eval_qspi.h"
#include "stm32h747i_eval_sd.h"
#include "stm32h747i_eval_sdram.h"
#include "stm32h747i_eval_ts.h"

/* MB1166: OTM8009A 800x480 over DSI, two lanes, with an FT6x06 touch
 * controller. The A02 revision of the daughterboard answers at 0x70 rather
 * than 0x54, and BSP_TS_Init tries both. */
#define IGROW_PANEL_W 800U
#define IGROW_PANEL_H 480U

/* The H743I-EVAL BSP publishes this name for layer 0's framebuffer and the
 * H747I-EVAL one does not, though both take the address from the same conf
 * header constant. */
#define LCD_FB_START_ADDRESS LCD_LAYER_0_ADDRESS

/* THE CORE SUPPLY IS NOT THE SAME ON THE TWO BOARDS, AND THE WRONG ONE BROWNS
 * THE CORE OUT UNTIL THE NEXT POWER CYCLE.
 *
 * CR3 bit 2 is SMPSEN on the H747 and SCUEN on the H753 -- same bit, different
 * meaning. This part has an SMPS and the board feeds VCORE from it, which is
 * why asking for LDO-only here (observed, key `3` of the bare-metal image)
 * switches off the supply that is powering the core: SWD then reports
 * "No STM32 target found" and NRST will not recover it, because the supply
 * configuration is write-once until a power-on reset.
 *
 * PWR_DIRECT_SMPS_SUPPLY keeps the SMPS and drops the LDO the board does not
 * use. PWR_EXTERNAL_SOURCE_SUPPLY is never correct on either board. */
#define IGROW_SUPPLY      PWR_DIRECT_SMPS_SUPPLY
#define IGROW_SUPPLY_NAME "direct SMPS"

#else

#define IGROW_BOARD_NAME "STM32H753I-EVAL"
#define IGROW_LCD_NAME   "MB1063"
#define IGROW_TS_NAME    "EXC7200 at 0x08"

#include "stm32h743i_eval.h"
#include "stm32h743i_eval_bus.h"
#include "stm32h743i_eval_io.h"
#include "stm32h743i_eval_lcd.h"
#include "stm32h743i_eval_qspi.h"
#include "stm32h743i_eval_sd.h"
#include "stm32h743i_eval_sdram.h"
#include "stm32h743i_eval_ts.h"

/* MB1063: AMPIRE 640x480 over LTDC parallel RGB, EXC7200 touch at 0x08. */
#define IGROW_PANEL_W 640U
#define IGROW_PANEL_H 480U

/* The STM32H753 has no SMPS at all, so the LDO is the sole core supply and
 * this asks for what is already feeding it. */
#define IGROW_SUPPLY      PWR_LDO_SUPPLY
#define IGROW_SUPPLY_NAME "LDO"

#endif

#endif /* IGROW_EVAL_BOARD_H */
