/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The calibration helper's flash side of ota.c, over the RAM driver (fm1_flash.h) and the helper's
 * FL_RANGE_OK (updater staging only). tests/ota_test.c -DOTA_HELPER_HOOKS=1 runs ota.c through these. */
static int ota_erase(uint32_t off){uint32_t took;return fl_erase4k_ram(off,&took);}
/* ota.c programs 512-B reads; fl_prog_ram takes one NOR page (<= 256 B, no wrap) and refuses more.
 * Interrupts stay off in the helper, so no fl_write (it re-enables them between pages). */
static int ota_prog(uint32_t off,const void *p,uint32_t n)
{
    const uint8_t *s=p;uint32_t took;
    while(n){
        uint32_t k=256u-(off&0xFFu);int rc;
        if(k>n)k=n;
        if((rc=fl_prog_ram(off,s,k,&took))!=0)return rc;
        off+=k;s+=k;n-=k;
    }
    return 0;
}
static int ota_fread(uint32_t off,void *p,uint32_t n){return fl_read_ram(off,p,n);}
