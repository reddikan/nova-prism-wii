/*
 * NOVA PRISM - rendering. Everything is built from flat-shaded low-poly
 * triangles plus additive glow, drawn into the gfx triangle list.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

enum { P_SPARK, P_GLOW, P_DEBRIS, P_SMOKE, P_CONF, P_STAR };   /* must match game.c */

#define WHITE 0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/* local-space transform for models                                    */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, c, s, sx, sy, flash, alpha; } Xf;

static Xf xf(float x, float y, float ang, float sx, float sy)
{
    Xf t = { x, y, cosf(ang), sinf(ang), sx, sy, 0.0f, 1.0f };
    return t;
}
static inline void xpt(const Xf *t, float lx, float ly, float *ox, float *oy)
{
    lx *= t->sx; ly *= t->sy;
    *ox = t->x + lx * t->c - ly * t->s;
    *oy = t->y + lx * t->s + ly * t->c;
}
static inline uint32_t xcol(const Xf *t, uint32_t c)
{
    if (t->flash > 0) c = col_mix(c, WHITE, t->flash);
    if (t->alpha < 1.0f) c = col_a(c, t->alpha);
    return c;
}
static void xtri(const Xf *t, float ax, float ay, float bx, float by, float cx, float cy, uint32_t col)
{
    float x0, y0, x1, y1, x2, y2;
    xpt(t, ax, ay, &x0, &y0); xpt(t, bx, by, &x1, &y1); xpt(t, cx, cy, &x2, &y2);
    gfx_tri1(x0, y0, x1, y1, x2, y2, xcol(t, col));
}
static void xquad(const Xf *t, float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, uint32_t col)
{
    xtri(t, ax, ay, bx, by, cx, cy, col);
    xtri(t, ax, ay, cx, cy, dx, dy, col);
}
static void xglow(const Xf *t, float lx, float ly, float r, uint32_t c)
{
    float x, y;
    xpt(t, lx, ly, &x, &y);
    gfx_glow_cheap(x, y, r, c);
}

static float wrapp(float x, float p)          /* into [-p/2, p/2) */
{
    x -= p * floorf(x / p + 0.5f);
    return x;
}

static int on_screen(float sx, float sy, float r) { return sx > -r && sx < VW + r && sy > -r && sy < VH + r; }

/* ------------------------------------------------------------------ */
/* background                                                           */
/* ------------------------------------------------------------------ */
static void draw_blob_layer(const Game *g, float factor, int n, uint32_t seed, float rmin, float rmax, int edges, uint32_t c, uint32_t chi)
{
    float period = g->W * factor;
    float cam = g->camx * factor;
    for (int i = 0; i < n; i++) {
        uint32_t h = seed + (uint32_t)i * 977u;
        float u = ((float)i + hashf(h) * 0.7f) * period / (float)n;
        float r = rmin + hashf(h + 1) * (rmax - rmin);
        float y;
        if (edges) y = (i & 1) ? 20.0f + hashf(h + 2) * 110.0f : 460.0f - hashf(h + 2) * 110.0f;
        else       y = 60.0f + hashf(h + 2) * 360.0f;
        float sx = VW * 0.5f + wrapp(u - cam, period);
        if (sx < -r - 10 || sx > VW + r + 10) continue;
        /* irregular faceted blob, lit from above */
        const int nv = 7;
        float px[8], py[8];
        for (int k = 0; k < nv; k++) {
            float a = TAU * (float)k / (float)nv + hashf(h + 10 + (uint32_t)k) * 0.5f;
            float rr = r * (0.75f + 0.25f * hashf(h + 20 + (uint32_t)k));
            px[k] = sx + cosf(a) * rr; py[k] = y + sinf(a) * rr * 0.85f;
        }
        for (int k = 0; k < nv; k++) {
            int j = (k + 1) % nv;
            float my = (py[k] + py[j]) * 0.5f - y;
            float lit = clampf(-my / r * 0.8f + 0.35f, 0, 1);
            gfx_tri1(sx, y, px[k], py[k], px[j], py[j], col_mix(c, chi, lit * ((k & 1) ? 1.0f : 0.75f)));
        }
    }
}

static void draw_glyph_layer(const Game *g)
{
    const float factor = 0.55f;
    float period = g->W * factor;
    float cam = g->camx * factor;
    const int n = 60;
    uint32_t base = g->L->glyph;
    for (int i = 0; i < n; i++) {
        uint32_t h = 0xC0FFEEu + (uint32_t)i * 7331u;
        float u = ((float)i + hashf(h) * 0.9f) * period / (float)n;
        float y = 30.0f + hashf(h + 1) * 420.0f + sinf(g->time * 0.6f + (float)i) * 6.0f;
        float sx = VW * 0.5f + wrapp(u - cam, period);
        if (sx < -20 || sx > VW + 20) continue;
        float s = 4.0f + hashf(h + 2) * 5.0f;
        float tw = 0.35f + 0.25f * sinf(g->time * (1.0f + hashf(h + 3) * 2.0f) + (float)i);
        uint32_t c = col_a(col_mix(base, 0x2A1450FFu, hashf(h + 4) * 0.5f), tw);
        float rot = g->time * (hashf(h + 5) - 0.5f) + (float)i;
        int kind = (int)(hashf(h + 6) * 5.0f);
        float ca = cosf(rot) * s, sa = sinf(rot) * s;
        switch (kind) {
        case 0:   /* plus */
            gfx_line(sx - s, y, sx + s, y, 2.0f, c, c); gfx_line(sx, y - s, sx, y + s, 2.0f, c, c); break;
        case 1:   /* cross */
            gfx_line(sx - ca, y - sa, sx + ca, y + sa, 2.0f, c, c); gfx_line(sx + sa, y - ca, sx - sa, y + ca, 2.0f, c, c); break;
        case 2:   /* diamond outline */
            gfx_ring(sx, y, s, 2.0f, 4, rot * 0.2f, c); break;
        case 3:   /* square outline */
            gfx_ring(sx, y, s, 2.0f, 4, rot * 0.2f + 0.785f, c); break;
        default:  /* dot */
            gfx_rect(sx - 2, y - 2, 4, 4, c); break;
        }
    }
}

static void draw_crystal_prism(float x, float y, float ang, float len, float w, uint32_t lt, uint32_t dk, uint32_t tip)
{
    Xf t = xf(x, y, ang, 1, 1);
    float h = w * 0.5f;
    /* base -> shoulder -> tip, split into a lit and a shaded half */
    xquad(&t, 0, -h, len * 0.72f, -h, len * 0.72f, 0, 0, 0, lt);
    xquad(&t, 0, 0, len * 0.72f, 0, len * 0.72f, h, 0, h, dk);
    xtri(&t, len * 0.72f, -h, len, 0, len * 0.72f, 0, tip);
    xtri(&t, len * 0.72f, 0, len, 0, len * 0.72f, h, col_mix(dk, tip, 0.3f));
}

