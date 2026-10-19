/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Live note effects, before the engines. The audio ISR owns all histories,
 * mappings and deadlines; the UI publishes only keys and four small controls.
 * No pattern, captured note, preset, parameter ID or MIDI output is modified. */
enum {
    PN_ROLL4, PN_ROLL8T, PN_ROLL8, PN_ROLL16T, PN_ROLL16, PN_ROLL32,
    PN_ROLL32T, PN_ROLL64, PN_FLAM, PN_TRIPLE, PN_DOUBLE, PN_ECHO4,
    PN_ECHO8, PN_ECHO16, PN_DOWN12, PN_DOWN7, PN_UP12, PN_UP7,
    PN_RANDOM_PITCH, PN_ACCENT, PN_RANDOM_VEL, PN_SOFT, PN_SHORT,
    PN_HUMAN, PN_DROP50, PN_DROP75, PN_MUTE, PN_COUNT
};
/* Physical chromatic keys, F3..G5. Changing this table changes neither MIDI
 * pitch nor stored data: these names describe performance gestures only. */
static const char *const PUNCH_NAME[PN_COUNT] = {
    "Roll 1/4", "Roll 1/8T", "Roll 1/8", "Roll 1/16T", "Roll 1/16", "Roll 1/32",
    "Roll 1/32T", "Roll 1/64", "Flam", "Triple", "Double", "Echo 1/4",
    "Echo 1/8", "Echo 1/16", "Octave down", "Fifth down", "Octave up", "Fifth up",
    "Pitch scatter", "Accent", "Velocity dice", "Soft", "Short gate",
    "Humanize", "Drop half", "Drop 3/4", "Drop all"
};
#define PN_BIT(k) (1u << (k))
#define PN_ROLL_MASK ((1u << PN_FLAM) - 1u)
#define PN_QUEUE 64u
#define PN_CHORD 12u
#define PN_NONE 255u
static volatile uint32_t punch_keys, punch_claimed;
static volatile uint32_t punch_panel_mask;
static volatile uint8_t punch_panel_lock;
static volatile uint8_t punch_target, punch_repeats = 3, punch_gate = 50, punch_chance = 100;
static struct {
    uint32_t time, beat, keys, targets, random;
    uint8_t ready, shifted, pending, panel_down, roll_collect[NTRK];
    /* 0 absent, 1..128 output pitch + 1, 255 suppressed or not started yet. */
    uint8_t map[NTRK][128];
    uint32_t generated[NTRK][4];
    struct { uint32_t at, next; uint8_t n, note[PN_CHORD], vel[PN_CHORD]; } recent[NTRK], roll[NTRK];
    struct { uint32_t due; uint8_t track, note, vel, source; } q[PN_QUEUE];
} punch;

static uint32_t punch_random(void)
{
    punch.random = punch.random * 1664525u + 1013904223u;
    return punch.random >> 8;
}
static int punch_has(uint32_t k) { return (punch.keys & PN_BIT(k)) != 0; }
static int punch_targeted(uint32_t t) { return (punch.targets & (1u << t)) != 0; }
static int punch_owned(uint32_t t, uint32_t note)
{
    if (punch.map[t][note] == note + 1u) return 1;
    if (!(punch.shifted & (1u << t))) return 0;
    for (uint32_t k = 0; k < 128u; k++) if (punch.map[t][k] == note + 1u) return 1;
    return 0;
}
static void punch_release(uint32_t t, uint32_t note)
{
    if (!punch_owned(t, note) && !(punch.generated[t][note >> 5] & (1u << (note & 31u))))
        trk_note_off_raw(&trk[t], note);
}
static void punch_dequeue(uint32_t k)
{
    if (punch.q[k].track != PN_NONE) { punch.q[k].track = PN_NONE; punch.pending--; }
}
static void punch_cancel_source(uint32_t t, uint32_t source)
{
    if (!punch.pending) return;
    uint32_t unstarted = 0;
    for (uint32_t k = 0; k < PN_QUEUE; k++)
        unstarted |= punch.q[k].track == t && punch.q[k].source == source && punch.q[k].vel;
    for (uint32_t k = 0; k < PN_QUEUE; k++)
        if (punch.q[k].track == t && (punch.q[k].source == source ||
            (unstarted && punch.q[k].vel && punch.q[k].source == 128u + source))) punch_dequeue(k);
}
static int punch_queue(uint32_t t, uint32_t note, uint32_t vel, uint32_t source, uint32_t delay)
{
    for (uint32_t k = 0; k < PN_QUEUE; k++) if (punch.q[k].track == PN_NONE) {
        punch.q[k].due = punch.time + delay;
        punch.q[k].note = (uint8_t)note; punch.q[k].vel = (uint8_t)vel; punch.q[k].source = (uint8_t)source;
        punch.q[k].track = (uint8_t)t; punch.pending++;
        return 1;
    }
    return 0; /* bounded admission: never start a note whose release cannot fit */
}
static void punch_generated(uint32_t t, uint32_t note, uint32_t vel, uint32_t gate)
{
    /* A retrigger replaces its earlier deadline, so an old release cannot cut
     * the new hit. Original notes sharing the pitch retain their ownership. */
    for (uint32_t k = 0; k < PN_QUEUE; k++)
        if (punch.q[k].track == t && punch.q[k].note == note && !punch.q[k].vel && punch.q[k].source == PN_NONE)
            punch_dequeue(k);
    if (!punch_queue(t, note, 0, PN_NONE, gate ? gate : 1u)) {
        punch.generated[t][note >> 5] &= ~(1u << (note & 31u)); punch_release(t, note); return;
    }
    punch.generated[t][note >> 5] |= 1u << (note & 31u);
    trk_note_on_raw(&trk[t], note, vel);
}
static void punch_cancel_track(uint32_t t)
{
    for (uint32_t k = 0; k < PN_QUEUE; k++) if (punch.q[k].track == t && punch.q[k].vel)
        punch_dequeue(k);
    for (uint32_t n = 0; n < 128u; n++) if (punch.generated[t][n >> 5] & (1u << (n & 31u))) {
        punch.generated[t][n >> 5] &= ~(1u << (n & 31u)); punch_release(t, n);
    }
    punch.roll[t].n = 0;
    punch.roll_collect[t] = 1;
}
static void punch_forget(uint32_t t)
{
    punch_cancel_track(t);
    for (uint32_t k = 0; k < PN_QUEUE; k++) if (punch.q[k].track == t) punch_dequeue(k);
    memset(punch.map[t], 0, sizeof punch.map[t]);
    punch.shifted &= (uint8_t)~(1u << t);
    memset(&punch.recent[t], 0, sizeof punch.recent[t]);
}
static void punch_init(void)
{
    memset(&punch, 0, sizeof punch);
    for (uint32_t k = 0; k < PN_QUEUE; k++) punch.q[k].track = PN_NONE;
    punch.beat = FS / 2u;
    punch.ready = 1;
    punch_keys = punch_claimed = 0;
    punch_panel_mask = punch_panel_lock = 0;
}

