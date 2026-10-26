/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#include <stdint.h>
#define memcpy bridge_memcpy
#define memset bridge_memset
#define printf bridge_printf
#define puts bridge_puts
#define free bridge_free
#include "../firmware/calibration/bridge.c"
void *bridge_memcpy(void *d,const void *s,unsigned n){uint8_t *a=d;const uint8_t *b=s;while(n--)*a++=*b++;return d;}
void *bridge_memset(void *d,int c,unsigned n){uint8_t *a=d;while(n--)*a++=c;return d;}
static unsigned last_us;static uintptr_t last_caller;
void fmcal_delay_at(unsigned n,uintptr_t caller){last_us=n;last_caller=caller;}
static unsigned callbacks;
static void finish_callback(void *p){assert(p==&callbacks);callbacks++;}
int main(void)
{
    fmcal_reset();uint8_t b[68];wifi_get_xosc(b);assert(b[0]==11&&b[1]==11);
    delay_us(1000);assert(last_us==1000&&last_caller);
    wifi_get_pa_trim_data(b);assert(b[0]==1&&b[4]==11&&b[6]==7);
    wifi_get_mcs_dgain(b);for(unsigned k=0;k<20;k++)assert(b[k]==0);
    assert(wifi_get_rf_trim_data(b,68)==-1); /* always forces live calibration */
    assert(wifi_set_rf_trim_data(b,67)==-1&&!fmcal_written);
    for(unsigned k=0;k<68;k++)b[k]=(uint8_t)k;
    assert(!wifi_set_rf_trim_data(b,68)&&fmcal_written==1);
    for(unsigned k=0;k<68;k++)assert(fmcal_trims[29+k]==k);
    assert(CRC16("123456789",9)==0x31c3);
    uint8_t *p=zalloc(32767);assert(p&&!fmcal_alloc_failed);
    for(unsigned k=0;k<32767;k++)assert(!p[k]);
    assert(!zalloc(4)&&fmcal_alloc_failed);
    fmcal_reset();assert(!fmcal_written&&!fmcal_alloc_failed&&zalloc(4));
    assert(fmcal_finish()==-1);
    assert(sys_timeout_add_to_task("app_core",&callbacks,finish_callback,1000)==1);
    assert(!callbacks&&!fmcal_finish()&&callbacks==1&&last_us==1000000u);
    assert(fmcal_finish()==-1&&callbacks==1);
    assert(!sys_timeout_add_to_task("app_core",0,finish_callback,2001));
    fmcal_reset();assert(sys_timeout_add_to_task("app_core",&callbacks,finish_callback,1000)==1);
    assert(!sys_timeout_add_to_task("app_core",&callbacks,finish_callback,1000));
    assert(fmcal_finish()==-1&&callbacks==1);
}
