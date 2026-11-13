/*
 * DOODLE SHOOTER - triangle list builder, procedural crayon/paper textures, hand-drawn primitives,
 * and the handwriting font. See gfx.h.
 */
#include "gfx.h"

#include <math.h>
#include <string.h>

#include "font_data.h"
#include "art_data.h"

#define TAU 6.28318530718f

GVtx gfx_v[GFX_MAX_VTX] __attribute__((aligned(32)));
GSeg gfx_seg[GFX_MAX_SEG];
int  gfx_nv, gfx_nseg;

static int   cur_tex;
static float v_ox, v_oy, v_zoom = 1.0f;
static float uv_ox, uv_oy, uv_k = 1.0f / 128.0f, uv_du, uv_dv;

/* ------------------------------------------------------------------ */
/* colour helpers                                                       */
/* ------------------------------------------------------------------ */
uint32_t col_mix(uint32_t a, uint32_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        r |= (uint32_t)(int)(x + (y - x) * t + 0.5f) << s;
    }
    return r;
}

uint32_t col_hsv(float h, float s, float v, int a)
{
    h -= floorf(h);
    float h6 = h * 6.0f;
    int   i = (int)h6;
    float f = h6 - (float)i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255), a);
}

uint32_t ihash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static float hf(uint32_t x) { return (float)(ihash(x) >> 8) / 16777216.0f; }

float noise1(uint32_t seed, float t)
{
    float fl = floorf(t);
    int   i = (int)fl;
    float f = t - fl;
    f = f * f * (3.0f - 2.0f * f);
    float a = hf(seed * 0x9E3779B9u + (uint32_t)i), b = hf(seed * 0x9E3779B9u + (uint32_t)(i + 1));
    return (a + (b - a) * f) * 2.0f - 1.0f;
}

/* ------------------------------------------------------------------ */
/* textures                                                             */
/* ------------------------------------------------------------------ */
#define CRAYON_N 128
#define PAPER_N  128
static uint8_t tex_solid[8 * 8] __attribute__((aligned(32)));
static uint8_t tex_crayon[CRAYON_N * CRAYON_N] __attribute__((aligned(32)));
static uint8_t tex_paper[PAPER_N * PAPER_N] __attribute__((aligned(32)));
static uint8_t tex_font[FONT_W * FONT_H] __attribute__((aligned(32)));
static uint8_t tex_font_big[FONTB_W * FONTB_H] __attribute__((aligned(32)));
static float   work[CRAYON_N * CRAYON_N];
static uint8_t tex_ship[SHIP_W * SHIP_H * 4] __attribute__((aligned(32)));
static uint8_t tex_logo[LOGO_W * LOGO_H * 4] __attribute__((aligned(32)));
static uint8_t tex_boss2[BOSS2_W * BOSS2_H * 4] __attribute__((aligned(32)));

static void unpack_art(uint8_t *dst, const unsigned char *pal, const unsigned char *idx, int n)
{
    for (int i = 0; i < n; i++) memcpy(dst + i * 4, pal + idx[i] * 4, 4);
}

