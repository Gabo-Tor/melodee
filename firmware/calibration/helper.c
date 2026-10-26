/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Uses unchanged Melodee/Felucca USB, display, OTA and system sources; their
 * original notices remain in the included files. */
#include <stdint.h>
#define MELODEE_LOADER 1
#define MELODEE_CDC 0
#define MELODEE_USB_AUDIO 0
#define MELODEE_USB_HOST 0
#define MELODEE_OTA 1
#define MELODEE_ID "FM-1_998"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_core1_stop.h"
#include "fm1_lcd_hw.h"
/* Calibration cannot write data. The ONLY writable region is updater staging. */
#define FL_RANGE_OK(off,n) FL_IN(off,n,0xE0000u,0xE5000u)
#include "fm1_flash.h"
#include "fm1_rf_calibration.h"
#include "../src/libc.c"
#include "../src/lcd.c"
#include "../src/gfx.c"
static uint32_t ty_pixel(void){return 1;}
#include "../src/usb.c"
#define BLE_API static
#include "../src/ble/ble_vm.c"
#define FMCAL_TICKS() fm1_ticks()
#define FMCAL_FEED() fm1_wdt_feed()
#define FMCAL_DELAY(n) fm1_delay_us(n)
#define FMCAL_EXPIRE() do{fm1_code_sync();for(;;){}}while(0)
#include "watchdog.h"
extern uint8_t fmcal_trims[97];
extern unsigned fmcal_written,fmcal_alloc_failed;
extern void fmcal_reset(void),wf_rf_init(void),wf_phy_init(void),
    wf_phy_mac_rst(void),wf_sifs_reset(void),cfr_int(unsigned);
extern void sdio_mac_wreg(unsigned,unsigned);
extern uint16_t CRC16(const void *,unsigned);
extern int fmcal_finish(void);
static uint8_t state,copy[100];
/* Outside cleared/copied memory and both stacks; survives a watchdog reset.
 * The boot stub owns 0x1C7C000..1F; this trace starts at 0x1C7C040. */
