/*
 * DOODLE SHOOTER - rendering, "notebook doodle" edition.
 *
 * Everything looks like a kid drew it with crayons and a felt pen on a page of school notebook paper:
 * crayon-scribbled fills, wobbly navy outlines, handwriting, no lighting at all.
 *
 * The background "boils" like stop-motion (its outlines are redrawn, its scribbles shift and the
 * paper nudges a hair BOIL_FPS times a second) while everything you play with moves smoothly at 60 fps.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

enum { P_SPARK, P_GLOW, P_DEBRIS, P_SMOKE, P_CONF, P_STAR };   /* must match game.c */

#define BOIL_FPS 8.0f

/* the kid's pencil case */
#define INK      0x22307CFFu    /* navy felt pen */
#define PAPER    0xF4EFE0FFu
#define PAPER_DK 0xD9CDB0FFu
#define RULE     0x9DB9E2FFu
#define MARGIN   0xE58A8AFFu
#define WHITE    0xFFFFFFFFu
#define C_ORANGE 0xFF9A2AFFu
#define C_YELLOW 0xFFD23AFFu
#define C_RED    0xF0483CFFu
#define C_BLUE   0x3D8CF0FFu
#define C_BLUE_D 0x2A64C8FFu
#define C_SKY    0x8FD6FFFFu
#define C_MINT   0x4CD9A0FFu
#define C_GREEN  0x8CD84AFFu
#define C_GREEN_D 0x5DB03AFFu
#define C_PINK   0xFF6FA8FFu
#define C_GREY   0x8A8A96FFu
#define T_ORANGE 0xEE7A12FFu    /* text colours: darker so solid letters read on cream */
#define T_GREEN  0x3A9A2EFFu
#define T_BLUE   0x2A64C8FFu

static int   boil;              /* stop-motion frame number for the background */
static float boil_t;            /* time of the current boil frame */

/* ------------------------------------------------------------------ */
/* local-space transform for doodles                                    */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, c, s, sx, sy; } Xf;

static Xf xf(float x, float y, float ang, float sx, float sy)
{
    Xf t = { x, y, cosf(ang), sinf(ang), sx, sy };
    return t;
}
static void xpt(const Xf *t, float lx, float ly, float *ox, float *oy)
{
    lx *= t->sx; ly *= t->sy;
    *ox = t->x + lx * t->c - ly * t->s;
    *oy = t->y + lx * t->s + ly * t->c;
}
static void xadd(Poly *p, const Xf *t, float lx, float ly)
{
    float x, y;
    xpt(t, lx, ly, &x, &y);
    poly_add(p, x, y);
}
/* a doodled shape from a list of local points */
static void shape(const Xf *t, const float (*pts)[2], int n, uint32_t fill, uint32_t ink, float w, uint32_t seed)
{
    Poly p;
    poly_clear(&p);
    for (int i = 0; i < n; i++) xadd(&p, t, pts[i][0], pts[i][1]);
    gfx_uv(t->x, t->y, 128.0f, (float)(seed & 63) / 64.0f, (float)((seed >> 6) & 63) / 64.0f);
    if (fill & 255) crayon_fill(&p, fill);
    if (ink & 255) pen_poly(&p, 1, w, ink, seed, 0.9f);
}
static void blob(float cx, float cy, float rx, float ry, uint32_t fill, uint32_t ink, float w, uint32_t seed, float lump)
{
    Poly p;
    int n = (int)(fmaxf(rx, ry) * 0.9f) + 9;
    if (n > 40) n = 40;
    poly_blob(&p, cx, cy, rx, ry, n, lump, seed);
    gfx_uv(cx, cy, 128.0f, (float)(seed & 63) / 64.0f, 0.3f);
    if (fill & 255) crayon_fill(&p, fill);
    if (ink & 255) pen_poly(&p, 1, w, ink, seed, 0.6f);
}
static void xblob(const Xf *t, float lx, float ly, float r, uint32_t fill, uint32_t ink, float w, uint32_t seed)
{
    float x, y;
    xpt(t, lx, ly, &x, &y);
    blob(x, y, r * fabsf(t->sx), r * fabsf(t->sy), fill, ink, w, seed, 0.06f);
}
static void xline(const Xf *t, float ax, float ay, float bx, float by, float w, uint32_t c, uint32_t seed)
{
    float x0, y0, x1, y1;
    xpt(t, ax, ay, &x0, &y0); xpt(t, bx, by, &x1, &y1);
    pen_line(x0, y0, x1, y1, w, c, seed, 0.5f);
}
/* googly eye looking at (lookx, looky) in screen space */
static void eye(const Xf *t, float lx, float ly, float r, float lookx, float looky, uint32_t seed)
{
    float x, y;
    xpt(t, lx, ly, &x, &y);
    blob(x, y, r, r, WHITE, INK, 1.4f, seed, 0.05f);
    float dx = lookx - x, dy = looky - y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
    float pr = r * 0.48f, m = r - pr - 0.6f;
    gfx_tex(TEX_SOLID);
    Poly p;
    poly_blob(&p, x + dx / d * m, y + dy / d * m, pr, pr, 8, 0.0f, seed);
    gfx_uv(x, y, 128, 0, 0);
    crayon_fill(&p, INK);
    crayon_fill(&p, INK);
}

static float posmod(float x, float p) { x = fmodf(x, p); return x < 0 ? x + p : x; }
static int on_screen(float sx, float sy, float r) { return sx > -r && sx < VW + r && sy > -r && sy < VH + r; }
static float hf(uint32_t x) { return (float)(ihash(x) >> 8) / 16777216.0f; }

/* ------------------------------------------------------------------ */
/* background: a plain page of ruled notebook paper with pencil doodles   */
/* ------------------------------------------------------------------ */
#define WALL_TOP 34.0f
#define WALL_BOT 446.0f

/* The page scrolls slower than the action (and shifts a touch up/down as you fly), the doodles on it
 * scroll faster - so the paper reads as the far layer and you get a sense of depth. */
#define PAPER_PX 0.35f            /* horizontal parallax of the page */
#define PAPER_PY 0.06f            /* vertical parallax of the page (from the ship's height) */

static void draw_paper(const Game *g)
{
    float P = g->W * PAPER_PX;                                  /* the page repeats once per trip round the level */
    float ox = -posmod(g->scroll * PAPER_PX, P), oy = (240.0f - g->py) * PAPER_PY;
    gfx_tex(TEX_SOLID);
    gfx_rect(-30, -30, VW + 60, VH + 60, PAPER);
    /* paper fibres and soft crumples */
    gfx_tex(TEX_PAPER);
    gfx_uv(ox, oy, 210.0f, 0, 0);
    gfx_rect(-30, -30, VW + 60, VH + 60, col_a(PAPER_DK, 0.55f));
    gfx_tex(TEX_SOLID);
    static const float CR[5][8] = {
        {120, -10, 420, -10, 300, 200, 60, 260}, {520, 60, 900, 20, 880, 300, 640, 220},
        {-20, 300, 260, 250, 380, 500, -20, 500}, {420, 300, 700, 260, 760, 500, 380, 500}, {250, 120, 520, 160, 420, 330, 210, 280} };
    for (float base = ox; base < VW + 40.0f; base += P)
        for (int i = 0; i < 5; i++) {
            if (base + CR[i][2] < -40 || base + CR[i][0] > VW + 40) continue;
            gfx_quad4(base + CR[i][0], CR[i][1] + oy, col_a(PAPER_DK, (i & 1) ? 0.10f : 0.0f), base + CR[i][2], CR[i][3] + oy, col_a(PAPER_DK, 0.08f),
                      base + CR[i][4], CR[i][5] + oy, col_a(PAPER_DK, (i & 1) ? 0.0f : 0.12f), base + CR[i][6], CR[i][7] + oy, col_a(PAPER_DK, 0.05f));
        }
    int graph = g->L->paper == 1;
    if (graph) {                                                /* graph paper: a fine green grid, bolder every 5 squares */
        const float GS = 17.0f;
        uint32_t gc = 0x8CC4B4FFu;
        int k0 = (int)floorf(-oy / GS) - 2;
        for (int k = k0; k < k0 + 34; k++) {
            float y = oy + (float)k * GS;
            if (y < -4 || y > VH + 4) continue;
            gfx_rect(-30, y, VW + 60, (k % 5 == 0) ? 1.5f : 0.9f, col_a(gc, (k % 5 == 0) ? 0.8f : 0.5f));
        }
        float gx = -posmod(g->scroll * PAPER_PX, GS * 5.0f * 400.0f);   /* continuous (the page period isn't a whole number of squares) */
        int j0 = (int)floorf(-gx / GS) - 2;
        for (int j = j0; j < j0 + 56; j++) {
            float x = gx + (float)j * GS;
            if (x < -4 || x > VW + 4) continue;
            gfx_rect(x, -30, (j % 5 == 0) ? 1.5f : 0.9f, VH + 60, col_a(gc, (j % 5 == 0) ? 0.8f : 0.5f));
        }
    } else {                                                    /* ruled lines */
        float first = 26.0f + posmod(oy, 21.0f) - 21.0f;
        for (float y = first; y < VH + 10.0f; y += 21.0f) gfx_rect(-30, y, VW + 60, 1.4f, col_a(RULE, 0.85f));
    }
    /* sheet edges, and on ruled paper the red margin with the binder holes, scrolling past */
    for (float base = ox; base < VW + 40.0f; base += P) {
        if (base + 70.0f < -10.0f) continue;
        /* where one sheet ends and the next begins: the new sheet's edge casts a soft shadow */
        gfx_rect(base - 1.0f, -30, 1.4f, VH + 60, col_a(0x9C8A70FFu, 0.55f));
        gfx_quad4(base - 14.0f, -30, col_a(0x7A6A50FFu, 0.0f), base - 1.0f, -30, col_a(0x7A6A50FFu, 0.22f),
                  base - 1.0f, VH + 30, col_a(0x7A6A50FFu, 0.22f), base - 14.0f, VH + 30, col_a(0x7A6A50FFu, 0.0f));
        if (graph) continue;
        gfx_rect(base + 62, -30, 1.6f, VH + 60, col_a(MARGIN, 0.85f));
        gfx_rect(base + 66, -30, 1.0f, VH + 60, col_a(MARGIN, 0.5f));
        for (float y = 36.0f + posmod(oy, 68.0f) - 68.0f; y < VH + 20.0f; y += 68.0f) {
            Poly p;
            float hx = base + 22.0f;
            uint32_t hs = (uint32_t)(y - oy + 100.0f);
            poly_blob(&p, hx, y, 9.5f, 9.5f, 14, 0.05f, hs);
            gfx_uv(hx, y, 128, 0, 0);
            crayon_fill(&p, 0x6E5E4EFFu);
            gfx_tex(TEX_SOLID);
            gfx_quad4(hx - 9, y - 2, 0x3E3226C0u, hx + 9, y - 2, 0x3E3226C0u, hx + 9, y + 2, 0x3E322600u, hx - 9, y + 2, 0x3E322600u);
            pen_poly(&p, 1, 1.0f, col_a(0x9C8A70FFu, 0.8f), hs, 0.3f);
        }
    }
}

