/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Opt-in audition of the mono PCM16 echo/diffusion prototype. No ADPCM or
 * effect-rate conversion. One arena allocation per active slot contains every
 * delay, diffusion history and control state. No pointers survive relocation.
 * Nested allpass concept; independently written, not a Greyhole/Rings port. */
#define FX_ECHOVERB_SAMPLES 22050u
#define FX_ECHOVERB_FADE 2205u
typedef struct {
    int16_t history[FX_ECHOVERB_SAMPLES];
    float diffusion[4032];
    uint32_t positions[12], phase[3], cursor, tick;
    uint32_t delay, previous_delay, delay_fade, initialized;
    float low, dc, modulation[3], diffusion_amount, feedback;
} fx_echoverb_t;
_Static_assert(FS==44100u,"Echoverb audition requires native 44.1 kHz");
_Static_assert(sizeof(fx_echoverb_t)+sizeof(fx_state_t)<=65536u,
               "Echoverb allocation and slot state must fit 64 KiB");
static const uint16_t EV_LENGTH[12] __attribute__((section(".data.fx_echoverb")))=
    {113,211,337,487,139,251,373,557,173,293,419,631};
static const uint16_t EV_OFFSET[12] __attribute__((section(".data.fx_echoverb")))=
    {0,117,332,673,1164,1307,1562,1939,2500,2677,2974,3397};
static const float EV_GAIN[12] __attribute__((section(".data.fx_echoverb")))=
    {.68f,-.64f,.67f,-.62f,.71f,-.66f,.63f,-.69f,.69f,-.63f,.71f,-.65f};
static inline __attribute__((always_inline)) float fx_echoverb_diffuse(fx_echoverb_t *s,float x,uint32_t group)
{
    float w[4],z[4];
    /* Descend into the nested allpasses, then unwind, without recursion. */
    for(uint32_t k=0;k<4u;k++){
        uint32_t j=group*4u+k,n=EV_LENGTH[j]+4u,p=s->positions[j];
        float delay=(float)EV_LENGTH[j]+(k?0.0f:s->modulation[group]);
        float read=(float)(p+n)-delay;if(read>=(float)n)read-=(float)n;
        uint32_t at=(uint32_t)read,next=at+1u;if(next==n)next=0;
        float *b=s->diffusion+EV_OFFSET[j],frac=read-(float)at;
        z[k]=b[at]+frac*(b[next]-b[at]);w[k]=x-EV_GAIN[j]*z[k];x=w[k];
    }
    for(uint32_t k=4u;k--;){
        uint32_t j=group*4u+k,n=EV_LENGTH[j]+4u,p=s->positions[j];
        s->diffusion[EV_OFFSET[j]+p]=x;
        s->positions[j]=p+1u==n?0u:p+1u;x=z[k]+EV_GAIN[j]*w[k];
    }
    return x;
}
static inline __attribute__((always_inline)) float fx_echoverb_read(const fx_echoverb_t *s,uint32_t delay)
{
    uint32_t at=s->cursor+FX_ECHOVERB_SAMPLES-delay;
    if(at>=FX_ECHOVERB_SAMPLES)at-=FX_ECHOVERB_SAMPLES;
    return (float)s->history[at]*(1.0f/32768.0f);
}
static __attribute__((noinline,section(".audio_text"))) void fx_echoverb_run(
    fx_state_t *restrict f,fx_echoverb_t *restrict s,uint32_t n,int32_t target,
    int32_t *restrict left,int32_t *restrict right)
{
    /* TIME 0..127 = 10..500 ms. Crossfade taps over 50 ms on a time change;
     * finish an in-flight change before accepting the latest knob position. */
    uint32_t requested=441u+(uint32_t)f->rate*(FX_ECHOVERB_SAMPLES-441u)/127u;
    if(!s->initialized){
        s->initialized=1;s->delay=s->previous_delay=requested;
        s->phase[1]=(uint32_t)(1.7f*(4294967296.0f/6.28318530718f));
        s->phase[2]=(uint32_t)(3.3f*(4294967296.0f/6.28318530718f));
        s->diffusion_amount=(float)f->character*(1.0f/127.0f);
        s->feedback=.573f+.225f*s->diffusion_amount;
    }
    float diffusion_target=(float)f->character*(1.0f/127.0f);
    int32_t mix=f->mix;
    for(uint32_t i=0;i<n;i++){
        if(!s->delay_fade && s->delay!=requested){
            s->previous_delay=s->delay;s->delay=requested;s->delay_fade=FX_ECHOVERB_FADE;
        }
        if(!(s->tick++&63u)){
            for(uint32_t g=0;g<3u;g++){
                s->phase[g]+=(uint32_t)((.13f+.041f*(float)g)*(4294967296.0f*64.0f/FS));
                s->modulation[g]=(float)fx_sine(s->phase[g])*(1.1f/32768.0f);
            }
        }
        s->diffusion_amount+=(diffusion_target-s->diffusion_amount)*(1.0f/(.010f*FS));
        s->feedback+=(.573f+.225f*s->diffusion_amount-s->feedback)*(1.0f/(.010f*FS));
        float delayed=fx_echoverb_read(s,s->delay);
        if(s->delay_fade){
            float old=fx_echoverb_read(s,s->previous_delay);
            delayed+=(old-delayed)*(float)s->delay_fade*(1.0f/FX_ECHOVERB_FADE);s->delay_fade--;
        }
        int32_t mono=fx_div_pow2(left[i]+right[i],1);
        /* Two-times internal headroom, restored at the wet output. */
        float x=(float)fx_tone_input(f,mono,1.0f/(.010f*FS))*(1.0f/65536.0f);
        s->low+=.56f*(delayed-s->low);s->dc+=.003f*(s->low-s->dc);
        float filtered=s->low-s->dc,diffuse=.52f*x+.86f*filtered;
        for(uint32_t g=0;g<3u;g++)diffuse=fx_echoverb_diffuse(s,diffuse,g);
        float fed=x+s->feedback*((1.0f-s->diffusion_amount)*filtered+s->diffusion_amount*diffuse);
        /* Round symmetrically to avoid adding truncation DC in quiet tails. */
        float pcm=fx_bound(fed*32768.0f,32767.0f);
        s->history[s->cursor]=(int16_t)(pcm+(pcm<0?-.5f:.5f));
        if(++s->cursor==FX_ECHOVERB_SAMPLES)s->cursor=0;
        float wet=(1.0f-.72f*s->diffusion_amount)*delayed+(.3f+.72f*s->diffusion_amount)*diffuse;
        int32_t w=(int32_t)fx_bound(wet*65536.0f,262143.0f);
        int32_t l=left[i],r=right[i];if(mix!=target)mix+=clamp(target-mix,-128,128);
        left[i]=l+fx_scale(w-l,(uint32_t)mix*2u);right[i]=r+fx_scale(w-r,(uint32_t)mix*2u);
    }
    f->mix=mix;
}