static void draw_terrain(const Game *g)
{
    const LevelDef *L = g->L;
    int   n = L->nterr;
    float sp = terr_spacing(g);
    const float B = 16.0f;
    for (int i = 0; i < n; i++) {
        float sx0 = sxof(g, (float)i * sp);
        if (sx0 > VW + 4 || sx0 + sp < -4) continue;
        float sx1 = sx0 + sp;
        int   j = (i + 1) % n;
        float t0 = g->ttop[i], t1 = g->ttop[j], b0 = g->tbot[i], b1 = g->tbot[j];
        float va = hashf(L->seed + (uint32_t)i * 13u), vb = hashf(L->seed + (uint32_t)i * 17u + 5u);
        /* ceiling: two big facets, a shadowed bevel and a bright rim */
        gfx_tri1(sx0, -10, sx1, -10, sx1, t1 - B, col_mix(L->terr, L->terr_lo, va * 0.35f));
        gfx_tri1(sx0, -10, sx1, t1 - B, sx0, t0 - B, col_mix(L->terr, L->terr_hi, vb * 0.25f));
        gfx_tri1(sx0, t0 - B, sx1, t1 - B, sx1, t1, L->terr_lo);
        gfx_tri1(sx0, t0 - B, sx1, t1, sx0, t0, col_mix(L->terr_lo, L->terr, 0.35f));
        /* floor: lit top bevel */
        gfx_tri1(sx0, VH + 10, sx1, VH + 10, sx1, b1 + B, col_mix(L->terr, L->terr_lo, vb * 0.35f));
        gfx_tri1(sx0, VH + 10, sx1, b1 + B, sx0, b0 + B, col_mix(L->terr, L->terr_hi, va * 0.2f));
        gfx_tri1(sx0, b0, sx1, b1, sx1, b1 + B, L->terr_hi);
        gfx_tri1(sx0, b0, sx1, b1 + B, sx0, b0 + B, col_mix(L->terr_hi, L->terr, 0.3f));
    }
    /* crystal clusters growing out of the rock */
    for (int i = 0; i < n; i++) {
        uint32_t h = L->seed * 3u + (uint32_t)i * 4099u;
        if (hashf(h) > 0.45f) continue;
        float x = ((float)i + 0.3f + hashf(h + 1) * 0.4f) * sp;
        float sx = sxof(g, x);
        if (sx < -80 || sx > VW + 80) continue;
        int floor = hashf(h + 2) > 0.4f;
        float y = floor ? terr_bot(g, x) + 6.0f : terr_top(g, x) - 6.0f;
        float base_ang = floor ? -PI_F * 0.5f : PI_F * 0.5f;
        int k = 2 + (int)(hashf(h + 3) * 2.0f);
        for (int c = 0; c < k; c++) {
            float a = base_ang + (hashf(h + 10 + (uint32_t)c) - 0.5f) * 1.1f;
            float len = 26.0f + hashf(h + 20 + (uint32_t)c) * 34.0f;
            draw_crystal_prism(sx + ((float)c - 1.0f) * 9.0f, y, a, len, 10.0f + len * 0.12f, 0xF39BFFFFu, 0xA03CE0FFu, 0xFFE0FFFFu);
        }
    }
    gfx_blend(BL_ADD);
    for (int i = 0; i < n; i++) {
        uint32_t h = L->seed * 3u + (uint32_t)i * 4099u;
        if (hashf(h) > 0.45f) continue;
        float x = ((float)i + 0.3f + hashf(h + 1) * 0.4f) * sp;
        float sx = sxof(g, x);
        if (sx < -80 || sx > VW + 80) continue;
        int floor = hashf(h + 2) > 0.4f;
        float y = floor ? terr_bot(g, x) - 20.0f : terr_top(g, x) + 20.0f;
        float pulse = 0.55f + 0.45f * sinf(g->time * 1.7f + (float)i);
        gfx_glow(sx, y, 60.0f, col_a(0xD060FFFFu, 0.22f * pulse));
    }
    /* thin bright rim lines along the cave edge */
    for (int i = 0; i < n; i++) {
        float sx0 = sxof(g, (float)i * sp);
        if (sx0 > VW + 4 || sx0 + sp < -4) continue;
        int j = (i + 1) % n;
        gfx_line(sx0, g->ttop[i] - 1, sx0 + sp, g->ttop[j] - 1, 2.0f, col_a(L->terr_hi, 0.5f), col_a(L->terr_hi, 0.5f));
        gfx_line(sx0, g->tbot[i] + 1, sx0 + sp, g->tbot[j] + 1, 2.0f, col_a(WHITE, 0.25f), col_a(WHITE, 0.25f));
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* ship                                                                 */
/* ------------------------------------------------------------------ */
static void ship_model(const Xf *t)
{
    /* nose at +x. Low-poly manta: lit upper facets, shaded lower facets */
    xtri(t, 26, 0, 4, -7, -10, 0, 0xFFC56BFFu);          /* upper hull */
    xtri(t, 26, 0, -10, 0, 4, 7, 0xE8742AFFu);           /* lower hull */
    xtri(t, 4, -7, -18, -19, -10, 0, 0xFFD96BFFu);       /* top wing */
    xtri(t, -18, -19, -20, -6, -10, 0, 0xD2592AFFu);
    xtri(t, 4, 7, -10, 0, -18, 19, 0xC9502AFFu);         /* bottom wing */
    xtri(t, -18, 19, -10, 0, -20, 6, 0xA83E25FFu);
    xtri(t, -10, 0, -20, -6, -20, 6, 0x8C3020FFu);       /* engine block */
    xtri(t, 15, 0, 6, -4, 2, 0, 0x7DF9FFFFu);            /* cockpit gem */
    xtri(t, 15, 0, 2, 0, 6, 3, 0x2FB5D6FFu);
    xtri(t, 10, -1, 6, -3, 5, -1, WHITE);
    xtri(t, -6, -12, -16, -17, -12, -10, 0xFF5C8AFFu);   /* wing stripes */
    xtri(t, -6, 12, -12, 10, -16, 17, 0xD93A6AFFu);
}

static void draw_trail(const Game *g)
{
    if (g->trail_n < 2) return;
    float px = 0, py = 0, pnx = 0, pny = 0;
    for (int i = 0; i < g->trail_n; i++) {
        float sx = sxof(g, g->trail[i][0]), sy = g->trail[i][1];
        float nx = 0, ny = 1;
        if (i + 1 < g->trail_n) {
            float dx = wd(g, g->trail[i + 1][0] - g->trail[i][0]), dy = g->trail[i + 1][1] - sy;
            float l = sqrtf(dx * dx + dy * dy);
            if (l > 0.01f) { nx = -dy / l; ny = dx / l; }
        } else { nx = pnx; ny = pny; }
        float t = 1.0f - (float)i / (float)(g->trail_n - 1);
        float w = 7.0f * t + 1.0f;
        if (i > 0 && fabsf(sx - px) < 200.0f) {
            float t0 = 1.0f - (float)(i - 1) / (float)(g->trail_n - 1);
            uint32_t c0 = col_a(col_mix(0xFF4F7AFFu, 0xFFC23DFFu, t0), t0 * 0.85f), c1 = col_a(col_mix(0xFF4F7AFFu, 0xFFC23DFFu, t), t * 0.85f);
            float w0 = 7.0f * t0 + 1.0f;
            gfx_quad4(px + pnx * w0, py + pny * w0, c0 & 0xFFFFFF00u, sx + nx * w, sy + ny * w, c1 & 0xFFFFFF00u, sx, sy, c1, px, py, c0);
            gfx_quad4(px, py, c0, sx, sy, c1, sx - nx * w, sy - ny * w, c1 & 0xFFFFFF00u, px - pnx * w0, py - pny * w0, c0 & 0xFFFFFF00u);
        }
        px = sx; py = sy; pnx = nx; pny = ny;
    }
}

static void draw_player(const Game *g)
{
    if (!g->alive) return;
    float sx = sxof(g, g->px), sy = g->py;
    float ft = g->flip_t > 0 ? g->flip_t : 0.0f;
    float scx = (float)g->face * cosf(PI_F * ft);
    float scy = 1.0f - 0.25f * sinf(PI_F * ft);
    float rot = g->tilt * (scx >= 0 ? 1.0f : -1.0f) + g->flip_dir * sinf(PI_F * ft) * 0.25f;
    if (fabsf(scx) < 0.08f) scx = scx < 0 ? -0.08f : 0.08f;
    Xf t = xf(sx, sy - sinf(PI_F * ft) * 6.0f, rot, scx, scy);
    int blink = g->inv_t > 0 && ((int)(g->time * 20.0f) & 1);

    gfx_blend(BL_ADD);
    draw_trail(g);
    /* engine flame */
    float fl = 0.75f + 0.25f * sinf(g->time * 60.0f) + 0.15f * hashf((uint32_t)g->frame);
    float boost = 1.0f + clampf((g->pvx * (float)g->face) / SHIP_SPEED, 0, 1) * 0.8f;
    float L = 16.0f * fl * boost;
    xtri(&t, -20, -6, -20 - L * 1.4f, 0, -20, 6, col_a(0xFF6A2AFFu, 0.8f));
    xtri(&t, -20, -4, -20 - L, 0, -20, 4, col_a(0xFFC23DFFu, 0.95f));
    xtri(&t, -20, -2, -20 - L * 0.55f, 0, -20, 2, WHITE);
    xglow(&t, -22, 0, 30.0f * fl, col_a(0xFF8A3DFFu, 0.55f));
    float lx, ly;
    xpt(&t, 0, 0, &lx, &ly);
    gfx_glow(lx, ly, 80.0f, col_a(0xFFA040FFu, 0.13f));     /* the ship lights up its surroundings */
    gfx_blend(BL_ALPHA);

    if (!blink) {
        Xf s = t;
        if (g->inv_t > 0) s.flash = 0.25f;
        ship_model(&s);
    }
    /* heat gauge */
    if (g->heat > 0.25f || g->overheat) {
        gfx_blend(BL_ADD);
        float h = g->heat;
        uint32_t c = g->overheat ? (((int)(g->time * 12.0f) & 1) ? 0xFF3030FFu : 0xFFA030FFu) : col_mix(0xFFC23DFFu, 0xFF3020FFu, h);
        gfx_arc(sx, sy, 30.0f, 33.0f, -PI_F * 0.5f, -PI_F * 0.5f + TAU * h, 24, col_a(c, 0.3f), col_a(c, 0.9f));
        gfx_blend(BL_ALPHA);
    }
}

static void draw_blade(const Game *g)
{
    if (!g->blade_on || !g->alive) return;
    float sx = sxof(g, g->px), sy = g->py;
    float dir = (float)g->face;
    float grow = clampf(g->blade_t / 0.1f, 0, 1);
    float R = BLADE_R * (0.6f + 0.4f * grow);
    gfx_blend(BL_ADD);
    gfx_glow(sx, sy, R * 1.25f, col_a(COL_B, 0.16f));
    gfx_ring2(sx, sy, R, 6.0f, 40, 0, col_a(COL_B, 0.08f), col_a(COL_B, 0.35f));
    for (int k = 0; k < 2; k++) {
        float a = g->blade_ang + PI_F * (float)k;
        /* motion-blur sweep behind the blade */
        gfx_arc(sx, sy, 24.0f, R, a - dir * 2.0f, a, 16, col_a(COL_B, 0.0f), col_a(COL_B, 0.55f));
        gfx_arc(sx, sy, R - 10.0f, R + 4.0f, a - dir * 1.2f, a, 10, col_a(WHITE, 0.0f), col_a(WHITE, 0.7f));
        float cx = cosf(a), cy = sinf(a);
        gfx_glowline(sx + cx * 22.0f, sy + cy * 22.0f, sx + cx * (R + 10.0f), sy + cy * (R + 10.0f), 16.0f, col_a(COL_B, 0.9f));
        gfx_line(sx + cx * 22.0f, sy + cy * 22.0f, sx + cx * (R + 8.0f), sy + cy * (R + 8.0f), 3.5f, WHITE, WHITE);
        gfx_glow_cheap(sx + cx * (R + 6.0f), sy + cy * (R + 6.0f), 22.0f, col_a(WHITE, 0.8f));
    }
    gfx_blend(BL_ALPHA);
}

static void draw_lance(const Game *g)
{
    if (!g->alive) return;
    float sx = sxof(g, g->px), sy = g->py;
    gfx_blend(BL_ADD);
    if (g->lance_charge > 0) {
        float p = 1.0f - g->lance_charge / 0.3f;
        float nx = sx + (float)g->face * 26.0f;
        gfx_glow(nx, sy, 20.0f + 50.0f * p, col_a(col_hsv(g->time * 3.0f, 0.5f, 1.0f, 255), 0.5f + 0.4f * p));
        gfx_ring(nx, sy, 70.0f * (1.0f - p) + 8.0f, 3.0f, 24, 0, col_a(COL_Y, p));
        gfx_ring(nx, sy, 40.0f * (1.0f - p) + 4.0f, 2.0f, 4, g->time * 6.0f, col_a(WHITE, p));
    }
    if (g->lance_on) {
        float lt = g->lance_t / 0.5f;                     /* 1 -> 0 */
        float wmul = lt > 0.85f ? (1.0f - lt) / 0.15f * 1.4f : sqrtf(lt) * (1.0f + 0.12f * sinf(g->time * 70.0f));
        float w = LANCE_W * wmul;
        float x0 = sx + (float)g->lance_dir * 22.0f, x1 = x0 + (float)g->lance_dir * LANCE_LEN;
        uint32_t hue = col_hsv(g->time * 1.5f, 0.6f, 1.0f, 255);
        gfx_glowline(x0, sy, x1, sy, w * 3.2f, col_a(0xFF4FD8FFu, 0.22f * lt + 0.08f));
        gfx_glowline(x0, sy, x1, sy, w * 1.8f, col_a(hue, 0.45f));
        gfx_glowline(x0, sy, x1, sy, w * 1.1f, col_a(COL_Y, 0.6f));
        gfx_rect(fminf(x0, x1), sy - w * 0.13f, fabsf(x1 - x0), w * 0.26f, WHITE);
        /* rainbow helix running along the beam */
        for (int k = 0; k < 2; k++) {
            float ph = g->time * 30.0f + (float)k * PI_F;
            float pxx = x0, pyy = sy + sinf(ph) * w * 0.8f;
            for (int s = 1; s <= 40; s++) {
                float d = (float)s / 40.0f * LANCE_LEN;
                float nx = x0 + (float)g->lance_dir * d, ny = sy + sinf(ph + d * 0.03f) * w * 0.8f;
                uint32_t c = col_hsv((float)s / 40.0f + g->time * 2.0f + (float)k * 0.5f, 0.7f, 1.0f, (int)(220.0f * lt));
                gfx_line(pxx, pyy, nx, ny, 3.0f, c, c);
                pxx = nx; pyy = ny;
            }
        }
        gfx_glow(x0, sy, 60.0f + 40.0f * lt, col_a(WHITE, 0.8f * lt));
        gfx_glow(x0, sy, 150.0f, col_a(COL_Y, 0.3f * lt));
        /* the beam floods the cave with light */
        gfx_rect_v(0, sy - 160.0f, VW, 160.0f, col_a(COL_Y, 0.0f), col_a(COL_Y, 0.10f * lt));
        gfx_rect_v(0, sy, VW, 160.0f, col_a(COL_Y, 0.10f * lt), col_a(COL_Y, 0.0f));
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* enemies                                                              */
/* ------------------------------------------------------------------ */
static void draw_shard(const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_SHARD];
    if (e->aux > 0.5f) {                                     /* winding up a lunge: glow red and tremble */
        t->x += sinf(e->t * 90.0f) * 1.5f;
        gfx_blend(BL_ADD);
        gfx_glow_cheap(t->x, t->y, 30.0f, col_a(0xFF3050FFu, 0.6f));
        gfx_blend(BL_ALPHA);
    }
    xtri(t, -9, -11, -16, -16, -13, -7, s->c_accent);        /* spikes */
    xtri(t, -9, 11, -13, 7, -16, 16, s->c_accent);
    xtri(t, -12, -3, -20, 0, -12, 3, s->c_accent);
    xtri(t, 18, 0, -10, -12, -12, -4, s->c_light);           /* faceted drill cone */
    xtri(t, 18, 0, -12, -4, -12, 4, s->c_mid);
    xtri(t, 18, 0, -12, 4, -10, 12, s->c_dark);
    for (int k = 0; k < 2; k++) {                            /* drill grooves */
        float xa = 6.0f - 9.0f * (float)k, xb = xa - 3.0f;
        float ha = 12.0f * (18.0f - xa) / 28.0f, hb = 12.0f * (18.0f - xb) / 28.0f;
        xquad(t, xa, -ha, xb, -hb, xb, hb, xa, ha, col_a(0x1A3A8CFFu, 0.55f));
    }
    (void)e;
}

static void draw_zip(const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_ZIP];
    xtri(t, 13, 0, 0, -8, -2, 0, s->c_light);
    xtri(t, 13, 0, -2, 0, 0, 8, s->c_mid);
    xtri(t, 0, -8, -11, -3, -2, 0, s->c_mid);
    xtri(t, 0, 8, -2, 0, -11, 3, s->c_dark);
    xtri(t, -11, -3, -2, 0, -11, 3, s->c_dark);
    (void)e;
}

static void draw_spore(const Game *g, const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_SPORE];
    float tele = e->cd < 0.5f ? 1.0f - e->cd / 0.5f : 0.0f;
    for (int k = 0; k < 8; k++) {                            /* spikes */
        float a = e->ang + TAU * (float)k / 8.0f;
        float ca = cosf(a), sa = sinf(a), pa = cosf(a + 0.3f), pb = sinf(a + 0.3f), qa = cosf(a - 0.3f), qb = sinf(a - 0.3f);
        float len = 30.0f + tele * 5.0f;
        xtri(t, pa * 16.0f, pb * 16.0f, ca * len, sa * len, qa * 16.0f, qb * 16.0f, (k & 1) ? s->c_accent : col_mix(s->c_accent, 0x80204FFFu, 0.3f));
    }
    for (int k = 0; k < 8; k++) {                            /* faceted octagon body lit from the top-left */
        float a0 = TAU * (float)k / 8.0f + 0.39f, a1 = a0 + TAU / 8.0f;
        float lit = clampf(0.5f - 0.5f * sinf((a0 + a1) * 0.5f + 0.6f), 0, 1);
        uint32_t c = col_mix(s->c_dark, s->c_light, lit);
        xtri(t, 0, 0, cosf(a0) * 21.0f, sinf(a0) * 21.0f, cosf(a1) * 21.0f, sinf(a1) * 21.0f, c);
    }
    for (int k = 0; k < 8; k++) {
        float a0 = TAU * (float)k / 8.0f, a1 = a0 + TAU / 8.0f;
        xtri(t, 0, 0, cosf(a0) * 11.0f, sinf(a0) * 11.0f, cosf(a1) * 11.0f, sinf(a1) * 11.0f, 0x3A1A80FFu);
    }
    gfx_blend(BL_ADD);
    float pulse = 0.6f + 0.4f * sinf(g->time * 5.0f + e->phase) + tele;
    gfx_poly(t->x, t->y, 7.0f + 3.0f * tele, 4, g->time * 2.0f, col_a(0xFF7BE0FFu, clampf(pulse, 0, 1)));
    gfx_glow(t->x, t->y, 30.0f + 30.0f * tele, col_a(0xFF5FC8FFu, 0.35f + 0.5f * tele));
    gfx_blend(BL_ALPHA);
}

static void draw_serpent_head(const Game *g, const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_SERPENT];
    xtri(t, -6, -14, -18, -24, -2, -16, s->c_accent);        /* crest spikes */
    xtri(t, -6, 14, -2, 16, -18, 24, s->c_accent);
    xtri(t, 26, 0, 6, -16, -16, -10, s->c_light);
    xtri(t, 26, 0, -16, -10, -16, 0, col_mix(s->c_light, s->c_mid, 0.5f));
    xtri(t, 26, 0, -16, 0, -16, 10, s->c_mid);
    xtri(t, 26, 0, -16, 10, 6, 16, s->c_dark);
    xtri(t, 10, -4, 2, -9, 0, -4, 0xFFE0F0FFu);              /* eye slits */
    xtri(t, 10, 4, 0, 4, 2, 9, 0xFFE0F0FFu);
    gfx_blend(BL_ADD);
    xglow(t, 4, -5, 14.0f, col_a(0xFF5FC8FFu, 0.8f));
    xglow(t, 4, 5, 14.0f, col_a(0xFF5FC8FFu, 0.8f));
    gfx_blend(BL_ALPHA);
    (void)g; (void)e;
}

