/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real panel, sequencer and engine paths: performance effects must never edit
 * the beat, send controller notes or strand a transposed/delayed note. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static int sounding(uint32_t t, uint32_t note)
{
    for (uint32_t k = 0; k < NVOICE; k++) if (trk[t].v[k].gate && trk[t].v[k].note == note) return 1;
    return 0;
}
static uint32_t velocity(uint32_t t, uint32_t note)
{
    for (uint32_t k = 0; k < NVOICE; k++) if (trk[t].v[k].gate && trk[t].v[k].note == note) return trk[t].v[k].vel;
    return 0;
}
static void reset(void)
{
    ui_power_on(); memset(&lys, 0, sizeof lys);
    for (uint32_t t = 0; t < NTRK; t++) {
        set_engine_of(&trk[t], ENGI_FM6); trk[t].engine = trk[t].eng_req;
        trk[t].p[P_VOICE] = V_POLY; trk[t].p[P_CHRD] = 0;
    }
    punch_target = 0; punch_chance = 100; punch_gate = 50; punch_repeats = 3;
    kb_prev = kb_layer = kb_lock = 0; fm1_in.notes = fm1_in.buttons = host_pressed = host_notes = 0;
    mo_r = mo_w = 0;
}
static void effect(uint32_t keys) { punch_panel_lock = 1; punch_claimed = kb_layer = keys; punch_keys = keys; punch_tick(0, FS / 2u); }
static void advance(uint32_t n)
{
    while (n) { uint32_t take = n > CTL ? CTL : n; punch_tick(take, FS / 2u); n -= take; }
}
static void fast_tap(uint32_t b) { btn_down(b); frame(); btn_up(b); frame(); }

