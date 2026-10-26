/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#pragma once
#include <stdint.h>
/* TIMER4 is crystal /1, independent of IRQs. The run is limited to 60 s;
 * after that delay service stops feeding and the ~8 s hardware WDT resets.
 * Host waits 80 s, including the cap, WDT expiry and USB enumeration margin. */
#define FMCAL_CAP_MS 60000u
#define FMCAL_TRACE_MAGIC 0x52464342u
#define FMCAL_TRACE_VERSION 1u
enum { FMCAL_PREPARE=1,FMCAL_MAC,FMCAL_SIFS,FMCAL_CFR,FMCAL_PHY,
       FMCAL_RF,FMCAL_STOP,FMCAL_VALIDATE,FMCAL_DONE,FMCAL_FAILED,FMCAL_LIMIT,FMCAL_FINALIZE };
typedef struct {
    uint32_t magic,version,stage,elapsed_ms,delay_calls,last_delay_us,
             caller,feeds,resets,reset_p3,reset_src,cap_ms,attempt;
} fmcal_trace_t;
typedef struct {fmcal_trace_t data;uint32_t guard;} fmcal_retained_t;
/* Other firmware can reuse .noinit between helper installs. A guarded stable
 * header distinguishes that from SRAM retained by this helper's WDT reset.
 * Progress fields remain single volatile word stores, with no CRC commit gap. */
static uint32_t fmcal_trace_guard(const volatile fmcal_trace_t *t)
{return t->magic^(t->version*0x9e3779b9u)^(t->attempt*0x85ebca6bu)^t->resets^t->cap_ms;}
static int fmcal_trace_valid(const volatile fmcal_retained_t *r)
{
    const volatile fmcal_trace_t *t=&r->data;
    return t->magic==FMCAL_TRACE_MAGIC&&t->version==FMCAL_TRACE_VERSION&&
        t->cap_ms==FMCAL_CAP_MS&&t->stage<=FMCAL_FINALIZE&&
        r->guard==fmcal_trace_guard(t);
}
static int fmcal_feed_allowed(uint32_t started,uint32_t now)
{return (uint32_t)(now-started)<FMCAL_CAP_MS*24000u;}

/* Hooks are polled hardware services, independent of IRQs. The host fixture
 * uses this same loop with a fake crystal and a simulated watchdog reset. */
#ifdef FMCAL_TICKS
static void fmcal_delay_service(volatile fmcal_trace_t *t,uint32_t started,
                                unsigned n,uintptr_t caller)
{
    t->delay_calls++;t->last_delay_us=n;t->caller=(uint32_t)caller;
    do {
        uint32_t now=FMCAL_TICKS();t->elapsed_ms=(now-started)/24000u;
        if(!fmcal_feed_allowed(started,now)){
            t->stage=FMCAL_LIMIT;FMCAL_EXPIRE();return;
        }
        FMCAL_FEED();t->feeds++;
        unsigned slice=n>1000u?1000u:n;FMCAL_DELAY(slice);n-=slice;
    }while(n);
}
#endif