static void draw_segment(const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_SEG];
    float r = e->r;
    if (e->idx & 1) xtri(t, -2, -r * 0.7f, -r * 0.9f, -r * 1.35f, r * 0.2f, -r * 0.8f, s->c_accent);
    else            xtri(t, -2, r * 0.7f, r * 0.2f, r * 0.8f, -r * 0.9f, r * 1.35f, s->c_accent);
    xtri(t, 0, 0, r * 1.1f, 0, 0, -r, s->c_light);           /* faceted diamond */
    xtri(t, 0, 0, 0, -r, -r * 1.1f, 0, col_mix(s->c_light, s->c_mid, 0.6f));
    xtri(t, 0, 0, -r * 1.1f, 0, 0, r, s->c_dark);
    xtri(t, 0, 0, 0, r, r * 1.1f, 0, s->c_mid);
}

static void draw_shield(const Game *g, const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_SHIELD];
    /* a hexagonal crystal prism pointing outward from the core */
    xquad(t, -18, -9, 12, -9, 12, 0, -18, 0, s->c_light);
    xquad(t, -18, 0, 12, 0, 12, 9, -18, 9, s->c_mid);
    xtri(t, 12, -9, 26, 0, 12, 0, WHITE);
    xtri(t, 12, 0, 26, 0, 12, 9, s->c_light);
    xtri(t, -18, -9, -26, 0, -18, 0, s->c_mid);
    xtri(t, -18, 0, -26, 0, -18, 9, s->c_dark);
    gfx_blend(BL_ADD);
    gfx_glow_cheap(t->x, t->y, 34.0f, col_a(s->c_light, 0.35f + 0.15f * sinf(g->time * 4.0f + (float)e->idx)));
    gfx_blend(BL_ALPHA);
}

