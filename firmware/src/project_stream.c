/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* A decoded project and either its serialized active record or its inactive
 * bank tail fit the shared canvas. They are alternative views, never peers.
 * Freeze decoded state + tail, write that tail first, then reuse its RAM for
 * the active record. Only the final validated flash header publishes a save. */
_Static_assert(sizeof(project_t)+sizeof(project_store_t)<=CV_WORK_BYTES,"active project codec fits canvas work");
_Static_assert(sizeof(project_t)+BANK_TAIL_SIZE<=CV_WORK_BYTES,"decoded project and bank tail fit canvas work");
static uint8_t *proj_image_work(void){return (uint8_t *)cv_work()+sizeof(project_t);}
static uint8_t *proj_image_tail(void){return proj_image_work();}
static project_store_t *proj_image_store(void){return (project_store_t *)proj_image_work();}
static uint32_t proj_source_hash;

/* The same representable ranges as proj_pack, without allocating its output.
 * This preflights generated images and track transforms before flash/runtime
 * mutation. The existing full codec remains the independent host oracle. */
static int proj_image_valid(const project_t *q)
{
    int8_t oct[PROJ_TRK];uint8_t step[8];
    if(q->sel>=NTRK||q->parts>NTRK||q->phys>PROJ_PHYS)return 0;
    if(P_COUNT>127u||!chain_valid(&q->chain)||!motion_valid(&q->motion)||q->g[G_FXRETURN]<0||q->g[G_FXRETURN]>127||q->g[G_FXMODE]<0||q->g[G_FXMODE]>1)return 0;
    for(uint32_t i=0;i<FX_SLOTS*FX_PARAMS;i++)if(q->g[G_MFXTYPE+i]<GP[G_MFXTYPE+i].min||q->g[G_MFXTYPE+i]>GP[G_MFXTYPE+i].max)return 0;
    for(uint32_t i=0;i<FX_SLOTS;i++)if(q->g[G_MFXTONE+i]<-FX_TONE_MAX||q->g[G_MFXTONE+i]>FX_TONE_MAX)return 0;
    for(uint32_t k=0;k<PROJ_TRK;k++){
        oct[k]=q->t[k].mpc_octave;
        if(q->t[k].engine>=NENGINES||q->pattern[k]>=NPAT||!cz_patch_valid(q->cz[k].raw)||!p5_patch_valid(&q->p5[k])||!drum_patch_valid(&q->drum[k]))return 0;
        for(uint32_t i=0;i<FM6_PACKED;i++)if(q->fm6[k][i]>127u)return 0;
        for(uint32_t i=0;i<P_COUNT;i++)if(q->t[k].p[i]<-64||q->t[k].p[i]>127)return 0;
        for(uint32_t i=0;i<NSTEP;i++)if(!proj_step_pack(step,&q->t[k].step[i]))return 0;
    }
    if(!mpc_octaves_pack(oct))return 0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(!recording_valid(&q->recording[i])||(q->recording[i].vel&&(recorded_owner(&q->recording[i])>=proj_owners()||recorded_step(&q->recording[i])>=64u)))return 0;
    return 68u+PROJ_TRK*(P_COUNT+2u+NSTEP*8u)+sizeof q->chain+sizeof q->motion+2u+2u*PROJ_TRK<=PROJ_MPC_OCT_WIRE;
}
static int proj_image_capture(int runtime)
{
    project_t *q=&proj_scratch;uint32_t f=motion_guard();
    project_capture(q);
    int ok=bank_pack_tail(proj_image_tail(),q,runtime);
    motion_unguard(f);
    /* The captured RAM is private until this synchronous operation returns;
     * validation can run with audio interrupts enabled. */
    return ok&&proj_image_valid(q)&&bank_tail_valid(proj_image_tail());
}
/* Hash-only probes can consume the serialized record before making the tail.
 * They never write flash. A concurrent active-bank/timing change is treated as
 * dirty; a subsequent save captures one guarded, consistent image. */
