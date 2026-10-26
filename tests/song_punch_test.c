/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void setup(uint32_t rows, uint32_t steps)
{
    ui_power_on();
    trk[0].p[P_SLEN] = (int16_t)steps; pattern_commit(&trk[0]);
    chain_config.count = (uint8_t)rows;
    go_page(GR_SONG); frame();
}
static void dsp(void)
{
    int32_t l[CTL], r[CTL];
    for (uint32_t b = 0; b < 6u; b++) {
        for (uint32_t i = 0; i < CTL; i++) { l[i] = 10000; r[i] = -2000; }
        punch_process(l, r, CTL);
    }
}
static int playback(void)
{
    int bad = 0; setup(2, 8);
    chain_config.row[0].repeat = 2;
    chain_config.fx[0][0] = (chain_fx_t){PX_MONO + 1, 1, 3};
    chain_config.fx[0][1] = (chain_fx_t){PX_LPF + 1, 2, 1};
    chain_config.fx[0][2] = (chain_fx_t){PX_REV + 1, 5, 1};
    chain_config.row[1].slot = CHAIN_REST; memset(chain_patterns[1], CHAIN_REST, NTRK);
    chain_config.fx[1][0] = (chain_fx_t){PX_GATE + 1, 1, 2};
    bad += check("timed punches prepare with the existing song transport", !chain_prepare());
    events_block(1); dsp();
    bad += check("a future buffered punch warms history from the song start", punch_song_req == -1 && punch_ring && punch_song_buffered);
    uint32_t period = seq_div_samples(trk[0].p[P_SDIV]);
    events_block(period); dsp();
    bad += check("the punch starts on its conductor step and reaches master DSP", punch_song_req == PX_MONO && punch.cur == PX_MONO);
    events_block(period); dsp();
    bad += check("an overlapping later punch temporarily takes priority", punch_song_req == PX_LPF && punch.cur == PX_LPF);
    events_block(period); dsp();
    bad += check("the earlier punch resumes until its own duration ends", punch_song_req == PX_MONO && punch.cur == PX_MONO);
    events_block(period); dsp();
    bad += check("duration ends on the first excluded step with a dry fade", punch_song_req == -1 && punch.cur == -1 && !punch.g);
    events_block(period); dsp();
    bad += check("buffered song effects play without the FX layer or a held key", punch_song_req == PX_REV && punch.cur == PX_REV && punch.req == -1 && !punch.keybit);
    for (uint32_t i = 0; i < 4; i++) events_block(period);
    bad += check("section repeats replay punch timing from step zero", !chain.row && chain.remaining == 1 && punch_song_req == PX_MONO);
    for (uint32_t i = 0; i < 7; i++) events_block(period);
    bad += check("entering a rest clears the preceding section punch", chain.row == 1 && punch_song_req == -1);
    events_block(seq_div_samples(2u)); dsp();
    bad += check("resting sections retain their own straight-step punch clock", punch_song_req == PX_GATE && punch.cur == PX_GATE);
    seq_stop(); dsp();
    bad += check("STOP clears song requests, fades dry and releases history", punch_song_req == -1 && !punch_song_buffered && punch.cur == -1 && !punch_ring);
    return bad;
}
static int overrides_and_restarts(void)
{
    int bad = 0; setup(1, 1);
    chain_config.row[0].repeat = 2;
    chain_config.fx[0][0] = (chain_fx_t){PX_LPF + 1, 0, 1};
    chain_prepare(); events_block(1); dsp();
    bad += check("filter-only songs do not allocate punch history", punch.cur == PX_LPF && !punch_ring);
    uint32_t gate = punch_song_gate;
    punch.cut = 40 << 8;
    events_block(seq_div_samples(trk[0].p[P_SDIV])); dsp();
    bad += check("a full-section punch restarts its sweep on the next repeat", punch_song_gate != gate && punch.cur == PX_LPF && punch.cut > (120 << 8));
    punch_panel_lock = 1; key_down(punch_key_of_fx(PX_MONO)); keyboard_block(); dsp();
    bad += check("a live FX key overrides the song without changing the saved lane", punch.cur == PX_MONO && punch_song_req == PX_LPF && chain_config.fx[0][0].fx == PX_LPF + 1);
    key_up(punch_key_of_fx(PX_MONO)); keyboard_block(); dsp();
    bad += check("releasing the live key resumes the active song punch", punch.cur == PX_LPF && punch.req == -1);
    punch_panel_lock = 0; seq_stop(); dsp();
    setup(2, 8); chain_patterns[1][0] = CHAIN_HOLD; chain_config.row[1].slot = CHAIN_HOLD;
    chain_config.fx[1][0] = (chain_fx_t){PX_MONO + 1, 0, 1};
    chain_prepare_from(1); events_block(1); dsp();
    bad += check("starting inside a continued block uses section-local FX timing", chain.row == 1 && punch_song_req == PX_MONO && punch.cur == PX_MONO);
    seq_stop(); dsp(); return bad;
}
static int editing(void)
{
    int bad = 0; setup(3, 16);
    press(B_OCTUP); sheet_do("Punch FX");
    bad += check("the section menu opens the existing visual punch selector", song_punch_on() && song_punch.row == 0 && song.grid == 2);
    key_down(punch_key_of_fx(PX_MONO)); keyboard_block(); frame();
    bad += check("choosing an FX key writes the punch without playing a note or live effect", chain_config.fx[0][0].fx == PX_MONO + 1 && kb_note[punch_key_of_fx(PX_MONO)] == KB_SILENT && punch.req == -1);
    key_up(punch_key_of_fx(PX_MONO)); keyboard_block(); frame();
    turn(EN_K3, 3);
    turn(EN_K4, 4);
    chain_fx_t expected = {PX_MONO + 1, 3, 5};
    bad += check("direct start and duration knobs edit a saved timed punch", !memcmp(&expected, &chain_config.fx[0][0], sizeof expected));
    bad += check("a new unsaved song with only punch edits is considered dirty", project_dirty());
    host_sent = 0; turn(EN_K4, 100);
    bad += check("punch duration cannot run beyond the section pattern", chain_config.fx[0][0].length == 13);
    bad += check("changing timing sends only placement controls, not the FX selector", host_sent < 6000u);
    press(B_OCTDN); hold(B_EDIT);
    bad += check("song undo restores a punch edit", chain_config.fx[0][0].length == 5);
    song_insert(1);
    bad += check("duplicating a section copies its timed master punches", !memcmp(chain_config.fx[0], chain_config.fx[1], sizeof chain_config.fx[0]));
    song_sections_move(); ui.song_row = chain_config.count; song_apply_destination();
    bad += check("moving a section moves its punches and closes the source gap", chain_config.fx[0][0].fx && !chain_config.fx[1][0].fx && chain_config.fx[3][0].fx);
    song_edit_history(0);
    bad += check("undo restores the section order and its punches together", chain_config.fx[0][0].fx && chain_config.fx[1][0].fx && !chain_config.fx[3][0].fx);
    song_punch.row = 0; song_punch_clear();
    bad += check("clear section FX preserves the section and other sections' punches", !chain_fx_count(0) && chain_fx_count(1) == 1 && chain_config.count == 4);
    song_edit_history(0);
    bad += check("clearing section FX is undoable", chain_fx_count(0) == 1);
    chain_prepare(); events_block(1); expected = chain_config.fx[0][0]; song_punch_edit(0, -1);
    bad += check("punch edits are refused during song playback", !memcmp(&expected, &chain_config.fx[0][0], sizeof expected) && msg_is("STOP TO EDIT"));
    seq_stop(); dsp(); return bad;
}
static int visual_controls(void)
{
    int bad = 0; setup(2, 16);
    press(B_FX);
    bad += check("FX tapped on SONG opens its visual punch editor directly", song_punch_on() && song_punch.row == 0);
    host_sent = 0; frame();
    bad += check("an idle song FX selector sends no screen redraw", !host_sent);
    ui_message("FIRST"); str_cpy(ui.msg2, "SECOND", sizeof ui.msg2); ui.msg_t = 1; frame();
    bad += check("feedback advances its queued message while the FX editor stays open", msg_is("SECOND") && !ui.msg2[0]);
    ui.msg_t = 0; frame();
    turn(EN_K1, 1);
    bad += check("knob 1 uses the existing keyboard's effect order", chain_config.fx[0][0].fx == punch_fx_of_key(0) + 1);
    turn(EN_K1, 1);
    bad += check("the next effect follows the next physical key", chain_config.fx[0][0].fx == punch_fx_of_key(1) + 1);
    press(B_OCTUP);
    bad += check("Enter selects the next free punch without adding phantom data", song_punch.event == 1 && chain_fx_count(0) == 1);
    turn(EN_K1, 1); turn(EN_K4, 4); turn(EN_K3, 100);
    bad += check("moving a punch to the section edge preserves its duration", chain_config.fx[0][1].start == 11 && chain_config.fx[0][1].length == 5);
    turn(EN_K3, -100);
    bad += check("moving the punch back retains its original duration", !chain_config.fx[0][1].start && chain_config.fx[0][1].length == 5);
    press(B_EDIT);
    bad += check("EDIT removes only the selected punch", !chain_config.fx[0][1].fx && chain_fx_count(0) == 1 && song_punch_on());
    hold(B_EDIT);
    bad += check("EDIT held restores the deleted punch through song undo", chain_config.fx[0][1].fx && song_punch_on());
    char text[32]; song_punch_beats(text, 18, 1);
    bad += check("start time is a one-based beat plus a musical fraction", !strcmp(text, "1+3/4"));
    song_punch_beats(text, 8, 0);
    bad += check("triplet durations display a reduced beat fraction", !strcmp(text, "1/3"));
    song_punch_beats(text, 30, 0);
    bad += check("longer durations display whole beats and their fraction", !strcmp(text, "1+1/4"));
    turn(EN_SELECT, 1);
    bad += check("SELECT browses sections without leaving the selector", song_punch_on() && song_punch.row == 1 && ui.song_row == 1);
    press(B_PLAY); events_block(1); frame();
    bad += check("PLAY auditions the song from the selected section", chain.running && chain.row == 1 && song_punch_on());
    turn(EN_K1, 1);
    bad += check("auditioning never changes saved punch data", !chain_fx_count(1) && msg_is("STOP TO EDIT"));
    press(B_PLAY); events_block(1); frame();
    bad += check("PLAY stops audition without leaving the editor", !chain.running && song_punch_on());
    press(B_OCTDN);
    bad += check("Back returns to SONG and restores ordinary key ownership", !pop.on && cur_page()->graph == GR_SONG && song.grid == keys_mode());
    press(B_FX); press(B_HOME);
    bad += check("HOME closes the selector and returns to Stage", !pop.on && ui.home);
    setup(1, 16); btn_down(B_FX); frames(800);
    bad += check("holding FX on SONG retains the live layer gesture", ui.layer == LAYER_FX && !song_punch_on() && !pop.on);
    btn_up(B_FX); frame();
    setup(1, 16); key_down(2); keyboard_block(); press(B_FX);
    bad += check("entering the editor keeps an already held musical note's ownership", gates() && kb_note[2] != KB_SILENT);
    key_up(2); keyboard_block(); frame();
    bad += check("that musical key releases normally inside the FX selector", !gates());
    setup(1, 16); song_punch_open(); pattern_epoch++; frame();
    bad += check("a project/pattern storage reset closes stale section selection", !song_punch_on());
    return bad;
}
static int persistence(void)
{
    int bad = 0; setup(2, 64);
    chain_config.fx[0][0] = (chain_fx_t){PX_FREEZE + 1, 63, 1};
    chain_config.fx[1][3] = (chain_fx_t){PX_MONO + 1, 0, 64};
    chain_config_t expected = chain_config;
    bad += check("songs containing timed punches save in the versioned FX format", !project_save(0) && *(uint32_t *)proj_bank_slot[0] == BANKN_MAGIC);
    chain_defaults(&chain_config); project_load(0);
    bad += check("all timed punches survive a complete project reload", !memcmp(&expected, &chain_config, sizeof expected));
    uint8_t *raw = proj_bank_slot[0];
    uint32_t offset = BANKL_STEPS_OFF + BANKM_SONG_BYTES;
    static const uint8_t malformed[][3] = {{PUNCH_NFX + 1, 0, 1}, {1, 64, 1}, {1, 63, 2}, {1, 0, 0}, {0, 1, 0}};
    int valid = 0;
    for (uint32_t j = 0; j < NELEM(malformed); j++) {
        memcpy(raw + offset, malformed[j], 3); bank_checksum(raw);
        valid += bank_valid(raw, BANK_STORE_SIZE);
    }
    bad += check("invalid effects, steps and durations fail before project publication", !valid);
    setup(1, 16); project_save(1); chain_config.fx[0][0] = expected.fx[0][0]; project_load(1);
    bad += check("loading an older project clears all previous song FX", !chain_fx_count(0));
    return bad;
}
int main(void)
{
    int bad = playback() + overrides_and_restarts() + editing() + visual_controls() + persistence();
    printf("Song punch: %s\n", bad ? "FAILED" : "passed"); return !!bad;
}