static void draw_boss(const Game *g, const Enemy *e, Xf *t)
{
    const EnemyStats *s = &ESTAT[EN_BOSS];
    int phase2 = e->hp < e->maxhp * 0.5f;
    uint32_t lt = phase2 ? 0xFF7BCBFFu : s->c_light, md = phase2 ? 0xD8358FFFu : s->c_mid, dk = phase2 ? 0x8A1458FFu : s->c_dark;
    float R = 64.0f, Ri = 38.0f;
    float spin = e->t * 0.4f;
    /* energy tethers to the shields */
    gfx_blend(BL_ADD);
    for (int k = 0; k < MAX_EN; k++) {
        const Enemy *o = &g->en[k];
        if (!o->alive || o->type != EN_SHIELD || o->parent != (int)(e - g->en) || o->warp) continue;
        float ox = sxof(g, o->x), oy = o->y;
        gfx_glowline(t->x, t->y, ox, oy, 10.0f, col_a(0x9FF7FFFFu, 0.25f + 0.15f * sinf(g->time * 9.0f + (float)k)));
    }
    gfx_glow(t->x, t->y, 190.0f, col_a(phase2 ? 0xFF3C9CFFu : 0x9A6BFFFFu, 0.2f));
    gfx_blend(BL_ALPHA);
    /* spikes on the corners */
    for (int k = 0; k < 6; k++) {
        float a = spin + TAU * (float)k / 6.0f;
        float ca = cosf(a), sa = sinf(a);
        float pa = cosf(a + 0.16f), pb = sinf(a + 0.16f), qa = cosf(a - 0.16f), qb = sinf(a - 0.16f);
        xtri(t, pa * R, pb * R, ca * (R + 24.0f), sa * (R + 24.0f), qa * R, qb * R, (k & 1) ? s->c_accent : 0xFFE070FFu);
    }
    /* faceted outer hexagon */
    for (int k = 0; k < 6; k++) {
        float a0 = spin + TAU * (float)k / 6.0f, a1 = a0 + TAU / 6.0f;
        float lit = clampf(0.5f - 0.5f * sinf((a0 + a1) * 0.5f + 0.7f), 0, 1);
        uint32_t c = col_mix(dk, lt, lit);
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        xquad(t, c0 * Ri, s0 * Ri, c0 * R, s0 * R, c1 * R, s1 * R, c1 * Ri, s1 * Ri, c);
        xtri(t, c0 * R, s0 * R, c1 * R, s1 * R, (c0 + c1) * 0.5f * (R - 9.0f), (s0 + s1) * 0.5f * (R - 9.0f), col_mix(c, WHITE, 0.15f));
        xtri(t, 0, 0, c0 * Ri, s0 * Ri, c1 * Ri, s1 * Ri, (k & 1) ? 0x2B1066FFu : 0x1E0A4CFFu);
    }
    (void)md;
    /* glowing star core */
    gfx_blend(BL_ADD);
    float pulse = 0.7f + 0.3f * sinf(g->time * 6.0f);
    uint32_t core = phase2 ? 0xFF5090FFu : 0xFF4FD8FFu;
    for (int k = 0; k < 6; k++) {
        float a = -e->t * 1.6f + TAU * (float)k / 6.0f;
        float ca = cosf(a), sa = sinf(a);
        gfx_tri(t->x, t->y, col_a(WHITE, pulse), t->x + cosf(a - 0.35f) * 12.0f, t->y + sinf(a - 0.35f) * 12.0f, col_a(core, pulse),
                t->x + ca * 32.0f, t->y + sa * 32.0f, col_a(core, 0.0f));
        gfx_tri(t->x, t->y, col_a(WHITE, pulse), t->x + ca * 32.0f, t->y + sa * 32.0f, col_a(core, 0.0f),
                t->x + cosf(a + 0.35f) * 12.0f, t->y + sinf(a + 0.35f) * 12.0f, col_a(core, pulse));
    }
    gfx_glow(t->x, t->y, 60.0f, col_a(core, 0.6f * pulse));
    if (e->flash > 0) gfx_glow(t->x, t->y, 90.0f, col_a(WHITE, e->flash * 0.6f));
    gfx_blend(BL_ALPHA);
}

static void draw_warp(const Game *g, const Enemy *e, float sx, float sy)
{
    if (e->warp_t < 0) return;
    float p = e->warp_t / 0.7f;
    float big = e->type == EN_BOSS ? 4.0f : (e->type == EN_SERPENT || e->type == EN_SPORE ? 1.6f : 1.0f);
    gfx_blend(BL_ADD);
    for (int k = 0; k < 3; k++) {
        float q = clampf(p * 1.3f - 0.15f * (float)k, 0, 1);
        float r = (1.0f - q) * 60.0f * big + 10.0f * big;
        uint32_t c = col_a(k == 1 ? 0xFF4FA0FFu : 0xFF6A50FFu, q * (1.0f - q) * 3.0f);
        gfx_ring(sx, sy, r, 3.0f + 2.0f * big, 4, g->time * 2.0f * (k & 1 ? 1.0f : -1.0f) + 0.785f, c);
    }
    gfx_glow(sx, sy, 20.0f * big + 40.0f * big * p, col_a(ESTAT[e->type].c_light, 0.5f * p));
    if (p > 0.85f) gfx_glow(sx, sy, 50.0f * big, col_a(WHITE, (p - 0.85f) / 0.15f));
    gfx_blend(BL_ALPHA);
}

static void draw_enemies(const Game *g)
{
    /* serpents: draw tail first so the head sits on top */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = MAX_EN - 1; i >= 0; i--) {
            const Enemy *e = &g->en[i];
            if (!e->alive) continue;
            int is_seg = e->type == EN_SEG;
            if ((pass == 0) != is_seg) continue;
            float sx = sxof(g, e->x), sy = e->y;
            float cull = e->type == EN_BOSS ? 260.0f : 60.0f;
            if (!on_screen(sx, sy, cull)) continue;
            if (e->warp) {
                draw_warp(g, e, sx, sy);
                if (e->warp_t / 0.7f < 0.6f) continue;
            }
            float sc = e->warp ? (e->warp_t / 0.7f - 0.6f) / 0.4f : 1.0f;
            float ang = e->ang;
            if (e->type == EN_SPORE || e->type == EN_BOSS) ang = 0;
            Xf t = xf(sx, sy, ang, sc, sc);
            t.flash = e->flash;
            switch (e->type) {
            case EN_SHARD:   draw_shard(e, &t); break;
            case EN_ZIP:
                gfx_blend(BL_ADD);
                gfx_capsule(sx, sy, e->vx, e->vy, 34.0f, 10.0f, col_a(ESTAT[EN_ZIP].c_light, 0.45f));
                gfx_blend(BL_ALPHA);
                draw_zip(e, &t);
                break;
            case EN_SPORE:   draw_spore(g, e, &t); break;
            case EN_SERPENT: draw_serpent_head(g, e, &t); break;
            case EN_SEG:     draw_segment(e, &t); break;
            case EN_BOSS:    draw_boss(g, e, &t); break;
            case EN_SHIELD:  draw_shield(g, e, &t); break;
            default: break;
            }
        }
    }
}

static void draw_offscreen_markers(const Game *g)
{
    gfx_blend(BL_ADD);
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->type == EN_SEG || e->type == EN_SHIELD) continue;
        if (e->warp && e->warp_t < 0) continue;
        float sx = sxof(g, e->x);
        if (sx > -10.0f && sx < VW + 10.0f) continue;
        float d = fabsf(sx - VW * 0.5f) - VW * 0.5f;
        if (d > 900.0f) continue;
        float a = clampf(1.0f - d / 900.0f, 0.15f, 1.0f);
        float y = clampf(e->y, 70.0f, 410.0f);
        int right = sx > VW * 0.5f;
        float x = right ? VW - 14.0f : 14.0f, dir = right ? 1.0f : -1.0f;
        if (e->warp) {                                     /* incoming: blinking warning triangle */
            if ((int)(g->time * 8.0f) & 1) continue;
            float tx = x - dir * 6.0f;
            gfx_tri1(tx, y - 12, tx + 11, y + 8, tx - 11, y + 8, col_a(0xFF4060FFu, a));
            gfx_tri1(tx, y - 7, tx + 6, y + 5, tx - 6, y + 5, 0x40081AFFu);
            gfx_rect(tx - 1, y - 3, 2, 4, col_a(WHITE, a));
            gfx_rect(tx - 1, y + 2, 2, 2, col_a(WHITE, a));
        } else {
            uint32_t c = col_a(e->type == EN_BOSS ? 0xFF4FD8FFu : 0xFF5F7AFFu, a);
            gfx_tri1(x + dir * 8.0f, y, x - dir * 6.0f, y - 9.0f, x - dir * 6.0f, y + 9.0f, c);
            gfx_glow_cheap(x, y, 20.0f, col_a(c, 0.3f * a));
        }
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* projectiles, pickups, particles                                      */
/* ------------------------------------------------------------------ */
static void draw_projectiles(const Game *g)
{
    gfx_blend(BL_ADD);
    for (int i = 0; i < MAX_PB; i++) {
        const Bolt *b = &g->pb[i];
        if (!b->alive) continue;
        float sx = sxof(g, b->x);
        if (!on_screen(sx, b->y, 40)) continue;
        gfx_glow_cheap(sx, b->y, 34.0f, col_a(COL_A, 0.16f));
        gfx_capsule(sx, b->y, b->vx, b->vy, 40.0f, 13.0f, col_a(0xFF8A2AFFu, 0.6f));
        gfx_capsule(sx, b->y, b->vx, b->vy, 26.0f, 5.0f, WHITE);
    }
    for (int i = 0; i < MAX_MS; i++) {
        const Missile *m = &g->ms[i];
        if (!m->alive) continue;
        float sx = sxof(g, m->x);
        if (!on_screen(sx, m->y, 40)) continue;
        gfx_glow_cheap(sx, m->y, 28.0f, col_a(COL_X, 0.5f));
        gfx_capsule(sx, m->y, m->vx, m->vy, 18.0f, 8.0f, col_a(0xFFD15CFFu, 0.9f));
    }
    gfx_blend(BL_ALPHA);
    for (int i = 0; i < MAX_MS; i++) {
        const Missile *m = &g->ms[i];
        if (!m->alive) continue;
        float sx = sxof(g, m->x);
        if (!on_screen(sx, m->y, 40)) continue;
        Xf t = xf(sx, m->y, atan2f(m->vy, m->vx), 1, 1);
        xtri(&t, 9, 0, -5, -4, -5, 4, COL_X);
        xtri(&t, 9, 0, -5, -4, -2, 0, 0xFFB0D8FFu);
    }
}

