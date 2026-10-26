/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* JieLi AC79 SDK calibration adapter. No VM or flash implementation is linked.
 * Missing factory settings use the SDK's documented board defaults; this is
 * first-boot RF calibration, not factory production alignment. */
#include <stdint.h>
extern void *memcpy(void *,const void *,unsigned);
extern void *memset(void *,int,unsigned);
extern void fmcal_delay_at(unsigned,uintptr_t);
uint8_t fmcal_trims[97];
unsigned fmcal_written, fmcal_alloc_failed;
static uint8_t heap[32768];
static unsigned used;
static void (*pending_callback)(void *);
static void *pending_argument;
static unsigned pending_ms,timer_failed;
void fmcal_reset(void)
{
    static const uint8_t base[29]={11,11,1,7,4,7,11,1,7,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    memcpy(fmcal_trims,base,29);memset(fmcal_trims+29,0,68);
    used=fmcal_written=fmcal_alloc_failed=0;
    pending_callback=0;pending_argument=0;pending_ms=timer_failed=0;
}
__attribute__((noinline)) void delay_us(unsigned n)
{fmcal_delay_at(n,(uintptr_t)__builtin_return_address(0));}
unsigned clk_get(const char *name){(void)name;return 24000000;}
void wifi_get_xosc(uint8_t *p){memcpy(p,fmcal_trims,2);}
void wifi_get_mcs_dgain(uint8_t *p){memcpy(p,fmcal_trims+9,20);}
int wifi_get_pa_trim_data(uint8_t *p){memcpy(p,fmcal_trims+2,7);return 1;}
int wifi_get_rf_trim_data(void *p,int n){(void)p;(void)n;return -1;}
int wifi_set_rf_trim_data(const void *p,int n)
{if(n!=68)return -1;memcpy(fmcal_trims+29,p,68);fmcal_written++;return 0;}
int printf(const char *format,...){(void)format;return 0;}
int puts(const char *text){(void)text;return 0;}
uint16_t CRC16(const void *p,unsigned n)
{
    const uint8_t *b=p;uint32_t c=0;
    while(n--){c^=(uint32_t)*b++<<8;for(unsigned k=0;k<8;k++)c=(c<<1)^((c&0x8000)?0x1021:0);}
    return (uint16_t)c;
}
void *zalloc(unsigned n)
{if(n>sizeof heap-used){fmcal_alloc_failed=1;return 0;}void *p=heap+used;used+=(n+3u)&~3u;memset(p,0,n);return p;}
void free(void *p){(void)p;}
const uint8_t RFIinitUseTrimValue=1;
/* SDK wifi_conf.c receiver thresholds (the radio's first BBP table load). */
const unsigned STF_END_CNT_THR_VAL=190, STF_DET_CNT_THR_VAL=12,
    DS_DET_CNT_THR0_VAL=4, STF_DET_THR1_VAL=190, STF_DET_THR2_VAL=150,
    STF_DET_THR3_VAL=140, DSSS_XCDET_THR1_VAL=80, DSSS_XCDET_THR2_VAL=100,
    DS_VLD_CNT_THR_VAL=80, DS_DET_CNT_THR1_VAL=4;
/* tx_rx_bw_set_save defers the CRC/cache callback by one second. Keep that
 * callback and drain it synchronously after wf_rf_init, without an RTOS/IRQ.
 * This is the RF cache finalisation, not a temperature-monitor timer. */
int sys_timeout_add_to_task(const char *task,void *p,void (*f)(void *),unsigned ms)
{
    (void)task;
    if(!f||pending_callback||ms>2000u){timer_failed=1;return 0;}
    pending_callback=f;pending_argument=p;pending_ms=ms;return 1;
}
int fmcal_finish(void)
{
    if(timer_failed||!pending_callback)return -1;
    void (*callback)(void *)=pending_callback;void *argument=pending_argument;
    fmcal_delay_at(pending_ms*1000u,(uintptr_t)callback);
    pending_callback=0;pending_argument=0;
    callback(argument);
    return timer_failed?-1:0;
}
