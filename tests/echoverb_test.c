/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Opt-in firmware kernel, UI/arena integration and native PCM history. */
#define MELODEE_ECHOVERB_TEST 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
static struct {uint32_t before[16];fx_echoverb_t s;uint32_t after[16];} guarded;
static fx_state_t state;
static int32_t samples[CTL],saved[FS/2u];
static void reset_kernel(uint32_t time,uint32_t diffuse)
{
    memset(&guarded,0,sizeof guarded);memset(&state,0,sizeof state);
    for(uint32_t k=0;k<16u;k++)guarded.before[k]=guarded.after[k]=0xa5e4c391u;
    state.type=FX_REVERB;state.rate=time;state.character=diffuse;state.mix=32768;
}
static void guards(void)
{
    for(uint32_t k=0;k<16u;k++)assert(guarded.before[k]==0xa5e4c391u && guarded.after[k]==0xa5e4c391u);
    for(uint32_t k=0;k<4032u;k++)assert(isfinite(guarded.s.diffusion[k]));
}
static void native_history(void)
{
    reset_kernel(127,0);int32_t rng=51;
    for(uint32_t i=0;i<FS/2u;i++)saved[i]=2*((int32_t)(noise32(&rng)%20001u)-10000);
    for(uint32_t at=0;at<FS/2u;){
        uint32_t n=1u+(at%CTL);if(n>FS/2u-at)n=FS/2u-at;
        int32_t l[CTL],r[CTL];for(uint32_t i=0;i<n;i++)l[i]=r[i]=saved[at+i];
        fx_echoverb_run(&state,&guarded.s,n,32768,l,r);at+=n;
    }
    assert(guarded.s.delay==22050u && guarded.s.cursor==0u && !state.decim);
    for(uint32_t i=0;i<22050u;i++)assert(guarded.s.history[i]==saved[i]/2);
    assert((int32_t)(fx_echoverb_read(&guarded.s,22050u)*65536.0f)==saved[0]);guards();
    printf("Echoverb: native 44100 Hz mono, exact 500 ms uncompressed PCM history; arena %zu + slot %zu = %zu bytes\n",
           sizeof(fx_echoverb_t),sizeof(fx_state_t),sizeof(fx_echoverb_t)+sizeof(fx_state_t));
}
static void tails_and_controls(void)
{
    for(uint32_t time=0;time<=127u;time+=127u)for(uint32_t diffuse=0;diffuse<=128u;diffuse+=64u){
        reset_kernel(time,diffuse>127u?127u:diffuse);uint64_t early=0,late=0;
        for(uint32_t at=0;at<24u*FS;){
            uint32_t n=CTL;if(n>24u*FS-at)n=24u*FS-at;
            int32_t l[CTL]={0},r[CTL]={0};if(!at)l[0]=r[0]=24000;
            fx_echoverb_run(&state,&guarded.s,n,32768,l,r);
            for(uint32_t i=0;i<n;i++){
                assert(l[i]==r[i] && abs(l[i])<=262143);
                if(at+i<2u*FS)early+=abs(l[i]);if(at+i>23u*FS)late+=abs(l[i]);
            }
            at+=n;
        }
        assert(early>10000 && late<early/100u);guards();
        printf("Time %u, diffuse %u: early %llu, late %llu\n",time,diffuse,(unsigned long long)early,(unsigned long long)late);
    }
    reset_kernel(127,64);int32_t rng=53;
    for(uint32_t b=0;b<8u*FS/CTL;b++){
        int32_t l[CTL],r[CTL];for(uint32_t i=0;i<CTL;i++){l[i]=(int32_t)(noise32(&rng)%131073u)-65536;r[i]=-l[i]/2;}
        if(b%137u==0u){state.rate=(int16_t)((b/137u)&127u);state.character=127-state.rate;}
        fx_echoverb_run(&state,&guarded.s,CTL,32768,l,r);
        for(uint32_t i=0;i<CTL;i++)assert(abs(l[i])<=262143 && abs(r[i])<=262143);
    }
    guards();puts("Echoverb: both delay extremes, diffusion/feedback range, decaying tails and hot stereo input bounded");
}
static void integration(void)
{
    ui_power_on();spread_on=0;
    trk[0].p[P_FXTYPE]=FX_REVERB;trk[0].p[P_FXMIX]=80;
    trk[0].p[P_FXRATE]=127;trk[0].p[P_FXCHAR]=66;
    assert(str_eq(N_FX[FX_REVERB],"ECHOVERB"));
    assert(str_eq(track_desc(&trk[0],P_FXRATE)->label,"TIME"));
    assert(str_eq(track_desc(&trk[0],P_FXCHAR)->label,"DIFFUSE"));
    for(uint32_t i=0;i<CTL;i++)samples[i]=5000;
    assert(fx_process(&trk[0],samples,CTL));
    assert(resource[RES_FX0].size==sizeof(fx_echoverb_t));
    void *memory=resource[RES_FX0].ptr;
    assert(fx_process(&trk[0],samples,CTL) && resource[RES_FX0].ptr==memory);
    trk[0].p[P_FXMIX]=0;for(uint32_t b=0;b<20u;b++)fx_process(&trk[0],samples,CTL);
    assert(!resource[RES_FX0].size);
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    memset(fx_state,0,sizeof fx_state);
    assert(resource_get(RES_NOTE_HISTORY,RESOURCE_MAIN_CAPACITY-32768u));
    trk[0].p[P_FXMIX]=127;assert(!fx_process(&trk[0],samples,CTL));
    assert(fx_state[0].failed && !resource[RES_FX0].size);
    for(uint32_t i=0;i<CTL;i++)assert(samples[i]==5000);
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    fx_clear_all();assert(fx_process(&trk[0],samples,CTL) && !fx_state[0].failed);
    /* The send bus uses the same bounded native-rate kernel. */
    song.g[G_MFXTYPE]=FX_REVERB;song.g[G_MFXMIX]=127;song.g[G_FXRETURN]=127;
    song.g[G_MFXRATE]=127;song.g[G_MFXCHAR]=117;
    assert(str_eq(global_desc(G_MFXRATE)->label,"TIME"));
    assert(str_eq(global_desc(G_MFXCHAR)->label,"DIFFUSE"));
    for(uint32_t i=0;i<CTL;i++)fx_send_l[i]=fx_send_r[i]=5000;
    fx_bus_start(CTL);fx_bus_join();
    assert(resource[RES_FX0+NPART*FX_SLOTS].size==sizeof(fx_echoverb_t));
    puts("Echoverb: UI labels, insert/send paths, rate-independent state, release, memory rejection and retry OK");
}
int main(void)
{
    native_history();tails_and_controls();integration();puts("ECHOVERB TEST PASSED");return 0;
}