static void punch_keyboard(uint32_t keys, uint32_t buttons)
{
    uint32_t down = (buttons & punch_panel_mask) != 0;
    if (down && !punch.panel_down && !punch_panel_lock) punch_target = song.sel;
    punch.panel_down = (uint8_t)down;
    punch_keys = down || punch_panel_lock ? keys : 0u;
}
static void punch_stop(void)
{
    for (uint32_t t = 0; t < NTRK; t++) punch_cancel_track(t);
}
static uint32_t punch_roll_period(void)
{
    static const uint8_t div[8] = {1, 3, 2, 6, 4, 8, 12, 16};
    uint32_t d = 0;
    for (uint32_t k = 0; k < 8u; k++) if (punch_has(k) && div[k] > d) d = div[k];
    return d ? punch.beat / d : 0;
}
static void punch_sync(void)
{
    uint32_t keys = punch_keys & ((1u << PN_COUNT) - 1u);
    uint32_t targets = punch_target < NTRK ? 1u << punch_target : (1u << trk_n()) - 1u;
    if (!keys) targets = 0;
    if (keys == punch.keys && targets == punch.targets) return;
    for (uint32_t t = 0; t < NTRK; t++) {
        if (!(targets & (1u << t)) || keys != punch.keys) punch_cancel_track(t);
        if ((keys & PN_ROLL_MASK) && (targets & (1u << t)) &&
            (!(punch.keys & PN_ROLL_MASK) || keys != punch.keys || !(punch.targets & (1u << t)))) {
            punch.roll[t] = punch.recent[t]; punch.roll[t].next = punch.time;
            punch.roll_collect[t] = !punch.roll[t].n;
        }
    }
    punch.keys = keys; punch.targets = targets;
}
static int punch_color(uint32_t *note, uint32_t *vel)
{
    int32_t pitch = (int32_t)*note + (punch_has(PN_UP12) ? 12 : 0) - (punch_has(PN_DOWN12) ? 12 : 0) +
        (punch_has(PN_UP7) ? 7 : 0) - (punch_has(PN_DOWN7) ? 7 : 0);
    if (punch_has(PN_RANDOM_PITCH)) pitch += (int32_t)(punch_random() % 5u) - 2;
    if (pitch < 0 || pitch > 127) return 0;
    *note = (uint32_t)pitch;
    if (punch_has(PN_ACCENT)) *vel = 127u;
    if (punch_has(PN_SOFT)) *vel = (*vel + 1u) / 2u;
    if (punch_has(PN_RANDOM_VEL)) *vel = 32u + punch_random() % 96u;
    return 1;
}
static void trk_note_off(track_t *t, uint32_t note)
{
    if (note > 127u) return;
    uint32_t tr = (uint32_t)(t - trk), mapped = punch.map[tr][note];
    punch_cancel_source(tr, note);
    punch.map[tr][note] = 0;
    if (mapped && mapped != PN_NONE) punch_release(tr, mapped - 1u);
    else if (!mapped) punch_release(tr, note);
}
static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    if (!punch.ready) punch_init();
    if (note > 127u || !vel) { trk_note_off(t, note); return; }
    uint32_t tr = (uint32_t)(t - trk);
    if (punch.recent[tr].at != punch.time) { punch.recent[tr].at = punch.time; punch.recent[tr].n = 0; }
    uint32_t n = 0;
    while (n < punch.recent[tr].n && punch.recent[tr].note[n] != note) n++;
    if (n < PN_CHORD) { punch.recent[tr].note[n] = (uint8_t)note; punch.recent[tr].vel[n] = (uint8_t)vel; if (n == punch.recent[tr].n) punch.recent[tr].n++; }
    uint32_t old = punch.map[tr][note];
    punch.map[tr][note] = 0; punch_cancel_source(tr, note);
    uint32_t active = punch.keys && punch_targeted(tr) && punch_chance, output = note;
    uint32_t apply = active && punch_random() % 100u < punch_chance;
    if ((active && (punch.keys & PN_ROLL_MASK)) || (apply && (punch_has(PN_MUTE) ||
        (punch_has(PN_DROP50) && punch_random() % 2u) || (punch_has(PN_DROP75) && punch_random() % 4u)))) {
        punch.map[tr][note] = PN_NONE;
        if ((punch.keys & PN_ROLL_MASK) && punch.roll_collect[tr]) { punch.roll[tr] = punch.recent[tr]; punch.roll[tr].next = punch.time; }
        if (old && old != PN_NONE) punch_release(tr, old - 1u);
        return;
    }
    if (apply) {
        if (!punch_color(&output, &vel)) { punch.map[tr][note] = PN_NONE; if (old && old != PN_NONE) punch_release(tr, old - 1u); return; }
        if (output != note) punch.shifted |= (uint8_t)(1u << tr);
        uint32_t delay = punch_has(PN_HUMAN) ? punch_random() % (FS / 50u + 1u) : 0;
        punch.map[tr][note] = (uint8_t)(output + 1u);
        if (delay) {
            punch.map[tr][note] = PN_NONE;
            punch_queue(tr, output, vel, note, delay);
        } else trk_note_on_raw(t, output, vel);
        if (punch_has(PN_SHORT)) punch_queue(tr, output, 0, note, delay + punch.beat / 16u);
        uint32_t gap = punch_has(PN_ECHO16) ? punch.beat / 4u : punch_has(PN_ECHO8) ? punch.beat / 2u :
            punch_has(PN_ECHO4) ? punch.beat : punch_has(PN_FLAM) ? FS / 50u :
            punch_has(PN_DOUBLE) || punch_has(PN_TRIPLE) ? punch.beat / 8u : 0;
        if (gap) {
            uint32_t count = punch_has(PN_FLAM) || punch_has(PN_DOUBLE) ? 1u : punch_has(PN_TRIPLE) ? 2u : punch_repeats;
            for (uint32_t k = 1; k <= count; k++) {
                uint32_t echo_vel = vel * 3u / 4u; vel = echo_vel ? echo_vel : 1u;
                punch_queue(tr, output, vel, 128u + note, delay + gap * k);
            }
        }
    } else {
        punch.map[tr][note] = (uint8_t)(output + 1u); trk_note_on_raw(t, output, vel);
    }
    if (old && old != PN_NONE && punch.map[tr][note] != old) punch_release(tr, old - 1u);
}
/* Real sample time is independent of the transport's clock representation.
 * Even a stopped external master must release generated gates on time. */
