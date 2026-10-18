/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the actual FLASH path, with the complete RAM codec as its oracle. */
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <assert.h>
static uint8_t oracle[BANK_STORE_SIZE],actual[BANK_STORE_SIZE],old_flash[sizeof nor];
static void workload(uint32_t count,uint32_t spread,uint32_t tail,uint32_t quant,uint32_t engine)
{
    reset();song.ntrk=NTRK;
    const uint8_t eng[]={19,12,15,3,10,10,0};
    for(uint32_t k=0;k<NTRK;k++){
        track_t *t=&trk[k];set_engine_of(t,eng[(engine+k)%NELEM(eng)]);apply_preset_to(t,0);
        t->p[P_VOICE]=V_POLY;t->p[P_RECQ]=quant?3:0;
        for(uint32_t b=0;b<NPAT;b++){
            pattern_request(t,b);t->p[P_SLEN]=16+b;t->p[P_SDIV]=2;
            t->step[3]=(step_t){{40+k*8+b,60,64,67},4,ST_NOTE,SF_ACCENT,100,0x81,0x80,20+b};
        }
    }
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r={.on=quant?0:(uint16_t)(8192u+i*1777u),.duration=(uint16_t)(14000u+i*109u),.note=48+i%40,.vel=70+i%58,.step=3,.pitch=(int8_t)(i%49u)-24,.length=i&1u};
        r.owner=(spread?i%32u:7u)|(tail?3u<<5:0u);recording[i]=r;assert(recording_valid(&r));
    }
    recording_reindex();chain_config.count=1;chain_config.row[0]=(chain_row_t){0,2};
    for(uint32_t k=0;k<NTRK;k++)chain_patterns[0][k]=k+1;
}
int main(void)
{
    uint32_t cases=0;const uint32_t counts[]={64,256,RECORD_MAX};
    for(uint32_t e=0;e<7;e++)for(uint32_t c=0;c<3;c++)for(uint32_t q=0;q<2;q++)for(uint32_t s=0;s<2;s++)for(uint32_t t=0;t<2;t++){
        workload(counts[c],s,t,q,e);project_capture(&proj_scratch);
        assert(bank_pack(oracle,&proj_scratch,1));
        uint32_t hash;project_capture(&proj_scratch);assert(proj_image_hash(1,&hash)&&hash==proj_hash(oracle,sizeof oracle));
        assert(proj_image_capture(1));st_stream_t w;
        assert(!proj_stream_generate(&w,OBJ_BANK0));
        assert(st_current(OBJ_BANK0,0)<0);
        assert(!st_read_range(w.obj,w.head.slot,0,actual,sizeof actual)&&!memcmp(actual,oracle,sizeof actual));
        assert(!proj_publish_slot(0,&w));proj_cur=0;assert(!project_dirty());
        assert(proj_read_slot(0)==BANK_STORE_SIZE);assert(proj_source_hash==hash);
        project_load(0);project_capture(&proj_scratch);assert(bank_pack(actual,&proj_scratch,1));
        assert(proj_image_hash(1,&hash)&&hash==proj_hash(actual,sizeof actual));
        assert(project_dirty()==(hash!=proj_saved_hash[0]));
        /* A rename alters only the active record and container checksum. */
        assert(!project_rename(0,"RENAMED"));
        assert(st_load(OBJ_BANK0,actual,sizeof actual)==sizeof actual);
        assert(!memcmp(actual+BANK_ACTIVE_OFF,oracle+BANK_ACTIVE_OFF,BANK_TAIL_SIZE-4u));
        cases++;
    }
    /* Generated write failure must preserve all four previously saved slots. */
    workload(256,1,1,0,0);for(uint32_t k=0;k<4;k++)assert(!project_save(k));
    memcpy(old_flash,nor,sizeof nor);memcpy(oracle,proj_bank_slot[0],sizeof oracle);
    erase_error=1;assert(project_save_as(0,"FAILED"));erase_error=0;
    assert(st_load(OBJ_BANK0,actual,sizeof actual)==sizeof actual&&!memcmp(actual,oracle,sizeof actual));
    assert(!memcmp(nor,old_flash,sizeof nor));
    assert(put_all(2,oracle,sizeof oracle,st_crc32(oracle,sizeof oracle)^1u)==2u);
    assert(st_load(OBJ_BANK0,actual,sizeof actual)==sizeof actual&&!memcmp(actual,oracle,sizeof actual));
    /* Track removal takes the same snapshot/restore route without using flash. */
    uint32_t erases_before=erases;assert(!track_remove(1));assert(erases==erases_before&&trk_n()==3);
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<NPAT;b++){
        pattern_t pat;pattern_read(k,b,&pat);
        assert(k==NTRK-1u?pat.step[3].time==ST_REST:pat.step[3].note[0]==40u+(k>=1u?k+1u:k)*8u+b);
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel)assert(recorded_owner(&recording[i])<3u*NPAT);
    assert(proj_image_capture(1));assert(proj_ok(&proj_scratch));
    /* Earlier raw snapshots use ten-byte notes, not the FUN wire codec. */
    static uint8_t legacy[sizeof(project_t)+RECORD_MAX];
    workload(RECORD_MAX,1,1,0,0);project_capture(&proj_scratch);
    static project_t expected;expected=proj_scratch;
    uint32_t at=__builtin_offsetof(project_t,recording),end=__builtin_offsetof(project_t,pattern);
    memcpy(legacy,&expected,at);
    for(uint32_t i=0;i<RECORD_MAX;i++){
        const recorded_note_t *r=&expected.recording[i];uint8_t *v=legacy+at+i*10u;
        v[0]=r->on;v[1]=r->on>>8;v[2]=r->duration;v[3]=r->duration>>8;
        v[4]=r->note;v[5]=r->vel;v[6]=r->owner;v[7]=r->step;v[8]=r->pitch;v[9]=recorded_legacy_length(r);
    }
    memcpy(legacy+at+RECORD_MAX*10u,(const uint8_t *)&expected+end,sizeof expected-end);
    uint32_t len=sizeof legacy,sum;memcpy(legacy+4,&len,4);sum=proj_hash(legacy,len-4u);memcpy(legacy+len-4u,&sum,4);
    assert(!st_save(OBJ_BANK0+3u,legacy,len));assert(proj_read_slot(3)==len);
    assert(!memcmp(&expected,&proj_scratch,sizeof expected));
    legacy[at+8u]=25;sum=proj_hash(legacy,len-4u);memcpy(legacy+len-4u,&sum,4);
    assert(!st_save(OBJ_BANK0+3u,legacy,len));assert(!proj_read_slot(3)&&proj_meta[3].old);
    printf("Project stream: %u complete RAM-codec comparisons, hash/dirty/save/load/rename, failed save and track removal passed\n",cases);
    return 0;
}