/* tileable 2D value noise on an n x n texture with `cells` grid cells per side */
static float vnoise2(int x, int y, int n, int cells, uint32_t seed)
{
    float cs = (float)n / (float)cells;
    float fx = (float)x / cs, fy = (float)y / cs;
    int ix = (int)fx, iy = (int)fy;
    float tx = fx - (float)ix, ty = fy - (float)iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    int x0 = ix % cells, y0 = iy % cells, x1 = (ix + 1) % cells, y1 = (iy + 1) % cells;
    float a = hf(seed + (uint32_t)(y0 * 977 + x0)), b = hf(seed + (uint32_t)(y0 * 977 + x1));
    float c = hf(seed + (uint32_t)(y1 * 977 + x0)), d = hf(seed + (uint32_t)(y1 * 977 + x1));
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

/* crayon: hundreds of short waxy diagonal strokes (the way a right-handed kid colours in),
 * drawn on a torus so the texture tiles, then roughened by the paper's tooth */
static void make_crayon(void)
{
    const int N = CRAYON_N;
    memset(work, 0, sizeof work);
    uint32_t r = 0xC0FFEEu;
    for (int k = 0; k < 125; k++) {
        float cx = hf(r++) * N, cy = hf(r++) * N;
        float a = -0.55f + (hf(r++) - 0.5f) * 0.16f;
        float len = 26.0f + hf(r++) * 60.0f, wid = 1.8f + hf(r++) * 1.8f, inten = 0.6f + hf(r++) * 0.4f;
        float dx = cosf(a), dy = sinf(a);
        float x0 = cx - dx * len * 0.5f, y0 = cy - dy * len * 0.5f;
        int bx0 = (int)floorf(fminf(x0, x0 + dx * len) - wid - 1), bx1 = (int)ceilf(fmaxf(x0, x0 + dx * len) + wid + 1);
        int by0 = (int)floorf(fminf(y0, y0 + dy * len) - wid - 1), by1 = (int)ceilf(fmaxf(y0, y0 + dy * len) + wid + 1);
        for (int py = by0; py <= by1; py++)
            for (int px = bx0; px <= bx1; px++) {
                float rx = (float)px + 0.5f - x0, ry = (float)py + 0.5f - y0;
                float t = rx * dx + ry * dy;
                if (t < 0 || t > len) continue;
                float d = fabsf(rx * -dy + ry * dx);
                float cov = wid * 0.5f - d + 0.5f;
                if (cov <= 0) continue;
                if (cov > 1) cov = 1;
                float u = t / len, taper = u < 0.15f ? u / 0.15f : (u > 0.8f ? (1 - u) / 0.2f : 1.0f);
                float v = cov * inten * (0.5f + 0.5f * taper);
                int wx = ((px % N) + N) % N, wy = ((py % N) + N) % N;
                float *o = &work[wy * N + wx];
                *o = 1.0f - (1.0f - *o) * (1.0f - v * 0.85f);
            }
    }
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float tooth = 0.6f + 0.4f * vnoise2(x, y, N, 32, 0x7007u);
            float fine = 0.78f + 0.22f * hf((uint32_t)(y * N + x) * 2654435761u);
            float v = 0.16f * tooth + work[y * N + x] * tooth * fine * 1.25f;   /* faint wax everywhere + strokes */
            if (v > 1) v = 1;
            tex_crayon[y * N + x] = (uint8_t)(v * 255.0f + 0.5f);
        }
}

/* paper: soft blotches plus little fibres - used as a mask of slightly darker paper colour */
static void make_paper(void)
{
    const int N = PAPER_N;
    memset(work, 0, sizeof work);
    uint32_t r = 0xFAB21Cu;
    for (int k = 0; k < 260; k++) {
        float cx = hf(r++) * N, cy = hf(r++) * N, a = hf(r++) * TAU, len = 3.0f + hf(r++) * 9.0f, inten = 0.25f + hf(r++) * 0.45f;
        for (float t = 0; t < len; t += 0.5f) {
            int px = ((int)(cx + cosf(a) * t) % N + N) % N, py = ((int)(cy + sinf(a) * t) % N + N) % N;
            if (work[py * N + px] < inten) work[py * N + px] = inten;
        }
    }
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float v = 0.45f * vnoise2(x, y, N, 4, 0x1234u) + 0.25f * vnoise2(x, y, N, 16, 0x777u)
                    + 0.12f * hf((uint32_t)(y * N + x) * 747796405u) + 0.5f * work[y * N + x];
            if (v > 1) v = 1;
            tex_paper[y * N + x] = (uint8_t)(v * 255.0f);
        }
}

