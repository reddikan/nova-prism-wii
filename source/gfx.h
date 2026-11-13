/*
 * DOODLE SHOOTER - tiny 2D triangle renderer (platform independent), "notebook doodle" edition.
 *
 * Everything is drawn as triangles into one big vertex list. Each vertex has a position, a colour
 * and a texture coordinate; the list is split into segments by texture. Every texture is a
 * greyscale mask that scales the vertex alpha (colour = vertex colour, alpha = vertex alpha * mask):
 *     TEX_SOLID  plain flat colour
 *     TEX_CRAYON crayon hatching (diagonal waxy strokes with paper showing through)
 *     TEX_PAPER  paper fibres
 *     TEX_FONT   the handwriting font atlas
 * On the Wii, main.c uploads the textures to the GPU and draws the list with GX; the native test
 * harness rasterizes the very same list in software.
 *
 * Virtual screen: 854 x 480 (16:9).
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define VW 854.0f
#define VH 480.0f

#define GFX_MAX_VTX 160000
#define GFX_MAX_SEG 8192

enum { TEX_SOLID, TEX_CRAYON, TEX_PAPER, TEX_FONT, TEX_FONT_BIG,   /* greyscale masks (alpha = vertex alpha x mask) */
       TEX_SHIP, TEX_LOGO, TEX_BOSS2,               /* full-colour hand-drawn pictures, RGBA (colour = vertex x picture) */
       TEX_COUNT };
#define TEX_FIRST_RGBA TEX_SHIP

typedef struct { float x, y; uint32_t c; float u, v; } GVtx;       /* c = 0xRRGGBBAA */
typedef struct { int tex, start, count; } GSeg;

extern GVtx gfx_v[GFX_MAX_VTX];
extern GSeg gfx_seg[GFX_MAX_SEG];
extern int  gfx_nv, gfx_nseg;

static inline uint32_t RGBA(int r, int g, int b, int a)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    if (a < 0) a = 0; if (a > 255) a = 255;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}
static inline uint32_t col_a(uint32_t c, float a)          /* scale alpha */
{
    int al = (int)((float)(c & 255) * a);
    if (al < 0) al = 0; if (al > 255) al = 255;
    return (c & 0xFFFFFF00u) | (uint32_t)al;
}
uint32_t col_mix(uint32_t a, uint32_t b, float t);           /* lerp all four channels */
uint32_t col_hsv(float h, float s, float v, int a);          /* h in turns (0..1) */

/* textures: greyscale masks, power-of-two sizes, generated / unpacked by gfx_init() */
void           gfx_init(void);
const uint8_t *gfx_tex_data(int tex, int *w, int *h);          /* masks: 8 bits per pixel */
const uint8_t *gfx_tex_rgba(int tex, int *w, int *h);          /* pictures: RGBA, 4 bytes per pixel */
/* draw a picture texture on a quad given by its four corners (top-left, top-right, bottom-right, bottom-left) */
void gfx_image(int tex, const float *x, const float *y, uint32_t tint);
/* the same, bent: a grid of (nx+1) x (ny+1) corner positions, row by row (used to make the Big Head's jaw drop) */
void gfx_image_mesh(int tex, int nx, int ny, const float *x, const float *y, uint32_t tint);

void gfx_begin(void);
void gfx_tex(int tex);
/* view transform applied to everything: zoom about the screen centre, then offset (screen shake) */
void gfx_view(float ox, float oy, float zoom);
void gfx_view_reset(void);
/* texture coordinates for crayon / paper fills: u = (x - ox) / scale + du (before the view transform).
 * Anchor it to a moving object so its scribbles travel with it; nudge du/dv to make the strokes "boil". */
void gfx_uv(float ox, float oy, float scale, float du, float dv);

void gfx_tri(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2);
void gfx_tri1(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c);
void gfx_quad4(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1,
               float x2, float y2, uint32_t c2, float x3, float y3, uint32_t c3);   /* corners in order */
void gfx_rect(float x, float y, float w, float h, uint32_t c);
void gfx_rect_v(float x, float y, float w, float h, uint32_t top, uint32_t bot);   /* vertical gradient */
void gfx_line(float x0, float y0, float x1, float y1, float w, uint32_t c0, uint32_t c1);

/* ---- hand-drawn primitives ---- */
#define POLY_MAX 96
typedef struct { int n; float x[POLY_MAX], y[POLY_MAX]; } Poly;

void poly_clear(Poly *p);
void poly_add(Poly *p, float x, float y);
/* lumpy kid-drawn ellipse */
void poly_blob(Poly *p, float cx, float cy, float rx, float ry, int n, float lump, uint32_t seed);
/* crayon-coloured fill (any simple polygon, convex or not), scribbled with the current uv anchor */
void crayon_fill(const Poly *p, uint32_t col);
/* felt-pen outline with a hand-wobble; seed picks the wobble, closed shapes overshoot a little at the end */
void pen_poly(const Poly *p, int closed, float width, uint32_t col, uint32_t seed, float wobble);
void pen_line(float x0, float y0, float x1, float y1, float width, uint32_t col, uint32_t seed, float wobble);
/* strokes only, no wash: big coloured-in areas where the paper should peek through */
void crayon_scribble(const Poly *p, uint32_t col);
/* fill + outline in one go */
void doodle(const Poly *p, uint32_t fill, uint32_t ink, float width, uint32_t seed);

/* ---- handwriting ---- size = font size in pixels; y = top of the text line ---- */
float gfx_text_width(const char *s, float size);
void  gfx_text(const char *s, float x, float y, float size, uint32_t c);
void  gfx_text_c(const char *s, float cx, float y, float size, uint32_t c);
void  gfx_text_r(const char *s, float rx, float y, float size, uint32_t c);
/* wobbly bouncing letters (solid colour, no outline; `outline` is ignored) */
void  gfx_text_fancy(const char *s, float cx, float y, float size, uint32_t fill, uint32_t ink, float outline, float t, float bounce);

/* smooth value noise in [-1, 1] */
float noise1(uint32_t seed, float t);
uint32_t ihash(uint32_t x);

#endif
