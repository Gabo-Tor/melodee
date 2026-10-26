/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
ENTRY(_start)
MEMORY {
 XIP (rx) : ORIGIN = 0x02001000, LENGTH = 0x8C000
 RAM (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x6A000
 TRACE (rw) : ORIGIN = 0x01C7C040, LENGTH = 0xC0
}
SECTIONS {
 .boot : { KEEP(*(.boot)) . = ALIGN(4); } > XIP
 .ram : AT(LOADADDR(.boot)+SIZEOF(.boot)) {
  _ram_start = .; *(.text*) *(.ram_text*) *(.volatile_ram*) *(.rf*)
  *(.rodata*) *(.data*) *(.sdata*) . = ALIGN(4); _ram_end = .;
 } > RAM
 _ram_load = LOADADDR(.ram);
 .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(.pool*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
 .noinit (NOLOAD) : { KEEP(*(.noinit.fmcal)) } > TRACE
 /DISCARD/ : { *(.comment) *(.note*) *(.debug*) *(.eh_frame*) }
 ASSERT(_bss_end <= 0x01C72000, "calibration helper overlaps stacks")
}
