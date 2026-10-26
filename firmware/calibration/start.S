/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
    .section .boot,"ax",@progbits
    .global _start
_start:
    cli
    sp = 0x01c74000
    ssp = 0x01c78000
    r0 = _ram_load
    r1 = _ram_start
    r2 = _ram_end
1:  r3 = [r0++=4]
    [r1++=4] = r3
    if (r1 < r2) goto 1b
    r1 = _bss_start
    r2 = _bss_end
    r3 = 0
2:  [r1++=4] = r3
    if (r1 < r2) goto 2b
    csync
    r0 = fmcal_main
    call r0
    goto .