/* little doodles floating in space: stars, plus signs, squares, planets, a smiley moon */
static void draw_space_doodles(const Game *g, float factor, int n, uint32_t seed0, int big)
{
    float P = 1800.0f, s = g->scroll * factor, oy = (240.0f - g->py) * factor * 0.2f;
    for (int i = 0; i < n; i++) {
        uint32_t h = seed0 + (uint32_t)i * 7919u;
        float x = posmod(hf(h) * P - s, P) - 120.0f;
        if (x < -60 || x > VW + 60) continue;
        float y = WALL_TOP + 30.0f + hf(h + 1) * (WALL_BOT - WALL_TOP - 60.0f) + oy;
        uint32_t seed = h + (uint32_t)boil * 131u;
        int kind = (int)(hf(h + 2) * (big ? 4.0f : 5.0f));
        float r = big ? 16.0f + hf(h + 3) * 14.0f : 4.0f + hf(h + 3) * 4.0f;
        uint32_t light = col_a(C_GREY, 0.55f);
        if (!big) {
            switch (kind) {
            case 0: pen_line(x - r, y, x + r, y, 1.6f, light, seed, 0.4f); pen_line(x, y - r, x, y + r, 1.6f, light, seed + 1, 0.4f); break;
            case 1: pen_line(x - r * 0.7f, y - r * 0.7f, x + r * 0.7f, y + r * 0.7f, 1.6f, light, seed, 0.4f);
                    pen_line(x + r * 0.7f, y - r * 0.7f, x - r * 0.7f, y + r * 0.7f, 1.6f, light, seed + 1, 0.4f); break;
            case 2: { Poly p; poly_clear(&p); poly_add(&p, x - r * 0.6f, y - r * 0.6f); poly_add(&p, x + r * 0.6f, y - r * 0.6f);
                      poly_add(&p, x + r * 0.6f, y + r * 0.6f); poly_add(&p, x - r * 0.6f, y + r * 0.6f);
                      pen_poly(&p, 1, 1.5f, light, seed, 0.4f); break; }
            case 3: { Poly p; poly_clear(&p);                       /* five-point star */
                      for (int k = 0; k < 10; k++) { float a = -PI_F * 0.5f + PI_F * (float)k / 5.0f, rr = (k & 1) ? r * 0.45f : r * 1.2f; poly_add(&p, x + cosf(a) * rr, y + sinf(a) * rr); }
                      pen_poly(&p, 1, 1.3f, light, seed, 0.4f); break; }
            default: blob(x, y, 2.0f, 2.0f, light, 0, 0, seed, 0.1f); break;
            }
        } else {
            uint32_t pen = col_a(C_GREY, 0.42f);
            switch (kind) {
            case 0: {                                                /* ringed planet */
                Poly pl; poly_blob(&pl, x, y, r, r, 18, 0.05f, seed);
                pen_poly(&pl, 1, 1.8f, pen, seed, 0.6f);
                Poly ring; poly_clear(&ring);
                for (int k = 0; k <= 18; k++) { float a = -0.3f + (PI_F + 0.6f) * (float)k / 18.0f; poly_add(&ring, x + cosf(a) * r * 1.8f, y + sinf(a) * r * 0.45f); }
                pen_poly(&ring, 0, 1.8f, pen, seed + 5, 0.5f);
                break;
            }
            case 1: {                                                /* smiley moon */
                Poly mn; poly_blob(&mn, x, y, r, r, 18, 0.04f, seed);
                pen_poly(&mn, 1, 1.8f, pen, seed, 0.5f);
                pen_line(x - r * 0.35f, y - r * 0.3f, x - r * 0.35f, y - r * 0.1f, 1.8f, pen, seed + 1, 0.1f);
                pen_line(x + r * 0.3f, y - r * 0.3f, x + r * 0.3f, y - r * 0.1f, 1.8f, pen, seed + 2, 0.1f);
                Poly m; poly_clear(&m);
                for (int k = 0; k <= 8; k++) { float a = 0.4f + (PI_F - 0.8f) * (float)k / 8.0f; poly_add(&m, x + cosf(a) * r * 0.5f, y + r * 0.05f + sinf(a) * r * 0.45f); }
                pen_poly(&m, 0, 1.6f, pen, seed + 3, 0.3f);
                break;
            }
            case 2: {                                                /* comet */
                for (int k = 0; k < 3; k++)
                    pen_line(x + r * 0.5f, y + (float)(k - 1) * 4.0f, x + r * 2.4f, y + (float)(k - 1) * 7.0f, 1.6f, pen, seed + (uint32_t)k, 0.6f);
                Poly c; poly_blob(&c, x, y, r * 0.45f, r * 0.45f, 10, 0.1f, seed);
                pen_poly(&c, 1, 1.8f, pen, seed, 0.4f);
                break;
            }
            default: {                                               /* spiral galaxy scribble */
                Poly sp2; poly_clear(&sp2);
                for (int k = 0; k < 28; k++) { float a = (float)k * 0.5f, rr = r * (float)k / 28.0f; poly_add(&sp2, x + cosf(a) * rr, y + sinf(a) * rr * 0.7f); }
                pen_poly(&sp2, 0, 1.8f, pen, seed, 0.6f);
                break;
            }
            }
        }
    }
}

static void draw_background(const Game *g, float shx, float shy)
{
    float jx = (hf((uint32_t)boil * 3u + 1u) - 0.5f) * 1.6f, jy = (hf((uint32_t)boil * 5u + 2u) - 0.5f) * 1.2f;
    gfx_view(shx + jx, shy + jy, 1.0f);
    draw_paper(g);
    draw_space_doodles(g, 0.55f, 7, 0xBEEFu, 1);
    draw_space_doodles(g, 0.8f, 46, 0xF00Du, 0);
}

/* ------------------------------------------------------------------ */
/* the player's rocket                                                  */
/* ------------------------------------------------------------------ */
#define SHIP_PIC 66.0f            /* size the 128x128 drawing is shown at */

static void ship_model(const Xf *t, float flame, float time)
{
    if (flame > 0) {                                             /* crayon flame out of the engine, behind the drawing */
        float L = flame * (1.0f + 0.25f * sinf(time * 47.0f)), z = sinf(time * 31.0f) * 2.0f, b = -23.0f;
        float F[7][2] = {{b, -6.5f}, {b - L * 0.55f, -8.5f + z}, {b - L * 0.5f, -3}, {b - L, z}, {b - L * 0.5f, 3}, {b - L * 0.55f, 8.5f - z}, {b, 6.5f}};
        shape(t, (const float (*)[2])F, 7, C_YELLOW, INK, 1.8f, 103);
        float F2[5][2] = {{b, -3.5f}, {b - L * 0.4f, -2.5f}, {b - L * 0.62f, z * 0.5f}, {b - L * 0.4f, 2.5f}, {b, 3.5f}};
        shape(t, (const float (*)[2])F2, 5, C_RED, 0, 0, 104);
    }
    float h = SHIP_PIC * 0.5f, X[4], Y[4];
    xpt(t, -h, -h, &X[0], &Y[0]); xpt(t, h, -h, &X[1], &Y[1]); xpt(t, h, h, &X[2], &Y[2]); xpt(t, -h, h, &X[3], &Y[3]);
    gfx_image(TEX_SHIP, X, Y, WHITE);
}

static void draw_trail(const Game *g)
{
    if (g->trail_n < 3) return;
    /* a curly pencil "swoosh" behind the ship, like the reference */
    float px = 0, py = 0;
    gfx_tex(TEX_SOLID);
    for (int i = 0; i < g->trail_n; i++) {
        float sx = sxof(g, g->trail[i][0]), sy = g->trail[i][1];
        float t = 1.0f - (float)i / (float)(g->trail_n - 1);
        if (i > 0 && fabsf(sx - px) < 120.0f) {
            gfx_line(px, py, sx, sy, 7.0f * t + 1.0f, col_a(0x5A5A68FFu, 0.75f * t), col_a(0x5A5A68FFu, 0.75f * t));
            gfx_line(px, py, sx, sy, 4.5f * t, col_a(PAPER, 0.95f * t), col_a(PAPER, 0.95f * t));
        }
        px = sx; py = sy;
    }
}

static void draw_player(const Game *g)
{
    if (!g->alive) return;
    float sx = sxof(g, g->px), sy = g->py;
    float ft = g->flip_t > 0 ? g->flip_t : 0.0f;
    float scx = (float)g->face * cosf(PI_F * ft);
    float scy = (1.0f - 0.25f * sinf(PI_F * ft));
    float rot = g->tilt * (scx >= 0 ? 1.0f : -1.0f) + g->flip_dir * sinf(PI_F * ft) * 0.25f;
    if (fabsf(scx) < 0.1f) scx = scx < 0 ? -0.1f : 0.1f;
    Xf t = xf(sx, sy - sinf(PI_F * ft) * 6.0f, rot, scx, scy);
    int blink = g->inv_t > 0 && ((int)(g->time * 16.0f) & 1);
    draw_trail(g);
    float boost = 1.0f + clampf((g->pvx * (float)g->face) / SHIP_SPEED, 0, 1) * 0.7f;
    if (!blink) ship_model(&t, 13.0f * boost, g->time);
    /* heat: a little thermometer drawn over the ship */
    if (g->heat > 0.25f || g->overheat) {
        float x = sx - 20.0f, y = sy - 36.0f, w = 40.0f, h = 7.0f;
        Poly p; poly_clear(&p);
        poly_add(&p, x, y); poly_add(&p, x + w, y); poly_add(&p, x + w, y + h); poly_add(&p, x, y + h);
        gfx_tex(TEX_SOLID);
        gfx_rect(x, y, w, h, col_a(PAPER, 0.9f));
        Poly f; poly_clear(&f);
        float fw = w * clampf(g->heat, 0, 1);
        poly_add(&f, x, y); poly_add(&f, x + fw, y); poly_add(&f, x + fw, y + h); poly_add(&f, x, y + h);
        gfx_uv(x, y, 64, 0, 0);
        uint32_t c = g->overheat ? (((int)(g->time * 10.0f) & 1) ? C_RED : C_ORANGE) : col_mix(C_YELLOW, C_RED, g->heat);
        crayon_fill(&f, c);
        crayon_fill(&f, c);
        pen_poly(&p, 1, 1.6f, INK, 0x7E7u, 0.4f);
        if (g->overheat) gfx_text_fancy("hot!", sx, y - 22.0f, 18.0f, C_RED, PAPER, 1.6f, g->time, 2.0f);
    }
}

static void draw_blade(const Game *g)
{
    if (!g->blade_on || !g->alive) return;
    float sx = sxof(g, g->px), sy = g->py, dir = (float)g->face;
    float grow = clampf(g->blade_t / 0.1f, 0, 1);
    float R = BLADE_R * (0.6f + 0.4f * grow);
    for (int k = 0; k < 2; k++) {
        float a = g->blade_ang + PI_F * (float)k;
        Poly p; poly_clear(&p);                                   /* a fat crayon crescent swoosh */
        const int N = 12;
        for (int i = 0; i <= N; i++) { float u = (float)i / N, aa = a - dir * 1.9f * u, rr = R + 4.0f - 2.0f * u; poly_add(&p, sx + cosf(aa) * rr, sy + sinf(aa) * rr); }
        for (int i = N; i >= 0; i--) { float u = (float)i / N, aa = a - dir * 1.9f * u, rr = R - 26.0f * (1.0f - u) - 2.0f; poly_add(&p, sx + cosf(aa) * rr, sy + sinf(aa) * rr); }
        gfx_uv(sx, sy, 128, 0.2f * (float)k, 0);
        crayon_fill(&p, C_MINT);
        pen_poly(&p, 1, 2.4f, INK, 0xB1Au + (uint32_t)k, 0.8f);
        for (int m = 0; m < 3; m++) {                              /* whoosh lines */
            float aa = a - dir * (0.5f + 0.45f * (float)m), r0 = R + 10.0f, r1 = R + 18.0f + 6.0f * (float)m;
            pen_line(sx + cosf(aa) * r0, sy + sinf(aa) * r0, sx + cosf(aa - dir * 0.25f) * r1, sy + sinf(aa - dir * 0.25f) * r1, 1.8f, INK, 0x77u + (uint32_t)m, 0.3f);
        }
    }
}

static void draw_lance(const Game *g)
{
    if (!g->alive) return;
    float sx = sxof(g, g->px), sy = g->py;
    if (g->lance_charge > 0) {                                     /* pencil spiral winding into the nose */
        float p = 1.0f - g->lance_charge / 0.3f, nx = sx + (float)g->face * 42.0f;
        Poly s; poly_clear(&s);
        for (int k = 0; k < 30; k++) { float a = (float)k * 0.55f + g->time * 14.0f, rr = (60.0f * (1.0f - p) + 6.0f) * (1.0f - (float)k / 34.0f); poly_add(&s, nx + cosf(a) * rr, sy + sinf(a) * rr); }
        pen_poly(&s, 0, 2.0f, C_BLUE, 0x5A1u, 0.4f);
        blob(nx, sy, 5.0f + 9.0f * p, 5.0f + 9.0f * p, C_SKY, INK, 1.8f, 0x5A2u, 0.15f);
    }
    if (g->lance_on) {
        float lt = g->lance_t / 0.5f;
        float wmul = lt > 0.85f ? (1.0f - lt) / 0.15f * 1.3f : sqrtf(lt);
        float w = LANCE_W * 0.5f * wmul;
        float dir = (float)g->lance_dir;
        float x0 = sx + dir * 40.0f, x1 = x0 + dir * LANCE_LEN;
        uint32_t seed = 0x1A2Cu + (uint32_t)(g->time * 30.0f);          /* jittery, crackly edges */
        Poly p; poly_clear(&p);
        poly_add(&p, x0, sy - w * 0.45f);
        for (int i = 1; i <= 10; i++) { float u = (float)i / 10; poly_add(&p, x0 + (x1 - x0) * u, sy - w * (0.7f + 0.3f * u) + noise1(seed, (float)i) * 4.0f); }
        for (int i = 10; i >= 1; i--) { float u = (float)i / 10; poly_add(&p, x0 + (x1 - x0) * u, sy + w * (0.7f + 0.3f * u) + noise1(seed + 9, (float)i) * 4.0f); }
        poly_add(&p, x0, sy + w * 0.45f);
        poly_add(&p, x0 - dir * w * 0.5f, sy);
        gfx_uv(sx, sy, 128, g->time * 2.0f, 0);
        crayon_fill(&p, C_SKY);
        pen_poly(&p, 1, 2.6f, INK, seed, 0.6f);
        gfx_tex(TEX_SOLID);
        gfx_quad4(x0, sy - w * 0.18f, WHITE, x1, sy - w * 0.3f, WHITE, x1, sy + w * 0.3f, WHITE, x0, sy + w * 0.18f, WHITE);
        for (int k = 0; k < 3; k++) {                                  /* zig-zag zaps along the beam */
            Poly z; poly_clear(&z);
            float yo = (float)(k - 1) * w * 0.5f;
            for (int i = 0; i <= 14; i++) poly_add(&z, x0 + (x1 - x0) * (float)i / 14.0f, sy + yo + ((i & 1) ? -1.0f : 1.0f) * w * 0.18f);
            pen_poly(&z, 0, 1.6f, C_BLUE, seed + (uint32_t)k, 0.5f);
        }
        if (lt > 0.4f) gfx_text_fancy("ZAP!", x0 + dir * 70.0f, sy - w - 34.0f, 30.0f, T_BLUE, INK, 2.2f, g->time, 3.0f);
    }
}

