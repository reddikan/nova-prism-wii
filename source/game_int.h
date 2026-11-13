/*
 * DOODLE SHOOTER - helpers shared by game.c (rules) and draw.c (rendering).
 */
#ifndef GAME_INT_H
#define GAME_INT_H

#include <math.h>
#include "game.h"
#include "gfx.h"
#include "art_features.h"

#define TAU 6.28318530718f
#define PI_F 3.14159265359f

/* tuning */
#define SHIP_R        11.0f
#define SHIP_SPEED    390.0f
#define BOLT_SPEED    1250.0f
#define BLADE_R       122.0f
#define LANCE_LEN     1000.0f
#define LANCE_W       58.0f
#define ENERGY_MAX    100.0f
#define COST_BLADE    10.0f
#define DRAIN_BLADE   36.0f
#define COST_MISSILE  30.0f
#define COST_LANCE    35.0f
#define SHIELDS_MAX   4
#define BOOST_MAX     2                   /* boost charges */
#define BOOST_RECHARGE 2.6f               /* seconds to get one charge back */
#define BOOST_TIME    0.3f
#define BOOST_SPEED   1050.0f
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

/* ---- the Big Head (level 2 boss): its tentacles are shared by the rules (collisions) and the drawing ---- */
#define TENT_N     6
#define TENT_SEGS  10
#define LASH_TIME  1.15f
#define BOSS2_PIC  205.0f                    /* size the drawing is shown at */
/* where a feature of the picture is, relative to the head's centre */
#define B2X(u) (((u) - 0.5f) * BOSS2_PIC)
#define B2Y(v) (((v) - 0.5f) * BOSS2_PIC)
/* is she cackling right now? (her jaw drops and the "HA HA" comes out) */
static inline int boss2_laughing(const Enemy *e)
{
    int mad = e->hp < e->maxhp * 0.5f;
    return e->lash_t > 0 || fmodf(e->t, mad ? 2.6f : 3.4f) < (mad ? 1.5f : 1.0f);
}

static inline float tentacle_lash(const Enemy *e, int k)
{
    if (e->lash_t <= 0) return 0.0f;
    if (k != e->lash_k && !(e->lash2 && k == (e->lash_k + 3) % TENT_N)) return 0.0f;
    float p = 1.0f - e->lash_t / LASH_TIME;              /* 0 -> 1 */
    if (p < 0.22f) return -0.25f * sinf(p / 0.22f * 1.5708f);   /* wind up */
    float q = (p - 0.22f) / 0.78f;
    return sinf(q * 3.14159f) * (q < 0.5f ? 1.0f : 1.0f - (q - 0.5f) * 0.6f);
}
/* points along tentacle k (world x, screen y); x[0], y[0] is the root under the chin */
static inline void tentacle_points(const Enemy *e, int k, float *x, float *y)
{
    float t = e->t, amt = tentacle_lash(e, k);
    float a = 1.5708f + ((float)k - 2.5f) * 0.3f;
    float seg = (15.0f + 4.0f * sinf(t * 1.7f + (float)k)) * (1.0f + (amt > 0 ? amt * 2.1f : amt));
    x[0] = e->x + ((float)k - 2.5f) * 20.0f;
    y[0] = e->y + 82.0f;
    for (int i = 1; i <= TENT_SEGS; i++) {
        a += sinf(t * 6.5f + (float)k * 1.9f + (float)i * 0.75f) * 0.46f * (1.0f - (amt > 0 ? amt : 0) * 0.55f);
        a += 0.09f * sinf(t * 2.0f + (float)k * 2.3f) * (float)i * 0.25f;      /* slow curl that builds to the tip */
        if (amt > 0) {
            float d = e->lash_ang - a;
            while (d > 3.14159f) d -= 6.28318f;
            while (d < -3.14159f) d += 6.28318f;
            a += d * amt * 0.45f;
        }
        x[i] = x[i - 1] + cosf(a) * seg;
        y[i] = y[i - 1] + sinf(a) * seg;
    }
}

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