static void draw_ebullets(const Game *g)
{
    gfx_blend(BL_ADD);
    for (int i = 0; i < MAX_EB; i++) {
        const EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        float sx = sxof(g, b->x);
        if (!on_screen(sx, b->y, 30)) continue;
        float r = b->big ? 9.0f : 7.0f;
        gfx_glow_cheap(sx, b->y, r * 3.2f, col_a(0xFF3F9AFFu, 0.45f));
        gfx_capsule(sx, b->y, b->vx, b->vy, r * 2.2f, r * 1.6f, col_a(0xFF5FA8FFu, 0.7f));
    }
    gfx_blend(BL_ALPHA);
    for (int i = 0; i < MAX_EB; i++) {
        const EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        float sx = sxof(g, b->x);
        if (!on_screen(sx, b->y, 30)) continue;
        float r = b->big ? 7.0f : 5.5f;
        float a = atan2f(b->vy, b->vx);
        Xf t = xf(sx, b->y, a, 1, 1);
        xquad(&t, -r * 1.4f, -r * 0.8f, r * 0.6f, -r, r * 0.6f, r, -r * 1.4f, r * 0.8f, 0xFF4F9AFFu);   /* pink pill */
        xtri(&t, r * 0.6f, -r, r * 1.3f, 0, r * 0.6f, r, 0xFF4F9AFFu);
        xquad(&t, -r * 0.8f, -r * 0.35f, r * 0.7f, -r * 0.4f, r * 0.7f, r * 0.4f, -r * 0.8f, r * 0.35f, 0xFFE8F4FFu);
    }
}

static void draw_crystals(const Game *g)
{
    gfx_blend(BL_ADD);
    for (int i = 0; i < MAX_CRY; i++) {
        const Crystal *c = &g->cr[i];
        if (!c->alive) continue;
        float sx = sxof(g, c->x);
        if (!on_screen(sx, c->y, 30)) continue;
        if (c->life < 2.0f && ((int)(c->life * 10.0f) & 1)) continue;
        gfx_glow_cheap(sx, c->y, c->kind ? 30.0f : 18.0f, col_a(c->kind ? 0x6BFF8AFFu : 0xFFD24DFFu, 0.45f));
    }
    gfx_blend(BL_ALPHA);
    for (int i = 0; i < MAX_CRY; i++) {
        const Crystal *c = &g->cr[i];
        if (!c->alive) continue;
        float sx = sxof(g, c->x);
        if (!on_screen(sx, c->y, 30)) continue;
        if (c->life < 2.0f && ((int)(c->life * 10.0f) & 1)) continue;
        if (c->kind == 0) {
            float w = 7.0f * fabsf(cosf(c->spin)) + 1.5f, h = 9.0f;
            uint32_t a = cosf(c->spin) > 0 ? 0xFFE580FFu : 0xF2A81CFFu, b = cosf(c->spin) > 0 ? 0xF2A81CFFu : 0xFFE580FFu;
            gfx_tri1(sx, c->y - h, sx - w, c->y, sx, c->y, a);
            gfx_tri1(sx, c->y - h, sx, c->y, sx + w, c->y, col_mix(a, WHITE, 0.4f));
            gfx_tri1(sx - w, c->y, sx, c->y + h, sx, c->y, b);
            gfx_tri1(sx, c->y, sx, c->y + h, sx + w, c->y, col_mix(b, 0x803000FFu, 0.3f));
        } else {
            float bob = sinf(g->time * 4.0f) * 2.0f;
            gfx_poly(sx, c->y + bob, 13.0f, 6, PI_F / 6.0f, 0x1E8C4AFFu);
            gfx_poly(sx, c->y + bob, 10.0f, 6, PI_F / 6.0f, 0x3EE07AFFu);
            gfx_rect(sx - 6, c->y + bob - 2, 12, 4, WHITE);
            gfx_rect(sx - 2, c->y + bob - 6, 4, 12, WHITE);
        }
    }
}

static void draw_particles(const Game *g, int additive)
{
    for (int i = 0; i < MAX_PT; i++) {
        const Part *p = &g->pt[i];
        if (!p->alive) continue;
        int add = p->kind == P_SPARK || p->kind == P_GLOW || p->kind == P_STAR;
        if (add != additive) continue;
        float sx = sxof(g, p->x);
        if (!on_screen(sx, p->y, p->size + 40.0f)) continue;
        float t = p->life / p->max_life;                    /* 1 -> 0 */
        switch (p->kind) {
        case P_SPARK: {
            float l = 0.035f;
            gfx_line(sx, p->y, sx - p->vx * l, p->y - p->vy * l, p->size * (0.4f + 0.6f * t), col_a(p->col, t), col_a(p->col, 0.0f));
            break;
        }
        case P_GLOW:
            gfx_glow_cheap(sx, p->y, p->size * (0.5f + 0.5f * t), col_a(p->col, t));
            break;
        case P_DEBRIS: {
            Xf x = xf(sx, p->y, p->rot, p->size, p->size);
            x.alpha = clampf(t * 2.5f, 0, 1);
            xtri(&x, 1.0f, 0, -0.6f, -0.8f, -0.4f, 0.9f, p->col);
            xtri(&x, 1.0f, 0, -0.4f, 0.9f, -0.2f, 0.1f, col_mix(p->col, 0x10001AFFu, 0.35f));
            break;
        }
        case P_SMOKE:
            gfx_poly2(sx, p->y, p->size, 6, p->rot, col_a(p->col, t * 0.6f), col_a(p->col, 0.0f));
            break;
        case P_CONF: {
            Xf x = xf(sx, p->y, p->rot, p->size, p->size * fabsf(cosf(p->rot * 1.7f)) + 0.5f);
            x.alpha = clampf(t * 3.0f, 0, 1);
            xquad(&x, -1, -0.6f, 1, -0.6f, 1, 0.6f, -1, 0.6f, p->col);
            break;
        }
        default: break;
        }
    }
}

static void draw_rings(const Game *g)
{
    gfx_blend(BL_ADD);
    for (int i = 0; i < MAX_RING; i++) {
        const Ring *r = &g->rg[i];
        if (!r->alive) continue;
        float t = r->life / r->max_life;
        float sx = sxof(g, r->x);
        if (!on_screen(sx, r->y, r->r + r->thick)) continue;
        float th = r->thick * (0.3f + 0.7f * t);
        gfx_ring2(sx, r->y, r->r, th, 40, 0, col_a(r->col, 0.0f), col_a(r->col, t));
        gfx_ring2(sx, r->y, r->r + th * 0.5f, th * 0.6f, 40, 0, col_a(r->col, t), col_a(r->col, 0.0f));
    }
    gfx_blend(BL_ALPHA);
}

/* ------------------------------------------------------------------ */
/* HUD                                                                  */
/* ------------------------------------------------------------------ */
static void shadow_text(const char *s, float x, float y, float px, uint32_t c, int align)
{
    float w = gfx_text_width(s, px);
    float lx = align == 0 ? x : (align == 1 ? x - w * 0.5f : x - w);
    gfx_text(s, lx + px * 0.6f, y + px * 0.6f, px, 0x14051EC0u);
    gfx_text(s, lx, y, px, c);
}

static void hex_badge(float cx, float cy, float r, uint32_t fill, uint32_t edge)
{
    gfx_poly(cx, cy, r + 3.0f, 6, 0, edge);
    gfx_poly(cx, cy, r, 6, 0, fill);
}