/* ------------------------------------------------------------------ */
/* enemies                                                              */
/* ------------------------------------------------------------------ */
static void draw_shard(const Game *g, const Enemy *e, Xf *t, uint32_t seed)
{
    static const float BODY[8][2] = {{18, 0}, {8, -9.5f}, {-6, -11}, {-14, -7}, {-15, 0}, {-14, 7}, {-6, 11}, {8, 9.5f}};
    static const float SP[6][3][2] = {
        {{-9, -10}, {-5, -18}, {-1, -10.5f}}, {{1, -10}, {5, -16}, {8, -9}}, {{-9, 10}, {-1, 10.5f}, {-5, 18}},
        {{1, 10}, {8, 9}, {5, 16}}, {{-14, -4}, {-22, 0}, {-14, 4}}, {{-12, -8}, {-17, -13}, {-9, -10}} };
    int angry = e->aux > 0.5f;
    if (angry) { t->x += sinf(e->t * 80.0f) * 1.8f; }
    for (int k = 0; k < 6; k++) shape(t, SP[k], 3, C_BLUE_D, INK, 1.8f, seed + 10u + (uint32_t)k);
    shape(t, BODY, 8, angry ? 0x6A9CF5FFu : C_BLUE, INK, 2.2f, seed);
    xline(t, -2, -10.4f, -4, 10.4f, 1.6f, INK, seed + 3);                        /* drill grooves */
    xline(t, -8, -10.8f, -9.5f, 10.8f, 1.6f, INK, seed + 4);
    eye(t, 7, -2.5f, 3.8f, sxof(g, g->px), g->py, seed + 5);
    if (angry) {
        xline(t, 3, -8, 11, -5, 2.0f, C_RED, seed + 6);
        float x, y; xpt(t, 0, -26, &x, &y);
        gfx_text_fancy("!", t->x, t->y - 40.0f, 24.0f, C_RED, PAPER, 1.6f, g->time, 0);
    }
    if (e->flash > 0.3f) blob(t->x, t->y, 14, 12, col_a(WHITE, e->flash * 0.8f), 0, 0, seed, 0.1f);
}

static void draw_zip(const Game *g, const Enemy *e, Xf *t, uint32_t seed)
{
    static const float BODY[7][2] = {{12, 0}, {7, -7}, {-8, -7}, {-11, -3}, {-11, 3}, {-8, 7}, {7, 7}};
    float sp = sqrtf(e->vx * e->vx + e->vy * e->vy) + 0.01f;
    for (int k = 0; k < 3; k++) {                                      /* speed lines */
        float oy = (float)(k - 1) * 5.0f;
        float x0, y0, x1, y1;
        xpt(t, -14, oy, &x0, &y0);
        x1 = x0 - e->vx / sp * (12.0f + 4.0f * (float)(k & 1)); y1 = y0 - e->vy / sp * (12.0f + 4.0f * (float)(k & 1));
        pen_line(x0, y0, x1, y1, 1.6f, INK, seed + (uint32_t)k, 0.2f);
    }
    shape(t, BODY, 7, C_MINT, INK, 2.0f, seed);
    xline(t, -2, -7, -2, 7, 1.5f, INK, seed + 4);
    xline(t, -6, -7, -6, 7, 1.5f, INK, seed + 5);
    eye(t, 5, -1.5f, 3.0f, sxof(g, g->px), g->py, seed + 6);
    if (e->flash > 0.3f) blob(t->x, t->y, 10, 8, col_a(WHITE, e->flash * 0.8f), 0, 0, seed, 0.1f);
}

