/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Reference: Melodee master_samples at 79436ad, before the dry-pass removal.
 * Compare output, mix, limiter/DC state and the return ramp independently. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef __attribute__
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instructions(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 r;
    if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&r))return r.ri_instructions;
#endif
    return 0;
}
/* hostsim strips attributes: restore the target's non-inlined entry. */
static void master_samples(int32_t *,uint32_t) __attribute__((noinline));
static __attribute__((noinline)) void reference(int32_t *out,uint32_t n)
{
    int32_t gain=fx_bus_mix,target=fx_bus_target;
    for(uint32_t i=0;i<n;i++){
        if(gain!=target)gain+=clamp(target-gain,-128,128);
        int32_t l=mix_l[i],r=mix_r[i];
        if(fx_bus_result){uint32_t ret=(uint32_t)gain*2u;
            l+=((fx_scale(fx_left[i],ret)>>2)*(int32_t)song.master_q12)>>10;
            r+=((fx_scale(fx_right[i],ret)>>2)*(int32_t)song.master_q12)>>10;}
        mix_l[i]=l;mix_r[i]=r;
    }
    punch_process(mix_l,mix_r,n);
    for(uint32_t i=0;i<n;i++){int32_t l=mix_l[i],r=mix_r[i];master_out(&l,&r);out[2u*i]=l;out[2u*i+1u]=r;}
    fx_bus_mix=gain;
}

static int32_t random_state=901;
static uint32_t random_u(void){return noise32(&random_state);}
static void setup(uint32_t wet,uint32_t gain,uint32_t target)
{
    punch_reset();song.master_q12=4096;
    fx_bus_result=(int)wet;fx_bus_mix=(int32_t)gain;fx_bus_target=(int32_t)target;
    lim_env=18000+(int32_t)(random_u()%100000u);
    dc_l=(int32_t)(random_u()%100000u)-50000;dc_r=-dc_l;
    dce_l=(int32_t)(random_u()%4096u);dce_r=4095-dce_l;
    for(uint32_t i=0;i<CTL;i++){
        mix_l[i]=(int32_t)(random_u()%400001u)-200000;mix_r[i]=-mix_l[i];
        fx_left[i]=(int32_t)(random_u()%131073u)-65536;fx_right[i]=-fx_left[i];
    }
}
static void parity(void)
{
    static const uint32_t gains[]={0,1,127,128,129,4095,16384,32641,32767,32768};
    int32_t a[CTL*2],b[CTL*2],left[CTL],right[CTL],state[6];
    for(uint32_t wet=0;wet<2;wet++)for(uint32_t n=0;n<=CTL;n++)
    for(uint32_t g=0;g<NELEM(gains);g++)for(uint32_t t=0;t<NELEM(gains);t++){
        int32_t seed=random_state;
        setup(wet,gains[g],gains[t]);memset(a,0x5a,sizeof a);reference(a,n);
        memcpy(left,mix_l,sizeof left);memcpy(right,mix_r,sizeof right);
        state[0]=lim_env;state[1]=dc_l;state[2]=dc_r;state[3]=dce_l;state[4]=dce_r;state[5]=fx_bus_mix;
        random_state=seed;setup(wet,gains[g],gains[t]);memset(b,0x5a,sizeof b);master_samples(b,n);
        assert(!memcmp(a,b,sizeof a) && !memcmp(left,mix_l,sizeof left) && !memcmp(right,mix_r,sizeof right));
        assert(state[0]==lim_env && state[1]==dc_l && state[2]==dc_r && state[3]==dce_l && state[4]==dce_r && state[5]==fx_bus_mix);
    }
    puts("master output: wet/dry, every block length, rising/falling/odd ramps, limiter/DC state and untouched samples exact");
}
static volatile uint32_t checksum;
static void benchmark(void)
{
    for(uint32_t wet=0;wet<2;wet++){
        double cost[2];
        for(uint32_t fast=0;fast<2;fast++){
            setup(wet,32768,32768);
            void (*volatile run)(int32_t *,uint32_t)=fast?master_samples:reference;
            int32_t out[CTL*2];uint64_t start=instructions();
            for(uint32_t k=0;k<200000;k++){
                /* The real mixer starts each block afresh; wet returns must
                 * not accumulate indefinitely in this repeated-call bench. */
                for(uint32_t i=0;i<CTL;i++){mix_l[i]=8000+(int32_t)i*31;mix_r[i]=-mix_l[i];}
                run(out,CTL);checksum+=(uint32_t)out[k%CTL];
            }
            cost[fast]=(double)(instructions()-start)/(200000u*CTL);
        }
        printf("master %s: %.3f -> %.3f host instructions/frame (%+.2f%%)\n",wet?"wet":"dry",cost[0],cost[1],cost[0]?100*(cost[1]/cost[0]-1):0);
        if(cost[0]>0)assert(cost[1]<cost[0]*(wet?1.05:0.95));
    }
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"--parity-only")));
    host_tracks_init();parity();if(argc==1)benchmark();return 0;
}
