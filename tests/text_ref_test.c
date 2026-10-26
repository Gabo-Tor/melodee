/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Pixel text against an independent framebuffer expansion: all four roles,
 * every palette, theme/surface and text/background pairs. Pens and pixels match. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"

static const char *const STR[] = {
    "CUTOFF", "RESO", "ATK", "DEC", "LEVEL", "PAN", "REV", "MUTE", "STEP", "LANE", "HIT", "ACC", "No.",
    "124", "1234.5", "-12", "+3", "0.87", "100 %", "/32", "C#4", "ANALOG", "DIGITAL", "OFF", "ON",
    "OVERWRITE PROJECT A?", "SONG PATTERN CHANGES", "OCT-   NO", "OCT+   YES", "PRESETS", "MOVE", "BACK",
    "AV AW LT TA YO To Vo", "KEYS = STEPS", "PAGE 1/2", "HARDWARE CALIBRATION", "Multi-engine synthesizer",
    "H\xFCgelton Instruments", "MELODEE", "MENU", "HOME", "ENV DEST 2/2", "ANLG", "SUPER SAW", "WIRE",
    "oeo coco eco nono", "minimum hello level", "decay release sound", "Pitch Cutoff", "bdpq doob", "-1 +2.5",
};
#define NSTR (sizeof STR / sizeof STR[0])
#define W 240
#define H 40
static uint16_t ref[W * H];

/* Independent lookup and bit-by-bit expansion onto a plain framebuffer. */
static int32_t ref_at(const aafont_t *f, uint32_t ch)       /* the glyph's number in the face, -1 = none */
{
    if (ch >= f->first && ch <= f->last) return (int32_t)(ch - f->first);
    for (uint32_t i = 0; i < f->nex; i++)
        if (f->ex[i] == ch) return (int32_t)(f->last - f->first + 1 + i);
    return -1;
}
static uint32_t ref_gi(const aafont_t *f, uint32_t ch)      /* the glyph's number (its phases: g[n << psh] ..) */
{
    int32_t k;
    if (ch >= 'a' && ch <= 'z' && f->last < 'a' && ref_at(f, ch) < 0) ch -= 32u;   /* (a face without it: the capital) */
    k = ref_at(f, ch);
    if (k < 0 && ch == 0x85u) k = ref_at(f, '.');          /* the ellipsis */
    if (k < 0) k = ref_at(f, '?');
    return k < 0 ? 0u : (uint32_t)k;                        /* a sparse face (L) without '?': its first glyph */
}
static int32_t ref_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t pen = x;
    for (; *s; s++) {
        const aag_t *g = &f->g[ref_gi(f, (uint8_t)*s)];
        for (uint32_t row = 0; row < g->bh; row++)
            for (uint32_t col = 0; col < g->bw; col++) {
                uint32_t bit = row * g->bw + col;
                if (!(f->data[g->off + bit / 8u] & (128u >> (bit % 8u)))) continue;
                for (uint32_t dy = 0; dy < f->px; dy++)
                    for (uint32_t dx = 0; dx < f->px; dx++) {
                        int32_t px = pen + (g->bx + (int32_t)col) * f->px + (int32_t)dx;
                        int32_t py = y + f->asc + ((int32_t)g->by - 9 + (int32_t)row) * f->px + (int32_t)dy;
                        if ((uint32_t)px < W && (uint32_t)py < H) ref[py * W + px] = fg;
                    }
            }
        pen += g->adv * f->px / 16;
    }
    return pen;
}

int main(void)
{
    const aafont_t *F[4] = {&AP_LB, &AP_TX, &AP_VL, &AP_HR};
    uint32_t n = 0, bad_px = 0, bad_pen = 0;
    for (uint32_t p = 0; p < NPALETTES; p++) {
        palette_set(p);
        for (uint32_t k = 0; k < 2u; k++)
            for (uint32_t f = 0; f < 4u; f++)
                for (uint32_t s = 0; s < NSTR; s++) {
                    uint16_t fg = k ? T_TEXT : T_THEME, bg = k ? T_BG : T_SURF;
                    int32_t e0, e1;
                    for (uint32_t i = 0; i < W * H; i++) ref[i] = bg;
                    e0 = ref_text(3, 2, F[f], STR[s], fg, bg);
                    cv_begin(W, H, bg);
                    e1 = cv_text(3, 2, F[f], STR[s], fg);
                    bad_pen += e0 != e1;
                    for (uint32_t i = 0; i < W * H; i++) bad_px += swap16(cv_px[i]) != ref[i];
                    n++;
                }
    }
    printf("text: %u strings x fonts x palettes against the reference renderer: %u end pens and %u pixels differ %s\n",
           n, bad_pen, bad_px, bad_pen || bad_px ? "FAIL" : "ok");
    return bad_pen || bad_px ? 1 : 0;
}
