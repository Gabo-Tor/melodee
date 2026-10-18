/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FBKL: compact FUN23 controls + 64 independently encoded pattern banks.
 * Empty, whole dense and sparse/repeated blocks share a fixed 16 KiB budget.
 * Keep the existing 27,904-byte A/B record and all reserved flash tails. */
#define BANK_MAGIC 0x4C4B4246u
#define BANK_LEGACY_MAGIC 0x4B4B4246u
#define BANK_STORE_SIZE PROJ_BANK_BYTES
#define BANK_PROJ_SIZE BANK_PREFIX_SIZE
#define BANK_STEP_SIZE 8u
#define BANK_ACTIVE_OFF (8u+BANK_PROJ_SIZE)
#define BANK_MODE_BYTES (NTRK*NPAT/4u)
#define BANK_TIMING_OFF (BANK_ACTIVE_OFF+BANK_MODE_BYTES)
#define BANK_TIME_SIZE 3u
#define BANK_CHAIN_OFF (BANK_TIMING_OFF+NTRK*NPAT*BANK_TIME_SIZE)
#define BANK_MOTION_OFF (BANK_CHAIN_OFF+CHAIN_ROWS*NTRK)
#define BANK_FN_OFF (BANK_MOTION_OFF+MOTION_MAX)
#define BANK_FN_MAGIC 0x31364E46u
#define BANK_EXTRA_OFF (BANK_FN_OFF+4u+NTRK*FM6_NFN)
#define BANK_TAIL_SIZE (BANK_STORE_SIZE-BANK_ACTIVE_OFF)
#define B16_AT(off) ((off)-BANK_ACTIVE_OFF)
_Static_assert(NPAT==16u&&NSTEP==64u,"FBKL capacity contract");
_Static_assert(BANK_EXTRA_OFF+PAT_WIRE_BUDGET<=BANK_STORE_SIZE-4u,"all admitted banks fit flash");
static int bank_full(uint32_t n){return n==BANK_STORE_SIZE;}
static void bank_checksum(uint8_t *raw){uint32_t h=proj_hash(raw,BANK_STORE_SIZE-4u);memcpy(raw+BANK_STORE_SIZE-4u,&h,4);}
static int bank_step_pack(uint8_t *b, const step_t *s)
{
    if (s->n > 4u || s->time > ST_REST || s->flags > 7u || s->vel > 127u || s->probability > 101u || !step_acc_valid(s)) return 0;
    uint32_t pos = 0;
    memset(b, 0, 8u);
    for (uint32_t j = 0; j < 4u; j++) { if (s->note[j] > 127u) return 0; wire_bits_put(b, &pos, s->note[j], 7u); }
    wire_bits_put(b, &pos, (s->time == ST_NOTE ? s->n : s->time == ST_TIE ? 5u : 6u) | (s->flags & SF_RECORDED ? 8u : 0u), 4u);
    wire_bits_put(b, &pos, s->flags & 3u, 2u);
    wire_bits_put(b, &pos, s->vel, 7u); wire_bits_put(b, &pos, s->hit, 8u);
    wire_bits_put(b, &pos, s->acc, 8u); wire_bits_put(b, &pos, s->probability, 7u);
    return 1;
}
static uint32_t bank_mode(const uint8_t *raw,uint32_t owner){return (raw[owner/4u]>>(2u*(owner%4u)))&3u;}
static void bank_set_mode(uint8_t *raw,uint32_t owner,uint32_t mode){raw[owner/4u]=(uint8_t)((raw[owner/4u]&~(3u<<(2u*(owner%4u))))|(mode<<(2u*(owner%4u))));}
static int bank_time_pack(uint8_t *out,const int16_t *v)
{
    for(uint32_t i=0;i<4;i++)if(v[i]<TP[P_SLEN+i].min||v[i]>TP[P_SLEN+i].max)return 0;
    uint32_t n=(uint32_t)(v[0]-1)|((uint32_t)v[1]<<6)|((uint32_t)v[2]<<10)|((uint32_t)v[3]<<17);
    out[0]=n;out[1]=n>>8;out[2]=n>>16;return 1;
}
static int bank_time_unpack(int16_t *v,const uint8_t *in)
{
    uint32_t n=in[0]|(uint32_t)in[1]<<8|(uint32_t)in[2]<<16;
    v[0]=(n&63u)+1;v[1]=(n>>6)&15u;v[2]=(n>>10)&127u;v[3]=n>>17;
    for(uint32_t i=0;i<4;i++)if(v[i]<TP[P_SLEN+i].min||v[i]>TP[P_SLEN+i].max)return 0;
    return 1;
}
static int bank_encode_steps(uint8_t *out,uint32_t room,const step_t *steps,uint32_t *mode)
{
    uint32_t n=pattern_wire_cost(steps);*mode=!n?0u:n==512u?1u:2u;
    if(n>room)return -1;
    if(!n)return 0;
    if(*mode==1u){for(uint32_t i=0;i<64;i++)if(!bank_step_pack(out+8u*i,steps+i))return -1;return 512;}
    uint32_t at=1;out[0]=0;
    for(uint32_t b=0;b<4;b++){
        uint32_t same,mask=pattern_block_shape(steps+16u*b,&same);
        if(!mask)continue;
        out[0]|=1u<<b;
        if(same)out[0]|=16u<<b;
        else {out[at++]=mask;out[at++]=mask>>8;}
        for(uint32_t i=0;i<(same?1u:16u);i++)if(same||(mask&(1u<<i))){
            if(!bank_step_pack(out+at,steps+16u*b+i))return -1;at+=8u;
        }
    }
    return at==n?(int)at:-1;
}
static int bank_decode_steps(const uint8_t *raw,uint32_t mode,uint32_t *pos,step_t *steps)
{
    for(uint32_t i=0;i<64;i++)steps[i]=(step_t){{0},0,ST_REST};
    if(!mode)return 1;
    uint32_t at=*pos;
    if(mode==1u){
        if(at+512u>BANK_TAIL_SIZE-4u)return 0;
        for(uint32_t i=0;i<64;i++)if(!step_wire_unpack(steps+i,raw+at+8u*i))return 0;
        *pos=at+512u;return 1;
    }
    if(mode!=2u||at>=BANK_TAIL_SIZE-4u)return 0;
    uint32_t header=raw[at++];if(!header||(header>>4&~header&15u))return 0;
    for(uint32_t b=0;b<4;b++)if(header&(1u<<b)){
        uint32_t same=header&(16u<<b),mask=0xffffu;
        if(!same){if(at+2u>BANK_TAIL_SIZE-4u)return 0;mask=raw[at]|(uint32_t)raw[at+1u]<<8;at+=2u;if(!mask)return 0;}
        for(uint32_t i=0;i<(same?1u:16u);i++)if(same||(mask&(1u<<i))){
            if(at+8u>BANK_TAIL_SIZE-4u||!step_wire_unpack(steps+16u*b+i,raw+at))return 0;at+=8u;
        }
        if(same)for(uint32_t i=1;i<16;i++)steps[16u*b+i]=steps[16u*b];
    }
    *pos=at;return 1;
}
typedef int (*bank_reader_t)(void *,uint32_t,void *,uint32_t);
/* reader addresses the legacy tail, whose offsets stay frozen at 8 x 64. */
static int bank_image_encode(uint8_t *raw,const project_t *q,int runtime,bank_reader_t reader,void *context)
{
    if(reader){uint8_t active[4];if(reader(context,0,active,4))return 0;for(uint32_t k=0;k<4;k++)if(active[k]>=8u||active[k]!=q->pattern[k])return 0;}
    memset(raw,0,BANK_TAIL_SIZE);uint32_t pos=B16_AT(BANK_EXTRA_OFF);
    const uint32_t old_steps=4u,old_time=4u+4u*7u*64u*8u,old_chain=old_time+4u*8u*4u,old_motion=old_chain+64u,old_fn=old_motion+64u;
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<NPAT;b++){
        pattern_t p;for(uint32_t i=0;i<64;i++)p.step[i]=(step_t){{0},0,ST_REST};
        for(uint32_t i=0;i<4;i++)p.timing[i]=TP[P_SLEN+i].def;
        if(b==q->pattern[k]){memcpy(p.step,q->t[k].step,sizeof p.step);memcpy(p.timing,&q->t[k].p[P_SLEN],sizeof p.timing);}
        if(runtime)pattern_read(k,b,&p);
        else if(reader&&b<8u){
            uint8_t time[4];if(reader(context,old_time+(k*8u+b)*4u,time,4))return 0;
            for(uint32_t i=0;i<4;i++)p.timing[i]=time[i];
            if(b!=q->pattern[k]){
                uint32_t index=k*7u+b-(b>q->pattern[k]);uint8_t cell[8];
                for(uint32_t i=0;i<64;i++)if(reader(context,old_steps+(index*64u+i)*8u,cell,8)||!step_wire_unpack(p.step+i,cell))return 0;
            }
        }
        if(!bank_time_pack(raw+B16_AT(BANK_TIMING_OFF)+(k*NPAT+b)*3u,p.timing))return 0;
        uint32_t mode;int n=bank_encode_steps(raw+pos,BANK_TAIL_SIZE-4u-pos,p.step,&mode);
        if(n<0)return 0;bank_set_mode(raw,k*NPAT+b,mode);pos+=(uint32_t)n;
    }
    if(pos-B16_AT(BANK_EXTRA_OFF)>PAT_WIRE_BUDGET)return 0;
    if(runtime){memcpy(raw+B16_AT(BANK_CHAIN_OFF),chain_patterns,sizeof chain_patterns);memcpy(raw+B16_AT(BANK_MOTION_OFF),motion_pattern,sizeof motion_pattern);}
    else if(reader){if(reader(context,old_chain,raw+B16_AT(BANK_CHAIN_OFF),64)||reader(context,old_motion,raw+B16_AT(BANK_MOTION_OFF),64))return 0;}
    if(reader){
        for(uint32_t i=0;i<64;i++)if(raw[B16_AT(BANK_CHAIN_OFF)+i]>=8u)return 0;
        for(uint32_t i=0;i<q->motion.count;i++)if(raw[B16_AT(BANK_MOTION_OFF)+i]>=8u)return 0;
        if(reader(context,old_fn,raw+B16_AT(BANK_FN_OFF),68))return 0;
    }
    else if(q->fm6_fn_ok){uint32_t m=BANK_FN_MAGIC;memcpy(raw+B16_AT(BANK_FN_OFF),&m,4);memcpy(raw+B16_AT(BANK_FN_OFF)+4,q->fm6_fn,sizeof q->fm6_fn);}
    return 1;
}
static int bank_pack_tail(uint8_t *raw,const project_t *q,int runtime)
{uint32_t f=motion_guard();int ok=bank_image_encode(raw,q,runtime,0,0);motion_unguard(f);return ok;}
static uint8_t bank_import_tags[MOTION_MAX];
static int bank_tail_valid(const uint8_t *raw)
{
    uint32_t pos=B16_AT(BANK_EXTRA_OFF),cost=0;
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<NPAT;b++){
        pattern_t p;if(!bank_time_unpack(p.timing,raw+B16_AT(BANK_TIMING_OFF)+(k*NPAT+b)*3u)||!bank_decode_steps(raw,bank_mode(raw,k*NPAT+b),&pos,p.step))return 0;
        cost+=pattern_steps_cost(p.step,64);
        if(b==proj_scratch.pattern[k]){
            if(memcmp(p.timing,&proj_scratch.t[k].p[P_SLEN],sizeof p.timing))return 0;
            memcpy(proj_scratch.t[k].step,p.step,sizeof p.step);
        }
    }
    if(cost>PAT_DENSE_BUDGET||pos-B16_AT(BANK_EXTRA_OFF)>PAT_WIRE_BUDGET)return 0;
    for(uint32_t i=pos;i<BANK_TAIL_SIZE-4u;i++)if(raw[i])return 0;
    for(uint32_t i=0;i<CHAIN_ROWS*NTRK;i++)if(raw[B16_AT(BANK_CHAIN_OFF)+i]>=NPAT)return 0;
    memset(bank_import_tags,0,sizeof bank_import_tags);
    for(uint32_t i=0;i<proj_scratch.motion.count;i++){
        uint32_t tag=raw[B16_AT(BANK_MOTION_OFF)+i];if(tag>=NPAT)return 0;
        for(uint32_t j=0;j<i;j++)if(bank_import_tags[j]==tag&&proj_scratch.motion.event[i].place==proj_scratch.motion.event[j].place&&proj_scratch.motion.event[i].param==proj_scratch.motion.event[j].param)return 0;
        bank_import_tags[i]=tag;
    }
    uint32_t magic;memcpy(&magic,raw+B16_AT(BANK_FN_OFF),4);
    if(magic==BANK_FN_MAGIC){
        for(uint32_t k=0;k<NTRK;k++)if(!fm6_fn_ok(raw+B16_AT(BANK_FN_OFF)+4u+k*FM6_NFN))return 0;
        memcpy(proj_scratch.fm6_fn,raw+B16_AT(BANK_FN_OFF)+4u,sizeof proj_scratch.fm6_fn);proj_scratch.fm6_fn_ok=1;
    }
    proj_scratch.sum=proj_sum(&proj_scratch);return 1;
}
static int bank_pack(uint8_t *raw,const project_t *q,int runtime)
{
    uint32_t h[2]={BANK_MAGIC,BANK_STORE_SIZE};memcpy(raw,h,8);
    if(!bank_prefix_pack(raw+8,q)||!bank_pack_tail(raw+BANK_ACTIVE_OFF,q,runtime))return 0;
    bank_checksum(raw);return 1;
}
static void *cv_work(void);
static uint8_t *bank_tail_work(void){return (uint8_t *)cv_work()+sizeof(project_t);}
static project_store_t *bank_prefix_work(void){return (project_store_t *)bank_tail_work();}
static int bank_memory_read(void *context,uint32_t off,void *out,uint32_t n){memcpy(out,(const uint8_t *)context+off,n);return 0;}
static int bank_valid(const uint8_t *raw,uint32_t len)
{
    uint32_t m,size,sum;if(!bank_full(len))return 0;
    memcpy(&m,raw,4);memcpy(&size,raw+4,4);memcpy(&sum,raw+len-4u,4);
    if(size!=len||sum!=proj_hash(raw,len-4u))return 0;
    if(m==BANK_MAGIC){
        memcpy(bank_prefix_work(),raw+8,BANK_PREFIX_SIZE);
        return bank_prefix_unpack(&proj_scratch,(uint8_t *)bank_prefix_work())&&bank_tail_valid(raw+BANK_ACTIVE_OFF);
    }
    if(m!=BANK_LEGACY_MAGIC||!proj_import(&proj_scratch,raw+8,PROJ_STORE_SIZE))return 0;
    if(!bank_image_encode(bank_tail_work(),&proj_scratch,0,bank_memory_read,(void *)(raw+8+PROJ_STORE_SIZE)))return 0;
    return bank_tail_valid(bank_tail_work());
}
static void bank_restore_tail(const uint8_t *raw)
{
    uint32_t pos=B16_AT(BANK_EXTRA_OFF),f=motion_guard();chain_config=proj_scratch.chain;
    memcpy(chain_patterns,raw+B16_AT(BANK_CHAIN_OFF),sizeof chain_patterns);memcpy(motion_pattern,bank_import_tags,sizeof motion_pattern);
    for(uint32_t k=0;k<NTRK;k++){
        trk[k].pattern=proj_scratch.pattern[k];trk[k].pattern_next=0xff;trk[k].pattern_gen++;
        for(uint32_t b=0;b<NPAT;b++){
            pattern_t p;bank_time_unpack(p.timing,raw+B16_AT(BANK_TIMING_OFF)+(k*NPAT+b)*3u);
            bank_decode_steps(raw,bank_mode(raw,k*NPAT+b),&pos,p.step);pattern_write(k,b,&p);
        }
    }
    motion_unguard(f);
}
static void bank_restore(const uint8_t *raw)
{uint32_t m;memcpy(&m,raw,4);bank_restore_tail(m==BANK_MAGIC?raw+BANK_ACTIVE_OFF:bank_tail_work());}
/* Drop a track's contiguous encoded bank range before shifting its metadata.
 * Forward copies are safe because every destination precedes its source. */
