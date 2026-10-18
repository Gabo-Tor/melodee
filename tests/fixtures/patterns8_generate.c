/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Constructed compatibility input. Do not regenerate on format changes. */
#define NPAT 8u
#define BACKUP_TEST_NO_MAIN 1
#include "../backup_test.c"
int main(void){reset();song.ntrk=4;
 for(uint32_t k=0;k<4;k++)for(uint32_t b=0;b<8;b++){pattern_request(&trk[k],b);trk[k].step[3]=(step_t){{40+k*8+b,60,64,67},4,ST_NOTE,SF_RECORDED,100,0,0,0};}
 for(uint32_t i=0;i<RECORD_MAX;i++)recording[i]=(recorded_note_t){.on=i*61u,.duration=1234,.note=60,.vel=100,.owner=(i%32u)|96u,.step=3,.pitch=(int8_t)(i%49u)-24,.length=i&1u};
 recording_reindex();project_capture(&proj_scratch);static uint8_t raw[BANK_STORE_SIZE];if(!bank_pack(raw,&proj_scratch,1))return 1;return fwrite(raw,1,sizeof raw,stdout)!=sizeof raw;}