void gfx_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    memset(tex_solid, 255, sizeof tex_solid);
    make_crayon();
    make_paper();
    for (int i = 0; i < FONT_W * FONT_H / 2; i++) {
        tex_font[i * 2]     = (uint8_t)((FONT_PIXELS[i] >> 4) * 17);
        tex_font[i * 2 + 1] = (uint8_t)((FONT_PIXELS[i] & 15) * 17);
    }
    for (int i = 0; i < FONTB_W * FONTB_H / 2; i++) {
        tex_font_big[i * 2]     = (uint8_t)((FONTB_PIXELS[i] >> 4) * 17);
        tex_font_big[i * 2 + 1] = (uint8_t)((FONTB_PIXELS[i] & 15) * 17);
    }
    unpack_art(tex_ship, SHIP_PAL, SHIP_IDX, SHIP_W * SHIP_H);
    unpack_art(tex_logo, LOGO_PAL, LOGO_IDX, LOGO_W * LOGO_H);
    unpack_art(tex_boss2, BOSS2_PAL, BOSS2_IDX, BOSS2_W * BOSS2_H);
}

const uint8_t *gfx_tex_data(int tex, int *w, int *h)
{
    switch (tex) {
    case TEX_CRAYON: *w = CRAYON_N; *h = CRAYON_N; return tex_crayon;
    case TEX_PAPER:  *w = PAPER_N;  *h = PAPER_N;  return tex_paper;
    case TEX_FONT:   *w = FONT_W;   *h = FONT_H;   return tex_font;
    case TEX_FONT_BIG: *w = FONTB_W; *h = FONTB_H; return tex_font_big;
    default:         *w = 8;        *h = 8;        return tex_solid;
    }
}

const uint8_t *gfx_tex_rgba(int tex, int *w, int *h)
{
    switch (tex) {
    case TEX_SHIP: *w = SHIP_W; *h = SHIP_H; return tex_ship;
    case TEX_LOGO: *w = LOGO_W; *h = LOGO_H; return tex_logo;
    case TEX_BOSS2: *w = BOSS2_W; *h = BOSS2_H; return tex_boss2;
    default:       *w = 0; *h = 0; return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* vertex list                                                          */
/* ------------------------------------------------------------------ */
void gfx_begin(void)
{
    gfx_init();
    gfx_nv = 0;
    gfx_nseg = 0;
    cur_tex = -1;
    gfx_view_reset();
    gfx_uv(0, 0, 128.0f, 0, 0);
    gfx_tex(TEX_SOLID);
}

void gfx_tex(int tex)
{
    if (tex == cur_tex) return;
    if (gfx_nseg > 0 && gfx_seg[gfx_nseg - 1].count == 0) {   /* empty segment: just retag it */
        gfx_seg[gfx_nseg - 1].tex = tex;
        cur_tex = tex;
        return;
    }
    if (gfx_nseg >= GFX_MAX_SEG) return;
    gfx_seg[gfx_nseg].tex   = tex;
    gfx_seg[gfx_nseg].start = gfx_nv;
    gfx_seg[gfx_nseg].count = 0;
    gfx_nseg++;
    cur_tex = tex;
}

void gfx_view(float ox, float oy, float zoom) { v_ox = ox; v_oy = oy; v_zoom = zoom; }
void gfx_view_reset(void) { v_ox = 0; v_oy = 0; v_zoom = 1.0f; }
void gfx_uv(float ox, float oy, float scale, float du, float dv) { uv_ox = ox; uv_oy = oy; uv_k = 1.0f / scale; uv_du = du; uv_dv = dv; }

static inline void emit_uv(float x, float y, uint32_t c, float u, float v)
{
    GVtx *o = &gfx_v[gfx_nv++];
    o->x = (x - VW * 0.5f) * v_zoom + VW * 0.5f + v_ox;
    o->y = (y - VH * 0.5f) * v_zoom + VH * 0.5f + v_oy;
    o->c = c;
    o->u = u; o->v = v;
}
static inline void emit(float x, float y, uint32_t c)
{
    emit_uv(x, y, c, (x - uv_ox) * uv_k + uv_du, (y - uv_oy) * uv_k + uv_dv);
}

void gfx_tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2)
{
    if (gfx_nv + 3 > GFX_MAX_VTX || gfx_nseg == 0) return;
    if (((c0 | c1 | c2) & 255) == 0) return;                  /* fully transparent */
    emit(x0, y0, c0); emit(x1, y1, c1); emit(x2, y2, c2);
    gfx_seg[gfx_nseg - 1].count += 3;
}

void gfx_tri1(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) { gfx_tri(x0, y0, c, x1, y1, c, x2, y2, c); }

void gfx_quad4(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
               float x2, float y2, uint32_t c2, float x3, float y3, uint32_t c3)
{
    gfx_tri(x0, y0, c0, x1, y1, c1, x2, y2, c2);
    gfx_tri(x0, y0, c0, x2, y2, c2, x3, y3, c3);
}

void gfx_rect(float x, float y, float w, float h, uint32_t c) { gfx_quad4(x, y, c, x + w, y, c, x + w, y + h, c, x, y + h, c); }
void gfx_rect_v(float x, float y, float w, float h, uint32_t top, uint32_t bot) { gfx_quad4(x, y, top, x + w, y, top, x + w, y + h, bot, x, y + h, bot); }

void gfx_line(float x0, float y0, float x1, float y1, float w, uint32_t c0, uint32_t c1)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.001f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    gfx_quad4(x0 + nx, y0 + ny, c0, x1 + nx, y1 + ny, c1, x1 - nx, y1 - ny, c1, x0 - nx, y0 - ny, c0);
}

