/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void setup(uint32_t type,uint32_t quality,uint32_t rate,uint32_t depth)
{
    ui_power_on();settings_fx_quality=quality;spread_on=0;
    trk[0].p[P_FXTYPE]=type;trk[0].p[P_FXMIX]=127;
    trk[0].p[P_FXRATE]=rate;trk[0].p[P_FXCHAR]=depth;
}
static uint32_t bytes(uint32_t type,uint32_t quality)
{return quality ? (quality==1u ? 16384u : 32768u) : type==FX_ECHO?8192u:4096u;}
static void timing_and_bounds(void)
{
    for(uint32_t type=FX_ECHO;type<=FX_REVERB;type+=2u)for(uint32_t q=0;q<3u;q++){
        setup(type,q,0,0);int32_t in[CTL]={0};assert(fx_process(&trk[0],in,CTL));
        assert(resource[RES_FX0].size==bytes(type,q));
        uint8_t *guard=resource_get(RES_FX0+1u,4096);assert(guard);memset(guard,0xa5,4096);
        fx_state[0].mix=32768;
        uint32_t shift=fx_time_shift(&fx_state[0]);
        assert((FS>>shift)==(q==2?44100u:q==1?22050u:type==FX_ECHO?11025u:5512u));
        if(type==FX_REVERB){
            fx_reverb_setup(&fx_state[0]);
            for(uint32_t k=0;k<8u;k++){
                assert(fx_state[0].rev_span[k]*(1u<<shift)==((uint32_t)REV_LEN[k]*64u/191u)*8u);
            }
        }
        uint32_t onset=~0u,dense=0;uint64_t late=0;
        for(uint32_t at=0;at<4u*FS;){
            uint32_t n=1u+(at/CTL)%CTL;if(n>4u*FS-at)n=4u*FS-at;
            memset(in,0,sizeof in);if(!at)in[0]=24000;
            assert(fx_process(&trk[0],in,n));
            for(uint32_t i=0;i<n;i++){
                int32_t l=fx_left[i],r=fx_right[i];assert(abs(l)<32768 && abs(r)<32768);
                if(onset==~0u && abs(l)+abs(r)>4)onset=at+i;
                if(at+i>FS/10u && at+i<FS/4u)dense+=abs(l)+abs(r)>2;
                if(at+i>3u*FS)late+=abs(l)+abs(r);
            }
            at+=n;
        }
        if(type==FX_ECHO)assert(onset>=440u && onset<=444u);
        else assert(onset<FS/10u && dense>100u);
        assert(late<1000u);
        if(q==2u)assert(!fx_state[0].decim && !fx_state[0].rev_input && !fx_state[0].rev_input2);
        for(uint32_t i=0;i<4096u;i++)assert(guard[i]==0xa5);
    }
    puts("Fidelity: exact rates, preserved echo/room times, bounded stereo tanks and buffer guards: OK");
}
static void full_rate_echo(void)
{
    setup(FX_ECHO,2,0,0);int32_t input[CTL]={0};assert(fx_process(&trk[0],input,CTL));
    fx_state_t *f=&fx_state[0];f->mix=32768;f->pos=0;memset(f->q,0,sizeof f->q);
    int32_t original[2000];int32_t rng=11;
    for(uint32_t i=0;i<NELEM(original);i++)original[i]=(int32_t)(noise32(&rng)%20001u)-10000;
    for(uint32_t at=0;at<NELEM(original);){
        uint32_t n=1u+at%CTL;if(n>NELEM(original)-at)n=NELEM(original)-at;
        for(uint32_t i=0;i<n;i++)fx_left[i]=fx_right[i]=fx_dry_l[i]=fx_dry_r[i]=original[at+i];
        fx_echo_hifi(f,resource[RES_FX0].ptr,n,32768);
        for(uint32_t i=0;i<n;i++)assert(fx_left[i]==(at+i<440u?0:original[at+i-440u]) && fx_right[i]==fx_left[i]);
        at+=n;
    }
    puts("44.1 kHz echo: arbitrary full-rate samples emerge unchanged at the exact delay, no rate conversion: OK");
}
static void extremes(void)
{
    for(uint32_t type=FX_ECHO;type<=FX_REVERB;type+=2u)for(uint32_t q=1;q<3u;q++)for(uint32_t rate=0;rate<=127u;rate+=127u){
        setup(type,q,rate,127);int32_t in[CTL],rng=91;uint64_t wet=0,tail=0;
        for(uint32_t frame=0;frame<12u*FS/CTL;frame++){
            for(uint32_t i=0;i<CTL;i++)in[i]=frame<FS/CTL?(int32_t)(noise32(&rng)%48001u)-24000:0;
            assert(fx_process(&trk[0],in,CTL));
            for(uint32_t i=0;i<CTL;i++){
                assert(abs(fx_left[i])<32768 && abs(fx_right[i])<32768);
                if(frame<2u*FS/CTL)wet+=abs(fx_left[i])+abs(fx_right[i]);
                if(frame>=11u*FS/CTL)tail+=abs(fx_left[i])+abs(fx_right[i]);
            }
        }
        printf("%s quality %u, time/size %u: wet %llu, late tail %llu\n",N_FX[type],q,rate,(unsigned long long)wet,(unsigned long long)tail);
        /* The longest maximum-feedback echo intentionally rings past 12 s;
         * require a strong decline rather than truncating that musical tail. */
        assert(wet>100000u && tail<wet/100u);
    }
    puts("High fidelity: maximum feedback/depth, both size/time extremes, hot input and decaying tails: OK");
}
static double band(uint32_t type,uint32_t q,uint32_t hz)
{
    setup(type,q,60,90);int32_t in[CTL]={0};fx_process(&trk[0],in,CTL);fx_state[0].mix=32768;
    uint32_t phase=0,inc=(uint32_t)((uint64_t)hz*0x100000000ull/FS);double re=0,im=0;
    for(uint32_t frame=0;frame<2u*FS/CTL;frame++){
        uint32_t ph=phase;
        for(uint32_t i=0;i<CTL;i++){phase+=inc;in[i]=sine_i(phase)/4;}
        fx_process(&trk[0],in,CTL);
        for(uint32_t i=0;i<CTL;i++){
            ph+=inc;if(frame>=FS/CTL){re+=(double)fx_left[i]*sine_i(ph);im+=(double)fx_left[i]*sine_i(ph+0x40000000u);}
        }
    }
    return re*re+im*im;
}
static void bandwidth(void)
{
    for(uint32_t type=FX_ECHO;type<=FX_REVERB;type+=2u){
        double classic=band(type,0,16000),half=band(type,1,16000),full=band(type,2,16000);
        printf("%s 16 kHz projected energy: Classic %.3g, 22.05k %.3g, 44.1k %.3g\n",N_FX[type],classic,half,full);
        assert(full>classic*8.0 && full>half*16.0 && full>1e16);
    }
    puts("44.1 kHz bandwidth: echo and reverb retain 16 kHz detail suppressed by lower rates: OK");
}
static void switching(void)
{
    setup(FX_REVERB,0,80,90);int32_t in[CTL];for(uint32_t i=0;i<CTL;i++)in[i]=8000;
    for(uint32_t frame=0;frame<64u;frame++)fx_process(&trk[0],in,CTL);
    for(uint32_t q=1;q<=3u;q++){
        settings_fx_quality=q%3u;
        fx_process(&trk[0],in,CTL);assert(fx_state[0].quality!=(q%3u) && fx_state[0].mix<32768);
        for(uint32_t frame=0;frame<64u;frame++)fx_process(&trk[0],in,CTL);
        assert(fx_state[0].quality==q%3u && resource[RES_FX0].size==bytes(FX_REVERB,q%3u));
    }
    /* Allocation failure returns the unchanged dry path. Lowering the rate retries. */
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    setup(FX_REVERB,2,127,127);fx_plan_t plan;
    void *held=resource_get(RES_NOTE_HISTORY,RESOURCE_MAIN_CAPACITY-8192u);assert(held);
    assert(resource_get(RES_ENGINE0,sizeof resource_host_low));
    assert(resource_get(RES_ENGINE0+1u,sizeof resource_host_data));
    assert(resource_get(RES_ENGINE0+2u,sizeof resource_host_retained));
    fx_slot_prepare(&plan,0,trk[0].p+P_FXTYPE);assert(!plan.active && fx_state[0].failed);
    settings_fx_quality=0;fx_slot_prepare(&plan,0,trk[0].p+P_FXTYPE);
    assert(plan.active && !fx_state[0].failed && resource[RES_FX0].size==4096u);
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    puts("Fidelity changes: fade/reallocate, allocation failure and lower-rate recovery: OK");
}
static void menu_and_shared(void)
{
    ui_power_on();settings_fx_quality=0;hold(B_HOME);ui.menu_sel=MI_FX_QUALITY;
    turn(EN_K1,-1);assert(!settings_fx_quality);turn(EN_K1,1);assert(settings_fx_quality==1u);
    turn(EN_K1,1);turn(EN_K1,1);assert(settings_fx_quality==2u);press(B_OCTUP);assert(!settings_fx_quality);
    turn(EN_K1,1);turn(EN_K1,1);hold(B_HOME);
    persist_t saved={0};settings_export(&saved);settings_fx_quality=0;
    assert(settings_import(&saved,sizeof saved) && settings_fx_quality==2u);
    song.g[G_MFXTYPE]=FX_REVERB;song.g[G_MFXMIX]=127;song.g[G_FXRETURN]=127;
    song.g[G_MFXRATE]=80;song.g[G_MFXCHAR]=90;
    for(uint32_t i=0;i<CTL;i++)fx_send_l[i]=fx_send_r[i]=i?0:12000;
    fx_bus_start(CTL);fx_bus_join();
    assert(fx_state[NPART*FX_SLOTS].quality==2u && resource[RES_FX0+NPART*FX_SLOTS].size==32768u);
    settings_fx_quality=0;
    puts("FX RATE: knob limits, Enter cycling, settings persistence and full-rate shared bus: OK");
}
int main(void)
{
    timing_and_bounds();full_rate_echo();extremes();bandwidth();switching();menu_and_shared();
    puts("FX FIDELITY TEST PASSED");return 0;
}
