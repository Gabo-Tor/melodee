/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* SONG placement around the existing punch selector (ui_layer.c).
 * Keys / knob 1 choose FX; knob 2 selects a punch; knobs 3/4 place it in beats.
 * Each region caches its contents; timing edits never resend the selector. */
static void punch_selector_draw(int32_t fx, int32_t held, uint32_t nomem, uint32_t height);
static struct { uint32_t selector, context, card[4]; } spv;
static uint32_t punch_selector_leds(int32_t fx)
{
    return (((fm1_ms / 250u) & 1u) == 0u ? 0x7ffffffu : 0u) |
        (fx >= 0 ? 1u << punch_key_of_fx((uint32_t)fx) : 0u);
}
static uint32_t song_punch_leds(void)
{
    return punch_selector_leds((int32_t)chain_config.fx[song_punch.row][song_punch.event].fx - 1);
}
/* Twenty-four ticks per beat represent every supported straight/triplet grid. */
static uint32_t song_punch_unit(void)
{
    static const uint16_t ticks[] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384};
    uint32_t bank = chain_patterns[song_punch.row][0];
    uint32_t div = bank < NPAT ? (uint32_t)pattern_timing(0, bank, 1) : 2u;
    return ticks[div < NELEM(ticks) ? div : 2u];
}
static void song_punch_beats(char *b, uint32_t ticks, int position)
{
    uint32_t whole = ticks / 24u, rem = ticks % 24u, den = 24u;
    if (position) whole++;
    b[0] = 0;
    if (whole || !rem) fmt_int(b, whole);
    if (!rem) return;
    if (whole) str_cpy(b + str_len(b), "+", 2);
    for (uint32_t d = 2; d <= 3; d++)
        while (!(rem % d) && !(den % d)) { rem /= d; den /= d; }
    fmt_int(b + str_len(b), rem);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), den);
}
static void song_punch_change(uint32_t field, int32_t delta)
{
    uint8_t force = ui.force;
    song_punch_edit(field, delta);
    ui.force = force;                               /* changed contents invalidate their own placement cards */
}
static void song_punch_choose(uint32_t fx)
{
    chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    song_punch_change(0, (int32_t)fx + 1 - e->fx);
}
static void song_punch_next(void)
{
    for (uint32_t i = 1; i <= CHAIN_FX_EVENTS; i++) {
        uint32_t j = (song_punch.event + i) % CHAIN_FX_EVENTS;
        if (!chain_config.fx[song_punch.row][j].fx) { song_punch.event = (uint8_t)j; return; }
    }
    ui_message("ALL FOUR IN USE");
}
static void song_punch_input(uint32_t notes, uint32_t pressed, uint32_t oct, int erase)
{
    if (ui.home || cur_page()->graph != GR_SONG) { pop_close(); return; }
    if (pressed & (1u << panel.btn[B_PLAY])) {
        if (song.playing || seq_counting() || chain_busy()) transport_req = 2;
        else chain_play_ui();
    }
    if (oct & 1u) { pop_close(); return; }
    int32_t delta = panel_enc(EN_SELECT);
    if (delta) {
        song_punch.row = (uint8_t)clamp((int32_t)song_punch.row + delta, 0, chain_config.count - 1u);
        ui.song_row = song_punch.row;
    }
    delta = panel_enc(EN_K2);
    if (delta) song_punch.event = (uint8_t)clamp((int32_t)song_punch.event + delta, 0, CHAIN_FX_EVENTS - 1u);
    if (oct & 2u) song_punch_next();
    chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    if (erase) song_punch_change(0, -(int32_t)e->fx);
    delta = panel_enc(EN_K1);
    if (delta) {
        int32_t key = e->fx ? (int32_t)punch_key_of_fx(e->fx - 1u) : -1;
        song_punch_choose((uint32_t)punch_fx_of_key((uint32_t)clamp(key + delta, 0, 26)));
    }
    for (uint32_t k = 0; k < 27u; k++) if ((notes >> k) & 1u) song_punch_choose((uint32_t)punch_fx_of_key(k));
    delta = panel_enc(EN_K3);
    if (delta) song_punch_change(1, delta);
    delta = panel_enc(EN_K4);
    if (delta) song_punch_change(2, delta);
}
static void song_punch_draw(void)
{
    const chain_fx_t *e = &chain_config.fx[song_punch.row][song_punch.event];
    uint32_t fx = e->fx ? e->fx - 1u : ui.punch_view % PUNCH_NFX, unit = song_punch_unit();
    uint32_t style = ux.gen * 977u + ux.pal * 31u;
    uint32_t sig = fx * 131u + style;
    char b[32];
    draw_head();
    if (ui.force || spv.selector != sig) {
        spv.selector = sig;
        punch_selector_draw((int32_t)fx, -1, 0, 160);
    }
    sig = song_punch.row + (uint32_t)(e->fx != 0) * 256u + style;
    if (ui.force || spv.context != sig) {
        spv.context = sig;
        cv_begin(240, 18, T_BG);
        str_cpy(b, "SECTION ", sizeof b); fmt_int(b + 8, song_punch.row + 1u);
        cv_text_on(8, ty_y(9, TY_LB), TY_LB, b, T_THEME, T_BG);
        cv_text_r(232, ty_y(9, TY_LB), TY_LB, e->fx ? "EACH REPEAT" : "KEYS: ADD FX", T_MID, T_BG);
        cv_blit(0, 190);
    }
    static const char *const label[] = {"FX", "SLOT", "START", "BEATS"};
    static const uint8_t xpos[] = {4, 46, 98, 168}, width[] = {40, 50, 68, 68};
    for (uint32_t k = 0; k < 4u; k++) {
        if (!k) {
            if (!e->fx) str_cpy(b, "Off", sizeof b);
            else {
                static const char *const name[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                uint32_t note = 53u + punch_key_of_fx(fx);
                str_cpy(b, name[note % 12u], sizeof b); fmt_int(b + str_len(b), note / 12u - 1u);
            }
        } else if (k == 1u) { fmt_int(b, song_punch.event + 1u); str_cpy(b + str_len(b), "/4", 3); }
        else if (!e->fx) str_cpy(b, "--", sizeof b);
        else song_punch_beats(b, (k == 2u ? e->start : e->length) * unit, k == 2u);
        sig = str_hash(style, b);
        if (!ui.force && spv.card[k] == sig) continue;
        spv.card[k] = sig;
        cv_begin(width[k], 32, T_BG);
        cv_rrect(0, 0, width[k], 32, 4, T_SURF, T_BG);
        cv_text_c(width[k] / 2, ty_y(8, TY_LB), TY_LB, label[k], T_MID, T_SURF);
        cv_text_c(width[k] / 2, ty_y(23, TY_LB), TY_LB, b, T_TEXT, T_SURF);
        cv_blit(xpos[k], 208);
    }
}