static void draw_spore(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    float tele = e->cd < 0.5f ? 1.0f - e->cd / 0.5f : 0.0f;
    float R = 20.0f * sc * (1.0f + 0.12f * tele + 0.04f * sinf(g->time * 6.0f + e->phase));
    for (int k = 0; k < 8; k++) {                                      /* spikes */
        float a = e->ang + TAU * (float)k / 8.0f;
        Poly p; poly_clear(&p);
        poly_add(&p, sx + cosf(a - 0.28f) * R * 0.85f, sy + sinf(a - 0.28f) * R * 0.85f);
        poly_add(&p, sx + cosf(a) * R * 1.55f, sy + sinf(a) * R * 1.55f);
        poly_add(&p, sx + cosf(a + 0.28f) * R * 0.85f, sy + sinf(a + 0.28f) * R * 0.85f);
        gfx_uv(sx, sy, 128, 0, 0);
        crayon_fill(&p, C_YELLOW);
        pen_poly(&p, 1, 1.8f, INK, seed + (uint32_t)k, 0.4f);
    }
    blob(sx, sy, R, R, tele > 0 ? col_mix(0xFF5A6EFFu, 0xFF2A3AFFu, tele) : 0xFF5A6EFFu, INK, 2.4f, seed, 0.07f);
    Xf t = xf(sx, sy, 0, sc, sc);
    float lx = sxof(g, g->px), ly = g->py;
    eye(&t, -7, -4, 5.5f, lx, ly, seed + 20);
    eye(&t, 7, -4, 5.5f, lx, ly, seed + 21);
    xline(&t, -12, -12, -3, -9, 2.2f, INK, seed + 22);                    /* grumpy eyebrows */
    xline(&t, 12, -12, 3, -9, 2.2f, INK, seed + 23);
    float mo = 3.0f + tele * 5.0f;                                         /* toothy mouth, opens before it spits */
    static const float M[4][2] = {{-8, 5}, {8, 5}, {8, 9}, {-8, 9}};
    float Mx[4][2];
    for (int i = 0; i < 4; i++) { Mx[i][0] = M[i][0]; Mx[i][1] = M[i][1] + (i >= 2 ? mo - 4.0f : 0); }
    shape(&t, (const float (*)[2])Mx, 4, WHITE, INK, 1.8f, seed + 24);
    for (int i = 0; i < 4; i++) xline(&t, -6 + (float)i * 4.0f, 5, -4 + (float)i * 4.0f, 5 + mo * 0.5f, 1.2f, INK, seed + 30u + (uint32_t)i);
    if (e->flash > 0.3f) blob(sx, sy, R, R, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_serpent_head(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    float dir = cosf(e->ang) >= 0 ? 1.0f : -1.0f;
    Xf t = xf(sx, sy, 0, sc * dir, sc);
    float bob = sinf(g->time * 8.0f) * 2.0f;
    xline(&t, 4, -16, 10, -32 + bob, 2.0f, INK, seed + 1);                  /* antennae */
    xline(&t, -6, -17, -8, -33 - bob, 2.0f, INK, seed + 2);
    xblob(&t, 10, -33 + bob, 4.0f, C_PINK, INK, 1.6f, seed + 3);
    xblob(&t, -8, -34 - bob, 4.0f, C_PINK, INK, 1.6f, seed + 4);
    xblob(&t, 0, 0, 22.0f, C_GREEN, INK, 2.6f, seed);
    float lx = sxof(g, g->px), ly = g->py;
    eye(&t, -1, -6, 6.5f, lx, ly, seed + 5);
    eye(&t, 13, -6, 6.5f, lx, ly, seed + 6);
    Poly m; poly_clear(&m);                                                /* big grin with fangs */
    for (int k = 0; k <= 8; k++) { float a = 0.3f + 2.2f * (float)k / 8.0f; xadd(&m, &t, 6 + cosf(a) * 12.0f, 2 + sinf(a) * 9.0f); }
    pen_poly(&m, 0, 2.0f, INK, seed + 7, 0.3f);
    static const float F1[3][2] = {{8, 11}, {11, 11}, {9.5f, 16}}, F2[3][2] = {{14, 9.5f}, {17, 8.5f}, {16, 13.5f}};
    shape(&t, F1, 3, WHITE, INK, 1.2f, seed + 8);
    shape(&t, F2, 3, WHITE, INK, 1.2f, seed + 9);
    xblob(&t, -10, 6, 3.5f, col_a(C_PINK, 0.8f), 0, 0, seed + 10);         /* blushing cheek */
    if (e->flash > 0.3f) blob(sx, sy, 22 * sc, 22 * sc, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_segment(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    float r = e->r * sc;
    float wig = sinf(g->time * 10.0f + (float)e->idx) * 3.0f;
    pen_line(sx - 4, sy + r * 0.8f, sx - 7 + wig, sy + r + 6, 1.8f, INK, seed + 1, 0.2f);     /* little legs */
    pen_line(sx + 4, sy + r * 0.8f, sx + 7 - wig, sy + r + 6, 1.8f, INK, seed + 2, 0.2f);
    blob(sx, sy, r, r, (e->idx & 1) ? C_GREEN_D : C_GREEN, INK, 2.2f, seed, 0.06f);
    blob(sx - r * 0.3f, sy - r * 0.3f, r * 0.22f, r * 0.18f, col_a(C_YELLOW, 0.9f), 0, 0, seed + 3, 0.1f);
    blob(sx + r * 0.35f, sy + r * 0.1f, r * 0.15f, r * 0.15f, col_a(C_YELLOW, 0.9f), 0, 0, seed + 4, 0.1f);
    if (e->flash > 0.3f) blob(sx, sy, r, r, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_shield(const Game *g, const Enemy *e, Xf *t, uint32_t seed)
{
    static const float D[4][2] = {{0, -15}, {11, 0}, {0, 15}, {-11, 0}};
    t->c = 1; t->s = 0;
    shape(t, D, 4, 0x9FE8FFFFu, INK, 2.2f, seed);
    xline(t, 0, -15, 0, 15, 1.4f, INK, seed + 1);
    xline(t, -4, -5, -2, -9, 1.8f, WHITE, seed + 2);
    (void)g;
    if (e->flash > 0.3f) blob(t->x, t->y, 11, 14, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_boss(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    int phase2 = e->hp < e->maxhp * 0.5f;
    float R = 64.0f * sc, spin = e->t * 0.4f;
    /* dotted tethers to the shields */
    for (int k = 0; k < MAX_EN; k++) {
        const Enemy *o = &g->en[k];
        if (!o->alive || o->type != EN_SHIELD || o->parent != (int)(e - g->en) || o->warp) continue;
        float ox = sxof(g, o->x), oy = o->y;
        for (int d = 1; d < 8; d += 2) {
            float a = (float)d / 9.0f, b = (float)(d + 1) / 9.0f;
            pen_line(sx + (ox - sx) * a, sy + (oy - sy) * a, sx + (ox - sx) * b, sy + (oy - sy) * b, 2.0f, col_a(INK, 0.7f), seed + (uint32_t)(k * 9 + d), 0.2f);
        }
    }
    for (int k = 0; k < 6; k++) {                                          /* spikes */
        float a = spin + TAU * (float)k / 6.0f;
        Poly p; poly_clear(&p);
        poly_add(&p, sx + cosf(a - 0.2f) * R * 0.92f, sy + sinf(a - 0.2f) * R * 0.92f);
        poly_add(&p, sx + cosf(a) * (R + 30.0f * sc), sy + sinf(a) * (R + 30.0f * sc));
        poly_add(&p, sx + cosf(a + 0.2f) * R * 0.92f, sy + sinf(a + 0.2f) * R * 0.92f);
        gfx_uv(sx, sy, 128, 0, 0);
        crayon_fill(&p, (k & 1) ? C_PINK : C_SKY);
        pen_poly(&p, 1, 2.2f, INK, seed + 40u + (uint32_t)k, 0.5f);
    }
    Poly hx; poly_clear(&hx);
    for (int k = 0; k < 6; k++) {
        float a = spin + TAU * (float)k / 6.0f + TAU / 12.0f;
        poly_add(&hx, sx + cosf(a) * R, sy + sinf(a) * R);
    }
    gfx_uv(sx, sy, 128, 0, 0);
    crayon_fill(&hx, phase2 ? 0xFF7A3AFFu : C_YELLOW);
    pen_poly(&hx, 1, 3.2f, INK, seed, 1.2f);
    /* the angry face stays upright */
    Xf t = xf(sx, sy, 0, sc, sc);
    float lx = sxof(g, g->px), ly = g->py;
    int squint = e->flash > 0.3f;
    if (squint) {
        xline(&t, -32, -10, -14, -6, 3.0f, INK, seed + 1);
        xline(&t, 32, -10, 14, -6, 3.0f, INK, seed + 2);
    } else {
        eye(&t, -22, -8, 12.0f, lx, ly, seed + 3);
        eye(&t, 22, -8, 12.0f, lx, ly, seed + 4);
    }
    xline(&t, -38, -28, -8, -18, 4.0f, INK, seed + 5);                         /* big angry brows */
    xline(&t, 38, -28, 8, -18, 4.0f, INK, seed + 6);
    static const float MOUTH[6][2] = {{-26, 14}, {26, 14}, {22, 30}, {8, 34}, {-8, 34}, {-22, 30}};
    shape(&t, MOUTH, 6, INK, INK, 2.0f, seed + 7);
    for (int k = 0; k < 5; k++) {                                             /* zig-zag teeth */
        float x0 = -24.0f + (float)k * 10.0f;
        float T1[3][2] = {{x0, 14}, {x0 + 10, 14}, {x0 + 5, 22}};
        shape(&t, (const float (*)[2])T1, 3, WHITE, INK, 1.2f, seed + 10u + (uint32_t)k);
    }
    if (phase2) {                                                             /* steam puffs from the ears */
        for (int k = 0; k < 2; k++) {
            float side = k ? 1.0f : -1.0f, ph = posmod(g->time * 1.4f + (float)k * 0.5f, 1.0f);
            blob(sx + side * (R + 8.0f + ph * 18.0f), sy - 30.0f - ph * 30.0f, 6.0f + ph * 8.0f, 5.0f + ph * 7.0f, col_a(WHITE, 1.0f - ph), col_a(C_GREY, 1.0f - ph), 1.6f, seed + 60u + (uint32_t)k, 0.15f);
        }
    }
}

static void starburst_small(float x, float y, float r, float rot, uint32_t seed)
{
    Poly p; poly_clear(&p);
    for (int k = 0; k < 10; k++) {
        float a = rot + TAU * (float)k / 10.0f, rr = (k & 1) ? r * 0.45f : r;
        poly_add(&p, x + cosf(a) * rr, y + sinf(a) * rr);
    }
    gfx_uv(x, y, 64, 0, 0);
    crayon_fill(&p, C_YELLOW);
    pen_poly(&p, 1, 1.2f, C_ORANGE, seed, 0.2f);
}

static void draw_ufo(const Game *g, const Enemy *e, Xf *t, uint32_t seed)
{
    int angry = e->cd < 0.5f || e->idx > 0;
    xblob(t, 0, -9, 7.0f, C_GREEN, INK, 1.6f, seed + 1);                      /* the little alien */
    xline(t, -2, -15, -5, -21, 1.4f, INK, seed + 2);
    xline(t, 2, -15, 5, -21, 1.4f, INK, seed + 3);
    xblob(t, -5, -21, 1.8f, C_YELLOW, INK, 1.0f, seed + 4);
    xblob(t, 5, -21, 1.8f, C_YELLOW, INK, 1.0f, seed + 5);
    eye(t, -2.5f, -9.5f, 2.6f, sxof(g, g->px), g->py, seed + 6);
    eye(t, 2.5f, -9.5f, 2.6f, sxof(g, g->px), g->py, seed + 7);
    Poly dome; poly_clear(&dome);                                            /* glass dome (just a pen line) */
    for (int k = 0; k <= 10; k++) { float a = PI_F + PI_F * (float)k / 10.0f; xadd(&dome, t, cosf(a) * 12.0f, -3.0f + sinf(a) * 14.0f); }
    pen_poly(&dome, 0, 1.6f, INK, seed + 8, 0.4f);
    static const float SAUCER[8][2] = {{-24, 0}, {-16, -5}, {0, -6}, {16, -5}, {24, 0}, {16, 6}, {0, 8}, {-16, 6}};
    shape(t, SAUCER, 8, 0xB8BCCCFFu, INK, 2.2f, seed);
    xline(t, -22, 1, 22, 1, 1.4f, INK, seed + 9);
    for (int k = 0; k < 3; k++) {
        int on = angry ? ((int)(g->time * 12.0f) & 1) : (((int)(g->time * 4.0f) + k) % 3 == 0);
        xblob(t, -12.0f + 12.0f * (float)k, 4.0f, 2.4f, angry ? (on ? C_RED : 0xFFB0B0FFu) : (on ? C_YELLOW : 0xFFF0B0FFu), INK, 1.0f, seed + 10u + (uint32_t)k);
    }
    if (e->flash > 0.3f) blob(t->x, t->y, 22, 10, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_bomb(const Game *g, const Enemy *e, Xf *t, uint32_t seed)
{
    int warn = e->fuse < 1.6f && ((int)(g->time * (e->fuse < 0.9f ? 16.0f : 8.0f)) & 1);
    float puff = 1.0f + (e->fuse < 0.9f ? 0.12f * (1.0f - e->fuse / 0.9f) : 0.0f);
    t->sx *= puff; t->sy *= puff;
    Poly f; poly_clear(&f);                                                  /* the fuse */
    xadd(&f, t, 5, -13); xadd(&f, t, 9, -19); xadd(&f, t, 15, -21); xadd(&f, t, 18, -26);
    pen_poly(&f, 0, 2.2f, 0x8A6A40FFu, seed + 1, 0.3f);
    float fx, fy; xpt(t, 18, -26, &fx, &fy);
    starburst_small(fx, fy, 5.0f + 2.0f * sinf(g->time * 40.0f), g->time * 9.0f, seed + 2);
    xblob(t, 0, 0, 15.0f, warn ? 0xD84A4AFFu : 0x3E4460FFu, INK, 2.4f, seed);
    static const float CAP[4][2] = {{1, -14}, {9, -16}, {11, -11}, {4, -10}};
    shape(t, CAP, 4, 0x8A90A8FFu, INK, 1.6f, seed + 3);
    xline(t, -9, -6, -6, -10, 2.0f, WHITE, seed + 4);                        /* shine */
    Xf u = *t; u.c = 1; u.s = 0;
    eye(&u, -5, 2, 3.4f, sxof(g, g->px), g->py, seed + 5);
    eye(&u, 5, 2, 3.4f, sxof(g, g->px), g->py, seed + 6);
    xline(&u, -9, -3, -2, -1, 1.8f, WHITE, seed + 7);
    xline(&u, 9, -3, 2, -1, 1.8f, WHITE, seed + 8);
    if (e->flash > 0.3f) blob(t->x, t->y, 15, 15, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

static void draw_blob(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    int small = e->type == EN_BLOBLET;
    float hop = small ? 2.6f : 1.5f, sq = sinf(e->t * hop * TAU);
    float r = e->r * sc;
    float rx = r * (1.0f + 0.14f * sq), ry = r * (1.0f - 0.14f * sq);
    Poly p;
    poly_blob(&p, sx, sy, rx, ry, small ? 12 : 16, 0.1f, seed);
    gfx_uv(sx, sy, 128, 0, 0);
    crayon_fill(&p, small ? 0x9CE85AFFu : C_GREEN);
    pen_poly(&p, 1, small ? 1.8f : 2.4f, INK, seed, 0.6f);
    Xf t = xf(sx, sy, 0, sc, sc);
    float lx = sxof(g, g->px), ly = g->py;
    if (small) {
        eye(&t, -3.5f, -2, 3.0f, lx, ly, seed + 1);
        eye(&t, 3.5f, -2, 3.0f, lx, ly, seed + 2);
    } else {
        eye(&t, 0, -4, 8.0f, lx, ly, seed + 1);                              /* one big googly eye */
        Poly m; poly_clear(&m);
        for (int k = 0; k <= 6; k++) { float a = 0.5f + 2.1f * (float)k / 6.0f; xadd(&m, &t, cosf(a) * 7.0f, 6.0f + sinf(a) * 4.0f); }
        pen_poly(&m, 0, 1.8f, INK, seed + 3, 0.2f);
        xblob(&t, -11, 3, 2.5f, col_a(C_PINK, 0.8f), 0, 0, seed + 4);
        xblob(&t, 11, 3, 2.5f, col_a(C_PINK, 0.8f), 0, 0, seed + 5);
    }
    if (e->flash > 0.3f) blob(sx, sy, rx, ry, col_a(WHITE, e->flash * 0.7f), 0, 0, seed, 0.1f);
}

/* the Big Head: your pencil drawing coloured in, with crazy crayon tentacles wiggling underneath */
static void draw_boss2(const Game *g, const Enemy *e, float sx, float sy, float sc, uint32_t seed)
{
    int phase2 = e->hp < e->maxhp * 0.5f;
    uint32_t tc = phase2 ? 0xC64AC8FFu : 0xA066F0FFu;
    float tx[TENT_SEGS + 1], ty[TENT_SEGS + 1];
    for (int k = 0; k < TENT_N; k++) {
        tentacle_points(e, k, tx, ty);
        float px[TENT_SEGS + 1], py[TENT_SEGS + 1];
        for (int i = 0; i <= TENT_SEGS; i++) { px[i] = sx + (sxof(g, tx[i]) - sx) * sc; py[i] = sy + (ty[i] - sy) * sc; }
        Poly p; poly_clear(&p);
        float nx[TENT_SEGS + 1], ny[TENT_SEGS + 1], w[TENT_SEGS + 1];
        for (int i = 0; i <= TENT_SEGS; i++) {
            int a = i > 0 ? i - 1 : 0, b = i < TENT_SEGS ? i + 1 : TENT_SEGS;
            float dx = px[b] - px[a], dy = py[b] - py[a], l = sqrtf(dx * dx + dy * dy) + 0.001f;
            nx[i] = -dy / l; ny[i] = dx / l;
            w[i] = (15.0f * (1.0f - (float)i / TENT_SEGS) + 2.5f) * sc;
        }
        for (int i = 0; i < TENT_SEGS; i++) poly_add(&p, px[i] + nx[i] * w[i], py[i] + ny[i] * w[i]);
        poly_add(&p, px[TENT_SEGS], py[TENT_SEGS]);
        for (int i = TENT_SEGS - 1; i >= 0; i--) poly_add(&p, px[i] - nx[i] * w[i], py[i] - ny[i] * w[i]);
        gfx_uv(sx, sy, 128, 0.17f * (float)k, 0);
        crayon_fill(&p, tc);
        pen_poly(&p, 1, 2.2f, INK, seed + 50u + (uint32_t)k, 0.5f);
        for (int i = 2; i < TENT_SEGS - 1; i += 2)                          /* suckers */
            blob(px[i] + nx[i] * w[i] * 0.45f, py[i] + ny[i] * w[i] * 0.45f, w[i] * 0.38f, w[i] * 0.38f, C_PINK, INK, 1.2f, seed + 60u + (uint32_t)(k * 9 + i), 0.05f);
    }
    /* the head: the drawing on a bendy mesh so her jaw can drop open when she cackles */
    float pic = BOSS2_PIC * sc, h = pic * 0.5f;
    int laugh = boss2_laughing(e);
    float jit = e->flash > 0.3f ? 2.0f : 0.0f;
    if (laugh) jit += phase2 ? 2.5f : 1.2f;                                  /* shaking with laughter */
    float bob = sinf(g->time * 2.6f) * 3.0f + (laugh ? fabsf(sinf(g->time * 13.0f)) * -4.0f : 0.0f);
    float rot = sinf(g->time * 1.4f) * 0.06f + (laugh ? sinf(g->time * 9.0f) * (phase2 ? 0.1f : 0.06f) : 0.0f);
    float sq = 1.0f + 0.03f * sinf(g->time * 5.0f) + (laugh ? 0.04f * sinf(g->time * 13.0f) : 0.0f);
    float hxp = sx + jit * sinf(g->time * 70.0f), hyp = sy + bob + jit * cosf(g->time * 53.0f);
    Xf t = xf(hxp, hyp, rot, sq * sc, (2.0f - sq) * sc);
    float open = laugh ? 0.45f + 0.55f * fabsf(sinf(g->time * 13.0f)) : 0.12f + 0.08f * sinf(g->time * 3.0f);
    const int N = 16;
    float MX[17 * 17], MY[17 * 17];
    for (int j = 0; j <= N; j++)
        for (int i2 = 0; i2 <= N; i2++) {
            float u = (float)i2 / N, v = (float)j / N;
            float lip = B2_LIP_L_V + (u - B2_LIP_L_U) / (B2_LIP_R_U - B2_LIP_L_U) * (B2_LIP_R_V - B2_LIP_L_V);
            float fy = clampf((v - lip) / 0.07f, 0, 1);
            float gl = clampf((u - (B2_LIP_L_U - 0.02f)) / 0.14f, 0, 1), gr = clampf(((B2_LIP_R_U + 0.02f) - u) / 0.14f, 0, 1);
            float gx = gl * gl * (3 - 2 * gl) * gr * gr * (3 - 2 * gr);
            float lx = (u - 0.5f) * BOSS2_PIC, ly = (v - 0.5f) * BOSS2_PIC + open * 0.1f * BOSS2_PIC * fy * gx;
            xpt(&t, lx, ly, &MX[j * (N + 1) + i2], &MY[j * (N + 1) + i2]);
        }
    uint32_t tint = phase2 ? 0xFFD0C8FFu : WHITE;
    if (e->flash > 0.3f) tint = 0xFFC0C0FFu;
    gfx_image_mesh(TEX_BOSS2, N, N, MX, MY, tint);
    (void)pic; (void)h;
    /* googly pupils that follow you around (tiny and twitchy once she's mad) */
    float lookx = sxof(g, g->px), looky = g->py;
    const float EU[2] = {B2_EYE_L_CU, B2_EYE_R_CU}, EV[2] = {B2_EYE_L_CV, B2_EYE_R_CV};
    const float ERX[2] = {B2_EYE_L_RX, B2_EYE_R_RX}, ERY[2] = {B2_EYE_L_RY, B2_EYE_R_RY};
    for (int k = 0; k < 2; k++) {
        float ex, ey;
        xpt(&t, B2X(EU[k]), B2Y(EV[k]), &ex, &ey);
        float pr = (phase2 ? 0.016f : 0.022f) * BOSS2_PIC * sc * (1.0f + 0.15f * sinf(g->time * 7.0f + (float)k));
        float mx = fmaxf(ERX[k] * BOSS2_PIC * sc - pr, 2.0f), my = fmaxf(ERY[k] * BOSS2_PIC * sc - pr, 2.5f);
        float dx = lookx - ex, dy = looky - ey, dd = sqrtf(dx * dx + dy * dy) + 0.01f;
        float ox = dx / dd * mx, oy = dy / dd * my;
        if (laugh && phase2) { float a = g->time * 14.0f + (float)k * 2.0f; ox = cosf(a) * mx; oy = sinf(a) * my; }   /* eyes rolling */
        if (phase2) { ox += sinf(g->time * 61.0f + (float)k) * 1.2f; oy += cosf(g->time * 47.0f) * 1.0f; }
        Poly p;
        poly_blob(&p, ex + ox, ey + oy, pr, pr, 10, 0.05f, seed + 90u + (uint32_t)k);
        gfx_uv(ex, ey, 64, 0, 0);
        crayon_fill(&p, INK); crayon_fill(&p, INK);
        pen_poly(&p, 1, 1.2f, INK, seed + 92u + (uint32_t)k, 0.2f);
        blob(ex + ox - pr * 0.35f, ey + oy - pr * 0.35f, pr * 0.3f, pr * 0.3f, WHITE, 0, 0, seed + 94u + (uint32_t)k, 0);
    }
    /* cackling */
    if (laugh) {
        static const char *HA[3] = {"HA HA!", "HEE HEE!", "HA HA HA!"};
        int w = (int)(e->t * 1.3f) % 3;
        float side = (w & 1) ? 1.0f : -1.0f;
        gfx_text_fancy(HA[w], sx + side * (h + 62.0f), fmaxf(sy - h * 0.35f, 96.0f), 30.0f, phase2 ? C_RED : 0xA040C8FFu, 0, 0, g->time, 4.0f);
    }
    if (phase2) {                                                            /* steaming mad */
        for (int k = 0; k < 2; k++) {
            float side = k ? 1.0f : -1.0f, ph = posmod(g->time * 1.4f + (float)k * 0.5f, 1.0f);
            blob(hxp + side * (h * 0.55f + ph * 12.0f), hyp - h * 0.9f - ph * 34.0f, 7.0f + ph * 9.0f, 6.0f + ph * 8.0f,
                 col_a(WHITE, 1.0f - ph), col_a(C_GREY, 1.0f - ph), 1.6f, seed + 70u + (uint32_t)k, 0.15f);
        }
        float ax = hxp + h * 0.35f, ay = hyp - h * 0.55f, s2 = 9.0f + sinf(g->time * 10.0f) * 1.5f;   /* anger mark */
        for (int k = 0; k < 4; k++) {
            float a = PI_F * 0.25f + PI_F * 0.5f * (float)k, ca = cosf(a), sa = sinf(a);
            pen_line(ax + ca * s2 * 0.35f, ay + sa * s2 * 0.35f, ax + ca * s2, ay + sa * s2, 3.0f, C_RED, seed + 80u + (uint32_t)k, 0.2f);
        }
    }
    (void)seed;
}

static void draw_warp(const Game *g, const Enemy *e, float sx, float sy)
{
    if (e->warp_t < 0) return;
    float p = e->warp_t / 0.7f;
    float big = IS_BOSS(e->type) ? 3.5f : (e->type == EN_SERPENT || e->type == EN_SPORE || e->type == EN_UFO || e->type == EN_BLOB ? 1.5f : 1.0f);
    for (int k = 0; k < 2; k++) {                                              /* orange diamonds closing in */
        float q = clampf(p * 1.25f - 0.2f * (float)k, 0, 1);
        if (q <= 0 || q >= 1) continue;
        float r = ((1.0f - q) * 46.0f + 12.0f) * big;
        Poly d; poly_clear(&d);
        poly_add(&d, sx, sy - r); poly_add(&d, sx + r, sy); poly_add(&d, sx, sy + r); poly_add(&d, sx - r, sy);
        pen_poly(&d, 1, 3.0f, 0xF06A3AFFu, (uint32_t)(e - g->en) * 7u + (uint32_t)k, 0.8f);
    }
}

static void draw_enemies(const Game *g)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = MAX_EN - 1; i >= 0; i--) {
            const Enemy *e = &g->en[i];
            if (!e->alive) continue;
            if ((pass == 0) != (e->type == EN_SEG)) continue;
            float sx = sxof(g, e->x), sy = e->y;
            if (!on_screen(sx, sy, e->type == EN_BOSS2 ? 320.0f : (e->type == EN_BOSS ? 160.0f : 60.0f))) continue;
            if (e->warp) {
                draw_warp(g, e, sx, sy);
                if (e->warp_t / 0.7f < 0.6f) continue;
            }
            float sc = 1.0f;
            if (e->warp) {                                                      /* pops in with a boing */
                float q = (e->warp_t / 0.7f - 0.6f) / 0.4f;
                sc = q < 0.7f ? q / 0.7f * 1.2f : 1.2f - (q - 0.7f) / 0.3f * 0.2f;
            }
            uint32_t seed = (uint32_t)i * 977u + 13u;
            Xf t = xf(sx, sy, e->ang, sc, sc);
            switch (e->type) {
            case EN_SHARD:   draw_shard(g, e, &t, seed); break;
            case EN_ZIP:     draw_zip(g, e, &t, seed); break;
            case EN_SPORE:   draw_spore(g, e, sx, sy, sc, seed); break;
            case EN_SERPENT: draw_serpent_head(g, e, sx, sy, sc, seed); break;
            case EN_SEG:     draw_segment(g, e, sx, sy, sc, seed); break;
            case EN_BOSS:    draw_boss(g, e, sx, sy, sc, seed); break;
            case EN_SHIELD:  draw_shield(g, e, &t, seed); break;
            case EN_UFO:     draw_ufo(g, e, &t, seed); break;
            case EN_BOMB:    draw_bomb(g, e, &t, seed); break;
            case EN_BLOB: case EN_BLOBLET: draw_blob(g, e, sx, sy, sc, seed); break;
            case EN_BOSS2:   draw_boss2(g, e, sx, sy, sc, seed); break;
            default: break;
            }
        }
    }
}

static void draw_offscreen_markers(const Game *g)
{
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->type == EN_SEG || e->type == EN_SHIELD) continue;
        if (e->warp && e->warp_t < 0) continue;
        float sx = sxof(g, e->x);
        if (sx > -10.0f && sx < VW + 10.0f) continue;
        float d = fabsf(sx - VW * 0.5f) - VW * 0.5f;
        if (d > 900.0f) continue;
        float y = clampf(e->y, 80.0f, 400.0f);
        int right = sx > VW * 0.5f;
        float x = right ? VW - 20.0f : 20.0f, dir = right ? 1.0f : -1.0f;
        uint32_t seed = (uint32_t)i * 31u + (uint32_t)boil;
        if (e->warp) {
            if ((int)(g->time * 6.0f) & 1) continue;
            gfx_text_fancy("!", x - dir * 4.0f, y - 16.0f, 30.0f, C_RED, PAPER, 2.0f, 0, 0);
        } else {
            Poly a; poly_clear(&a);
            poly_add(&a, x + dir * 10.0f, y); poly_add(&a, x - dir * 6.0f, y - 10.0f); poly_add(&a, x - dir * 6.0f, y + 10.0f);
            gfx_uv(x, y, 64, 0, 0);
            crayon_fill(&a, IS_BOSS(e->type) ? C_YELLOW : C_RED);
            pen_poly(&a, 1, 1.8f, INK, seed, 0.4f);
        }
    }
}

/* ------------------------------------------------------------------ */
/* shots, pickups, effects                                              */
/* ------------------------------------------------------------------ */
static void draw_projectiles(const Game *g)
{
    static const float PILL[8][2] = {{16, 0}, {12, -5.5f}, {-12, -5.5f}, {-17, -3}, {-17, 3}, {-12, 5.5f}, {12, 5.5f}, {16, 0}};
    for (int i = 0; i < MAX_PB; i++) {
        const Bolt *b = &g->pb[i];
        if (!b->alive) continue;
        float sx = sxof(g, b->x);
        if (!on_screen(sx, b->y, 40)) continue;
        float a = atan2f(b->vy, b->vx);
        Xf t = xf(sx, b->y, a, 1, 1);
        xline(&t, -22, -3, -34, -3, 1.6f, C_ORANGE, (uint32_t)i);                     /* motion dashes */
        xline(&t, -24, 3, -40, 3, 1.6f, C_ORANGE, (uint32_t)i + 1u);
        shape(&t, PILL, 7, C_ORANGE, INK, 1.8f, 0x900u + (uint32_t)i);
        xline(&t, -9, -2, 10, -2, 2.4f, C_YELLOW, (uint32_t)i + 2u);
    }
    static const float BODY[5][2] = {{9, 0}, {4, -4}, {-8, -4}, {-8, 4}, {4, 4}};
    static const float FT[3][2] = {{-4, -4}, {-11, -9}, {-9, -3}}, FB[3][2] = {{-4, 4}, {-9, 3}, {-11, 9}};
    for (int i = 0; i < MAX_MS; i++) {
        const Missile *m = &g->ms[i];
        if (!m->alive) continue;
        float sx = sxof(g, m->x);
        if (!on_screen(sx, m->y, 40)) continue;
        Xf t = xf(sx, m->y, atan2f(m->vy, m->vx), 1.25f, 1.25f);
        uint32_t seed = 0xA00u + (uint32_t)i * 11u;
        shape(&t, FT, 3, C_YELLOW, INK, 1.4f, seed);
        shape(&t, FB, 3, C_YELLOW, INK, 1.4f, seed + 1);
        shape(&t, BODY, 5, C_RED, INK, 1.8f, seed + 2);
    }
}

static void draw_ebullets(const Game *g)
{
    for (int i = 0; i < MAX_EB; i++) {
        const EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        float sx = sxof(g, b->x), sy = b->y;
        if (!on_screen(sx, sy, 30)) continue;
        float r = b->big ? 9.5f : 7.5f;
        float sp = sqrtf(b->vx * b->vx + b->vy * b->vy) + 0.01f, ux = b->vx / sp, uy = b->vy / sp;
        uint32_t seed = 0xE00u + (uint32_t)i * 7u;
        for (int k = -1; k <= 1; k += 2)                                   /* whoosh lines */
            pen_line(sx - ux * (r + 2) - uy * k * r * 0.5f, sy - uy * (r + 2) + ux * k * r * 0.5f,
                     sx - ux * (r + 12) - uy * k * r * 0.5f, sy - uy * (r + 12) + ux * k * r * 0.5f, 1.4f, C_RED, seed + (uint32_t)(k + 1), 0.2f);
        blob(sx, sy, r, r, 0xFF4A55FFu, INK, 1.8f, seed, 0.06f);
        blob(sx - r * 0.3f, sy - r * 0.35f, r * 0.28f, r * 0.22f, WHITE, 0, 0, seed + 3, 0.1f);
    }
}

static void heart(float cx, float cy, float s, uint32_t fill, uint32_t seed)
{
    Poly p; poly_clear(&p);
    for (int k = 0; k < 20; k++) {
        float a = TAU * (float)k / 20.0f;
        float x = 16.0f * powf(sinf(a), 3.0f);
        float y = -(13.0f * cosf(a) - 5.0f * cosf(2 * a) - 2.0f * cosf(3 * a) - cosf(4 * a));
        poly_add(&p, cx + x * s / 16.0f, cy + y * s / 16.0f);
    }
    gfx_uv(cx, cy, 64, (float)(seed & 15) / 16.0f, 0);
    crayon_fill(&p, fill);
    pen_poly(&p, 1, 1.8f, INK, seed, 0.5f);
}

static void draw_crystals(const Game *g)
{
    for (int i = 0; i < MAX_CRY; i++) {
        const Crystal *c = &g->cr[i];
        if (!c->alive) continue;
        float sx = sxof(g, c->x);
        if (!on_screen(sx, c->y, 30)) continue;
        if (c->life < 2.0f && ((int)(c->life * 10.0f) & 1)) continue;
        uint32_t seed = 0xC00u + (uint32_t)i * 5u;
        if (c->kind == 0) {
            float w = 7.5f * fabsf(cosf(c->spin)) + 2.0f, h = 9.5f;
            Poly p; poly_clear(&p);
            poly_add(&p, sx, c->y - h); poly_add(&p, sx + w, c->y); poly_add(&p, sx, c->y + h); poly_add(&p, sx - w, c->y);
            gfx_uv(sx, c->y, 64, 0, 0);
            crayon_fill(&p, C_YELLOW);
            pen_poly(&p, 1, 1.6f, INK, seed, 0.4f);
        } else {
            float bob = sinf(g->time * 4.0f) * 2.5f, pulse = 1.0f + 0.08f * sinf(g->time * 9.0f);
            heart(sx, c->y + bob, 15.0f * pulse, C_RED, seed);
            gfx_text_fancy("+", sx + 14.0f, c->y + bob - 28.0f, 22.0f, T_GREEN, INK, 1.4f, 0, 0);
        }
    }
}

/* kid-style explosion: a jagged starburst that pops out and shrinks away */
static void starburst(float x, float y, float r, float rot, uint32_t outer, uint32_t inner, uint32_t seed)
{
    Poly p; poly_clear(&p);
    for (int k = 0; k < 18; k++) {
        float a = rot + TAU * (float)k / 18.0f, rr = (k & 1) ? r * (0.5f + 0.1f * noise1(seed, (float)k)) : r * (1.0f + 0.12f * noise1(seed + 3, (float)k));
        poly_add(&p, x + cosf(a) * rr, y + sinf(a) * rr);
    }
    gfx_uv(x, y, 128, 0, 0);
    crayon_fill(&p, outer);
    pen_poly(&p, 1, 2.2f, INK, seed, 0.4f);
    if (inner & 255) {
        Poly q; poly_clear(&q);
        for (int k = 0; k < 12; k++) {
            float a = rot + 0.3f + TAU * (float)k / 12.0f, rr = (k & 1) ? r * 0.25f : r * 0.52f;
            poly_add(&q, x + cosf(a) * rr, y + sinf(a) * rr);
        }
        crayon_fill(&q, inner);
    }
}

static void draw_particles(const Game *g)
{
    int bursts = 0, debris = 0;
    for (int i = 0; i < MAX_PT; i++) {
        const Part *p = &g->pt[i];
        if (!p->alive) continue;
        float sx = sxof(g, p->x);
        if (!on_screen(sx, p->y, p->size + 40.0f)) continue;
        float t = p->life / p->max_life;                    /* 1 -> 0 */
        uint32_t seed = (uint32_t)i * 37u + 3u;
        switch (p->kind) {
        case P_SPARK: {
            float l = 0.03f, w = 1.4f + p->size * 0.35f * t;
            uint32_t c = col_mix(p->col, INK, 0.25f);
            if ((p->col & 0xFFFFFF00u) == 0xFFFFFF00u) c = INK;
            gfx_tex(TEX_SOLID);
            gfx_line(sx, p->y, sx - p->vx * l, p->y - p->vy * l, w, c, c);
            break;
        }
        case P_GLOW: {
            if ((p->col & 255) < 140 || bursts >= 36) break;   /* soft light isn't drawn - no lights in a drawing */
            bursts++;
            float age = 1.0f - t, pop = age < 0.25f ? 0.6f + age / 0.25f * 0.6f : 1.2f - (age - 0.25f) / 0.75f * 0.9f;
            float r = p->size * 0.5f * pop;
            if (r < 3.0f) break;
            starburst(sx, p->y, r, hf(seed) * TAU, r > 18.0f ? WHITE : C_YELLOW, r > 18.0f ? C_YELLOW : 0, seed);
            break;
        }
        case P_DEBRIS: {
            Xf x = xf(sx, p->y, p->rot, p->size, p->size);
            static const float D[3][2] = {{1.0f, 0}, {-0.6f, -0.8f}, {-0.4f, 0.9f}};
            if ((t < 0.25f && ((int)(t * 40.0f) & 1)) || debris >= 70) break;
            debris++;
            shape(&x, D, 3, p->col | 255u, INK, 1.4f, seed);
            break;
        }
        case P_SMOKE:                                   /* every 4th puff, a bit bigger: a cartoon smoke trail */
            if (i & 3) break;
            blob(sx, p->y, p->size * 1.3f, p->size * 1.15f, col_a(WHITE, t), col_a(C_GREY, t), 1.4f, seed, 0.15f);
            break;
        case P_CONF: break;                             /* confetti isn't hand-drawn: not shown */
        case P_STAR: {
            Xf x = xf(sx, p->y, p->rot, p->size, p->size * fabsf(cosf(p->rot * 1.7f)) + 0.5f);
            static const float Q[4][2] = {{-1, -0.6f}, {1, -0.6f}, {1, 0.6f}, {-1, 0.6f}};
            if (t < 0.3f && ((int)(t * 40.0f) & 1)) break;
            shape(&x, Q, 4, p->col | 255u, 0, 0, seed);
            break;
        }
        default: break;
        }
    }
}

/* shockwaves become comic-book impact lines: short dashes bursting outward */
static void draw_rings(const Game *g)
{
    for (int i = 0; i < MAX_RING; i++) {
        const Ring *r = &g->rg[i];
        if (!r->alive) continue;
        float t = r->life / r->max_life;
        float sx = sxof(g, r->x);
        if (!on_screen(sx, r->y, r->r + r->thick)) continue;
        if ((r->col & 0xFFFFFF00u) == 0xFFFFFF00u) continue;      /* plain white flashes: skip */
        if (r->maxr > 220.0f && r->maxr < 900.0f && r->r > 180.0f) continue;
        uint32_t c = col_a(col_mix(r->col | 255u, INK, 0.25f), clampf(t * 2.0f, 0, 1));
        int n = r->maxr > 150.0f ? 14 : 9;
        float len = 6.0f + r->thick * 0.9f * t, rot = hf((uint32_t)i * 7u) * TAU;
        gfx_tex(TEX_SOLID);
        for (int k = 0; k < n; k++) {
            float a = rot + TAU * (float)k / (float)n, ca = cosf(a), sa = sinf(a);
            float r0 = r->r - len * 0.5f, r1 = r->r + len * 0.5f;
            gfx_line(sx + ca * r0, r->y + sa * r0, sx + ca * r1, r->y + sa * r1, 1.4f + r->thick * 0.18f * t, c, c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HUD                                                                  */
/* ------------------------------------------------------------------ */
static void ink_text(const char *s, float x, float y, float size, uint32_t c, int align)
{
    float w = gfx_text_width(s, size);
    float lx = align == 0 ? x : (align == 1 ? x - w * 0.5f : x - w);
    gfx_text(s, lx, y, size, c);
}
/* solid-colour handwriting (optionally bouncy) */
static void rim_text(const char *s, float x, float y, float size, uint32_t c, int align, float t, float bounce)
{
    float w = gfx_text_width(s, size);
    float cx = align == 0 ? x + w * 0.5f : (align == 1 ? x : x - w * 0.5f);
    gfx_text_fancy(s, cx, y, size, c, 0, 0, t, bounce);
}

static void box(float x, float y, float w, float h, uint32_t fill, uint32_t ink, float pw, uint32_t seed)
{
    Poly p; poly_clear(&p);
    poly_add(&p, x, y); poly_add(&p, x + w, y); poly_add(&p, x + w, y + h); poly_add(&p, x, y + h);
    gfx_uv(x, y, 128, 0, 0);
    if (fill & 255) crayon_fill(&p, fill);
    if (ink & 255) pen_poly(&p, 1, pw, ink, seed, 0.5f);
}

static void bar(float x, float y, float w, float h, float frac, uint32_t col, uint32_t seed)
{
    gfx_tex(TEX_SOLID);
    gfx_rect(x, y, w, h, col_a(PAPER, 0.95f));
    if (frac > 0.01f) {
        Poly f; poly_clear(&f);
        float fw = w * clampf(frac, 0, 1);
        poly_add(&f, x + 1, y + 1); poly_add(&f, x + fw - 1, y + 1); poly_add(&f, x + fw - 1, y + h - 1); poly_add(&f, x + 1, y + h - 1);
        gfx_uv(x, y, 64, 0, 0);
        crayon_fill(&f, col);
        crayon_fill(&f, col);
    }
    box(x, y, w, h, 0, INK, 1.8f, seed);
}

static void mini_rocket(float x, float y, float s)
{
    Xf t = xf(x, y, 0, s, s);
    ship_model(&t, 0, 0);
}

static void draw_minimap(const Game *g)
{
    float cx = VW * 0.5f, y0 = 8.0f, w = 250.0f, h = 30.0f, x0 = cx - w * 0.5f;
    box(x0, y0, w, h, col_a(PAPER, 0.92f), INK, 1.8f, 0x3A9u);
    gfx_tex(TEX_SOLID);
    float scale = (w - 8.0f) / g->W;
    float vw = VW * scale, vx = cx + wd(g, g->camx - g->px) * scale - vw * 0.5f;
    Poly v; poly_clear(&v);
    poly_add(&v, vx, y0 + 4); poly_add(&v, vx + vw, y0 + 4); poly_add(&v, vx + vw, y0 + h - 4); poly_add(&v, vx, y0 + h - 4);
    pen_poly(&v, 1, 1.2f, col_a(INK, 0.6f), 0x3AAu, 0.3f);
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || (e->warp && e->warp_t < 0)) continue;
        float mx = cx + wd(g, e->x - g->px) * scale, my = y0 + 3.0f + (e->y - WALL_TOP) / (WALL_BOT - WALL_TOP) * (h - 6.0f);
        float s = IS_BOSS(e->type) ? 4.5f : (e->type == EN_SEG || e->type == EN_SHIELD ? 1.6f : 2.4f);
        uint32_t c = e->warp ? (((int)(g->time * 10.0f) & 1) ? INK : C_RED) : (IS_BOSS(e->type) ? 0xE0A000FFu : C_RED);
        gfx_quad4(mx - s, my, c, mx, my - s, c, mx + s, my, c, mx, my + s, c);
    }
    float py = y0 + 3.0f + (g->py - WALL_TOP) / (WALL_BOT - WALL_TOP) * (h - 6.0f), f = (float)g->face;
    gfx_tri1(cx + f * 6.0f, py, cx - f * 5.0f, py - 4.5f, cx - f * 5.0f, py + 4.5f, C_ORANGE);
}

static void weapon_icon(int w, float cx, float cy, float time)
{
    switch (w) {
    case 0: {
        static const float PILL[6][2] = {{9, 0}, {6, -3.5f}, {-8, -3.5f}, {-10, 0}, {-8, 3.5f}, {6, 3.5f}};
        Xf t = xf(cx + 2, cy - 5, 0, 1, 1); shape(&t, PILL, 6, C_ORANGE, INK, 1.4f, 0x401u);
        t = xf(cx - 2, cy + 5, 0, 1, 1); shape(&t, PILL, 6, C_ORANGE, INK, 1.4f, 0x402u);
        break;
    }
    case 1: {
        Poly p; poly_clear(&p);
        for (int i = 0; i <= 8; i++) { float a = -2.6f + 2.4f * (float)i / 8; poly_add(&p, cx + cosf(a) * 12.0f, cy + sinf(a) * 12.0f); }
        for (int i = 8; i >= 0; i--) { float a = -2.6f + 2.4f * (float)i / 8; poly_add(&p, cx + cosf(a) * (6.0f + 2.0f * (float)i / 8), cy + sinf(a) * (6.0f + 2.0f * (float)i / 8)); }
        gfx_uv(cx, cy, 64, 0, 0); crayon_fill(&p, C_MINT); pen_poly(&p, 1, 1.4f, INK, 0x403u, 0.3f);
        break;
    }
    case 2: {
        static const float BODY[5][2] = {{9, 0}, {4, -4}, {-8, -4}, {-8, 4}, {4, 4}};
        Xf t = xf(cx, cy, -0.6f, 1.2f, 1.2f); shape(&t, BODY, 5, C_RED, INK, 1.4f, 0x404u);
        pen_line(cx - 9, cy + 9, cx - 14, cy + 13, 1.4f, C_ORANGE, 0x405u, 0.2f);
        break;
    }
    default:
        box(cx - 13, cy - 4, 26, 8, C_SKY, INK, 1.4f, 0x406u);
        break;
    }
    (void)time;
}

static void draw_weapon_boxes(const Game *g)
{
    static const char *KEY[4] = {"A", "B", "X", "Y"};
    static const float COST[4] = {0, COST_BLADE, COST_MISSILE, COST_LANCE};
    for (int w = 0; w < 4; w++) {
        float x = 20.0f + (float)w * 50.0f, y = 418.0f;
        int ready = w == 0 ? !g->overheat : g->energy >= COST[w];
        float fl = g->wpn_flash[w], bounce = fl * 3.0f;
        int bad = g->empty_t > 0 && (int)g->empty_wpn == w;
        box(x, y - bounce, 42, 40, col_a(ready ? PAPER : 0xD8D0C0FFu, 0.96f), bad && ((int)(g->time * 16.0f) & 1) ? C_RED : INK, 2.0f, 0x410u + (uint32_t)w);
        weapon_icon(w, x + 21.0f, y + 17.0f - bounce, g->time);
        ink_text(KEY[w], x + 3.0f, y + 21.0f - bounce, 14.0f, INK, 0);
        if (!ready) pen_line(x + 4, y + 36 - bounce, x + 38, y + 4 - bounce, 2.2f, col_a(C_RED, 0.85f), 0x420u + (uint32_t)w, 0.6f);
    }
}

static void draw_hud(const Game *g)
{
    char buf[48];
    /* hearts = shields, little rockets = lives */
    for (int k = 0; k < SHIELDS_MAX; k++) {
        float x = 92.0f + (float)k * 28.0f, y = 18.0f;
        int on = k < g->shields;
        float beat = (on && g->shields == 1) ? 1.0f + 0.15f * sinf(g->time * 12.0f) : 1.0f;
        heart(x, y, 12.0f * beat, on ? C_RED : col_a(PAPER, 0.9f), 0x500u + (uint32_t)k);
    }
    mini_rocket(220.0f, 18.0f, 0.42f);
    snprintf(buf, sizeof buf, "x%d", g->lives);
    ink_text(buf, 236.0f, 6.0f, 20.0f, INK, 0);
    /* boost charges: little lightning bolts; the one recharging fills up from the bottom */
    for (int k = 0; k < BOOST_MAX; k++) {
        static const float BOLT[7][2] = {{1, -11}, {-6, 1}, {-0.5f, 1}, {-3, 11}, {6, -2}, {0.5f, -2}, {4, -11}};
        float bx = 232.0f + (float)k * 20.0f, by = 48.0f;
        int full = k < g->boost_charges;
        float fill = full ? 1.0f : (k == g->boost_charges ? g->boost_recharge / BOOST_RECHARGE : 0.0f);
        Xf t = xf(bx, by, 0, 1, 1);
        shape(&t, BOLT, 7, col_a(PAPER, 0.95f), 0, 0, 0x5F0u + (uint32_t)k);
        if (fill > 0.02f) {
            Poly p; poly_clear(&p);
            float top = 11.0f - 22.0f * fill;
            for (int i = 0; i < 7; i++) poly_add(&p, bx + BOLT[i][0], by + fmaxf(BOLT[i][1], top));
            gfx_uv(bx, by, 64, 0, 0);
            crayon_fill(&p, full ? C_YELLOW : 0xFFE89AFFu);
            crayon_fill(&p, full ? C_YELLOW : 0xFFE89AFFu);
        }
        Poly o; poly_clear(&o);
        for (int i = 0; i < 7; i++) poly_add(&o, bx + BOLT[i][0], by + BOLT[i][1]);
        pen_poly(&o, 1, 1.6f, INK, 0x5F8u + (uint32_t)k, 0.3f);
    }
    if (g->wave < g->L->nwaves) snprintf(buf, sizeof buf, "Wave %d of %d", g->wave + 1, g->L->nwaves);
    else snprintf(buf, sizeof buf, "All clear!");
    rim_text(buf, 82.0f, 36.0f, 17.0f, INK, 0, 0, 0);

    draw_minimap(g);
    /* wave pips under the map */
    for (int k = 0; k < g->L->nwaves; k++) {
        float px = VW * 0.5f + ((float)k - (float)(g->L->nwaves - 1) * 0.5f) * 15.0f;
        blob(px, 48.0f, 4.5f, 4.5f, k < g->wave ? C_GREEN : (k == g->wave ? C_YELLOW : col_a(PAPER, 0.9f)), INK, 1.4f, 0x520u + (uint32_t)k, 0.1f);
    }

    /* boss bar */
    if (g->boss >= 0 && g->en[g->boss].alive && !g->en[g->boss].warp) {
        float w = 260.0f, x = VW * 0.5f - w * 0.5f, y = 60.0f;
        bar(x, y, w, 12.0f, g->en[g->boss].hp / g->en[g->boss].maxhp, C_RED, 0x530u);
        rim_text(ESTAT[g->en[g->boss].type].name, VW * 0.5f, y + 12.0f, 16.0f, INK, 1, 0, 0);
    }

    /* score, multiplier, chain */
    long s = g->shown_score;
    if (s > 9999999) s = 9999999;
    char num[24];
    snprintf(num, sizeof num, "%ld", s);
    int n = (int)strlen(num), o = 0;
    for (int i = 0; i < n; i++) { if (i > 0 && (n - i) % 3 == 0) buf[o++] = ','; buf[o++] = num[i]; }
    buf[o] = 0;
    char line[64];
    snprintf(line, sizeof line, "Score: %s", buf);
    ink_text(line, VW - 22.0f, 2.0f, 26.0f, INK, 2);
    snprintf(buf, sizeof buf, "x%.1f", (double)g->mult);
    float ms = 30.0f + g->mult_pulse * 10.0f;
    uint32_t mc = g->mult >= 10.0f ? col_hsv(g->time * 0.5f, 0.8f, 0.82f, 255) : T_ORANGE;
    rim_text(buf, VW - 22.0f, 34.0f - g->mult_pulse * 4.0f, ms, mc, 2, g->time, g->mult >= 10.0f ? 2.0f : 0.0f);
    if (g->chain >= 2) {
        snprintf(buf, sizeof buf, "%d hits!", g->chain);
        rim_text(buf, VW - 22.0f, 72.0f, 20.0f, C_RED, 2, 0, 0);
        float cw = 80.0f * clampf(g->chain_t / 2.4f, 0, 1);
        pen_line(VW - 22.0f - cw, 98.0f, VW - 22.0f, 98.0f, 2.4f, C_RED, 0x540u, 0.6f);
    }
    if (g->chain_show > 0) {
        snprintf(buf, sizeof buf, "+%ld bonus", g->chain_bonus);
        rim_text(buf, VW - 22.0f, 104.0f, 18.0f, col_a(T_GREEN, clampf(g->chain_show, 0, 1)), 2, 0, 0);
    }

    draw_weapon_boxes(g);
    /* energy + heat bars */
    float bx = 232.0f;
    ink_text("energy", bx, 437.0f, 15.0f, INK, 0);
    bar(bx + 52.0f, 442.0f, 190.0f, 13.0f, g->energy / ENERGY_MAX, C_YELLOW, 0x550u);
    ink_text("heat", bx + 8.0f, 418.0f, 15.0f, INK, 0);
    bar(bx + 52.0f, 423.0f, 190.0f, 9.0f, g->heat, g->overheat ? (((int)(g->time * 10.0f) & 1) ? C_RED : C_ORANGE) : C_ORANGE, 0x551u);
}

static void draw_popups(const Game *g)
{
    for (int i = 0; i < MAX_POP; i++) {
        const Popup *p = &g->pp[i];
        if (!p->alive) continue;
        float age = p->max_life - p->life;
        float pop = age < 0.12f ? 1.0f + (0.12f - age) / 0.12f * 0.7f : 1.0f;
        float a = clampf(p->life / 0.3f, 0, 1);
        uint32_t c = col_a(col_mix(p->col | 255u, INK, 0.38f), a);
        if (p->big) gfx_text_fancy(p->txt, p->x, p->y, 40.0f * pop, c, col_a(INK, a), 2.6f, g->time, 3.0f);
        else gfx_text_fancy(p->txt, p->x, p->y, 19.0f * pop, c, col_a(INK, a), 1.3f, 0, 0);
    }
}

static void draw_banner(const Game *g)
{
    if (g->banner_t <= 0) return;
    float t = g->banner_t;
    float in = clampf((2.6f - t) / 0.3f, 0, 1), out = clampf(t / 0.35f, 0, 1);
    float slide = (1.0f - in) * (1.0f - in) * 500.0f - (1.0f - out) * (1.0f - out) * 500.0f;
    float a = in * out;
    float y = 170.0f;
    int warn = strcmp(g->banner, "WARNING") == 0;
    uint32_t c = warn ? C_RED : T_ORANGE;
    gfx_text_fancy(g->banner, VW * 0.5f + slide, y, 64.0f, col_a(c, a), col_a(INK, a), 3.4f, g->time, 3.0f);
    float w = gfx_text_width(g->banner, 64.0f) * 0.5f;                     /* scribbly underline */
    Poly u; poly_clear(&u);
    for (int k = 0; k <= 12; k++) poly_add(&u, VW * 0.5f + slide - w + 2.0f * w * (float)k / 12.0f, y + 70.0f + ((k & 1) ? 3.0f : -3.0f));
    pen_poly(&u, 0, 3.0f, col_a(warn ? C_RED : C_ORANGE, a), 0x600u + (uint32_t)boil, 0.6f);
    gfx_text_fancy(g->sub, VW * 0.5f - slide * 0.6f, y + 82.0f, 24.0f, col_a(INK, a), col_a(PAPER, a), 2.0f, 0, 0);
}

static void draw_boss_card(const Game *g)
{
    if (g->boss_intro <= 0) return;
    float t = g->boss_intro, a = clampf(t / 0.4f, 0, 1) * clampf((2.5f - t) / 0.2f, 0, 1);
    float x = VW * 0.5f + (1.0f - clampf((2.5f - t) / 0.3f, 0, 1)) * -400.0f;
    box(x - 190.0f, 300.0f, 380.0f, 74.0f, col_a(PAPER, 0.97f * a), col_a(INK, a), 2.4f, 0x610u);
    gfx_text_fancy(g->boss >= 0 ? ESTAT[g->en[g->boss].type].name : "BOSS", x, 300.0f, 44.0f, col_a(T_ORANGE, a), col_a(INK, a), 2.6f, g->time, 2.0f);
    ink_text(g->L->boss_sub ? g->L->boss_sub : "the boss", x, 348.0f, 18.0f, col_a(INK, a), 1);
}

/* ------------------------------------------------------------------ */
/* screens                                                              */
/* ------------------------------------------------------------------ */
static void title_logo(const Game *g, float cx, float y, float w)
{
    float h = w * 272.0f / 768.0f, bob = sinf(g->time * 2.0f) * 3.0f, rot = sinf(g->time * 1.3f) * 0.012f;
    Xf t = xf(cx, y + h * 0.5f + bob, rot, 1, 1);
    float X[4], Y[4];
    xpt(&t, -w * 0.5f, -h * 0.5f, &X[0], &Y[0]); xpt(&t, w * 0.5f, -h * 0.5f, &X[1], &Y[1]);
    xpt(&t, w * 0.5f, h * 0.5f, &X[2], &Y[2]);   xpt(&t, -w * 0.5f, h * 0.5f, &X[3], &Y[3]);
    gfx_image(TEX_LOGO, X, Y, WHITE);
}

static void sticky_note(float cx, float cy, float w, float h, float rot, uint32_t col)
{
    Xf t = xf(cx, cy, rot, 1, 1);
    Poly p; poly_clear(&p);
    xadd(&p, &t, -w * 0.5f, -h * 0.5f); xadd(&p, &t, w * 0.5f, -h * 0.5f); xadd(&p, &t, w * 0.5f, h * 0.5f); xadd(&p, &t, -w * 0.5f, h * 0.5f);
    gfx_tex(TEX_SOLID);
    float sx[4], sy[4];
    for (int i = 0; i < 4; i++) { sx[i] = p.x[i] + 4.0f; sy[i] = p.y[i] + 5.0f; }
    gfx_quad4(sx[0], sy[0], 0x00000030u, sx[1], sy[1], 0x00000030u, sx[2], sy[2], 0x00000030u, sx[3], sy[3], 0x00000030u);
    gfx_quad4(p.x[0], p.y[0], col, p.x[1], p.y[1], col, p.x[2], p.y[2], col_mix(col, 0x806020FFu, 0.08f), p.x[3], p.y[3], col);
    gfx_tex(TEX_PAPER);
    gfx_uv(0, 0, 180, 0.3f, 0.1f);
    gfx_quad4(p.x[0], p.y[0], 0xA0802040u, p.x[1], p.y[1], 0xA0802040u, p.x[2], p.y[2], 0xA0802040u, p.x[3], p.y[3], 0xA0802040u);
    gfx_tex(TEX_SOLID);
    /* a strip of tape */
    Xf tt = xf(cx + (-h * 0.5f) * -sinf(rot), cy + (-h * 0.5f) * cosf(rot), rot - 0.08f, 1, 1);
    Poly tp; poly_clear(&tp);
    xadd(&tp, &tt, -40, -10); xadd(&tp, &tt, 40, -10); xadd(&tp, &tt, 40, 10); xadd(&tp, &tt, -40, 10);
    gfx_quad4(tp.x[0], tp.y[0], 0xEDEAD8B0u, tp.x[1], tp.y[1], 0xEDEAD8B0u, tp.x[2], tp.y[2], 0xEDEAD8B0u, tp.x[3], tp.y[3], 0xEDEAD8B0u);
}

static void draw_title(const Game *g)
{
    title_logo(g, VW * 0.5f, 6.0f, 600.0f);
    char buf[64];
    snprintf(buf, sizeof buf, NUM_LEVELS > 1 ? "L  < Level %d: %s >  R" : "Level %d: %s", g->level + 1, LEVELS[g->level].name);
    rim_text(buf, VW * 0.5f, 226.0f, 24.0f, T_ORANGE, 1, 0, 0);
    if (game_best_score(g->level) > 0) {
        snprintf(buf, sizeof buf, "best: %ld", game_best_score(g->level));
        rim_text(buf, VW * 0.5f, 254.0f, 18.0f, INK, 1, 0, 0);
    }
    if ((int)(g->time * 2.0f) & 1) gfx_text_fancy("press A to launch!", VW * 0.5f, 278.0f, 32.0f, C_RED, INK, 2.2f, g->time, 2.0f);
    /* controls, on a sticky note */
    sticky_note(VW * 0.5f, 410.0f, 610.0f, 92.0f, -0.015f, 0xFFF08AFFu);
    static const char *L[4] = {"A  pulse blaster", "B  circle shield", "X  swarm missiles", "Y  power lazer"};
    for (int k = 0; k < 4; k++) {
        float x = 170.0f + (float)(k % 2) * 270.0f, y = 368.0f + (float)(k / 2) * 26.0f;
        weapon_icon(k, x - 18.0f, y + 12.0f, g->time);
        ink_text(L[k], x, y, 20.0f, INK, 0);
    }
    ink_text("stick  fly     L / R  turn around     ZL / ZR  boost     +  pause", VW * 0.5f, 424.0f, 17.0f, INK, 1);
}

static void draw_pause(const Game *g)
{
    gfx_tex(TEX_SOLID);
    gfx_rect(0, 0, VW, VH, col_a(PAPER, 0.35f));
    sticky_note(VW * 0.5f, 250.0f, 340.0f, 300.0f, 0.03f, 0xFFF08AFFu);
    gfx_text_fancy("paused", VW * 0.5f, 112.0f, 52.0f, T_ORANGE, INK, 2.6f, g->time, 2.0f);
    const char *items[4] = {"keep going", "start over", g->sound_on ? "sound: on" : "sound: off", "quit to title"};
    for (int k = 0; k < 4; k++) {
        float y = 190.0f + (float)k * 42.0f;
        int sel = k == g->menu_sel;
        ink_text(items[k], VW * 0.5f, y, 26.0f, sel ? C_RED : INK, 1);
        if (sel) {                                                      /* circled in red crayon */
            float w = gfx_text_width(items[k], 26.0f) * 0.5f + 16.0f;
            Poly c;
            poly_blob(&c, VW * 0.5f, y + 18.0f, w, 20.0f, 24, 0.06f, 0x700u + (uint32_t)k);
            pen_poly(&c, 1, 2.4f, C_RED, 0x700u + (uint32_t)boil, 1.5f);
        }
    }
    ink_text("A  pick     B  back", VW * 0.5f, 360.0f, 18.0f, INK, 1);
}

static void sad_face(float cx, float cy, float r, float t)
{
    blob(cx, cy, r, r, C_YELLOW, INK, 2.6f, 0x800u, 0.05f);
    blob(cx - r * 0.35f, cy - r * 0.2f, 3.0f, 4.0f, INK, 0, 0, 0x801u, 0);
    blob(cx + r * 0.35f, cy - r * 0.2f, 3.0f, 4.0f, INK, 0, 0, 0x802u, 0);
    Poly m; poly_clear(&m);
    for (int k = 0; k <= 8; k++) { float a = PI_F + 0.5f + (PI_F - 1.0f) * (float)k / 8.0f; poly_add(&m, cx + cosf(a) * r * 0.45f, cy + r * 0.55f + sinf(a) * r * 0.3f); }
    pen_poly(&m, 0, 2.2f, INK, 0x803u, 0.3f);
    float ty = posmod(t * 0.8f, 1.0f);                                      /* a tear */
    blob(cx - r * 0.35f, cy + ty * r * 0.8f, 3.0f, 4.5f, C_SKY, INK, 1.2f, 0x804u, 0.05f);
}

static void draw_over(const Game *g)
{
    gfx_tex(TEX_SOLID);
    gfx_rect(0, 0, VW, VH, col_a(PAPER, clampf(g->state_t * 1.2f, 0, 0.55f)));
    float drop = 1.0f - clampf(g->state_t / 0.6f, 0, 1);
    drop = drop * drop * 300.0f;
    gfx_text_fancy("GAME OVER", VW * 0.5f, 110.0f - drop, 80.0f, C_RED, INK, 3.6f, g->time, 3.0f);
    sad_face(VW * 0.5f, 250.0f, 34.0f, g->time);
    char buf[48];
    snprintf(buf, sizeof buf, "score: %ld", g->score);
    rim_text(buf, VW * 0.5f, 300.0f, 30.0f, INK, 1, 0, 0);
    snprintf(buf, sizeof buf, "you made it to wave %d", g->wave + 1);
    rim_text(buf, VW * 0.5f, 338.0f, 20.0f, INK, 1, 0, 0);
    if (g->state_t > 1.2f) gfx_text_fancy("A  try again     B  title", VW * 0.5f, 384.0f, 26.0f, T_ORANGE, INK, 2.0f, g->time, 1.5f);
}

static void draw_clear(const Game *g)
{
    gfx_tex(TEX_SOLID);
    gfx_rect(0, 0, VW, VH, col_a(PAPER, clampf(g->state_t * 1.2f, 0, 0.5f)));
    float t = g->state_t;
    sticky_note(330.0f, 262.0f, 500.0f, 300.0f, -0.02f, 0xFFFDF3FFu);
    gfx_text_fancy("Level clear!", VW * 0.5f, 26.0f, 64.0f, T_GREEN, INK, 3.2f, g->time, 3.0f);
    char buf[48];
    int mins = (int)(g->play_time / 60.0f), secs = (int)g->play_time % 60;
    const char *lab[5] = {"score", "best multiplier", "longest chain", "baddies popped", "time"};
    for (int k = 0; k < 5; k++) {
        float show = clampf((t - 0.3f - 0.35f * (float)k) / 0.25f, 0, 1);
        if (show <= 0) continue;
        float y = 148.0f + (float)k * 44.0f;
        switch (k) {
        case 0: snprintf(buf, sizeof buf, "%ld", g->score); break;
        case 1: snprintf(buf, sizeof buf, "x%.1f", (double)g->best_mult); break;
        case 2: snprintf(buf, sizeof buf, "%d", g->max_chain); break;
        case 3: snprintf(buf, sizeof buf, "%d", g->kills); break;
        default: snprintf(buf, sizeof buf, "%d:%02d", mins, secs); break;
        }
        ink_text(lab[k], 112.0f, y, 26.0f, col_a(INK, show), 0);
        ink_text(buf, 552.0f, y, 26.0f, col_a(C_RED, show), 2);
        pen_line(112.0f, y + 38.0f, 112.0f + 440.0f * show, y + 38.0f, 1.0f, col_a(RULE, 0.9f), 0x900u + (uint32_t)k, 0.3f);
    }
    if (t > 2.6f) {                                                          /* the teacher's grade */
        static const char *R[4] = {"S", "A", "B", "C"};
        static const char *M[4] = {"WOW!!", "great job!", "good work", "keep trying!"};
        float k = clampf((t - 2.6f) / 0.25f, 0, 1);
        float cx = VW - 150.0f, cy = 250.0f, sc = 1.0f + (1.0f - k) * 1.2f;
        if (g->rank == 0) {                                                  /* gold star sticker */
            Poly st; poly_clear(&st);
            for (int i = 0; i < 10; i++) { float a = -PI_F * 0.5f + PI_F * (float)i / 5.0f, rr = (i & 1) ? 38.0f : 84.0f; poly_add(&st, cx + cosf(a) * rr * sc, cy + sinf(a) * rr * sc); }
            gfx_uv(cx, cy, 128, 0, 0);
            crayon_fill(&st, C_YELLOW); crayon_fill(&st, C_YELLOW);
            pen_poly(&st, 1, 3.0f, INK, 0x910u, 0.6f);
        } else {
            Poly c;
            poly_blob(&c, cx, cy, 70.0f * sc, 66.0f * sc, 30, 0.05f, 0x911u);
            pen_poly(&c, 1, 4.0f, C_RED, 0x911u + (uint32_t)boil, 2.0f);
        }
        gfx_text_fancy(R[g->rank], cx, cy - 56.0f * sc, 100.0f * sc, C_RED, PAPER, 2.0f, 0, 0);
        gfx_text_fancy(M[g->rank], cx, cy + 86.0f, 28.0f, C_RED, PAPER, 1.6f, g->time, 2.0f);
    }
    if (t > 3.0f) {
        const char *msg = g->level + 1 < NUM_LEVELS ? "press A for the next level" : "press A to go back to the title";
        gfx_text_fancy(msg, VW * 0.5f, 424.0f, 26.0f, ((int)(g->time * 2.0f) & 1) ? T_ORANGE : C_RED, INK, 2.0f, 0, 0);
    }
}

/* ------------------------------------------------------------------ */
void game_render(const Game *g)
{
    gfx_begin();
    boil = (int)(g->time * BOIL_FPS);
    boil_t = (float)boil / BOIL_FPS;

    float ox = 0, oy = 0;
    if (g->shake > 0) {
        ox = (hf((uint32_t)g->frame * 2u + 1u) - 0.5f) * 2.0f * g->shake;
        oy = (hf((uint32_t)g->frame * 2u + 2u) - 0.5f) * 2.0f * g->shake;
    }
    draw_background(g, ox * 0.5f, oy * 0.5f);
    gfx_view(ox, oy, 1.0f + g->zoom_kick);

    if (g->state != ST_TITLE) {
        draw_crystals(g);
        draw_enemies(g);
        draw_blade(g);
        draw_player(g);
        draw_lance(g);
        draw_projectiles(g);
        draw_ebullets(g);
        draw_particles(g);
        draw_rings(g);
    } else {
        draw_particles(g);
    }

    gfx_view_reset();
    /* getting hurt: a red crayon scribble frame (there are no lights in a drawing) */
    if (g->flash > 0.05f && g->flash_r > 0.9f && g->flash_g < 0.5f) {
        float a = clampf(g->flash * 1.4f, 0, 1);
        gfx_uv(0, 0, 128, hf((uint32_t)g->frame), 0);
        gfx_tex(TEX_CRAYON);
        uint32_t c = col_a(C_RED, a);
        gfx_rect(0, 0, VW, 26, c); gfx_rect(0, VH - 26, VW, 26, c);
        gfx_rect(0, 26, 26, VH - 52, c); gfx_rect(VW - 26, 26, 26, VH - 52, c);
        gfx_tex(TEX_SOLID);
    }

    switch (g->state) {
    case ST_TITLE:                                     /* the ship flies around behind the logo and the writing */
        gfx_view(ox, oy, 1.0f);
        draw_player(g);
        gfx_view_reset();
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
        draw_particles(g);
        draw_clear(g);
        break;
    }
    if (g->fade > 0) { gfx_tex(TEX_SOLID); gfx_rect(0, 0, VW, VH, col_a(PAPER, clampf(g->fade, 0, 1))); }
}
