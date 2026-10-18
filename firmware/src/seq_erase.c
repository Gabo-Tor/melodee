/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* EDIT held during live sequence recording erases the selected bank at audio
 * step boundaries. The UI publishes a scoped request; physical release, track
 * changes, disarm and transport stop disable it without waiting for a UI frame. */
static int seq_erase_active(const track_t *t)
{
    uint32_t request = seq_erase_request, buttons = fm1_in.buttons;
    return (request >> 31) && rec_on(t) && song.seq_mode && song.sel == trk_index(t) &&
           (request & 63u) == recording_owner(t) && seq_erase_generation == t->pattern_gen &&
           (buttons & seq_erase_button) && !(buttons & ~((request >> 16) & 0x3FFFu));
}
static void seq_erase_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* Rebuild all affected rounded overview cells in one pass. A late hit can
 * share its rounded cell with an unvisited interval, including across zero. */
typedef void (*seq_event_projection_t)(track_t *,uint32_t,recorded_note_t *,const void *);
static void seq_erase_rebuild_projected(track_t *t,step_t *steps,uint64_t touched,const uint8_t *drop,seq_event_projection_t project,const void *context)
{
    if (!touched) return;
    uint64_t found = 0;
    for (uint32_t s = 0; s < NSTEP; s++) if ((touched >> s) & 1u) {
        step_t *st = &steps[s];
        st->n = st->hit = st->acc = st->vel = 0;
        st->flags &= (uint8_t)~SF_ACCENT;
    }
    for (uint32_t i = recording_head[recording_owner(t)]; i < RECORD_MAX; i = recording_next[i]) {
        if((drop&&((drop[i/8u]>>(i%8u))&1u))||!recording_active(t,i))continue;
        recorded_note_t projected;const recorded_note_t *r=&recording[i];
        if(project){project(t,i,&projected,context);r=&projected;}
        uint32_t view = recording_view(t, r);
        if (!((touched >> view) & 1u)) continue;
        found |= (uint64_t)1u << view;
        step_t *st = &steps[view];
        if (r->vel > st->vel) st->vel = r->vel;
        uint32_t lane = drum_lane(r->note);
        if (drum_track(t) && r->note == DRUM_LANE_NOTE[lane]) {
            st->hit |= (uint8_t)(1u << lane);
            if (r->vel > 110u) st->acc |= (uint8_t)(1u << lane);
        } else {
            uint32_t j;
            for (j = 0; j < st->n && st->note[j] != r->note; j++);
            if (j == st->n && st->n < 4u) st->note[st->n++] = r->note;
        }
        if (st->vel > 110u) st->flags |= SF_ACCENT;
    }
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    for (uint32_t s = 0; s < NSTEP; s++) if (((touched & ~found) >> s) & 1u) {
        seq_erase_clear(&steps[s]);
        for (uint32_t n = 1; s < len && n < len; n++) {
            step_t *tail = &steps[(s + n) % len];
            if (tail->time != ST_TIE) break;
            seq_erase_clear(tail);
        }
    }
}

static void seq_erase_rebuild_work(track_t *t,step_t *steps,uint64_t touched,const uint8_t *drop)
{seq_erase_rebuild_projected(t,steps,touched,drop,0,0);}
static void seq_erase_rebuild(track_t *t,uint64_t touched)
{
    step_t steps[NSTEP];memcpy(steps,t->step,sizeof steps);
    seq_erase_rebuild_work(t,steps,touched,0);
    if(pattern_can_steps(t,steps))memcpy(t->step,steps,sizeof steps);
}

static void seq_erase_pass(track_t *t,uint32_t step,int entered)
{
    uint32_t tr=trk_index(t);
    if(!seq_erase_active(t)){seq_erase_seen[tr]=0;return;}
    if(t->seq_pos>=0x7FFFFFFFu||(seq_erase_seen[tr]&&!entered))return;
    int starting=!seq_erase_seen[tr];uint8_t drop[RECORD_MAX/8u]={0};uint64_t touched=0;
    for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;i=recording_next[i])
        if(recording_active(t,i)&&(recorded_step(&recording[i])==step||(starting&&(recording_sounding(i)||(recording_flags[i]&1u))))){
            drop[i/8u]|=1u<<(i%8u);touched|=1ull<<recording_view(t,&recording[i]);
        }
    step_t preview[NSTEP];memcpy(preview,t->step,sizeof preview);seq_erase_rebuild_work(t,preview,touched,drop);
    uint32_t len=(uint32_t)clamp(t->p[P_SLEN],1,NSTEP),source=step;
    for(uint32_t n=0;n<len&&preview[source].time==ST_TIE;n++)source=(source+len-1u)%len;
    if(!(preview[source].flags&SF_RECORDED)){
        if(preview[source].time==ST_NOTE&&(preview[source].n||preview[source].hit))step=source;
        seq_erase_clear(&preview[step]);
        for(uint32_t n=1;n<len&&preview[(step+n)%len].time==ST_TIE;n++)seq_erase_clear(&preview[(step+n)%len]);
    }
    seq_erase_seen[tr]=1;
    if(!pattern_can_steps(t,preview))return;
    if(touched||memcmp(t->step,preview,sizeof preview))recording_undo_capture(t);
    for(uint32_t i=recording_head[recording_owner(t)];i<RECORD_MAX;){uint32_t next=recording_next[i];if((drop[i/8u]>>(i%8u))&1u)recording_remove(t,i);i=next;}
    memcpy(t->step,preview,sizeof preview);seq_release(t);
    t->rh_n=t->rh_ties=t->rskip_n=0;t->rh_elapsed=0;
}
