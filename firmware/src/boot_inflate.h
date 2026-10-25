/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded raw-DEFLATE decoder for the resident-code load image. Boot only:
 * no heap, globals, libc, or calls into the SRAM being initialized. Huffman
 * codes are canonical; symbols of each length retain their numeric order. */
#ifndef MELODEE_BOOT_INFLATE_H
#define MELODEE_BOOT_INFLATE_H
#include <stdint.h>
#define BOOT_AUDIO_MAGIC 0x31465A41u /* AZF1 */
typedef struct {const uint8_t *at,*end;uint32_t bits,count;int bad;} boot_bits_t;
typedef struct {uint16_t count[16],symbol[288];} boot_huffman_t;
static uint32_t boot_bits(boot_bits_t *s,uint32_t n)
{
    while(s->count<n){
        if(s->at==s->end){s->bad=1;return 0;}
        s->bits|=(uint32_t)*s->at++<<s->count;s->count+=8u;
    }
    uint32_t v=s->bits&((1u<<n)-1u);s->bits>>=n;s->count-=n;return v;
}
static int boot_tree(boot_huffman_t *h,const uint8_t *length,uint32_t n)
{
    /* Volatile initialization cannot become a call into uninitialized RAM. */
    volatile uint16_t *count=h->count;
    for(uint32_t i=0;i<16u;i++)count[i]=0;
    for(uint32_t i=0;i<n;i++){if(length[i]>15u)return 0;h->count[length[i]]++;}
    int32_t left=1;
    for(uint32_t i=1;i<16u;i++){left=left*2-(int32_t)h->count[i];if(left<0)return 0;}
    uint16_t pos[16];pos[1]=0;
    for(uint32_t i=1;i<15u;i++)pos[i+1u]=pos[i]+h->count[i];
    for(uint32_t i=0;i<n;i++)if(length[i])h->symbol[pos[length[i]]++]=(uint16_t)i;
    return 1;
}
static uint32_t boot_symbol(boot_bits_t *s,const boot_huffman_t *h)
{
    uint32_t code=0,first=0,pos=0;
    for(uint32_t length=1;length<16u;length++){
        code=(code<<1)|boot_bits(s,1);
        uint32_t count=h->count[length];
        if(code>=first && code-first<count)return h->symbol[pos+code-first];
        pos+=count;first=(first+count)<<1;
    }
    s->bad=1;return 0;
}
static __attribute__((noinline)) int boot_inflate(uint8_t *out,uint32_t size,const uint8_t *in,uint32_t packed)
{
    static const uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    static const uint16_t length_base[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t length_bits[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t distance_base[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t distance_bits[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    boot_bits_t s={in,in+packed,0,0,0};boot_huffman_t lit,dist;
    uint8_t lengths[320];uint32_t used=0,final;
    do{
        final=boot_bits(&s,1);uint32_t kind=boot_bits(&s,2);
        if(s.bad)return 0;
        if(!kind){
            s.bits=s.count=0;
            uint32_t n=boot_bits(&s,16),check=boot_bits(&s,16);
            if(s.bad || (n^65535u)!=check || n>size-used)return 0;
            while(n--)out[used++]=(uint8_t)boot_bits(&s,8);
            if(s.bad)return 0;
            continue;
        }
        if(kind==3u)return 0;
        uint32_t nl=288,nd=32;
        if(kind==1u){
            for(uint32_t i=0;i<288u;i++)lengths[i]=(uint8_t)(i<144u?8u:i<256u?9u:i<280u?7u:8u);
            for(uint32_t i=288u;i<320u;i++)lengths[i]=5;
        }else{
            nl=257u+boot_bits(&s,5);nd=1u+boot_bits(&s,5);uint32_t nc=4u+boot_bits(&s,4);
            if(s.bad || nl>286u)return 0;
            uint8_t code[19];for(uint32_t i=0;i<19u;i++)code[i]=0;
            for(uint32_t i=0;i<nc;i++)code[order[i]]=(uint8_t)boot_bits(&s,3);
            if(s.bad || !boot_tree(&lit,code,19u))return 0;
            uint32_t i=0;
            while(i<nl+nd){
                uint32_t v=boot_symbol(&s,&lit),n=1,value=v;
                if(v==16u){if(!i)return 0;value=lengths[i-1u];n=3u+boot_bits(&s,2);}
                else if(v==17u){value=0;n=3u+boot_bits(&s,3);}
                else if(v==18u){value=0;n=11u+boot_bits(&s,7);}
                else if(v>15u)return 0;
                if(s.bad || n>nl+nd-i)return 0;
                while(n--)lengths[i++]=(uint8_t)value;
            }
        }
        if(!lengths[256] || !boot_tree(&lit,lengths,nl) || !boot_tree(&dist,lengths+nl,nd))return 0;
        for(;;){
            uint32_t v=boot_symbol(&s,&lit);
            if(s.bad)return 0;
            if(v<256u){if(used==size)return 0;out[used++]=(uint8_t)v;continue;}
            if(v==256u)break;
            if(v>285u)return 0;
            v-=257u;uint32_t n=length_base[v]+boot_bits(&s,length_bits[v]);
            uint32_t d=boot_symbol(&s,&dist);
            if(s.bad || d>=30u)return 0;
            d=distance_base[d]+boot_bits(&s,distance_bits[d]);
            if(s.bad || d>used || n>size-used)return 0;
            while(n--){out[used]=out[used-d];used++;}
        }
    }while(!final);
    return !s.bad && used==size && s.at==s.end;
}
#endif