static volatile fmcal_retained_t trace_store __attribute__((section(".noinit.fmcal")));
#define trace (trace_store.data)
static uint32_t run_started;
static uint32_t ms(void){return fm1_micros()/1000;}
static void poll(void){fm1_wdt_feed();usb_poll();}
static void stage(uint32_t s)
{
    uint32_t now=fm1_ticks();trace.elapsed_ms=(now-run_started)/24000u;
    if(!fmcal_feed_allowed(run_started,now)){trace.stage=FMCAL_LIMIT;FMCAL_EXPIRE();}
    trace.stage=s;
}
void fmcal_delay_at(unsigned n,uintptr_t caller)
{fmcal_delay_service(&trace,run_started,n,caller);}
static void show(const char *text)
{
    lcd_fill(0,0,240,240,0x0841);
    draw_text_box(0,14,240,TY_TX,"BLE RECOVERY",0xffff,1);
    draw_text_box(0,94,240,TY_TX,text,0x07ff,1);
    draw_text_box(0,182,240,TY_LB,"KEEP USB CONNECTED",0xffff,1);
    lcd_sync();
}
static uint32_t ota_now_ms(void){return ms();}
static void ota_idle(void){poll();}
#include "ota_flash.h"
static void ota_show(uint32_t step,int32_t code){(void)code;show(step==9?"UPDATE FAILED":"RETURNING TO MELODEE");}
static void ota_commit(const uint8_t *p){usb_detach();fm1_delay_ms(30);fm1_enter_update(p);}
#include "../src/ota.c"
static void run(void)
{
    if(state)return; /* one attempt per boot; a timeout leaves USB update usable */
    state=1;show("CALIBRATING");fmcal_reset();run_started=fm1_ticks();
    trace.attempt++;trace.delay_calls=trace.feeds=trace.elapsed_ms=0;
    trace.last_delay_us=trace.caller=0;trace.cap_ms=FMCAL_CAP_MS;
    trace_store.guard=fmcal_trace_guard(&trace);
    stage(FMCAL_PREPARE);
    fm1_wdt_feed();fmcal_hw_prepare();
    stage(FMCAL_MAC);wf_phy_mac_rst();
    stage(FMCAL_SIFS);wf_sifs_reset();
    sdio_mac_wreg(0x308u,0x9407u); /* stock V15 / Open bt_rf_init +0x176..180 */
    wf_phy_mac_rst();
    stage(FMCAL_CFR);cfr_int(0);
    stage(FMCAL_PHY);wf_phy_init();
    stage(FMCAL_RF);wf_rf_init();
    stage(FMCAL_FINALIZE);int finished=fmcal_finish();
    stage(FMCAL_STOP);fmcal_hw_stop();
    stage(FMCAL_VALIDATE);
    if(!finished && fmcal_written==1 && !fmcal_alloc_failed){
        copy[0]=0xad;memcpy(copy+1,fmcal_trims,97);
        uint16_t crc=CRC16(copy,98);copy[98]=crc;copy[99]=crc>>8;
        state=ble_rf_copy_ok(copy)?2:3;
    }else state=3;
    stage(state==2?FMCAL_DONE:FMCAL_FAILED);
    show(state==2?"CALIBRATION READY":"CALIBRATION FAILED");
}
static void command(void)
{
    if(!sx_ready)return;
    uint32_t n=sx_frame_len;
    if(n<5||sx_frame[0]!=0x7d||sx_frame[1]!=0x46||sx_frame[2]!=0x4c||sx_frame[3]!=82)return;
    uint8_t action=sx_frame[4],rc=0;
    if(n!=5||action>4)rc=1;
    else if(action==1)run();
    else if(action==2 && state!=2)rc=2;
    else if(action==4){state=0;trace.stage=0;} /* explicit retry after a reset */
    uint8_t out[124]={0xf0,0x7d,0x46,0x4c,82,rc,1,state};unsigned k=8;
    if(!rc && action==2){
        for(unsigned i=0;i<100;i+=7){unsigned m=k++;out[m]=0;
            for(unsigned j=0;j<7&&i+j<100;j++){out[m]|=(copy[i+j]>>7)<<j;out[k++]=copy[i+j]&127;}}
    }
    if(!rc && action==3){
        /* Explicit LE serialisation; never a flash or RAM read supplied by host. */
        const volatile uint32_t *words=(const volatile uint32_t *)&trace;
        uint8_t raw[sizeof trace];
        for(unsigned i=0;i<sizeof trace/4;i++)for(unsigned j=0;j<4;j++)raw[i*4+j]=(uint8_t)(words[i]>>(8*j));
        for(unsigned i=0;i<sizeof raw;i+=7){unsigned m=k++;out[m]=0;
            for(unsigned j=0;j<7&&i+j<sizeof raw;j++){out[m]|=(raw[i+j]>>7)<<j;out[k++]=raw[i+j]&127;}}
    }
    out[k++]=0xf7;ota_wire_send(out,k);ota_frame_done();
}
void fmcal_main(void)
{
    fm1_time_init();fm1_reset_reason();fm1_wdt_arm(0x0d);fm1_core1_stop();fm1_updata_parm_clear();
    if(!fmcal_trace_valid(&trace_store)){
        volatile uint32_t *p=(volatile uint32_t *)&trace_store;
        for(unsigned i=0;i<sizeof trace_store/4;i++)p[i]=0;
        trace.version=FMCAL_TRACE_VERSION;trace.magic=FMCAL_TRACE_MAGIC;trace.cap_ms=FMCAL_CAP_MS;
    }else trace.resets++;
    trace_store.guard=fmcal_trace_guard(&trace);
    trace.reset_p3=fm1_boot.p3_rst;trace.reset_src=fm1_boot.rst_src;
    if((trace.stage>=FMCAL_PREPARE&&trace.stage<FMCAL_DONE)||trace.stage==FMCAL_LIMIT||trace.stage==FMCAL_FINALIZE)state=3;
    lcd_init();show(state==3?"PREVIOUS RUN RESET":"READY");usb_start();
    for(;;){
        poll();usb_retry(ms());command();ota_service();
        if(usb.uboot_req){usb_detach();fm1_delay_ms(30);fm1_enter_uboot();}
        if(usb.ota_req){usb.ota_req=0;ota_session();}
    }
}
