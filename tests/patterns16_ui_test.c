/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
static void high_controls(void)
{
    ui_power_on();stop_transport();go_home();frame();btn_down(B_SEQ);frame();
    tap_key(PAT_KEY[15]);assert(TSEL->pattern==15u&&!strcmp(ui.msg,"PATTERN 16"));
    btn_up(B_SEQ);frame();song.rec=0;
    my_steps(TSEL);TSEL->step[0].note[0]=77;fm1_ms+=400;
    btn_down(B_SEQ);frame();key_down(PAT_KEY[15]);frames(1100);
    assert(ui.confirm==CF_PAT_KEY&&ui.pat_dlg==15u);
    tap_key(PAT_KEY[14]);key_up(PAT_KEY[15]);frame();btn_up(B_SEQ);frame();
    assert(pattern_at(0,14)->step[0].note[0]==77);
    uint32_t dim,breath;TSEL->pattern_next=13;seq_pend[0]=1u<<12;
    uint32_t lit=pattern_leds(&dim,&breath);
    assert((lit&(1u<<26))&&(dim&(1u<<24))&&(breath&(1u<<23))&&(breath&(1u<<21)));
    TSEL->pattern_next=0xff;seq_pend[0]=0;
    assert(PAT_KEY[0]==0&&PAT_KEY[7]==12&&PAT_KEY[8]==14&&PAT_KEY[15]==26);
    song.playing=1;trk[0].seq_idx=0;trk[0].p[P_SLEN]=64;
    assert(dq_defer(CF_PASTE_PAT,0,15,0));assert(seq_pend[0]&(1u<<15));
    song.playing=0;dq_poll();assert(!seq_pend[0]);
    assert(pattern_length_edit(17)==32&&pattern_length_edit(1)==16&&pattern_length_edit(64)==64);
}
static void full_pool_edits(void)
{
    ui_power_on();stop_transport();song.ntrk=4;
    pattern_t p;for(uint32_t i=0;i<4;i++)p.timing[i]=TP[P_SLEN+i].def;
    for(uint32_t i=0;i<64;i++)p.step[i]=(step_t){{40+i%70u,60,64,67},4,ST_NOTE,0,(uint8_t)(60+i%60u),0,0,0};
    for(uint32_t k=0;k<2;k++)for(uint32_t b=0;b<16;b++)assert(pattern_write(k,b,&p));
    song.sel=2;uint32_t generation=recording_generation;step_t untouched[NSTEP];memcpy(untouched,trk[2].step,sizeof untouched);
    uint8_t note=60;seq_entry_notes(&note,1);
    assert(!memcmp(untouched,trk[2].step,sizeof untouched)&&recording_generation==generation&&!ui.entry_open);
    assert(!recording_note(&trk[2],60,100,0));
    assert(recording_generation==generation&&!memcmp(untouched,trk[2].step,sizeof untouched));
    song.sel=0;
}
int main(void){high_controls();full_pool_edits();puts("16-pattern controls, queued bit 15, length grid and full-pool recording/entry rejection passed");return 0;}