/* ------------------------------------------------------------------ */
/* hand-drawn primitives                                                */
/* ------------------------------------------------------------------ */
void poly_clear(Poly *p) { p->n = 0; }
void poly_add(Poly *p, float x, float y)
{
    if (p->n >= POLY_MAX) return;
    p->x[p->n] = x; p->y[p->n] = y; p->n++;
}

void poly_blob(Poly *p, float cx, float cy, float rx, float ry, int n, float lump, uint32_t seed)
{
    poly_clear(p);
    if (n > POLY_MAX) n = POLY_MAX;
    for (int i = 0; i < n; i++) {
        float a = TAU * (float)i / (float)n;
        float k = 1.0f + lump * noise1(seed, (float)i * 0.9f);
        poly_add(p, cx + cosf(a) * rx * k, cy + sinf(a) * ry * k);
    }
}

static float tri_area2(float ax, float ay, float bx, float by, float cx, float cy) { return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay); }

/* ear-clipping triangulation; calls back with each triangle */
static void triangulate(const Poly *p, uint32_t col, float dx, float dy)
{
    int n = p->n;
    if (n < 3) return;
    int idx[POLY_MAX];
    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += p->x[i] * p->y[j] - p->x[j] * p->y[i];
    }
    for (int i = 0; i < n; i++) idx[i] = area > 0 ? i : n - 1 - i;
    int m = n, guard = 0;
    while (m > 3 && guard++ < 4 * n) {
        int found = 0;
        for (int i = 0; i < m; i++) {
            int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            if (tri_area2(p->x[a], p->y[a], p->x[b], p->y[b], p->x[c], p->y[c]) <= 0) continue;   /* reflex */
            int inside = 0;
            for (int k = 0; k < m && !inside; k++) {
                int q = idx[k];
                if (q == a || q == b || q == c) continue;
                float x = p->x[q], y = p->y[q];
                if (tri_area2(p->x[a], p->y[a], p->x[b], p->y[b], x, y) >= 0 &&
                    tri_area2(p->x[b], p->y[b], p->x[c], p->y[c], x, y) >= 0 &&
                    tri_area2(p->x[c], p->y[c], p->x[a], p->y[a], x, y) >= 0) inside = 1;
            }
            if (inside) continue;
            gfx_tri1(p->x[a] + dx, p->y[a] + dy, p->x[b] + dx, p->y[b] + dy, p->x[c] + dx, p->y[c] + dy, col);
            for (int k = i; k < m - 1; k++) idx[k] = idx[k + 1];
            m--;
            found = 1;
            break;
        }
        if (!found) break;
    }
    for (int i = 1; i + 1 < m; i++)        /* the last triangle (or a fan fallback for degenerate input) */
        gfx_tri1(p->x[idx[0]] + dx, p->y[idx[0]] + dy, p->x[idx[i]] + dx, p->y[idx[i]] + dy, p->x[idx[i + 1]] + dx, p->y[idx[i + 1]] + dy, col);
}

