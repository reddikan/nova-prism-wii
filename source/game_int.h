/*
 * NOVA PRISM - helpers shared by game.c (rules) and draw.c (rendering).
 */
#ifndef GAME_INT_H
#define GAME_INT_H

#include <math.h>
#include "game.h"
#include "gfx.h"

#define TAU 6.28318530718f
#define PI_F 3.14159265359f

/* tuning */
#define SHIP_R        10.0f
#define SHIP_SPEED    390.0f
#define BOLT_SPEED    1250.0f
#define BLADE_R       112.0f
#define LANCE_LEN     1000.0f
#define LANCE_W       40.0f
#define ENERGY_MAX    100.0f
#define COST_BLADE    10.0f
#define DRAIN_BLADE   36.0f
#define COST_MISSILE  30.0f
#define COST_LANCE    35.0f
#define SHIELDS_MAX   4
#define MINIMAP_W     280.0f

/* weapon colours: A blaster, B blade, X missiles, Y lance */
#define COL_A 0xFFC23DFFu
#define COL_B 0x4DFFC3FFu
#define COL_X 0xFF4FA0FFu
#define COL_Y 0x5CE1FFFFu

typedef struct {
    float    hp, r, score, mult;
    int      crystals;
    uint32_t c_light, c_mid, c_dark, c_accent;
    const char *name;
} EnemyStats;
extern const EnemyStats ESTAT[EN_TYPES];

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float approach(float x, float target, float rate) { return x + (target - x) * (rate > 1.0f ? 1.0f : rate); }

/* the level wraps around horizontally: positions live in [0, W) */
static inline float wrapx(const Game *g, float x)
{
    if (x >= g->W) x -= g->W * floorf(x / g->W);
    else if (x < 0) x += g->W * (1.0f + floorf(-x / g->W));
    if (x >= g->W) x -= g->W;
    return x;
}
/* shortest signed horizontal offset, in [-W/2, W/2) */
static inline float wd(const Game *g, float dx)
{
    float h = g->W * 0.5f;
    dx = wrapx(g, dx + h) - h;
    return dx;
}
static inline float sxof(const Game *g, float x) { return VW * 0.5f + wd(g, x - g->camx); }

static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static inline float hashf(uint32_t x) { return (float)(hash32(x) >> 8) / 16777216.0f; }

/* terrain: piecewise-linear ceiling and floor */
static inline float terr_spacing(const Game *g) { return g->W / (float)g->L->nterr; }
static inline float terr_at(const Game *g, const float *h, float x)
{
    float sp = terr_spacing(g);
    float u = wrapx(g, x) / sp;
    int   i = (int)u;
    float f = u - (float)i;
    int   n = g->L->nterr;
    if (i >= n) i = n - 1;
    int   j = (i + 1) % n;
    return h[i] + (h[j] - h[i]) * f;
}
static inline float terr_top(const Game *g, float x) { return terr_at(g, g->ttop, x); }
static inline float terr_bot(const Game *g, float x) { return terr_at(g, g->tbot, x); }

#endif
