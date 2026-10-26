/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SONG arranges the current project's banks, one per track per row.
 * Main-loop preparation commits the original selections. The ISR switches
 * bounded in-RAM patterns and timing; STOP restores the original selections. */
#include "pattern.c"
static chain_config_t chain_config;
static struct {
    uint8_t original[NTRK];
    chain_config_t config;
    int16_t timing[NTRK][4];
    volatile uint8_t armed, running, row, remaining;
    uint8_t slot, rec, start, seek_chase;
    uint32_t carry, rest_pos, seek_phase[NTRK];
    uint64_t seek_samples[NTRK];
} chain;

static void seq_release(track_t *t);
static void seq_stop(void);
static uint32_t div_samples(uint32_t div);
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);

static void chain_defaults(chain_config_t *c)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < CHAIN_ROWS; i++)
        c->row[i].repeat = 1;
}
static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS || c->rsv[CHAIN_LOOP] > 1u)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->row[i].slot > CHAIN_HOLD || !c->row[i].repeat || c->row[i].repeat > 16u ||
            (c->row[i].slot == CHAIN_HOLD && (!i || c->row[i-1u].slot == CHAIN_REST)))
            return 0;
    return 1;
}
static void chain_legacy_defaults(chain_legacy_t *c)
{
    memset(c, 0, sizeof *c);
    for (uint32_t i = 0; i < CHAIN_LEGACY_ROWS; i++) c->row[i].repeat = 1;
}
static int chain_legacy_valid(const chain_legacy_t *c)
{
    if (c->count > CHAIN_LEGACY_ROWS) return 0;
    for (uint32_t i = 0; i < c->count; i++)
        if (c->row[i].slot >= NPAT || !c->row[i].repeat || c->row[i].repeat > 16u) return 0;
    return 1;
}
static void chain_from_legacy(const chain_legacy_t *c)
{
    chain_defaults(&chain_config);
    chain_config.count = c->count;
    memcpy(chain_config.row, c->row, sizeof c->row);
}
static int chain_rest(const track_t *t)
{
    return chain.running && chain_patterns[chain.row][t - trk] == CHAIN_REST;
}
static uint32_t chain_origin(uint32_t row, uint32_t track)
{
    while (row && chain_patterns[row][track] == CHAIN_HOLD) row--;
    return row;
}
static uint32_t chain_bank(uint32_t row, uint32_t track)
{
    return chain_patterns[chain_origin(row, track)][track];
}
static uint32_t chain_block_end(uint32_t row, uint32_t track)
{
    while (row + 1u < chain_config.count && chain_patterns[row+1u][track] == CHAIN_HOLD) row++;
    return row + 1u;
}
static uint64_t chain_row_samples(uint32_t row)
{
    uint32_t bank = chain_patterns[row][0];
    if (bank >= NPAT) return (uint64_t)16u * seq_div_samples(2u) * chain_config.row[row].repeat;
    uint32_t len = (uint32_t)pattern_timing(0, bank, 0);
    uint32_t period = seq_div_samples((uint32_t)pattern_timing(0, bank, 1));
    int32_t swing = clamp(pattern_timing(0, bank, 2) + song.g[G_SWING], 0, SWING_MAX);
    return ((uint64_t)len * period + (len & 1u) * (uint32_t)(swing * (int32_t)period / 250)) * chain_config.row[row].repeat;
}
/* Long arrangements exceed 32-bit samples. The target has no 64-bit divide
 * runtime; the common short case still uses its native 32-bit operation. */
static uint32_t chain_div_samples(uint64_t value, uint32_t divisor, uint32_t *remainder)
{
    if (value <= 0xffffffffu) {
        if (remainder) *remainder = (uint32_t)value % divisor;
        return (uint32_t)value / divisor;
    }
    uint32_t q = 0, r = 0;
    for (int bit = 63; bit >= 0; bit--) {
        r = (r << 1) | (uint32_t)(value >> bit & 1u);
        uint32_t take = r >= divisor;
        if (take) r -= divisor;
        q = (q << 1) | take;
    }
    if (remainder) *remainder = r;
    return q;
}
static const step_t *seq_steps(const track_t *t)
{
    return t->step;
}
static void motion_restore(track_t *t);
static void chain_apply(int starting)
{
    uint32_t i;
    chain.slot = chain.config.row[chain.row].slot;
    chain.rest_pos = chain.carry;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (!starting && chain_patterns[chain.row][i] == CHAIN_HOLD) continue;
        seq_release(t);
        motion_restore(t);
        recording_stop(t);
        uint32_t bank = chain_bank(chain.row, i);
        if (bank < NPAT) pattern_apply(t, bank);
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
    }
}
static void chain_start(void)
{
    uint32_t i;
    if (!chain.armed)
        return;
    chain.rec = song.rec;
    song.rec = 0;
    for (i = 0; i < NTRK; i++) {
        chain.original[i] = trk[i].pattern;
        memcpy(chain.timing[i], &trk[i].p[P_SLEN], sizeof chain.timing[i]);
    }
    chain.row = chain.start;
    chain.remaining = chain.config.row[chain.row].repeat;
    chain.carry = 0;
    chain.running = 1;
    chain.armed = 0;
    chain_apply(1);
}
static void chain_stop(void)
{
    uint32_t i;
    chain.armed = 0;
    if (!chain.running)
        return;
    for (i = 0; i < NTRK; i++) {
        pattern_apply(&trk[i], chain.original[i]);
        memcpy(&trk[i].p[P_SLEN], chain.timing[i], sizeof chain.timing[i]);
    }
    song.rec = chain.rec;
    chain.running = 0;
}
static void chain_tick(uint32_t n)
{
    const track_t *t = &trk[0];
    uint32_t length;
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu) return;
    uint32_t position;
    int timed = chain_patterns[chain.row][0] >= NPAT;
    if (timed) {
        /* Rests and continuations retain the section's four-beat clock. */
        length = 16u * seq_div_samples(2u);
        position = chain.rest_pos;
        chain.rest_pos += n;
    } else {
        if (t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != (uint32_t)t->p[P_SLEN]) return;
        length = step_samples(t, seq_div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
        position = t->seq_pos;
    }
    if (position + n < length) return;
    if (chain.remaining > 1u) {
        chain.remaining--;
        if (timed) chain.rest_pos -= length;
        return;
    }
    chain.carry = position + n - length;
    if (chain.row + 1u >= chain.config.count) {
        if (!chain.config.rsv[CHAIN_LOOP]) { seq_stop(); return; }
        chain.row = 0;
    } else chain.row++;
    chain.remaining = chain.config.row[chain.row].repeat;
    chain_apply(0);
}
/* Foreground preparation follows a queued manual choice or SONG's next row.
 * STOP can always restore the original selections through the bounded fallback. */
static void pattern_poll(void)
{
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t bank = trk[k].pattern_next;
        if (chain.armed) bank = chain_bank(chain.start, k);
        else if (chain.running) {
            uint32_t row = chain.row + 1u;
            bank = row < chain.config.count ? chain_bank(row, k) :
                chain.config.rsv[CHAIN_LOOP] ? chain_bank(0, k) : chain.original[k];
        }
        pattern_prepare(k, bank);
    }
}