void crayon_fill(const Poly *p, uint32_t col)
{
    gfx_tex(TEX_SOLID);
    triangulate(p, col_a(col_mix(col, 0xFFFFFFFFu, 0.15f), 0.45f), 0, 0);     /* thin wash so small shapes read solid */
    gfx_tex(TEX_CRAYON);
    triangulate(p, col, 0, 0);                                                /* waxy strokes (+ a faint wax floor) */
}

static void dot(float x, float y, float r, uint32_t c)
{
    gfx_quad4(x - r, y, c, x, y - r, c, x + r, y, c, x, y + r, c);
}

void pen_poly(const Poly *p, int closed, float width, uint32_t col, uint32_t seed, float wobble)
{
    if (p->n < 2) return;
    gfx_tex(TEX_SOLID);
    int   n = p->n, edges = closed ? n : n - 1;
    float dist = 0, px = 0, py = 0;
    int   have = 0;
    for (int e = 0; e <= edges; e++) {
        int last = e == edges;
        if (last && !closed) break;
        int i = e % n, j = (e + 1) % n;
        float ax = p->x[i], ay = p->y[i], bx = p->x[j], by = p->y[j];
        float frac = 1.0f;
        if (last) {                                         /* closed: overshoot past the start like a quick hand */
            frac = 0.0f;
            float l = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
            if (l > 0.01f) frac = fminf(5.0f / l, 0.35f);
            if (frac <= 0) break;
        }
        float dx = (bx - ax) * frac, dy = (by - ay) * frac, len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f) continue;
        float nx = -dy / len, ny = dx / len;
        int steps = (int)(len / 9.0f) + 1;
        for (int s = (have ? 1 : 0); s <= steps; s++) {
            float t = (float)s / (float)steps;
            float d = dist + len * t;
            float off = wobble * noise1(seed, d * 0.06f);
            if (last) off += t * 1.6f;
            float x = ax + dx * t + nx * off, y = ay + dy * t + ny * off;
            if (have) {
                float w = width * (0.85f + 0.3f * (noise1(seed + 7u, d * 0.05f) * 0.5f + 0.5f));
                if (last) w *= 1.0f - 0.6f * t;
                gfx_line(px, py, x, y, w, col, col);
                if (s == 0 || s == steps) dot(x, y, w * 0.5f, col);
            }
            px = x; py = y; have = 1;
        }
        dist += len;
    }
}

void pen_line(float x0, float y0, float x1, float y1, float width, uint32_t col, uint32_t seed, float wobble)
{
    Poly p;
    p.n = 2; p.x[0] = x0; p.y[0] = y0; p.x[1] = x1; p.y[1] = y1;
    pen_poly(&p, 0, width, col, seed, wobble);
    dot(x0, y0, width * 0.45f, col);
    dot(x1, y1, width * 0.45f, col);
}

void crayon_scribble(const Poly *p, uint32_t col)
{
    gfx_tex(TEX_CRAYON);
    triangulate(p, col, 0, 0);
    gfx_tex(TEX_SOLID);
}

void doodle(const Poly *p, uint32_t fill, uint32_t ink, float width, uint32_t seed)
{
    crayon_fill(p, fill);
    pen_poly(p, 1, width, ink, seed, 1.0f);
}

/* ------------------------------------------------------------------ */
/* handwriting                                                          */
/* ------------------------------------------------------------------ */
/* two copies of the handwriting: big text uses the high-resolution one so it stays crisp */
typedef struct { int tex, bake, w, h; const short (*m)[7]; } FontSet;
static const FontSet FS_SMALL = { TEX_FONT, FONT_BAKE, FONT_W, FONT_H, FONT_METRICS };
static const FontSet FS_BIG   = { TEX_FONT_BIG, FONTB_BAKE, FONTB_W, FONTB_H, FONTB_METRICS };
static const FontSet *font_for(float size) { return size >= 26.0f ? &FS_BIG : &FS_SMALL; }

static const short *glyph_m(const FontSet *f, char ch)
{
    unsigned c = (unsigned char)ch;
    if (c < 32 || c > 126) c = '?';
    return f->m[c - 32];
}