static int proj_image_hash(int runtime,uint32_t *result)
{
    project_t *q=&proj_scratch;
    bank_wide=(uint8_t)(runtime && cap_tracks()>PROJ_TRK);   /* (the FUN record's takes: as the tail's kind) */
    if(!proj_pack(proj_image_store(),q))return 0;
    uint32_t header[2]={bank_magic(),BANK_STORE_SIZE};
    uint32_t hash=fnv(fnv(2166136261u,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    if(!bank_pack_tail(proj_image_tail(),q,runtime)||!bank_tail_valid(proj_image_tail()))return 0;
    hash=fnv(hash,proj_image_tail(),BANK_TAIL_SIZE-4u);
    *result=fnv(hash,&hash,sizeof hash);return 1;
}
static int proj_stream_read_hash(uint32_t obj,uint32_t copy,uint32_t off,uint32_t n,uint32_t *fn,uint32_t *crc)
{
    wire_take();
    while(n){
        uint32_t k=n>sizeof proj_wire_u.raw?sizeof proj_wire_u.raw:n;
        if(st_read_range(obj,copy,off,proj_wire_u.raw,k))return -1;
        if(fn)*fn=fnv(*fn,proj_wire_u.raw,k);
        if(crc)*crc=st_crc32_update(*crc,proj_wire_u.raw,k);
        off+=k;n-=k;
    }
    return 0;
}
static int proj_stream_generate(st_stream_t *w,uint32_t obj)
{
    project_t *q=&proj_scratch;uint8_t *tail=proj_image_tail();
    if(!proj_image_valid(q)||!bank_tail_valid(tail))return -1;
    uint32_t tail_crc=st_crc32(tail,BANK_TAIL_SIZE-4u);
    int rc=st_stream_begin(w,obj,BANK_STORE_SIZE);if(rc)return rc;
    if((rc=st_stream_write_at(w,BANK_ACTIVE_OFF,tail,BANK_TAIL_SIZE-4u)))return rc;
    /* Verify the tail while its captured bytes still exist in RAM. */
    uint32_t check=0xffffffffu;
    if(proj_stream_read_hash(obj,w->head.slot,BANK_ACTIVE_OFF,BANK_TAIL_SIZE-4u,0,&check)||~check!=tail_crc)return -7;
    /* The tail is now immutable flash scratch; its RAM becomes the active
     * serializer output. The decoded project remains untouched beside it. */
    if(!proj_pack(proj_image_store(),q))return -1;
    uint32_t header[2]={bank_magic(),BANK_STORE_SIZE};
    uint32_t hash=fnv(fnv(2166136261u,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    uint32_t crc=st_crc32_update(st_crc32_update(0xffffffffu,header,sizeof header),proj_image_store(),sizeof(project_store_t));
    if(proj_stream_read_hash(obj,w->head.slot,BANK_ACTIVE_OFF,BANK_TAIL_SIZE-4u,&hash,&crc))return -7;
    if((rc=st_stream_write_at(w,0,header,sizeof header))||
       (rc=st_stream_write_at(w,8u,proj_image_store(),sizeof(project_store_t)))||
       (rc=st_stream_write_at(w,BANK_STORE_SIZE-4u,&hash,4u)))return rc;
    crc=st_crc32_update(crc,&hash,4u);st_stream_seal(w,crc);
    proj_source_hash=fnv(hash,&hash,4u);
    return st_stream_check(w);
}
static int proj_stream_load(uint32_t obj,uint32_t copy,uint32_t len)
{
    project_t *q=&proj_scratch;uint32_t hash=2166136261u;
    proj_source_hash=0;bank_wide=0;
    if(bank_full(len)){
        uint32_t header[2];
        if(st_read_range(obj,copy,0,header,sizeof header)||(header[0]!=BANK_MAGIC&&header[0]!=BANKL_MAGIC)||header[1]!=len)return 0;
        bank_wide=(uint8_t)(header[0]==BANKL_MAGIC);
        if(bank_wide&&st_read_range(obj,copy,BANKL_REC_OFF,proj_rec_ext,sizeof proj_rec_ext))return 0;
        if(st_read_range(obj,copy,8u,proj_image_store(),sizeof(project_store_t)))return 0;
        hash=fnv(fnv(hash,header,sizeof header),proj_image_store(),sizeof(project_store_t));
        if(!proj_import(q,proj_image_store(),sizeof(project_store_t)))return 0;
        if(st_read_range(obj,copy,BANK_ACTIVE_OFF,proj_image_tail(),BANK_TAIL_SIZE))return 0;
        hash=fnv(hash,proj_image_tail(),BANK_TAIL_SIZE-4u);uint32_t sum;
        memcpy(&sum,proj_image_tail()+BANK_TAIL_SIZE-4u,4);
        if(sum!=hash||!bank_tail_valid(proj_image_tail()))return 0;
        proj_source_hash=fnv(hash,&sum,4u);return 1;
    }
    if(len==sizeof(project_store_t)){
        if(st_read_range(obj,copy,0,proj_image_store(),len))return 0;
        return proj_import(q,proj_image_store(),(int)len);
    }
    if(len==sizeof *q){
        if(st_read_range(obj,copy,0,q,len))return 0;
        return proj_ok(q);
    }
    /* Prior raw snapshots: expand/convert each ten-byte note without ever
     * holding the complete old and new layouts at once. */
    if(len==sizeof *q+RECORD_MAX){
        uint32_t sum,head[2];
        if(st_read_range(obj,copy,0,head,sizeof head)||head[0]!=PROJ_MAGIC||head[1]!=len||
           st_read_range(obj,copy,len-4u,&sum,4u)||proj_stream_read_hash(obj,copy,0,len-4u,&hash,0)||hash!=sum)return 0;
        uint32_t at=__builtin_offsetof(project_t,recording),end=__builtin_offsetof(project_t,pattern);
        if(st_read_range(obj,copy,0,q,at))return 0;
        for(uint32_t i=0;i<RECORD_MAX;i++){
            uint8_t r[10];if(st_read_range(obj,copy,at+i*10u,r,sizeof r))return 0;
            recorded_note_t v={0};
            if(r[5]){
                if(r[4]>127u||r[5]>127u||(int8_t)r[8]<-24||(int8_t)r[8]>24||r[9]>=8u)return 0;
                v=(recorded_note_t){.on=(uint16_t)(r[0]|r[1]<<8),.duration=(uint16_t)(r[2]|r[3]<<8),.note=r[4],.vel=r[5],.owner=r[6],.step=r[7],.pitch=(int8_t)r[8],.length=r[9]&1u,.extended=r[9]>>1};
            }
            q->recording[i]=v;
        }
        if(st_read_range(obj,copy,at+RECORD_MAX*10u,(uint8_t *)q+end,sizeof *q-end))return 0;
        q->size=sizeof *q;q->sum=proj_sum(q);return proj_ok(q);
    }
    return 0;
}