static int bank_tail_remove_track(uint8_t *raw,uint32_t k)
{
    uint32_t offsets[NTRK+1u],pos=B16_AT(BANK_EXTRA_OFF);step_t steps[64];
    for(uint32_t t=0;t<NTRK;t++){
        offsets[t]=pos;
        for(uint32_t b=0;b<NPAT;b++)if(!bank_decode_steps(raw,bank_mode(raw,t*NPAT+b),&pos,steps))return 0;
    }
    offsets[NTRK]=pos;uint32_t n=pos-offsets[k+1u],dest=offsets[k];
    for(uint32_t i=0;i<n;i++)raw[dest+i]=raw[offsets[k+1u]+i];
    memset(raw+dest+n,0,BANK_TAIL_SIZE-4u-dest-n);
    for(uint32_t t=k;t<NTRK-1u;t++){
        memcpy(raw+t*4u,raw+(t+1u)*4u,4u);
        memcpy(raw+B16_AT(BANK_TIMING_OFF)+t*48u,raw+B16_AT(BANK_TIMING_OFF)+(t+1u)*48u,48u);
    }
    memset(raw+(NTRK-1u)*4u,0,4);
    int16_t defaults[4];for(uint32_t i=0;i<4;i++)defaults[i]=TP[P_SLEN+i].def;
    for(uint32_t b=0;b<NPAT;b++)bank_time_pack(raw+B16_AT(BANK_TIMING_OFF)+((NTRK-1u)*NPAT+b)*3u,defaults);
    for(uint32_t r=0;r<CHAIN_ROWS;r++){
        for(uint32_t t=k;t<NTRK-1u;t++)raw[B16_AT(BANK_CHAIN_OFF)+r*NTRK+t]=raw[B16_AT(BANK_CHAIN_OFF)+r*NTRK+t+1u];
        raw[B16_AT(BANK_CHAIN_OFF)+r*NTRK+NTRK-1u]=0;
    }
    return 1;
}
