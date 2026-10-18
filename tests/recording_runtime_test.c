/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Capture duration and queued playback deadlines share one word per event.
 * Prove the live -> sealed -> sounding and sounding -> recaptured handoffs,
 * including nonzero stale words, clock wrap and Take Taker pause boundaries. */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
#if !MELODEE_RECORD_QUEUE
#error "This fixture exercises shared queued-recorder counters"
#endif
static int complete_store(void)
{
    raw_begin();seq_stop();song.rec=0;track_t *t=TSEL;t->p[P_SLEN]=NSTEP;
    for(uint32_t s=0;s<NSTEP;s++){
        memset(&t->step[s],0,sizeof t->step[s]);t->step[s].time=ST_NOTE;t->step[s].flags=SF_RECORDED;
    }
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recorded_note_t r={.duration=1,.note=(uint8_t)(i%128u),.vel=100};
        recorded_set_step(&r,i%NSTEP,0,0);recording_restore_note(t,i,r);
        recording_flags[i]=1;recording_run[i].held=1000u+i*6000u;
    }
    recording_reindex();int ok=sizeof recording_run==RECORD_MAX*4u;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        uint32_t duration=1000u+i*6000u,exp=0;
        while(duration>65535u){duration>>=1;exp++;}
        recording_finish(i);
        ok &= recording[i].duration==duration && recorded_exponent(&recording[i])==exp;
        /* The counter is nonzero, but a sealed event is not sounding. */
        ok &= !recording_sounding(i) && recording_left(i)==0;
    }
    int bad=check("all 1024 hold counters seal exact durations without creating phantom releases",ok);
    recording_clock[0]=UINT32_MAX-500u;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        recorded_note_t saved=recording[i];recording_fire(t,i,RECORD_UNIT,0);
        uint32_t gate=(uint32_t)saved.duration<<recorded_exponent(&saved);
        ok &= recording_sounding(i) && recording_left(i)==gate && !memcmp(&recording[i],&saved,sizeof saved);
    }
    recording_queue_tick(t,999);ok &= recording_left(0)==1;
    recording_queue_tick(t,1);ok &= !recording_sounding(0) && recording_sounding(1);
    recording_queue_tick(t,7000000);ok &= !recording_heap_count[0];
    bad+=check("playback reuses every sealed counter and releases correctly across clock wrap",ok);
    seq_stop();return bad;
}
static int recapture_and_capture_pause(void)
{
    raw_begin();seq_stop();song.rec=0;track_t *t=TSEL;
    recorded_note_t r={.duration=1000,.note=60,.vel=100};recording_restore_note(t,0,r);
    t->step[0].time=ST_NOTE;t->step[0].flags=SF_RECORDED;recording_reindex();
    recording_fire(t,0,RECORD_UNIT,0);
    int ok=recording_sounding(0) && recording_heap_count[0]==1;
    t->seq_idx=0;t->seq_pos=0;
    ok &= recording_note(t,60,99,0);
    ok &= (recording_flags[0]&1u) && !recording_sounding(0) && !recording_heap_count[0] && recording_run[0].held==0;
    /* Pass the old deadline while the replacement capture is held. */
    recording_tick(t,1001);
    uint32_t held=recording_run[0].held;
    ok &= held>0 && (recording_flags[0]&1u) && recording_left(0)==0;
    song.playing=1;song.rec=1;uint32_t w=cap_w;
    cap_on(&trk[1],67,90);cap_off(&trk[1],67);
    ok &= cap_w==w && recording_run[0].held==held;
    recording_finish(0);recorded_note_t saved=recording[0];
    recording_flags[0]&=(uint8_t)~4u;recording_fire(t,0,RECORD_UNIT,0);
    ok &= recording_sounding(0) && recording_left(0)==((uint32_t)saved.duration<<recorded_exponent(&saved));
    ok &= !memcmp(&recording[0],&saved,sizeof saved);
    int bad=check("recapture cancels the old deadline; Take Taker pauses on other tracks",ok);
    seq_stop();return bad;
}
static int take_taker_boundaries(void)
{
    pass_begin(0,16,0);
    for(uint32_t k=0;k<NTRK;k++){
        trk[k].p[P_SDIV]=TSEL->p[P_SDIV];trk[k].p[P_SSWING]=0;
    }
    trk[1].p[P_SLEN]=32;
    uint32_t loop=recording_loop(TSEL,seq_div_samples(TSEL->p[P_SDIV]));
    seq_advance(100);
    uint32_t first=cap_w;
    input_on(&trk[1],72,90);rec_set(TSEL,1);
    input_on(&trk[1],73,91);input_off(&trk[1],73);
    int bad=check("queued recording leaves Take Taker collecting until loop zero",
        song.rec_wait==1 && !rec_any() && cap_w==first+2u);
    seq_advance(loop-100u);
    uint32_t w=cap_w;
    int ok=rec_any() && !song.rec_wait;
    for(uint32_t k=0;k<NTRK;k++){cap_on(&trk[k],80+k,90);cap_off(&trk[k],80+k);}
    /* Releasing an onset retained before punch-in must still close its hold. */
    input_off(&trk[1],72);
    ok &= cap_w==w && !(cap_ev[first%CAP_N].tr&128u) && cap_ev[first%CAP_N].dur>0;
    uint32_t gen=recording_generation;song.sel=1;
    capture_take();song.sel=0;
    ok &= recording_generation==gen && msg_is("RECORDING");
    bad+=check("any active take pauses every track and capture action, while old held notes close",ok);
    rec_set(&trk[1],1);
    seq_advance(loop);
    cap_on(&trk[2],80,90);
    bad+=check("Take Taker stays paused when one take ends as another track punches in",
        song.rec==2 && rec_any() && cap_w==w);
    seq_advance(2u*loop);
    cap_on(&trk[2],80,90);cap_off(&trk[2],80);
    bad+=check("Take Taker resumes immediately when the final one-pass recording ends",
        !song.rec && !rec_any() && song.playing && cap_w==w+1u);
    rec_set(TSEL,1);song.rec_latch|=1;
    seq_advance(2u*loop);w=cap_w;
    cap_on(&trk[3],81,90);
    ok=rec_any() && song.rec_latch==1 && cap_w==w;
    rec_set(TSEL,0);cap_on(&trk[3],81,90);cap_off(&trk[3],81);
    bad+=check("latched recording keeps Take Taker paused across loops; manual disarm resumes it",
        ok && !rec_any() && cap_w==w+1u);
    seq_stop();return bad;
}
int main(void)
{
    int bad=complete_store()+recapture_and_capture_pause()+take_taker_boundaries();
    if(!bad)puts("Recording shared runtime: 4096 bytes, capture/playback handoffs preserved");
    return bad!=0;
}
