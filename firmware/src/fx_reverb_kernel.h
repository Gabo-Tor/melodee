/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Intentionally included twice by fx.c: preserve Classic's specialized SRAM
 * kernel while sharing the tank algorithm with the optional high-rate kernel.
 * Extracted from Melodee fx.c, authored by Kerem Kilic. */
static __attribute__((noinline,section(FX_REVERB_SECTION))) void FX_REVERB_KERNEL(fx_state_t *restrict f,int16_t *restrict mem,uint32_t n,const int32_t *drift,int32_t target,int32_t *left,int32_t *right)
{
    /* The Classic instantiation keeps compile-time eighth-rate constants.
     * The high-fidelity instantiation scales every delay in seconds. */
#if FX_REVERB_HIFI
    uint32_t shift=fx_time_shift(f),scale=1u<<(3u-shift),div=1u<<shift;
#else
    const uint32_t shift=3u,scale=1u,div=8u;
#endif
    int32_t a=f->rev_input,b=f->rev_input2,previous=f->echo,difference=f->damp[0],low=f->damp[1];
    uint32_t phase=f->decim;
    int32_t mix=f->mix,tone=166+f->character*80/127,level=724+f->character*3;
    for(uint32_t i=0;i<n;i++){
        int32_t mono=fx_div_pow2(left[i]+right[i],1);
        if(shift){
            a=(int32_t)((uint32_t)a+(uint32_t)mono);b=(int32_t)((uint32_t)b+(uint32_t)a);
        }
        if(!phase){
            int32_t input=mono;
            if(shift){
                uint32_t comb=(uint32_t)b-(uint32_t)previous;previous=b;
                input=fx_div_pow2((int32_t)(comb-(uint32_t)difference),2u*shift);difference=(int32_t)comb;
                low=fx_div_pow2(low+(int32_t)(div-1u)*input,shift);input=low;
                /* Rate-conversion compensation restores upper-mid body.
                 * At full rate the input skips conversion and this shelf. */
                int32_t previous_input=f->damp[2];f->damp[2]=input;
                input+=fx_div_pow2(input-previous_input,2);
            }
            input=fx_tone_input(f,input,div/(0.010f*FS));
            int32_t v[8],x;uint32_t off=0;
            f->rev_dc+=((float)input-f->rev_dc)*(0.006f/scale);
            x=clamp(fx_div_pow2((int32_t)((float)input-f->rev_dc),2),-16000,16000);
            off=1610u*scale;
            for(uint32_t k=0;k<4u;k++){x=fx_reverb_ap(f,mem,k,x,off);off+=REV_AP_LEN[k]*scale;}
            off=0;int32_t sum=0;
            for(uint32_t k=0;k<8u;k++){
                uint32_t pos=f->rev_pos[k];
                int32_t read=(int32_t)(pos+REV_LEN[k]*scale-f->rev_span[k])-((drift[k]*(int32_t)scale)>>10);
                if(read<0)read+=REV_LEN[k]*scale;else if(read>=REV_LEN[k]*scale)read-=REV_LEN[k]*scale;
                uint32_t prev=read?read-1u:REV_LEN[k]*scale-1u;
                int32_t y=mem[off+(uint32_t)read];
                y+=fx_div_pow2((mem[off+prev]-y)*((drift[k]*(int32_t)scale)&1023),10);
                /* Remove bass on each round trip (~FS / 8 / (2 pi 2^shift)) so a
                 * long tail cannot accumulate booming low modes and duck the dry mix. */
                f->rev_bass[k]=fx_div_pow2(((1<<(FX_REV_LOOP_SHIFT+3u-shift))-1)*f->rev_bass[k]+y,(FX_REV_LOOP_SHIFT+3u-shift));y-=f->rev_bass[k];
                f->rev_damp[k]=fx_div_pow2(f->rev_damp[k]*256+(y-f->rev_damp[k])*tone,8);
                v[k]=f->rev_damp[k];sum+=v[k];off+=REV_LEN[k]*scale;
            }
            int32_t l=fx_div_pow2(v[0]-v[1]+v[2]-v[3],1),r=fx_div_pow2(v[4]-v[5]+v[6]-v[7],1);
            /* Householder H = I - 2 uu': for eight equal weights, subtract
             * sum/4 from each line. Orthogonal mixing with one sum replaces
             * the nested Hadamard butterflies and their normalization multiply. */
            int32_t shared=fx_div_pow2(sum,2);
            off=0;
            for(uint32_t k=0;k<8u;k++){
                int32_t feedback=fx_reverb_mul(v[k]-shared,(uint32_t)f->rev_gain[k]);
                mem[off+f->rev_pos[k]]=(int16_t)clamp(feedback+((k&1u)?-x:x),-32767,32767);
                if(++f->rev_pos[k]>=REV_LEN[k]*scale)f->rev_pos[k]=0;off+=REV_LEN[k]*scale;
            }
            off=1768u*scale;
            l=fx_reverb_ap(f,mem,4,l,off);off+=REV_AP_LEN[4]*scale;l=fx_reverb_ap(f,mem,5,l,off);off+=REV_AP_LEN[5]*scale;
            r=fx_reverb_ap(f,mem,6,r,off);off+=REV_AP_LEN[6]*scale;r=fx_reverb_ap(f,mem,7,r,off);
            /* Round only hot wet peaks; the dry signal remains untouched. */
            f->rev_prev_l=f->rev_l;f->rev_prev_r=f->rev_r;
            f->rev_l=softclip(fx_div_pow2(l*level,8));f->rev_r=softclip(fx_div_pow2(r*level,8));
        }
        int32_t l=f->rev_prev_l+fx_div_pow2((f->rev_l-f->rev_prev_l)*(phase+1u),shift);
        int32_t r=f->rev_prev_r+fx_div_pow2((f->rev_r-f->rev_prev_r)*(phase+1u),shift);
        int32_t dry_l=left[i],dry_r=right[i];
        if(mix!=target)mix+=clamp(target-mix,-128,128);
        left[i]=dry_l+fx_scale(l-dry_l,(uint32_t)mix*2u);
        right[i]=dry_r+fx_scale(r-dry_r,(uint32_t)mix*2u);
        phase=(phase+1u)&(div-1u);
    }
    f->mix=mix;f->decim=(uint8_t)phase;f->rev_input=a;f->rev_input2=b;f->echo=previous;f->damp[0]=difference;f->damp[1]=low;
}