static void draw_minimap(const Game *g)
{
    float cx = VW * 0.5f, y0 = 10.0f, w = MINIMAP_W, h = 34.0f;
    float x0 = cx - w * 0.5f;
    gfx_rect(x0, y0, w, h, 0x1A0828B0u);
    /* corner brackets */
    uint32_t bc = 0xFFFFFFC0u;
    float b = 8.0f;
    gfx_rect(x0 - 2, y0 - 2, b, 2, bc); gfx_rect(x0 - 2, y0 - 2, 2, b, bc);
    gfx_rect(x0 + w - b + 2, y0 - 2, b, 2, bc); gfx_rect(x0 + w, y0 - 2, 2, b, bc);
    gfx_rect(x0 - 2, y0 + h, b, 2, bc); gfx_rect(x0 - 2, y0 + h - b + 2, 2, b, bc);
    gfx_rect(x0 + w - b + 2, y0 + h, b, 2, bc); gfx_rect(x0 + w, y0 + h - b + 2, 2, b, bc);
    float scale = w / g->W;
    /* the view window */
    float vw = VW * scale, vx = cx + wd(g, g->camx - g->px) * scale - vw * 0.5f;
    gfx_rect(vx, y0 + 2, vw, 2, 0xFFFFFF90u); gfx_rect(vx, y0 + h - 4, vw, 2, 0xFFFFFF90u);
    gfx_rect(vx, y0 + 2, 2, h - 4, 0xFFFFFF90u); gfx_rect(vx + vw - 2, y0 + 2, 2, h - 4, 0xFFFFFF90u);
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || (e->warp && e->warp_t < 0)) continue;
        float mx = cx + wd(g, e->x - g->px) * scale;
        float my = y0 + e->y / VH * h;
        float s = e->type == EN_BOSS ? 6.0f : (e->type == EN_SEG || e->type == EN_SHIELD ? 2.0f : 3.0f);
        uint32_t c = e->warp ? (((int)(g->time * 10.0f) & 1) ? 0xFFFFFFFFu : 0xFF4060FFu) : (e->type == EN_BOSS ? 0xFF4FD8FFu : 0xFF3A5AFFu);
        gfx_rect(mx - s * 0.5f, my - s * 0.5f, s, s, c);
    }
    float py = y0 + g->py / VH * h;
    float f = (float)g->face;
    gfx_tri1(cx + f * 5.0f, py, cx - f * 4.0f, py - 4.0f, cx - f * 4.0f, py + 4.0f, 0xFFD15CFFu);
    /* wave pips under the map */
    for (int k = 0; k < g->L->nwaves; k++) {
        float px = cx + ((float)k - (float)(g->L->nwaves - 1) * 0.5f) * 14.0f;
        uint32_t c = k < g->wave ? 0xFFFFFFFFu : (k == g->wave ? 0xFFD15CFFu : 0x7A6090FFu);
        gfx_poly(px, y0 + h + 10.0f, 4.0f, 4, 0, c);
    }
}

static void draw_weapon_icon(int w, float cx, float cy, uint32_t c)
{
    switch (w) {
    case 0:   /* blaster: twin dashes */
        gfx_rect(cx - 8, cy - 5, 14, 3, c); gfx_rect(cx - 4, cy + 2, 14, 3, c); break;
    case 1:   /* blade: two crossed sabres */
        gfx_line(cx - 9, cy + 7, cx + 9, cy - 7, 3.0f, c, c); gfx_line(cx - 9, cy - 7, cx + 9, cy + 7, 3.0f, c, c);
        gfx_poly(cx, cy, 4.0f, 4, 0, c); break;
    case 2:   /* missiles: three darts */
        for (int k = -1; k <= 1; k++) gfx_tri1(cx + 7, cy + (float)k * 6.0f, cx - 5, cy + (float)k * 6.0f - 3.0f, cx - 5, cy + (float)k * 6.0f + 3.0f, c);
        break;
    default:  /* lance: long beam */
        gfx_rect(cx - 10, cy - 2.5f, 20, 5, c); gfx_tri1(cx + 10, cy - 5, cx + 14, cy, cx + 10, cy + 5, c); break;
    }
}

static void draw_weapon_pad(const Game *g)
{
    /* Classic Controller layout: X top, A right, B bottom, Y left */
    static const float POS[4][2] = { {30, 0}, {0, 30}, {0, -30}, {-30, 0} };   /* A B X Y */
    static const char *KEY[4] = {"A", "B", "X", "Y"};
    static const uint32_t COL[4] = {COL_A, COL_B, COL_X, COL_Y};
    static const float COST[4] = {0, COST_BLADE, COST_MISSILE, COST_LANCE};
    float cx = VW - 82.0f, cy = VH - 64.0f;
    for (int w = 0; w < 4; w++) {
        float x = cx + POS[w][0], y = cy + POS[w][1];
        int ready = w == 0 ? !g->overheat : g->energy >= COST[w];
        float fl = g->wpn_flash[w];
        float s = 20.0f + fl * 3.0f;
        uint32_t edge = ready ? col_mix(COL[w], WHITE, fl * 0.6f) : 0x6A5070FFu;
        uint32_t fill = ready ? col_mix(0x22102EE0u, COL[w], 0.18f + fl * 0.3f) : 0x1A0A22D0u;
        if (g->empty_t > 0 && (int)g->empty_wpn == w && ((int)(g->time * 20.0f) & 1)) edge = 0xFF3040FFu;
        gfx_poly(x, y, s + 3.0f, 4, 0, edge);
        gfx_poly(x, y, s, 4, 0, fill);
        draw_weapon_icon(w, x, y - 1.0f, ready ? COL[w] : 0x7A6488FFu);
        gfx_text(KEY[w], x + s * 0.35f, y + s * 0.2f, 1.5f, ready ? WHITE : 0x9A88A8FFu);
        if (fl > 0) {
            gfx_blend(BL_ADD);
            gfx_glow_cheap(x, y, 34.0f, col_a(COL[w], fl * 0.6f));
            gfx_blend(BL_ALPHA);
        }
    }
}

static void draw_hud(const Game *g)
{
    char buf[48];
    /* ---- top left: wave + level ---- */
    if (g->wave < g->L->nwaves) snprintf(buf, sizeof buf, "WAVE %d/%d", g->wave + 1, g->L->nwaves);
    else snprintf(buf, sizeof buf, "CLEAR!");
    shadow_text(buf, 22, 16, 2.5f, WHITE, 0);
    shadow_text(g->L->name, 22, 40, 1.5f, 0xD8B8F0FFu, 0);

    draw_minimap(g);

    /* ---- boss bar ---- */
    if (g->boss >= 0 && g->en[g->boss].alive && !g->en[g->boss].warp) {
        float w = 300.0f, x = VW * 0.5f - w * 0.5f, y = 70.0f;
        gfx_quad4(x - 8, y, 0x14051EE0u, x + w + 8, y, 0x14051EE0u, x + w, y + 12, 0x14051EE0u, x, y + 12, 0x14051EE0u);
        float hp = g->boss_shown_hp, real = g->en[g->boss].hp / g->en[g->boss].maxhp;
        gfx_rect(x, y + 3, w * hp, 6, 0xFFFFFFC0u);
        gfx_rect_v(x, y + 3, w * clampf(real, 0, 1), 6, 0xFF7BE0FFu, 0xC0208AFFu);
        gfx_rect(x + w * 0.5f - 1, y + 1, 2, 10, 0xFFFFFF80u);
        shadow_text(ESTAT[EN_BOSS].name, VW * 0.5f, y + 16, 1.5f, 0xFF9BE8FFu, 1);
    }

    /* ---- top right: score, multiplier, chain ---- */
    long s = g->shown_score;
    if (s > 9999999) s = 9999999;
    snprintf(buf, sizeof buf, "%07ld", s);
    int lead = 0;
    while (lead < 6 && buf[lead] == '0') lead++;
    float px = 4.0f, w = gfx_text_width(buf, px), x = VW - 22.0f - w;
    char dim[8];
    memcpy(dim, buf, (size_t)lead); dim[lead] = 0;
    gfx_text(dim, x, 14, px, 0x6E5288A0u);
    shadow_text(buf + lead, x + (float)lead * 6.0f * px, 14, px, WHITE, 0);
    snprintf(buf, sizeof buf, "%.1fx", (double)g->mult);
    float mp = 4.0f + g->mult_pulse * 1.5f;
    uint32_t mc = g->mult >= 10.0f ? col_hsv(g->time * 0.5f, 0.35f, 1.0f, 255) : WHITE;
    shadow_text(buf, VW - 22.0f, 50 - g->mult_pulse * 4.0f, mp, mc, 2);
    if (g->chain >= 2) {
        snprintf(buf, sizeof buf, "%d HITS", g->chain);
        shadow_text(buf, VW - 22.0f, 88, 2.5f, 0xFFE070FFu, 2);
        float cw = 90.0f * clampf(g->chain_t / 2.4f, 0, 1);
        gfx_rect(VW - 22.0f - cw, 110, cw, 3, 0xFFE070FFu);
    }
    if (g->chain_show > 0) {
        snprintf(buf, sizeof buf, "+%ld", g->chain_bonus);
        shadow_text(buf, VW - 22.0f, 118, 2.0f, col_a(0xFFFFFFFFu, clampf(g->chain_show, 0, 1)), 2);
    }

    /* ---- bottom left: ship badge, lives, shields ---- */
    float bx = 50.0f, by = VH - 46.0f;
    hex_badge(bx, by, 26.0f, 0x2A1238E0u, 0xFFFFFFE0u);
    Xf t = xf(bx, by, 0, 0.8f, 0.8f);
    ship_model(&t);
    snprintf(buf, sizeof buf, "x%d", g->lives);
    shadow_text(buf, bx + 36, by - 24, 2.5f, WHITE, 0);
    for (int k = 0; k < SHIELDS_MAX; k++) {
        float qx = bx + 36.0f + (float)k * 22.0f, qy = by + 4.0f;
        int on = k < g->shields;
        uint32_t c = on ? (g->shields == 1 ? (((int)(g->time * 6.0f) & 1) ? 0xFF5050FFu : 0xFFB0B0FFu) : 0x6BE04AFFu) : 0x3A2448FFu;
        gfx_quad4(qx + 5, qy, 0xFFFFFFE0u, qx + 22, qy, 0xFFFFFFE0u, qx + 17, qy + 14, 0xFFFFFFE0u, qx, qy + 14, 0xFFFFFFE0u);
        gfx_quad4(qx + 6.5f, qy + 2, c, qx + 19.5f, qy + 2, c, qx + 15.5f, qy + 12, c, qx + 2.5f, qy + 12, c);
    }

    /* ---- bottom centre: energy + heat ---- */
    {
        float w = 300.0f, x = VW * 0.5f - w * 0.5f + 10.0f, y = VH - 44.0f;
        gfx_quad4(x - 14, y - 4, 0xFFFFFFD0u, x + w + 10, y - 4, 0xFFFFFFD0u, x + w, y + 18, 0xFFFFFFD0u, x - 24, y + 18, 0xFFFFFFD0u);
        gfx_quad4(x - 12, y - 2, 0x2A1238F0u, x + w + 7, y - 2, 0x2A1238F0u, x + w - 2, y + 16, 0x2A1238F0u, x - 21, y + 16, 0x2A1238F0u);
        float e = g->energy / ENERGY_MAX;
        float ew = (w - 8.0f) * e;
        gfx_quad4(x - 4, y + 2, 0xFFD24DFFu, x - 4 + ew + 6, y + 2, 0xFFD24DFFu, x - 4 + ew, y + 12, 0xF29A1CFFu, x - 10, y + 12, 0xF29A1CFFu);
        for (int k = 1; k < 10; k++) gfx_rect(x - 4 + (w - 8.0f) * (float)k / 10.0f, y + 2, 1.5f, 10, 0x2A123880u);
        /* crystal icon */
        float ix = x - 36, iy = y + 7;
        gfx_poly(ix, iy, 13, 4, 0, 0xFFFFFFE0u);
        gfx_poly(ix, iy, 10, 4, 0, 0xF2A81CFFu);
        gfx_tri1(ix, iy - 10, ix - 10, iy, ix, iy, 0xFFE580FFu);
        /* heat */
        float hw = (w - 30.0f) * g->heat;
        uint32_t hc = g->overheat ? (((int)(g->time * 12.0f) & 1) ? 0xFF3030FFu : 0xFFA030FFu) : col_mix(0xFFC23DFFu, 0xFF3020FFu, g->heat);
        gfx_rect(x, y - 12, w - 30.0f, 4, 0x2A1238C0u);
        gfx_rect(x, y - 12, hw, 4, hc);
        gfx_text("HEAT", x - 32, y - 14, 1.0f, 0xD8B8F0FFu);
    }

    draw_weapon_pad(g);
}

