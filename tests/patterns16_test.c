/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define NPAT 16u
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <assert.h>
static uint8_t image[BANK_STORE_SIZE],copy[BANK_STORE_SIZE];
static void all_banks(void)
{
    reset();memset(&motion,0,sizeof motion);memset(motion_pattern,0,sizeof motion_pattern);recording_reset();song.ntrk=4;
    for(uint32_t k=0;k<4;k++)for(uint32_t b=0;b<16;b++){
        assert(!pattern_request(&trk[k],b));trk[k].p[P_SLEN]=16+(b%4)*16;trk[k].p[P_SSWING]=b;trk[k].p[P_SGATE]=32+b;
        for(uint32_t i=0;i<64;i++)trk[k].step[i]=(step_t){{40+k*16+b,60,64,67},4,ST_NOTE,0,100,0,0,0};
        trk[k].step[3].flags|=SF_RECORDED;
        assert(!motion_set_event(&trk[k],3,P_PAN,(int16_t)(k*16+b)));
    }
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recorded_note_t r={.on=(uint16_t)(i*61),.duration=1234,.note=60,.vel=100,.step=3,.pitch=(int8_t)(i%49u)-24,.length=i&1u};
        recorded_set_owner(&r,i%64u);recorded_set_exponent(&r,3);recording[i]=r;
    }
    recording_reindex();
    assert(!project_save_as(0,"SIXTEEN"));assert(!project_dirty());
    assert(st_load(OBJ_BANK0,image,sizeof image)==sizeof image);
    assert(!put_all(3,image,sizeof image,st_crc32(image,sizeof image)));
    pattern_init();recording_reset();memset(&motion,0,sizeof motion);project_load(1);
    for(uint32_t k=0;k<4;k++)for(uint32_t b=0;b<16;b++){
        assert(!pattern_request(&trk[k],b));assert(trk[k].step[3].note[0]==40+k*16+b);
        assert(trk[k].p[P_SLEN]==16+(b%4)*16&&trk[k].p[P_SSWING]==b&&trk[k].p[P_SGATE]==32+b);
        assert(motion_count(&trk[k])==1);
    }
    uint32_t counts[64]={0};for(uint32_t i=0;i<RECORD_MAX;i++){
        assert(recording[i].vel&&recorded_owner(&recording[i])<64&&recorded_step(&recording[i])==3&&recorded_exponent(&recording[i])==3);
        counts[recorded_owner(&recording[i])]++;
    }
    for(uint32_t b=0;b<64;b++)assert(counts[b]==16);
    assert(!project_save(1));assert(!project_dirty());
    assert(!project_rename(1,"SIXTEEN TWO"));project_load(1);assert(!strcmp(proj_name,"SIXTEEN TWO"));
}
static void legacy(void)
{
    reset();FILE *f=fopen("tests/fixtures/patterns8.fbkk","rb");assert(f&&fread(image,1,sizeof image,f)==sizeof image);fclose(f);
    assert(proj_hash(image,sizeof image)==0xdae5ca39u);
    assert(!st_save(OBJ_BANK0,image,sizeof image));project_load(0);assert(proj_cur==0);
    for(uint32_t k=0;k<4;k++)for(uint32_t b=0;b<16;b++){
        pattern_t p;pattern_read(k,b,&p);assert(b<8?p.step[3].note[0]==40+k*8+b:p.step[3].time==ST_REST);
    }
    uint32_t counts[64]={0};for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel){
        uint32_t owner=recorded_owner(&recording[i]);assert(owner%16u<8u&&recorded_exponent(&recording[i])==3&&recording[i].duration==1234);counts[owner]++;
    }
    for(uint32_t k=0;k<4;k++)for(uint32_t b=0;b<16;b++)assert(counts[k*16+b]==(b<8?32:0));
    assert(!project_save(0));assert(st_load(OBJ_BANK0,copy,sizeof copy)==sizeof copy);uint32_t magic;memcpy(&magic,copy,4);assert(magic==BANK_MAGIC);
    project_load(0);assert(!project_dirty());
}
static void dense_capacity(void)
{
    reset();recording_reset();memset(&motion,0,sizeof motion);memset(motion_pattern,0,sizeof motion_pattern);
    pattern_t p;for(uint32_t i=0;i<4;i++)p.timing[i]=TP[P_SLEN+i].def;
    for(uint32_t i=0;i<64;i++)p.step[i]=(step_t){{40+i%70u,60,64,67},4,ST_NOTE,0,(uint8_t)(60+i%60u),0,0,0};
    for(uint32_t k=0;k<2;k++)for(uint32_t b=0;b<16;b++)assert(pattern_write(k,b,&p));
    assert(pattern_total_cost()==PAT_DENSE_BUDGET&&pattern_total_wire_cost()==PAT_WIRE_BUDGET);
    assert(!pattern_write(2,0,&p));step_t before=trk[2].step[0],value={.note={60},.n=1,.time=ST_NOTE,.vel=100};
    assert(!pattern_set_step(&trk[2],0,&value)&&!memcmp(&before,&trk[2].step[0],sizeof before));
    for(uint32_t b=0;b<16;b++)assert(!pattern_request(&trk[0],b));
    assert(!project_save(0));project_load(0);assert(pattern_total_cost()==PAT_DENSE_BUDGET);
    assert(!track_remove(0));assert(trk_n()==3);assert(!project_save(0));
}
int main(void){all_banks();legacy();dense_capacity();puts("16 patterns: all banks, 64 note owners, exact timing, full motion, four-slot storage, legacy mapping and dense-capacity admission passed");return 0;}