static void gestures(void)
{
    reset(); fast_tap(B_FX); assert(!ui.home && cur_page()->graph == GR_FXRACK && !ui.layer);
    reset(); fast_tap(B_FX); fast_tap(B_FX);
    assert(ui.layer == LAYER_FX && ui.lock == LAYER_FX && ui.home);
    uint32_t w = mo_w;
    key_down(PN_UP12); frame();
    assert((punch_keys & PN_BIT(PN_UP12)) && !gates() && mo_w == w);
    frame(); punch_tick(0, FS / 2u); trk_note_on(TSEL, 60, 90); assert(sounding(0, 72) && !sounding(0, 60));
    key_up(PN_UP12); frame(); frame(); trk_note_off(TSEL, 60); assert(!gates());
    uint8_t page = ui.page; int16_t cutoff = TSEL->p[P_LRATE], level = TSEL->p[P_LEVEL];
    turn(EN_K1, 1); turn(EN_K2, 1); turn(EN_K3, 1); turn(EN_K4, -1); turn(EN_SELECT, 1);
    assert(punch_target == 1 && punch_repeats == 4 && punch_gate == 55 && punch_chance == 95);
    assert(ui.page == page && ui.home && TSEL->p[P_LRATE] == cutoff && TSEL->p[P_LEVEL] == level);
    fast_tap(B_FX); assert(!ui.lock && !ui.layer);
    reset(); key_down(white(0)); frame(); assert(gates());
    btn_down(B_FX); frame(); key_down(PN_ACCENT); frame();
    assert(ui.layer == LAYER_FX && (kb_layer & PN_BIT(PN_ACCENT)) && gates());
    btn_up(B_FX); frame(); assert(!punch_keys && !ui.layer);
    key_up(PN_ACCENT); key_up(white(0)); frame(); assert(!gates());
    reset(); fast_tap(B_FX); fast_tap(B_FX); fast_tap(B_OCTDN); assert(!ui.layer && !ui.lock && !song.octave);
    reset(); hold(B_HOME); btn_down(B_FX); frame(); key_down(PN_ACCENT); frame();
    assert(!ui.layer && !punch_keys); key_up(PN_ACCENT); btn_up(B_FX); frame();
    reset(); events_block(CTL); btn_down(B_FX); frame();
    key_down(PN_UP12); events_block(CTL); /* no UI frame between key and incoming note */
    midi_event(0x90, 0, 60, 90); assert(sounding(0, 72) && !sounding(0, 60));
    key_up(PN_UP12); events_block(CTL); assert(!punch.keys);
    midi_event(0x80, 0, 60, 0); assert(!gates()); btn_up(B_FX); frame();
    reset(); btn_down(B_GLO); frame(); key_down(white(0)); frame(); btn_up(B_GLO); frame();
    btn_down(B_FX); frame(); assert(!punch_keys); /* another layer's held key is never repurposed */
    key_down(PN_UP12); frame(); assert(punch_keys == PN_BIT(PN_UP12));
    key_up(PN_UP12); key_up(white(0)); btn_up(B_FX); frame();
    puts("MIDI punch: tap/hold/double tap, silent keys, preheld notes, exclusive knobs, exit and modal guard passed");
}
static void ownership(void)
{
    reset(); effect(PN_BIT(PN_UP12));
    trk_note_on(TSEL, 60, 90); assert(sounding(0, 72));
    effect(0); trk_note_on(TSEL, 72, 80); trk_note_off(TSEL, 60);
    assert(sounding(0, 72)); trk_note_off(TSEL, 72); assert(!gates());
    effect(PN_BIT(PN_DOWN12)); trk_note_on(TSEL, 4, 90); assert(!gates()); trk_note_off(TSEL, 4);
    effect(PN_BIT(PN_UP12)); punch_target = NTRK; punch_tick(0, FS / 2u);
    for (uint32_t t = 0; t < NTRK; t++) { trk_note_on(&trk[t], 60, 90); assert(sounding(t, 72)); }
    effect(0); for (uint32_t t = 0; t < NTRK; t++) trk_note_off(&trk[t], 60); assert(!gates());
    effect(PN_BIT(PN_ACCENT)); trk_note_on(TSEL, 60, 40); assert(velocity(0, 60) == 127); trk_note_off(TSEL, 60);
    effect(PN_BIT(PN_SOFT)); trk_note_on(TSEL, 60, 100); assert(velocity(0, 60) == 50); trk_note_off(TSEL, 60);
    punch_chance = 0; effect(PN_BIT(PN_UP12) | PN_BIT(PN_MUTE)); trk_note_on(TSEL, 60, 90);
    assert(sounding(0, 60)); trk_note_off(TSEL, 60); assert(!gates());
    assert(!mo_w);
    puts("MIDI punch: pitch collision ownership, bounds, all tracks, velocity and zero-chance bypass passed");
}
static void deadlines(void)
{
    reset(); effect(PN_BIT(PN_ECHO8)); trk_note_on(TSEL, 60, 100); trk_note_off(TSEL, 60);
    advance(FS / 4u - 1u); assert(!gates()); advance(1); assert(sounding(0, 60) && velocity(0, 60) == 75);
    advance(FS / 16u + 1u); assert(!gates()); effect(0); advance(FS * 2u); assert(!gates());
    effect(PN_BIT(PN_HUMAN) | PN_BIT(PN_UP12));
    for (uint32_t k = 0; k < 100u; k++) { trk_note_on(TSEL, 60, 80); trk_note_off(TSEL, 60); advance(FS / 40u); assert(!gates()); }
    effect(PN_BIT(PN_HUMAN) | PN_BIT(PN_ECHO8));
    for (uint32_t k = 0; k < 100u; k++) {
        trk_note_on(TSEL, 60, 80); uint32_t unstarted = punch.map[0][60] == PN_NONE;
        trk_note_off(TSEL, 60); if (unstarted) assert(!punch.pending);
        effect(0); advance(FS); assert(!gates()); effect(PN_BIT(PN_HUMAN) | PN_BIT(PN_ECHO8));
    }
    effect(PN_BIT(PN_SHORT)); trk_note_on(TSEL, 60, 80); assert(gates()); advance(FS / 32u + 1u); assert(!gates());
    trk_note_off(TSEL, 60);
    effect(0); trk_note_on(TSEL, 60, 100); trk_note_on(TSEL, 64, 90); trk_note_off(TSEL, 60); trk_note_off(TSEL, 64);
    effect(PN_BIT(PN_ROLL16T)); assert(sounding(0, 60) && sounding(0, 64));
    advance(FS / 24u + 1u); assert(!gates()); advance(FS / 24u); assert(gates());
    seq_stop(); assert(!gates()); advance(FS); assert(!gates());
    effect(0); effect(PN_BIT(PN_ECHO16));
    for (uint32_t k = 0; k < 128u; k++) { trk_note_on(TSEL, k, 90); trk_note_off(TSEL, k); }
    assert(punch.pending <= PN_QUEUE); advance(FS); effect(0); advance(FS); assert(!gates());
    for (uint32_t t = 0; t < NTRK; t++) trk_all_off(&trk[t]); assert(!punch.pending);
    puts("MIDI punch: echo timing, early humanized release, short gates, chord rolls, stop and queue overload passed");
}
static void unchanged_patterns(void)
{
    reset(); step_t *s = &TSEL->step[0]; *s = (step_t){{60},1,ST_NOTE,0,96};
    TSEL->p[P_SLEN] = 1; pattern_t before; pattern_read(0, TSEL->pattern, &before);
    for (uint32_t k = 0; k < PN_COUNT; k++) {
        effect(PN_BIT(k)); seq_start();
        for (uint32_t block = 0; block < FS / CTL; block++) events_block(CTL);
        seq_stop(); effect(0); advance(FS); assert(!gates());
        pattern_t after; pattern_read(0, TSEL->pattern, &after); assert(!memcmp(&before, &after, sizeof before));
        assert(!mo_w);
    }
    puts("MIDI punch: every effect through playing sequencer leaves patterns and USB MIDI untouched");
}
static void original_recording(void)
{
    reset(); song.rec = song.rec_latch = 1; seq_start(); seq_advance(0); effect(PN_BIT(PN_UP12) | PN_BIT(PN_ACCENT));
    input_on(TSEL, 60, 73); assert(sounding(0, 72) && velocity(0, 72) == 127);
    seq_advance(1200); input_off(TSEL, 60); seq_stop();
    assert(recording[0].note == 60 && recording[0].vel == 73 && recording[0].duration && !recording[1].vel);
    puts("MIDI punch: live recording retains the original pitch, velocity and gate");
}
int main(void) { gestures(); ownership(); deadlines(); unchanged_patterns(); original_recording(); printf("MIDI punch state: %zu bytes\n", sizeof punch); return 0; }