static void draw_popups(const Game *g)
{
    for (int i = 0; i < MAX_POP; i++) {
        const Popup *p = &g->pp[i];
        if (!p->alive) continue;
        float age = p->max_life - p->life;
        float pop = age < 0.12f ? 1.0f + (0.12f - age) / 0.12f * 0.8f : 1.0f;
        float a = clampf(p->life / 0.3f, 0, 1);
        if (p->big) {
            float px = 5.0f * pop;
            float w = gfx_text_width(p->txt, px);
            gfx_blend(BL_ADD);
            gfx_rect_v(p->x - w * 0.5f - 30, p->y - 8, w + 60, 25 + 10, col_a(p->col, 0.0f), col_a(p->col, 0.25f * a));
            gfx_rect_v(p->x - w * 0.5f - 30, p->y + 27, w + 60, 25, col_a(p->col, 0.25f * a), col_a(p->col, 0.0f));
            gfx_blend(BL_ALPHA);
            gfx_text_wave(p->txt, p->x + px * 0.7f, p->y + px * 0.7f, px, col_a(0x14051EFFu, a * 0.8f), g->time, 3.0f);
            gfx_text_wave(p->txt, p->x, p->y, px, col_a(p->col, a), g->time, 3.0f);
        } else {
            float px = 2.0f * pop;
            gfx_text_c(p->txt, p->x + 1.2f, p->y + 1.2f, px, col_a(0x14051EFFu, a * 0.7f));
            gfx_text_c(p->txt, p->x, p->y, px, col_a(p->col, a));
        }
    }
}

static void draw_banner(const Game *g)
{
    if (g->banner_t <= 0) return;
    float t = g->banner_t;
    float in = clampf((2.6f - t) / 0.3f, 0, 1), out = clampf(t / 0.35f, 0, 1);
    float slide = (1.0f - in) * (1.0f - in) * 500.0f - (1.0f - out) * (1.0f - out) * 500.0f;
    float a = in * out;
    float y = 180.0f;
    int warn = strcmp(g->banner, "WARNING") == 0;
    uint32_t c = warn ? (((int)(g->time * 6.0f) & 1) ? 0xFF3050FFu : 0xFFFFFFFFu) : 0xFFFFFFFFu;
    gfx_blend(BL_ADD);
    uint32_t bar = warn ? 0xFF2040FFu : 0xB060FFFFu;
    gfx_rect_v(0, y - 20, VW, 36, col_a(bar, 0.0f), col_a(bar, 0.35f * a));
    gfx_rect_v(0, y + 16, VW, 36, col_a(bar, 0.35f * a), col_a(bar, 0.0f));
    gfx_blend(BL_ALPHA);
    gfx_rect(0, y - 2 + slide * 0.0f, VW, 2, col_a(WHITE, 0.4f * a));
    gfx_rect(0, y + 36, VW, 2, col_a(WHITE, 0.4f * a));
    float px = 6.0f;
    gfx_text_wave(g->banner, VW * 0.5f + slide + 4, y + 4, px, col_a(0x14051EFFu, a), g->time, 2.0f);
    gfx_text_wave(g->banner, VW * 0.5f + slide, y, px, col_a(c, a), g->time, 2.0f);
    shadow_text(g->sub, VW * 0.5f - slide * 0.6f, y + 52, 2.0f, col_a(warn ? 0xFFB0C0FFu : 0xD8B8F0FFu, a), 1);
}

static void draw_boss_card(const Game *g)
{
    if (g->boss_intro <= 0) return;
    float t = g->boss_intro, a = clampf(t / 0.4f, 0, 1) * clampf((2.5f - t) / 0.2f, 0, 1);
    float x = VW * 0.5f + (1.0f - clampf((2.5f - t) / 0.3f, 0, 1)) * -400.0f;
    gfx_rect(0, 300, VW, 50, col_a(0x14051EFFu, 0.6f * a));
    gfx_rect(0, 300, VW, 2, col_a(0xFF4FD8FFu, a));
    gfx_rect(0, 348, VW, 2, col_a(0xFF4FD8FFu, a));
    shadow_text("HEXACORE", x, 308, 4.0f, col_a(0xFF9BE8FFu, a), 1);
    shadow_text("GUARDIAN OF THE CAVERNS", x, 336, 1.5f, col_a(WHITE, a), 1);
}

/* ------------------------------------------------------------------ */
/* screens                                                              */
/* ------------------------------------------------------------------ */
static void logo(const Game *g, float cx, float y, float px)
{
    const char *word[2] = {"NOVA", "PRISM"};
    for (int line = 0; line < 2; line++) {
        const char *s = word[line];
        float w = gfx_text_width(s, px), x0 = cx - w * 0.5f, ly = y + (float)line * px * 9.0f;
        for (int i = 0; s[i]; i++) {
            char ch[2] = {s[i], 0};
            float lx = x0 + (float)i * 6.0f * px;
            float bob = sinf(g->time * 2.5f - (float)i * 0.6f - (float)line) * px * 0.6f;
            for (int d = 5; d >= 1; d--)   /* chunky extruded depth */
                gfx_text(ch, lx + (float)d * px * 0.25f, ly + bob + (float)d * px * 0.3f, px, col_mix(0x3A0C52FFu, 0x8A1C6AFFu, (float)(5 - d) / 5.0f));
            uint32_t c = col_hsv(0.88f + 0.06f * (float)i + 0.08f * (float)line + g->time * 0.05f, 0.55f, 1.0f, 255);
            gfx_text(ch, lx, ly + bob, px, c);
        }
    }
}

static void draw_title(const Game *g)
{
    gfx_rect_v(0, 0, VW, VH, 0x10041A70u, 0x10041AC0u);
    gfx_blend(BL_ADD);
    gfx_glow(VW * 0.5f, 150, 360, col_a(0xFF4FD8FFu, 0.25f));
    for (int k = 0; k < 12; k++) {                         /* rotating light rays */
        float a = g->time * 0.15f + TAU * (float)k / 12.0f;
        gfx_tri(VW * 0.5f, 150, col_a(0xFFB0F0FFu, 0.12f), VW * 0.5f + cosf(a - 0.08f) * 700.0f, 150 + sinf(a - 0.08f) * 700.0f, 0xFFB0F000u,
                VW * 0.5f + cosf(a + 0.08f) * 700.0f, 150 + sinf(a + 0.08f) * 700.0f, 0xFFB0F000u);
    }
    gfx_blend(BL_ALPHA);
    logo(g, VW * 0.5f, 28, 9.5f);
    shadow_text("A LOW-POLY SPACE SHOOTER", VW * 0.5f, 198, 2.0f, 0xD8B8F0FFu, 1);

    char buf[48];
    snprintf(buf, sizeof buf, NUM_LEVELS > 1 ? "< LEVEL %d  %s >" : "LEVEL %d  %s", g->level + 1, LEVELS[g->level].name);
    shadow_text(buf, VW * 0.5f, 228, 2.5f, 0xFFE070FFu, 1);
    if (game_best_score(g->level) > 0) {
        snprintf(buf, sizeof buf, "BEST %ld", game_best_score(g->level));
        shadow_text(buf, VW * 0.5f, 254, 2.0f, WHITE, 1);
    }
    if ((int)(g->time * 2.0f) & 1) shadow_text("PRESS A TO LAUNCH", VW * 0.5f, 284, 3.0f, WHITE, 1);

    /* controls legend */
    static const char *L1[] = {"A  PULSE BLASTER", "B  NOVA BLADE", "X  SWARM MISSILES", "Y  PRISM LANCE"};
    static const uint32_t C[] = {COL_A, COL_B, COL_X, COL_Y};
    gfx_rect(110, 382, VW - 220, 84, 0x14051EB0u);
    for (int k = 0; k < 4; k++) {
        float x = 140.0f + (float)(k % 2) * 300.0f, y = 392.0f + (float)(k / 2) * 22.0f;
        gfx_poly(x, y + 5, 7, 4, 0, C[k]);
        gfx_text(L1[k], x + 14, y, 2.0f, WHITE);
    }
    shadow_text("L / R  TURN AROUND     STICK  FLY     +  PAUSE", VW * 0.5f, 444, 1.5f, 0xD8B8F0FFu, 1);
}

