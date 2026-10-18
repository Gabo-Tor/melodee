/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FBKL omits the active-step arrays (all banks are in its compact tail) and
 * 96 unused control bytes. FUN23 itself keeps the old fixed offsets/extent. */
#define BANK_PREFIX_SIZE (PROJ_STORE_SIZE-NTRK*64u*8u-96u)
static int bank_prefix_hole(uint32_t pos)
{
    for(uint32_t k=0;k<NTRK;k++){
        uint32_t at=68u+(k+1u)*(P_COUNT+2u)+k*64u*8u;
        if(pos>=at&&pos<at+64u*8u)return 1;
    }
    return pos>=PROJ_GAP_START&&pos<PROJ_GAP_END;
}
static int bank_prefix_pack(uint8_t *out,const project_t *q)
{
    if(!proj_pack((project_store_t *)out,q))return 0;
    for(uint32_t i=0;i<PROJ_STORE_SIZE;i++)if(bank_prefix_hole(i))out[i]=0;
    uint32_t sum=proj_hash(out,PROJ_STORE_SIZE-4u);memcpy(out+PROJ_STORE_SIZE-4u,&sum,4);
    uint32_t n=0;for(uint32_t i=0;i<PROJ_STORE_SIZE;i++)if(!bank_prefix_hole(i))out[n++]=out[i];
    return n==BANK_PREFIX_SIZE;
}
static int bank_prefix_unpack(project_t *q,uint8_t *work)
{
    uint32_t n=BANK_PREFIX_SIZE;
    for(uint32_t i=PROJ_STORE_SIZE;i--;)work[i]=bank_prefix_hole(i)?0:work[--n];
    uint32_t magic;memcpy(&magic,work,4);return !n&&magic==PROJ_16_MAGIC&&work[66]==P_COUNT&&proj_import(q,work,PROJ_STORE_SIZE);
}
