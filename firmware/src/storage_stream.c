/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded sequential bank staging. An unpublished stage is scratch: readers
 * continue choosing the previous committed copy. Publication follows payload
 * readback and format validation by the caller. No raw addresses cross the API. */
typedef struct {
    st_hdr_t head;
    uint32_t obj, pos, crc, generation;
    int error;
} st_stream_t;
static int st_stream_begin(st_stream_t *w,uint32_t obj,uint32_t len)
{
    memset(w,0,sizeof *w);w->error=-1;
#if defined(MELODEE_AUDIO_PROFILE) && MELODEE_AUDIO_PROFILE
    if(AUDIO_LAB_GUARDED())return -9;
#endif
    if(!st_banked(obj)||len>st_capacity(obj))return -1;
    w->obj=obj; w->crc=0xffffffffu;
    w->generation=++st_stream_generation;
    int rc=st_project_allocate(obj,len,&w->head);
    if(rc)return w->error=rc;
    w->error=0;return 0;
}
/* Input must be RAM: the flash-program window cannot fetch from XIP. Calls
 * may use arbitrary packet lengths; split at physical pages and extents. */
static int st_stream_write_at(st_stream_t *w,uint32_t off,const void *data,uint32_t n)
{
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error)return w->error;
    if(off>w->head.len||n>w->head.len-off)return w->error=-1;
    const uint8_t *p=data;
    while(n){
        uint32_t span,at=st_address_at(w->obj,w->head.slot,off,&span,ST_PAYLOAD_OFF);
        if(!span)return w->error=-1;
        uint32_t k=256u-(at&255u);if(k>span)k=span;if(k>n)k=n;
        int rc=st_prog(at,p,k);if(rc)return w->error=rc;
        off+=k;p+=k;n-=k;
    }
    return 0;
}
static int st_stream_write(st_stream_t *w,const void *data,uint32_t n)
{
    int rc=st_stream_write_at(w,w->pos,data,n);if(rc)return rc;
    w->crc=st_crc32_update(w->crc,data,n);w->pos+=n;return 0;
}
/* Generated images may write disjoint pieces out of order. The caller seals
 * their exact coverage and expected CRC; readback must match before publish. */
static void st_stream_seal(st_stream_t *w,uint32_t crc)
{w->pos=w->head.len;w->crc=crc;}
static int st_stream_check(st_stream_t *w)
{
    if(w->generation!=st_stream_generation)return w->error=-6;
    if(w->error)return w->error;
    if(w->pos!=w->head.len)return -1;
    w->head.crc=~w->crc;
    return st_body(w->obj,w->head.slot,&w->head)?-7:0;
}
/* Optional commit: runtime backup/restore stages stay unpublished. A caller
 * can stream/read their payload with st_read_range using this exact copy. */
static int st_stream_publish(st_stream_t *w)
{
    int rc=st_stream_check(w);if(rc)return rc;
    st_hdr_t current;int copy=st_current(w->obj,&current);
    if(copy>=0?(copy==w->head.slot||current.seq+1u!=w->head.seq):w->head.seq!=1u)return -6;
    w->head.magic=ST_MAGIC;
    w->head.hcrc=st_crc32(&w->head,sizeof w->head-4u);
    if((rc=st_prog(st_sector(w->obj,w->head.slot),&w->head,sizeof w->head)))return rc;
    st_hdr_t check;
    if(st_head(w->obj,w->head.slot,&check)||memcmp(&check,&w->head,sizeof check)||st_body(w->obj,w->head.slot,&check))return -7;
    return 0;
}

/* Buffered and streamed saves share allocation, verification and publication. */
static int st_project_save(uint32_t obj, const void *src, uint32_t len)
{
    st_stream_t w; int rc=st_stream_begin(&w,obj,len);
    if(rc)return rc;
    if((rc=st_stream_write(&w,src,len)))return rc;
    return st_stream_publish(&w);
}
