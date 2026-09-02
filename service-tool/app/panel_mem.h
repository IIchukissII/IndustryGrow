/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hand-partitioned external SDRAM. The board carries 32 MB at 0xD0000000
 * (SDRAM_DEVICE_ADDR / SDRAM_DEVICE_SIZE, BSP). The low 9 MB are assigned here
 * by address because two consumers -- the LTDC and LVGL -- need fixed addresses
 * given to them at init, not linker symbols. Everything above 0xD0900000 is the
 * linker's, as the SDRAM region of ldscripts/STM32H753XIHX_FLASH.ld.
 *
 * Change one number here and you must change the linker script to match.
 */
#ifndef PANEL_MEM_H
#define PANEL_MEM_H

#include "eval_board.h"

/* LTDC layer 0 framebuffer, at the BSP's own default (LCD_LAYER_0_ADDRESS in
 * the board conf header). The 4 MB reserved for it holds either panel at
 * RGB565: 640 x 480 x 2 = 614400 B, 800 x 480 x 2 = 768000 B. */
#define PANEL_FB0_ADDR        0xD0000000U

/* LVGL partial-render draw buffers, double-buffered, 256 KB apart. 120 lines
 * is a quarter of the screen on either panel: 640 x 120 x 2 = 153600 B,
 * 800 x 120 x 2 = 192000 B. */
#define PANEL_DRAWBUF_LINES   120U
#define PANEL_DRAWBUF_1_ADDR  0xD0400000U
#define PANEL_DRAWBUF_2_ADDR  0xD0440000U
_Static_assert((IGROW_PANEL_W * IGROW_PANEL_H * 2U) <=
                   (PANEL_DRAWBUF_1_ADDR - PANEL_FB0_ADDR),
               "the framebuffer runs into the LVGL draw buffers");
_Static_assert((IGROW_PANEL_W * PANEL_DRAWBUF_LINES * 2U) <=
                   (PANEL_DRAWBUF_2_ADDR - PANEL_DRAWBUF_1_ADDR),
               "the draw buffers overlap");

/* LVGL's own heap (LV_MEM_ADR in lv_conf.h must equal this). */
#define PANEL_LVGL_HEAP_ADDR  0xD0480000U
#define PANEL_LVGL_HEAP_SIZE  (4U * 1024U * 1024U)
_Static_assert((PANEL_LVGL_HEAP_ADDR + PANEL_LVGL_HEAP_SIZE) <= 0xD0900000U,
               "the LVGL heap runs into the linker-managed SDRAM region");

/* Linker-managed SDRAM starts here -- keep in step with the .sdram_bss region. */
/* First address the linker owns. Must sit above the LVGL heap's end
 * (PANEL_LVGL_HEAP_ADDR + PANEL_LVGL_HEAP_SIZE = 0xD0880000). */
#define PANEL_LINKER_SDRAM    0xD0900000U

#define PANEL_SDRAM_BSS       __attribute__((section(".sdram_bss")))
#define PANEL_AXI_BSS         __attribute__((section(".axi_bss")))

#endif /* PANEL_MEM_H */