static void dim(float a) { gfx_rect(0, 0, VW, VH, col_a(0x0A0212FFu, a)); }

static void draw_pause(const Game *g)
{
    dim(0.6f);
    shadow_text("PAUSED", VW * 0.5f, 110, 7.0f, WHITE, 1);
    const char *items[4] = {"RESUME", "RESTART LEVEL", g->sound_on ? "SOUND: ON" : "SOUND: OFF", "QUIT TO TITLE"};
    for (int k = 0; k < 4; k++) {
        float y = 210.0f + (float)k * 42.0f;
        int sel = k == g->menu_sel;
        if (sel) {
            gfx_blend(BL_ADD);
            gfx_rect_v(VW * 0.5f - 200, y - 8, 400, 18, col_a(0xB060FFFFu, 0.0f), col_a(0xB060FFFFu, 0.4f));
            gfx_rect_v(VW * 0.5f - 200, y + 10, 400, 18, col_a(0xB060FFFFu, 0.4f), col_a(0xB060FFFFu, 0.0f));
            gfx_blend(BL_ALPHA);
            float bob = sinf(g->time * 8.0f) * 4.0f;
            gfx_poly(VW * 0.5f - 170 + bob, y + 10, 8, 3, 0, COL_A);
        }
        shadow_text(items[k], VW * 0.5f, y, 3.0f, sel ? 0xFFE070FFu : 0xD8C8E8FFu, 1);
    }
    shadow_text("A SELECT    B RESUME", VW * 0.5f, 400, 2.0f, 0xB8A0D0FFu, 1);
}

static void draw_over(const Game *g)
{
    dim(clampf(g->state_t * 1.5f, 0, 0.65f));
    float drop = (1.0f - clampf(g->state_t / 0.6f, 0, 1));
    drop = drop * drop * 300.0f;
    gfx_text_wave("GAME OVER", VW * 0.5f + 5, 140 - drop + 5, 9.0f, 0x14051EFFu, g->time, 4.0f);
    gfx_text_wave("GAME OVER", VW * 0.5f, 140 - drop, 9.0f, 0xFF4F7AFFu, g->time, 4.0f);
    char buf[40];
    snprintf(buf, sizeof buf, "SCORE %ld", g->score);
    shadow_text(buf, VW * 0.5f, 250, 3.5f, WHITE, 1);
    snprintf(buf, sizeof buf, "REACHED WAVE %d", g->wave + 1);
    shadow_text(buf, VW * 0.5f, 290, 2.0f, 0xD8B8F0FFu, 1);
    if (g->state_t > 1.2f) shadow_text("A RETRY    B TITLE", VW * 0.5f, 350, 3.0f, ((int)(g->time * 2.0f) & 1) ? WHITE : 0xFFE070FFu, 1);
}

static void draw_clear(const Game *g)
{
    dim(clampf(g->state_t * 1.5f, 0, 0.55f));
    float t = g->state_t;
    gfx_text_wave("LEVEL CLEAR!", VW * 0.5f + 4, 50 + 4, 7.0f, 0x14051EFFu, g->time, 3.0f);
    gfx_text_wave("LEVEL CLEAR!", VW * 0.5f, 50, 7.0f, col_hsv(g->time * 0.3f, 0.45f, 1.0f, 255), g->time, 3.0f);
    char buf[48];
    int mins = (int)(g->play_time / 60.0f), secs = (int)g->play_time % 60;
    const char *lab[5] = {"SCORE", "BEST MULTIPLIER", "LONGEST CHAIN", "ENEMIES DESTROYED", "TIME"};
    for (int k = 0; k < 5; k++) {
        float show = clampf((t - 0.3f - 0.35f * (float)k) / 0.25f, 0, 1);
        if (show <= 0) continue;
        float y = 140.0f + (float)k * 36.0f, x = 200.0f - (1.0f - show) * 80.0f;
        switch (k) {
        case 0: snprintf(buf, sizeof buf, "%ld", g->score); break;
        case 1: snprintf(buf, sizeof buf, "%.1fx", (double)g->best_mult); break;
        case 2: snprintf(buf, sizeof buf, "%d", g->max_chain); break;
        case 3: snprintf(buf, sizeof buf, "%d", g->kills); break;
        default: snprintf(buf, sizeof buf, "%d:%02d", mins, secs); break;
        }
        shadow_text(lab[k], x, y, 2.5f, col_a(0xD8B8F0FFu, show), 0);
        shadow_text(buf, VW - 330.0f + (1.0f - show) * 80.0f, y, 2.5f, col_a(WHITE, show), 2);
    }
    if (t > 2.6f) {
        static const char *R[4] = {"S", "A", "B", "C"};
        static const uint32_t RC[4] = {0xFFE070FFu, 0xFF7BE0FFu, 0x5CE1FFFFu, 0x9CFFD8FFu};
        float k = clampf((t - 2.6f) / 0.25f, 0, 1);
        float px = 12.0f + (1.0f - k) * 24.0f;
        float cx = VW - 170.0f, cy = 230.0f;
        gfx_blend(BL_ADD);
        gfx_glow(cx, cy, 150.0f, col_a(RC[g->rank], 0.4f));
        for (int i = 0; i < 10; i++) {
            float a = g->time * 0.6f + TAU * (float)i / 10.0f;
            gfx_tri(cx, cy, col_a(RC[g->rank], 0.25f), cx + cosf(a - 0.1f) * 200.0f, cy + sinf(a - 0.1f) * 200.0f, RC[g->rank] & 0xFFFFFF00u,
                    cx + cosf(a + 0.1f) * 200.0f, cy + sinf(a + 0.1f) * 200.0f, RC[g->rank] & 0xFFFFFF00u);
        }
        gfx_blend(BL_ALPHA);
        hex_badge(cx, cy, 70.0f, 0x2A1238F0u, RC[g->rank]);
        shadow_text(R[g->rank], cx, cy - px * 3.5f, px, RC[g->rank], 1);
        shadow_text("RANK", cx, cy + 82.0f, 2.5f, WHITE, 1);
    }
    if (t > 3.0f) {
        const char *msg = g->level + 1 < NUM_LEVELS ? "PRESS A FOR THE NEXT LEVEL" : "PRESS A TO RETURN TO TITLE";
        shadow_text(msg, VW * 0.5f, 420, 2.5f, ((int)(g->time * 2.0f) & 1) ? WHITE : 0xFFE070FFu, 1);
    }
}

/* ------------------------------------------------------------------ */
void game_render(const Game *g)
{
    gfx_begin();
    const LevelDef *L = g->L;

    float ox = 0, oy = 0;
    if (g->shake > 0) {
        ox = (hashf((uint32_t)g->frame * 2u + 1u) - 0.5f) * 2.0f * g->shake;
        oy = (hashf((uint32_t)g->frame * 2u + 2u) - 0.5f) * 2.0f * g->shake;
    }
    gfx_view(ox, oy, 1.0f + g->zoom_kick);

    /* background */
    gfx_rect_v(-40, -40, VW + 80, VH + 80, L->sky_top, L->sky_bot);
    draw_blob_layer(g, 0.3f, 10, 0xB10Bu, 70, 140, 0, L->blob, L->blob_hi);
    draw_glyph_layer(g);
    draw_blob_layer(g, 0.65f, 14, 0x51DEu, 60, 120, 1, col_mix(L->blob, L->sky_bot, 0.2f), col_mix(L->blob_hi, L->terr_lo, 0.18f));
    draw_terrain(g);

    if (g->state != ST_TITLE) {
        draw_crystals(g);
        gfx_blend(BL_ALPHA);
        draw_particles(g, 0);
        draw_enemies(g);
        draw_blade(g);
        draw_player(g);
        draw_lance(g);
        draw_projectiles(g);
        draw_ebullets(g);
        gfx_blend(BL_ADD);
        draw_particles(g, 1);
        gfx_blend(BL_ALPHA);
        draw_rings(g);
    } else {
        gfx_blend(BL_ADD);
        draw_particles(g, 1);
        gfx_blend(BL_ALPHA);
        draw_player(g);
    }

    gfx_view_reset();
    /* vignette */
    gfx_rect_v(0, 0, VW, 50, 0x0A021280u, 0x0A021200u);
    gfx_rect_v(0, VH - 50, VW, 50, 0x0A021200u, 0x0A021280u);
    gfx_quad4(0, 0, 0x0A021270u, 90, 0, 0x0A021200u, 90, VH, 0x0A021200u, 0, VH, 0x0A021270u);
    gfx_quad4(VW - 90, 0, 0x0A021200u, VW, 0, 0x0A021270u, VW, VH, 0x0A021270u, VW - 90, VH, 0x0A021200u);

    if (g->flash > 0) {
        gfx_blend(BL_ADD);
        float f = clampf(g->flash, 0, 1);
        gfx_rect(0, 0, VW, VH, RGBA((int)(255 * g->flash_r), (int)(255 * g->flash_g), (int)(255 * g->flash_b), (int)(f * 170.0f)));
        gfx_blend(BL_ALPHA);
    }

    switch (g->state) {
    case ST_TITLE:
        draw_title(g);
        break;
    case ST_PLAY:
    case ST_PAUSE:
        draw_offscreen_markers(g);
        draw_popups(g);
        draw_banner(g);
        draw_boss_card(g);
        draw_hud(g);
        if (g->state == ST_PAUSE) draw_pause(g);
        break;
    case ST_OVER:
        draw_hud(g);
        draw_over(g);
        break;
    case ST_CLEAR:
        draw_clear(g);
        break;
    }
    if (g->fade > 0) gfx_rect(0, 0, VW, VH, col_a(0x000000FFu, clampf(g->fade, 0, 1)));
}
