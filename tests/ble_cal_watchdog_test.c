/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
static uint32_t ticks,last_feed,feeds,expired,max_slice;
static jmp_buf reset;
static void feed(void){assert(ticks-last_feed<8000u*24000u);last_feed=ticks;feeds++;}
static void delay(unsigned us){assert(us<=1000);if(us>max_slice)max_slice=us;ticks+=us*24u;}
#define FMCAL_TICKS() ticks
#define FMCAL_FEED() feed()
#define FMCAL_DELAY(n) delay(n)
#define FMCAL_EXPIRE() do{expired++;longjmp(reset,1);}while(0)
#include "../firmware/calibration/watchdog.h"
static volatile fmcal_trace_t trace;
int main(void)
{
    uint32_t start=0;
    /* Millisecond ADC sampling lasts longer than one 8 s WDT period. No IRQ. */
    for(unsigned step=0;step<12u*600u+12u*200u;step++)fmcal_delay_service(&trace,start,1000,0x12345678u);
    assert(ticks==9600u*24000u&&feeds==9600&&trace.caller==0x12345678u&&!expired);
    /* One oversized SDK delay is sliced; it cannot skip feeding or the cap. */
    fmcal_delay_service(&trace,start,10000000u,4);
    assert(max_slice==1000&&trace.last_delay_us==10000000u&&!expired);
    ticks=last_feed=UINT32_MAX-9999u;start=ticks;trace.stage=FMCAL_RF;
    if(!setjmp(reset)){
        fmcal_delay_service(&trace,start,(FMCAL_CAP_MS+1000u)*1000u,0x9988u);
        assert(0&&"hard cap must reset");
    }
    assert(expired==1&&trace.stage==FMCAL_LIMIT&&trace.elapsed_ms==FMCAL_CAP_MS);
    assert(trace.caller==0x9988&&trace.last_delay_us==(FMCAL_CAP_MS+1000u)*1000u);
    uint32_t fed=feeds;ticks+=8000u*24000u;
    assert(feeds==fed); /* expiry loop never feeds, even across TIMER4 wrap */
    fmcal_retained_t retained={0};retained.data.magic=FMCAL_TRACE_MAGIC;
    retained.data.version=FMCAL_TRACE_VERSION;retained.data.cap_ms=FMCAL_CAP_MS;
    retained.data.stage=FMCAL_RF;retained.data.attempt=1;
    retained.guard=fmcal_trace_guard(&retained.data);assert(fmcal_trace_valid(&retained));
    retained.data.delay_calls=600;retained.data.elapsed_ms=600;
    assert(fmcal_trace_valid(&retained)); /* progress survives a reset at any sample */
    retained.data.stage=FMCAL_LIMIT;retained.data.elapsed_ms=FMCAL_CAP_MS+7000u;
    assert(fmcal_trace_valid(&retained)); /* delayed cap check must keep its evidence */
    retained.data.resets=0x44424732;assert(!fmcal_trace_valid(&retained));
}
