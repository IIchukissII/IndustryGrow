/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Linker script for STM32F405RGT6: 1024 KB flash, 128 KB SRAM (SRAM1+SRAM2),
 * plus 64 KB CCM (not used here). Conventional ST/GCC layout.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* top of stack */
_Min_Heap_Size  = 0x400;               /* 1 KB */
_Min_Stack_Size = 0x800;               /* 2 KB */

/* The last 128 KB sector (sector 11) is NOT part of FLASH: it is the Node-ID
 * store of ADR-0027 d2, written by the running node and read at boot. Keeping
 * it out of the application region is what makes d4's rule -- an update must
 * not erase the sector -- a property of the image rather than of the operator:
 * a programmer writing only the sectors the image covers cannot reach it.
 * common/carrier/identity.h states the same range in C and is checked against
 * _identity_start at init. */
MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 896K
  IDENTITY (r)   : ORIGIN = 0x080E0000, LENGTH = 128K
  RAM      (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    /* .RamFunc rides the .data copy into SRAM. The flash driver needs it: the
     * controller stalls every flash read for the whole of an erase, so the
     * routine that polls BSY and reloads the watchdog cannot itself be in
     * flash (common/platform/flash.c). */
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* No output section is placed in IDENTITY: the sector holds runtime-written
   * data, and emitting anything there would put 128 KB of padding into
   * igrow.bin. The symbol is the address only. */
  PROVIDE(_identity_start = ORIGIN(IDENTITY));

  /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