float gfx_text_width(const char *s, float size)
{
    const FontSet *f = font_for(size);
    float k = size / (float)f->bake, w = 0;
    for (; *s; s++) w += (float)glyph_m(f, *s)[6] * k;
    return w;
}

static void glyph_quad(const FontSet *f, const short *m, float x, float base, float k, float rot, uint32_t c, float ox, float oy)
{
    float x0 = x + (float)m[4] * k + ox, y0 = base + (float)m[5] * k + oy, w = (float)m[2] * k, h = (float)m[3] * k;
    float u0 = (float)m[0] / f->w, v0 = (float)m[1] / f->h, u1 = (float)(m[0] + m[2]) / f->w, v1 = (float)(m[1] + m[3]) / f->h;
    float cx = x0 + w * 0.5f, cy = y0 + h * 0.5f, cr = cosf(rot), sr = sinf(rot);
    float X[4] = {x0, x0 + w, x0 + w, x0}, Y[4] = {y0, y0, y0 + h, y0 + h}, U[4] = {u0, u1, u1, u0}, V[4] = {v0, v0, v1, v1};
    if (gfx_nv + 6 > GFX_MAX_VTX || gfx_nseg == 0) return;
    for (int i = 0; i < 4; i++) {
        float dx = X[i] - cx, dy = Y[i] - cy;
        X[i] = cx + dx * cr - dy * sr; Y[i] = cy + dx * sr + dy * cr;
    }
    static const int T[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) emit_uv(X[T[i]], Y[T[i]], c, U[T[i]], V[T[i]]);
    gfx_seg[gfx_nseg - 1].count += 6;
}

void gfx_image(int tex, const float *x, const float *y, uint32_t tint)
{
    static const float U[4] = {0, 1, 1, 0}, V[4] = {0, 0, 1, 1};
    static const int T[6] = {0, 1, 2, 0, 2, 3};
    gfx_tex(tex);
    if (gfx_nv + 6 > GFX_MAX_VTX || gfx_nseg == 0) return;
    for (int i = 0; i < 6; i++) emit_uv(x[T[i]], y[T[i]], tint, U[T[i]], V[T[i]]);
    gfx_seg[gfx_nseg - 1].count += 6;
    gfx_tex(TEX_SOLID);
}

void gfx_text(const char *s, float x, float y, float size, uint32_t c)
{
    if ((c & 255) == 0) return;
    const FontSet *f = font_for(size);
    gfx_tex(f->tex);
    float k = size / (float)f->bake, base = y + size * 0.86f;
    for (; *s; s++) {
        const short *m = glyph_m(f, *s);
        if (m[2] > 0) glyph_quad(f, m, x, base, k, 0, c, 0, 0);
        x += (float)m[6] * k;
    }
    gfx_tex(TEX_SOLID);
}

void gfx_text_c(const char *s, float cx, float y, float size, uint32_t c) { gfx_text(s, cx - gfx_text_width(s, size) * 0.5f, y, size, c); }
void gfx_text_r(const char *s, float rx, float y, float size, uint32_t c) { gfx_text(s, rx - gfx_text_width(s, size), y, size, c); }

void gfx_text_fancy(const char *s, float cx, float y, float size, uint32_t fill, uint32_t ink, float outline, float t, float bounce)
{
    (void)outline; (void)ink;                     /* lettering is solid colour: no outline */
    if ((fill & 255) == 0) return;
    const FontSet *f = font_for(size);
    gfx_tex(f->tex);
    float k = size / (float)f->bake, base = y + size * 0.86f;
    float x = cx - gfx_text_width(s, size) * 0.5f;
    for (int i = 0; s[i]; i++) {
        const short *m = glyph_m(f, s[i]);
        if (m[2] > 0) {
            float by = base + sinf(t * 6.0f - (float)i * 0.7f) * bounce;
            float rot = 0.09f * noise1(0xABCu + (uint32_t)i * 17u, t * 1.5f);
            glyph_quad(f, m, x, by, k, rot, fill, 0, 0);
        }
        x += (float)m[6] * k;
    }
    gfx_tex(TEX_SOLID);
}
