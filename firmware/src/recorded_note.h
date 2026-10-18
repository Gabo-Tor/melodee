/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#ifndef MELODEE_RECORDED_NOTE_H
#define MELODEE_RECORDED_NOTE_H
#include <stdint.h>

/* Keep the original ten-byte payload and its duration/rounding bits. The
 * former boolean length byte has room for the sixth owner bit and seventh
 * step bit. Access these fields through the helpers: length is no longer a
 * boolean byte, and owner/step alone are no longer complete identifiers.
 * Existing 32-owner, 64-step events retain their exact representation. This
 * runtime representation is independent of a project's serialized schema. */
typedef struct { uint16_t on, duration; uint8_t note, vel, owner, step; int8_t pitch; uint8_t length; } recorded_note_t;
_Static_assert(sizeof(recorded_note_t) == 10u, "timed note layout");
static uint32_t recorded_owner(const recorded_note_t *r)
{ return (r->owner & 31u) | ((uint32_t)(r->length & 2u) << 4); }
static uint32_t recorded_step(const recorded_note_t *r)
{ return (r->step & 63u) | ((uint32_t)(r->length & 4u) << 4); }
static uint32_t recorded_exponent(const recorded_note_t *r) { return r->owner >> 5; }
static int recorded_gate(const recorded_note_t *r) { return (r->length & 1u) != 0; }
static int recorded_rounded(const recorded_note_t *r) { return (r->step & 64u) != 0; }
static int recorded_wrapped(const recorded_note_t *r) { return (r->step & 128u) != 0; }
static void recorded_set_owner(recorded_note_t *r, uint32_t owner)
{
    r->owner = (uint8_t)((r->owner & 224u) | (owner & 31u));
    r->length = (uint8_t)((r->length & ~2u) | ((owner >> 4) & 2u));
}
static void recorded_set_exponent(recorded_note_t *r, uint32_t exponent)
{ r->owner = (uint8_t)((r->owner & 31u) | (exponent << 5)); }
static void recorded_set_gate(recorded_note_t *r, int enabled)
{ r->length = (uint8_t)((r->length & ~1u) | (enabled != 0)); }
static void recorded_set_step(recorded_note_t *r, uint32_t step, int rounded, int wrapped)
{
    r->step = (uint8_t)((step & 63u) | (rounded ? 64u : 0u) | (wrapped ? 128u : 0u));
    r->length = (uint8_t)((r->length & ~4u) | ((step >> 4) & 4u));
}
#endif