static void punch_tick(uint32_t n, uint32_t beat)
{
    if (!punch.ready) punch_init();
    punch.beat = beat ? beat : FS / 2u;
    punch_sync(); punch.time += n;
    if (!punch.keys && !punch.pending) return;
    uint32_t period = punch_roll_period();
    for (uint32_t t = 0; period && t < trk_n(); t++) if (punch_targeted(t) && punch.roll[t].n &&
        (int32_t)(punch.time - punch.roll[t].next) >= 0) {
        punch.roll[t].next += ((punch.time - punch.roll[t].next) / period + 1u) * period;
        punch.roll_collect[t] = 0;
        if (!punch_has(PN_MUTE) && punch_random() % 100u < punch_chance)
            for (uint32_t k = 0; k < punch.roll[t].n; k++) {
                uint32_t note = punch.roll[t].note[k], vel = punch.roll[t].vel[k];
                if (punch_color(&note, &vel)) punch_generated(t, note, vel, period * punch_gate / 100u);
            }
    }
    for (uint32_t k = 0; k < PN_QUEUE; k++) if (punch.q[k].track < NTRK && (int32_t)(punch.time - punch.q[k].due) >= 0) {
        uint32_t tr = punch.q[k].track, note = punch.q[k].note, vel = punch.q[k].vel, source = punch.q[k].source;
        punch_dequeue(k);
        if (source < 128u) {
            if (vel) { punch.map[tr][source] = (uint8_t)(note + 1u); trk_note_on_raw(&trk[tr], note, vel); }
            else if (punch.map[tr][source] == note + 1u) { punch.map[tr][source] = PN_NONE; punch_release(tr, note); }
        } else if (vel) punch_generated(tr, note, vel, punch.beat * punch_gate / 400u);
        else { punch.generated[tr][note >> 5] &= ~(1u << (note & 31u)); punch_release(tr, note); }
    }
}
