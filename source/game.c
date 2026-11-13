/*
 * DOODLE SHOOTER - game rules: ship, weapons, enemies, waves, scoring, menus.
 * Rendering lives in draw.c.
 */
#include "game_int.h"

#include <stdio.h>
#include <string.h>

const EnemyStats ESTAT[EN_TYPES] = {
    /*            hp    r     score  mult  cry  light        mid          dark         accent       name */
    /* SHARD   */ {4,   14,   50,    0.1f, 2,  0x7DC0FFFFu, 0x3C7FE6FFu, 0x2350B8FFu, 0xFF6F91FFu, "SHARD"},
    /* ZIP     */ {1,   11,   30,    0.1f, 1,  0x9CFFD8FFu, 0x3FE0A6FFu, 0x1FA77AFFu, 0xFFFFFFFFu, "ZIP"},
    /* SPORE   */ {9,   21,   150,   0.4f, 5,  0xB58BFFFFu, 0x8A5AF0FFu, 0x5E2FC8FFu, 0xFF5FA8FFu, "SPORE"},
    /* SERPENT */ {24,  22,   600,   1.0f, 8,  0xC79BFFFFu, 0x8F5BEAFFu, 0x6236C4FFu, 0xFF5FA8FFu, "SERPENT"},
    /* SEG     */ {4,   15,   40,    0.1f, 1,  0xB78CFFFFu, 0x9566F0FFu, 0x5A2CB4FFu, 0xFF5FA8FFu, "SEGMENT"},
    /* BOSS    */ {650, 62,   10000, 3.0f, 40, 0xA57BFFFFu, 0x6E3FD8FFu, 0x3A1890FFu, 0xFF4FD8FFu, "HEXACORE"},
    /* SHIELD  */ {22,  20,   250,   0.3f, 3,  0xA8F8FFFFu, 0x3CC6E8FFu, 0x1D86B8FFu, 0xFFFFFFFFu, "SHIELD"},
    /* UFO     */ {7,   19,   120,   0.3f, 3,  0xC8CCD8FFu, 0x9AA0B4FFu, 0x6A7088FFu, 0x7FE07FFFu, "SAUCER"},
    /* BOMB    */ {3,   15,   80,    0.2f, 2,  0x5A607AFFu, 0x3A3F58FFu, 0x262A3CFFu, 0xFFD23AFFu, "BOMB"},
    /* BLOB    */ {8,   20,   100,   0.3f, 2,  0xA8EE6AFFu, 0x7AD84AFFu, 0x4FA830FFu, 0xFFFFFFFFu, "BLOB"},
    /* BLOBLET */ {2,   11,   30,    0.1f, 1,  0xA8EE6AFFu, 0x7AD84AFFu, 0x4FA830FFu, 0xFFFFFFFFu, "BLOBLET"},
    /* BOSS2   */ {900, 80,   15000, 3.0f, 50, 0xFAC296FFu, 0xF0A070FFu, 0xC07050FFu, 0xA066F0FFu, "THE BIG HEAD"},
    /* SNIP    */ {7,   17,   140,   0.3f, 3,  0xD8DCE8FFu, 0xA8AEC0FFu, 0x707890FFu, 0xF0483CFFu, "SCISSORS"},
    /* PLANE   */ {3,   15,   90,    0.2f, 2,  0xFFFFFFFFu, 0xE6E8F2FFu, 0xB0B4C8FFu, 0x3D8CF0FFu, "PAPER PLANE"},
    /* PINWHEEL*/ {16,  22,   220,   0.5f, 5,  0xFF9A2AFFu, 0x3D8CF0FFu, 0x5DB03AFFu, 0xFF6FA8FFu, "PINWHEEL"},
    /* BOSS3   */ {1150, 30,  16000, 3.0f, 60, 0xFFD23AFFu, 0xF0B020FFu, 0xC08010FFu, 0xFF8AB0FFu, "THE PENCIL"},
};

enum { P_SPARK, P_GLOW, P_DEBRIS, P_SMOKE, P_CONF, P_STAR };

static long best_scores[8];

/* ------------------------------------------------------------------ */
/* random, sound                                                        */
/* ------------------------------------------------------------------ */
static uint32_t rnd(Game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}
static float frnd(Game *g) { return (float)(rnd(g) >> 8) / 16777216.0f; }
static float frange(Game *g, float a, float b) { return a + (b - a) * frnd(g); }

/* minimum time between two plays of the same effect, so a storm of hits stays musical */
static const float SFX_GAP[SFX_COUNT] = {
    /* SHOT */ 0.05f, /* OVERHEAT */ 0.3f, /* BLADE */ 0.22f, /* MISSILE */ 0.1f, /* CHARGE */ 0.2f, /* LANCE */ 0.2f,
    /* HIT */ 0.04f, /* EXPL_S */ 0.045f, /* EXPL_M */ 0.08f, /* EXPL_L */ 0.25f, /* CRYSTAL */ 0.035f, /* HURT */ 0.2f,
    /* DIE */ 0.5f, /* FLIP */ 0.08f, /* WARP */ 0.12f, /* ESHOT */ 0.1f, /* DEFLECT */ 0.05f, /* WAVE */ 0.5f,
    /* BOSS */ 0.5f, /* CLEAR */ 0.5f, /* PERFECT */ 0.5f, /* COMBO */ 0.3f, /* EMPTY */ 0.2f, /* POWERUP */ 0.1f,
    /* SELECT */ 0.05f, /* MOVE */ 0.04f, /* START */ 0.3f, /* PAUSE */ 0.1f, /* OVER */ 0.5f, /* RANK */ 0.2f
};

static void sfxp(Game *g, int id, int pitch)
{
    if (!g->sound_on || g->sfx_cd[id] > 0.0f) return;
    g->sfx_cd[id] = SFX_GAP[id];
    int n = (g->sfx_head + 1) % 48;
    if (n == g->sfx_tail) return;
    if (pitch < 40) pitch = 40;
    if (pitch > 255) pitch = 255;
    g->sfxq[g->sfx_head] = (uint8_t)id;
    g->sfxp[g->sfx_head] = (uint8_t)pitch;
    g->sfx_head = n;
}
static void sfx(Game *g, int id) { sfxp(g, id, 128); }

int game_pop_sound(Game *g, int *pitch)
{
    if (g->sfx_tail == g->sfx_head) return -1;
    int id = g->sfxq[g->sfx_tail];
    if (pitch) *pitch = g->sfxp[g->sfx_tail];
    g->sfx_tail = (g->sfx_tail + 1) % 48;
    return id;
}

/* ------------------------------------------------------------------ */
/* effects                                                              */
/* ------------------------------------------------------------------ */
static Part *newpart(Game *g, int kind, float x, float y, float vx, float vy, float life, float size, uint32_t col)
{
    Part *p = &g->pt[g->pt_next];
    g->pt_next = (g->pt_next + 1) % MAX_PT;
    memset(p, 0, sizeof *p);
    p->alive = 1; p->kind = (uint8_t)kind;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->max_life = life;
    p->size = size; p->col = col;
    p->drag = 2.5f;
    return p;
}

static void spark(Game *g, float x, float y, float ang, float spd, float life, float size, uint32_t col)
{
    newpart(g, P_SPARK, x, y, cosf(ang) * spd, sinf(ang) * spd, life, size, col)->drag = 3.5f;
}

static void pglow(Game *g, float x, float y, float vx, float vy, float life, float size, uint32_t col)
{
    newpart(g, P_GLOW, x, y, vx, vy, life, size, col);
}

static void debris(Game *g, float x, float y, float spd, float size, uint32_t col)
{
    float a = frange(g, 0, TAU), s = spd * frange(g, 0.35f, 1.0f);
    Part *p = newpart(g, P_DEBRIS, x, y, cosf(a) * s, sinf(a) * s, frange(g, 0.6f, 1.2f), size * frange(g, 0.6f, 1.3f), col);
    p->rot = frange(g, 0, TAU);
    p->vr = frange(g, -14, 14);
    p->drag = 1.6f;
}

static void ring(Game *g, float x, float y, float maxr, float life, float thick, uint32_t col)
{
    int best = 0;
    float bl = 1e9f;
    for (int i = 0; i < MAX_RING; i++) {
        if (!g->rg[i].alive) { best = i; break; }
        if (g->rg[i].life < bl) { bl = g->rg[i].life; best = i; }
    }
    Ring *r = &g->rg[best];
    r->alive = 1; r->x = x; r->y = y; r->r = 0; r->maxr = maxr;
    r->life = r->max_life = life; r->thick = thick; r->col = col;
}

static void popup(Game *g, float x, float y, const char *txt, uint32_t col, int big)
{
    int best = 0;
    float bl = 1e9f;
    for (int i = 0; i < MAX_POP; i++) {
        if (!g->pp[i].alive) { best = i; break; }
        if (g->pp[i].life < bl) { bl = g->pp[i].life; best = i; }
    }
    Popup *p = &g->pp[best];
    p->alive = 1; p->big = (uint8_t)big;
    p->x = x; p->y = y; p->vy = big ? -20.0f : -55.0f;
    p->life = p->max_life = big ? 1.6f : 0.9f;
    p->col = col;
    snprintf(p->txt, sizeof p->txt, "%s", txt);
}

static void shake(Game *g, float amt) { if (amt > g->shake) g->shake = amt; }

static void flash(Game *g, float amt, float r, float gg, float b)
{
    if (amt < g->flash) return;
    g->flash = amt; g->flash_r = r; g->flash_g = gg; g->flash_b = b;
}

/* the big one: shockwave rings, sparks, glows, low-poly debris */
static void explode(Game *g, float x, float y, float size, uint32_t c1, uint32_t c2)
{
    ring(g, x, y, 70.0f * size, 0.32f + 0.08f * size, 9.0f * size, c1);
    ring(g, x, y, 40.0f * size, 0.22f, 4.0f * size, 0xFFFFFFB0u);
    pglow(g, x, y, 0, 0, 0.18f + 0.05f * size, 70.0f * size, col_a(col_mix(c1, 0xFFFFFFFFu, 0.5f), 0.9f));
    pglow(g, x, y, 0, 0, 0.45f + 0.1f * size, 140.0f * size, col_a(c1, 0.35f));
    int ns = 10 + (int)(10.0f * size);
    for (int i = 0; i < ns; i++) {
        uint32_t c = (i % 3 == 0) ? 0xFFFFFFFFu : ((i & 1) ? c1 : c2);
        spark(g, x, y, frange(g, 0, TAU), frange(g, 150, 520) * sqrtf(size), frange(g, 0.25f, 0.6f), frange(g, 2.0f, 4.0f), c);
    }
    int ng = 3 + (int)(2.0f * size);
    for (int i = 0; i < ng; i++) {
        float a = frange(g, 0, TAU), s = frange(g, 30, 140) * size;
        pglow(g, x, y, cosf(a) * s, sinf(a) * s, frange(g, 0.3f, 0.6f), frange(g, 18, 36) * size, (i & 1) ? c1 : c2);
    }
    int nd = 3 + (int)(3.0f * size);
    for (int i = 0; i < nd; i++)
        debris(g, x, y, 260.0f * sqrtf(size), frange(g, 5, 9) * (0.7f + 0.3f * size), (i & 1) ? c1 : col_mix(c1, 0x200030FFu, 0.4f));
    shake(g, 2.5f * size);
}

static void hit_sparks(Game *g, float x, float y, float dir, uint32_t col, int n)
{
    for (int i = 0; i < n; i++)
        spark(g, x, y, dir + frange(g, -0.9f, 0.9f), frange(g, 120, 380), frange(g, 0.12f, 0.3f), 2.0f, (i & 1) ? col : 0xFFFFFFFFu);
    pglow(g, x, y, 0, 0, 0.12f, 22.0f, col_a(col, 0.8f));
}

static void confetti(Game *g, float x, float y, int n, float spd)
{
    for (int i = 0; i < n; i++) {
        float a = frange(g, 0, TAU), s = frange(g, 0.3f, 1.0f) * spd;
        Part *p = newpart(g, P_CONF, x, y, cosf(a) * s, sinf(a) * s - 120.0f, frange(g, 1.0f, 2.2f), frange(g, 3, 6), col_hsv(frnd(g), 0.7f, 1.0f, 255));
        p->rot = frange(g, 0, TAU); p->vr = frange(g, -12, 12); p->drag = 1.2f;
    }
}

/* ------------------------------------------------------------------ */
/* terrain                                                              */
/* ------------------------------------------------------------------ */
static void build_terrain(Game *g)
{
    const LevelDef *L = g->L;
    uint32_t s = L->seed;
    if (L->style == STYLE_SKYWAY) {                    /* open sky above, the track's near barrier below */
        for (int i = 0; i < L->nterr; i++) { g->ttop[i] = 34.0f; g->tbot[i] = 446.0f; }
        return;
    }
    for (int i = 0; i < L->nterr; i++) {
        float a = hashf(s + (uint32_t)i * 7919u), b = hashf(s * 31u + (uint32_t)i * 104729u);
        float wav = sinf((float)i * TAU / (float)L->nterr * 3.0f) * 0.5f + 0.5f;
        g->ttop[i] = 28.0f + (a * 0.7f + wav * 0.3f) * 85.0f * L->rough;
        g->tbot[i] = 452.0f - (b * 0.7f + (1.0f - wav) * 0.3f) * 85.0f * L->rough;
    }
    /* a calm stretch where the ship starts */
    g->ttop[0] = g->ttop[1] = g->ttop[L->nterr - 1] = 40.0f;
    g->tbot[0] = g->tbot[1] = g->tbot[L->nterr - 1] = 440.0f;
}

static void corridor(const Game *g, float x, float pad, float *lo, float *hi)
{
    *lo = terr_top(g, x) + pad;
    *hi = terr_bot(g, x) - pad;
    if (*hi < *lo) *lo = *hi = (*lo + *hi) * 0.5f;
}

/* ------------------------------------------------------------------ */
/* enemies                                                              */
/* ------------------------------------------------------------------ */
static int alive_enemies(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_EN; i++) n += g->en[i].alive;
    return n;
}

static int spawn_enemy(Game *g, int type, float x, float y, float delay)
{
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (e->alive) continue;
        memset(e, 0, sizeof *e);
        e->alive = 1; e->type = (uint8_t)type; e->warp = 1;
        e->warp_t = -delay;
        e->x = wrapx(g, x); e->y = y;
        e->hp = e->maxhp = ESTAT[type].hp;
        e->r = ESTAT[type].r;
        e->parent = e->head = -1;
        e->phase = frange(g, 0, TAU);
        e->cd = frange(g, 1.8f, 3.0f);
        e->ang = wd(g, g->px - x) > 0 ? 0.0f : PI_F;
        return i;
    }
    return -1;
}

static void spawn_serpent(Game *g, float x, float y, float delay)
{
    int head = spawn_enemy(g, EN_SERPENT, x, y, delay);
    if (head < 0) return;
    float dir = wd(g, g->px - x) > 0 ? -1.0f : 1.0f;     /* body trails away from the player */
    int prev = head;
    for (int k = 1; k <= 10; k++) {
        int s = spawn_enemy(g, EN_SEG, x + dir * 24.0f * (float)k, y, delay + 0.03f * (float)k);
        if (s < 0) break;
        Enemy *e = &g->en[s];
        e->parent = prev; e->head = head; e->idx = k;
        e->r = 15.0f * (1.0f - 0.045f * (float)k);
        e->ang = g->en[head].ang;
        prev = s;
    }
}

static void spawn_shields(Game *g, int boss, float delay)
{
    for (int k = 0; k < 6; k++) {
        int s = spawn_enemy(g, EN_SHIELD, g->en[boss].x, g->en[boss].y, delay + 0.08f * (float)k);
        if (s < 0) break;
        g->en[s].parent = boss; g->en[s].idx = k;
    }
}

static void spawn_group(Game *g, const SpawnGroup *sg)
{
    int   side = sg->side ? sg->side : ((rnd(g) & 1) ? 1 : -1);
    float dir  = (float)side * (float)g->face;
    float ax   = g->px + dir * frange(g, 430, 620);
    float lo, hi;
    corridor(g, ax, 70.0f, &lo, &hi);
    float ay = frange(g, lo, hi);
    float base_phase = frange(g, 0, TAU);

    for (int k = 0; k < sg->count; k++) {
        float d = 0.07f * (float)k;
        switch (sg->type) {
        case EN_SHARD: {
            float x = ax + frange(g, -110, 110), y = clampf(ay + frange(g, -90, 90), lo, hi);
            spawn_enemy(g, EN_SHARD, x, y, d);
            break;
        }
        case EN_ZIP: {
            int i = spawn_enemy(g, EN_ZIP, ax + dir * 40.0f * (float)k, ay, d * 0.6f);
            if (i >= 0) {
                g->en[i].phase = base_phase + 0.55f * (float)k;
                g->en[i].aux = ay;
                g->en[i].vx = -dir * 280.0f;
            }
            break;
        }
        case EN_SPORE: {
            float x = ax + frange(g, -260, 260), l2, h2;
            corridor(g, x, 80.0f, &l2, &h2);
            spawn_enemy(g, EN_SPORE, x, frange(g, l2, h2), d * 3.0f);
            break;
        }
        case EN_SERPENT:
            spawn_serpent(g, ax + dir * 320.0f * (float)k, clampf(ay, 160.0f, 320.0f), d * 6.0f);
            break;
        case EN_UFO: case EN_BOMB: case EN_BLOB: {
            float x = ax + frange(g, -220, 220), l2, h2;
            corridor(g, x, 70.0f, &l2, &h2);
            int e = spawn_enemy(g, sg->type, x, frange(g, l2, h2), d * 3.0f);
            if (e >= 0 && sg->type == EN_BOMB) g->en[e].fuse = frange(g, 5.5f, 7.0f);
            break;
        }
        case EN_SNIP: case EN_PINWHEEL: {
            float x = ax + frange(g, -220, 220), l2, h2;
            corridor(g, x, 70.0f, &l2, &h2);
            int e = spawn_enemy(g, sg->type, x, frange(g, l2, h2), d * 3.0f);
            if (e >= 0) g->en[e].cd = frange(g, 1.0f, 2.0f);
            break;
        }
        case EN_PLANE: {                                    /* squadrons in a V, flying across */
            int e = spawn_enemy(g, EN_PLANE, ax + dir * 34.0f * (float)(k / 2 + 1) * 1.2f, clampf(ay + ((k & 1) ? 1.0f : -1.0f) * 30.0f * (float)((k + 1) / 2), 70.0f, 410.0f), d * 0.5f);
            if (e >= 0) { g->en[e].vx = -dir * 380.0f; g->en[e].cd = 0.8f + 0.2f * (float)k; }
            break;
        }
        case EN_BOSS3: {
            int b = spawn_enemy(g, EN_BOSS3, g->px + (float)g->face * 450.0f, 200.0f, 1.9f);
            if (b >= 0) {
                g->boss = b;
                g->en[b].cd = 2.4f;
                g->en[b].ang = PI_F;
                g->boss_shown_hp = 0;
            }
            break;
        }
        case EN_BOSS2: {
            int b = spawn_enemy(g, EN_BOSS2, g->px + (float)g->face * 470.0f, 190.0f, 1.9f);
            if (b >= 0) {
                g->boss = b;
                g->en[b].cd = 2.6f;
                g->boss_shown_hp = 0;
            }
            break;
        }
        case EN_BOSS: {
            int b = spawn_enemy(g, EN_BOSS, g->px + (float)g->face * 470.0f, 240.0f, 1.9f);
            if (b >= 0) {
                g->boss = b;
                g->en[b].cd = 2.5f;
                g->boss_shown_hp = 0;
                spawn_shields(g, b, 2.3f);
            }
            break;
        }
        default: break;
        }
    }
}

static void fire_ebullet(Game *g, float x, float y, float ang, float spd, int big)
{
    for (int i = 0; i < MAX_EB; i++) {
        EBullet *b = &g->eb[i];
        if (b->alive) continue;
        b->alive = 1; b->big = (uint8_t)big;
        b->x = x; b->y = y;
        b->vx = cosf(ang) * spd; b->vy = sinf(ang) * spd;
        b->life = 7.0f;
        return;
    }
}

static void drop_crystals(Game *g, float x, float y, int n, int shield_chance_pct)
{
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < MAX_CRY; i++) {
            Crystal *c = &g->cr[i];
            if (c->alive) continue;
            float a = frange(g, 0, TAU), s = frange(g, 80, 240);
            c->alive = 1; c->kind = 0;
            c->x = x; c->y = y; c->vx = cosf(a) * s; c->vy = sinf(a) * s;
            c->life = 8.0f; c->spin = frange(g, 0, TAU);
            break;
        }
    }
    if ((int)(rnd(g) % 100) < shield_chance_pct) {
        for (int i = 0; i < MAX_CRY; i++) {
            Crystal *c = &g->cr[i];
            if (c->alive) continue;
            c->alive = 1; c->kind = 1;
            c->x = x; c->y = y; c->vx = frange(g, -60, 60); c->vy = -80;
            c->life = 10.0f; c->spin = 0;
            break;
        }
    }
}

static void add_score(Game *g, long pts, float x, float y, uint32_t col)
{
    g->score += pts;
    char buf[24];
    snprintf(buf, sizeof buf, "+%ld", pts);
    popup(g, sxof(g, x), y - 10.0f, buf, col, 0);
}

static void announce_combo(Game *g)
{
    static const int   MS[]   = {10, 25, 50, 100, 200};
    static const char *TXT[]  = {"Nice!", "Great!", "Amazing!", "Incredible!", "Unstoppable!"};
    for (int i = 4; i >= 0; i--) {
        if (g->chain >= MS[i] && g->milestone < MS[i]) {
            g->milestone = MS[i];
            char buf[24];
            snprintf(buf, sizeof buf, "%s %d in a row", TXT[i], MS[i]);
            popup(g, VW * 0.5f, 150.0f, buf, col_hsv(0.08f * (float)i + 0.85f, 0.6f, 1.0f, 255), 1);
            sfxp(g, SFX_COMBO, 128 + 12 * i);
            ring(g, g->px, g->py, 260.0f, 0.6f, 14.0f, col_hsv(0.1f * (float)i, 0.7f, 1.0f, 255));
            ring(g, g->px, g->py, 180.0f, 0.5f, 8.0f, 0xFFFFFFFFu);
            confetti(g, g->px, g->py, 30, 420.0f);
            g->mult_pulse = 1.5f;
            break;
        }
    }
}

static void kill_enemy(Game *g, int i, int scored);

/* does a point (plus some slack) touch this enemy? The pencil boss is a long capsule, everything else a circle */
static int hits_enemy(const Game *g, const Enemy *e, float x, float y, float extra)
{
    float r = e->r + extra;
    if (e->type == EN_BOSS3) {
        float tx, ty, ex, ey;
        pencil_ends(e, &tx, &ty, &ex, &ey);
        float ax = wd(g, tx - x), ay = ty - y, bx = wd(g, ex - x), by = ey - y;
        float dx = bx - ax, dy = by - ay, t = clampf(-(ax * dx + ay * dy) / (dx * dx + dy * dy + 0.001f), 0, 1);
        float px = ax + dx * t, py = ay + dy * t;
        return px * px + py * py < r * r;
    }
    float dx = wd(g, e->x - x), dy = e->y - y;
    return dx * dx + dy * dy < r * r;
}

static void boss_start_death(Game *g, int i)
{
    Enemy *e = &g->en[i];
    if (g->boss_dying > 0) return;
    g->boss_dying = 3.2f;
    e->hp = 0;
    g->slowmo = 0.3f; g->slowmo_t = 1.4f;
    flash(g, 1.0f, 1, 1, 1);
    sfx(g, SFX_EXPL_L);
    for (int k = 0; k < MAX_EN; k++)                     /* everything else in the room goes too */
        if (k != i && g->en[k].alive) kill_enemy(g, k, 1);
    for (int k = 0; k < MAX_EB; k++) g->eb[k].alive = 0;
}

static void kill_enemy(Game *g, int i, int scored)
{
    Enemy *e = &g->en[i];
    if (!e->alive) return;
    const EnemyStats *s = &ESTAT[e->type];
    if (IS_BOSS(e->type)) { boss_start_death(g, i); return; }

    e->alive = 0;
    float size = 1.0f;
    int   shield_pct = 0;
    switch (e->type) {
    case EN_SHARD:   size = 1.15f; shield_pct = 2;  sfxp(g, SFX_EXPL_S, 120 + (int)(rnd(g) % 24)); break;
    case EN_ZIP:     size = 0.85f; shield_pct = 1;  sfxp(g, SFX_EXPL_S, 140 + (int)(rnd(g) % 24)); break;
    case EN_SPORE:   size = 2.0f;  shield_pct = 15; sfx(g, SFX_EXPL_M); g->hitstop = 0.05f; break;
    case EN_SERPENT: size = 2.6f;  shield_pct = 60; sfx(g, SFX_EXPL_M); g->hitstop = 0.07f; break;
    case EN_SEG:     size = 1.0f;  sfxp(g, SFX_EXPL_S, 110 + e->idx * 6); break;
    case EN_SHIELD:  size = 1.6f;  sfx(g, SFX_EXPL_M); g->hitstop = 0.04f; break;
    case EN_UFO:     size = 1.7f;  shield_pct = 12; sfx(g, SFX_EXPL_M); g->hitstop = 0.04f; break;
    case EN_BOMB:    size = 1.3f;  shield_pct = 4;  sfxp(g, SFX_EXPL_S, 100); break;
    case EN_BLOB:    size = 1.6f;  shield_pct = 8;  sfxp(g, SFX_EXPL_M, 150); break;
    case EN_BLOBLET: size = 0.8f;  sfxp(g, SFX_EXPL_S, 170 + (int)(rnd(g) % 20)); break;
    case EN_SNIP:    size = 1.5f;  shield_pct = 6;  sfx(g, SFX_EXPL_M); break;
    case EN_PLANE:   size = 1.1f;  shield_pct = 3;  sfxp(g, SFX_EXPL_S, 130); break;
    case EN_PINWHEEL: size = 2.0f; shield_pct = 20; sfx(g, SFX_EXPL_M); g->hitstop = 0.05f; break;
    default: break;
    }
    explode(g, e->x, e->y, size, s->c_light, s->c_accent);
    drop_crystals(g, e->x, e->y, s->crystals, shield_pct);

    if (e->type == EN_SERPENT) {                          /* the whole body goes up in a chain reaction */
        for (int k = 0; k < MAX_EN; k++)
            if (g->en[k].alive && g->en[k].type == EN_SEG && g->en[k].head == i)
                g->en[k].fuse = 0.05f + 0.07f * (float)g->en[k].idx;
        shake(g, 8.0f);
    }
    if (e->type == EN_BLOB && g->boss_dying <= 0)       /* a blob splits into two little ones */
        for (int k = 0; k < 2; k++) {
            float side = k ? 1.0f : -1.0f;
            int b = spawn_enemy(g, EN_BLOBLET, e->x + side * 12.0f, e->y, 0);
            if (b >= 0) {
                Enemy *o = &g->en[b];
                o->warp = 0; o->warp_t = 1.0f;
                o->vx = side * 190.0f; o->vy = -120.0f;
                o->hitcd = 0.25f;
            }
        }
    if (e->type == EN_SEG)                                /* close the gap in the chain */
        for (int k = 0; k < MAX_EN; k++)
            if (g->en[k].alive && g->en[k].type == EN_SEG && g->en[k].parent == i) g->en[k].parent = e->parent;

    if (scored) {
        long pts = (long)(s->score * g->mult + 0.5f);
        add_score(g, pts, e->x, e->y, s->c_light);
        g->mult += s->mult;
        if (g->mult > 30.0f) g->mult = 30.0f;
        if (g->mult > g->best_mult) g->best_mult = g->mult;
        g->mult_pulse = 1.0f;
        g->chain++;
        g->chain_t = 2.4f;
        if (g->chain > g->max_chain) g->max_chain = g->chain;
        g->kills++;
        announce_combo(g);
    }
}

static void hurt_enemy(Game *g, int i, float dmg, float kx, float ky)
{
    Enemy *e = &g->en[i];
    if (!e->alive || e->warp) return;
    if (IS_BOSS(e->type) && g->boss_dying > 0) return;
    e->hp -= dmg;
    e->flash = IS_BOSS(e->type) ? 0.45f : 0.8f;
    switch (e->type) {
    case EN_SHARD: case EN_ZIP: case EN_SPORE: case EN_UFO: case EN_BOMB: case EN_BLOB: case EN_BLOBLET:
    case EN_SNIP: case EN_PLANE: case EN_PINWHEEL:
        e->vx += kx; e->vy += ky; break;
    default: break;
    }
    sfxp(g, SFX_HIT, 110 + (int)(rnd(g) % 40));
    if (e->hp <= 0) kill_enemy(g, i, 1);
}

static void hurt_player(Game *g)
{
    if (!g->alive || g->inv_t > 0.0f || g->level_done) return;
    g->wave_hit = 1;
    g->chain = 0; g->chain_t = 0; g->milestone = 0;
    if (g->mult > 1.0f) {
        g->mult = 1.0f + (g->mult - 1.0f) * 0.5f;
        popup(g, sxof(g, g->px), g->py - 40.0f, "multiplier halved!", 0xFF5070FFu, 0);
    }
    g->shields--;
    flash(g, 0.6f, 1.0f, 0.15f, 0.25f);
    shake(g, 12.0f);
    g->hitstop = 0.06f;
    for (int k = 0; k < 18; k++) spark(g, g->px, g->py, frange(g, 0, TAU), frange(g, 150, 450), 0.4f, 3.0f, (k & 1) ? 0xFF4060FFu : 0xFFFFFFFFu);
    ring(g, g->px, g->py, 90.0f, 0.35f, 8.0f, 0xFF4060FFu);
    if (g->shields > 0) {
        g->inv_t = 1.5f;
        sfx(g, SFX_HURT);
        return;
    }
    /* destroyed */
    g->alive = 0;
    g->blade_on = 0; g->lance_on = 0; g->lance_charge = 0;
    explode(g, g->px, g->py, 3.0f, 0xFFB347FFu, 0xFF4FA0FFu);
    sfx(g, SFX_DIE);
    shake(g, 18.0f);
    g->slowmo = 0.35f; g->slowmo_t = 0.8f;
    if (g->lives > 0) { g->lives--; g->respawn_t = 1.8f; }
    else { g->respawn_t = 0; g->dead_t = 2.6f; }
}

static void respawn(Game *g)
{
    g->alive = 1;
    g->shields = SHIELDS_MAX;
    g->boost_charges = BOOST_MAX;
    g->inv_t = 2.5f;
    g->pvx = g->pvy = 0;
    g->heat = 0; g->overheat = 0;
    g->energy = clampf(g->energy, 50.0f, ENERGY_MAX);
    float lo, hi;
    corridor(g, g->px, 40.0f, &lo, &hi);
    g->py = clampf(g->py, lo, hi);
    for (int i = 0; i < MAX_EB; i++) g->eb[i].alive = 0;      /* clear the air */
    ring(g, g->px, g->py, 380.0f, 0.7f, 22.0f, 0x7DF9FFFFu);
    ring(g, g->px, g->py, 260.0f, 0.5f, 10.0f, 0xFFFFFFFFu);
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive || e->warp || IS_BOSS(e->type) || e->type == EN_SHIELD) continue;
        float dx = wd(g, e->x - g->px), dy = e->y - g->py;
        if (dx * dx + dy * dy < 220.0f * 220.0f) { e->vx += dx * 4.0f; e->vy += dy * 4.0f; }
    }
    sfx(g, SFX_POWERUP);
    g->trail_n = 0;
}

/* ------------------------------------------------------------------ */
/* weapons                                                              */
/* ------------------------------------------------------------------ */
static void out_of_energy(Game *g, int w)
{
    if (g->empty_t > 0) return;
    g->empty_t = 0.4f; g->empty_wpn = (float)w;
    sfx(g, SFX_EMPTY);
    popup(g, sxof(g, g->px), g->py - 34.0f, "no energy!", 0xFF8090FFu, 0);
}

static void fire_blaster(Game *g)
{
    for (int i = 0; i < MAX_PB; i++) {
        Bolt *b = &g->pb[i];
        if (b->alive) continue;
        g->shot_side = !g->shot_side;
        b->alive = 1;
        b->x = wrapx(g, g->px + (float)g->face * 36.0f);
        b->y = g->py + (g->shot_side ? -5.0f : 5.0f);
        b->vx = (float)g->face * BOLT_SPEED + g->pvx * 0.3f;
        b->vy = g->pvy * 0.12f + frange(g, -22, 22);
        b->life = 0.6f;
        b->dmg = 1.0f;
        break;
    }
    pglow(g, g->px + (float)g->face * 42.0f, g->py, g->pvx, g->pvy, 0.07f, 40.0f, col_a(COL_A, 0.95f));
    spark(g, g->px + (float)g->face * 42.0f, g->py, (g->face > 0 ? 0.0f : PI_F) + frange(g, -0.5f, 0.5f), frange(g, 200, 420), 0.12f, 3.0f, 0xFFFFFFFFu);
    spark(g, g->px + (float)g->face * 42.0f, g->py, (g->face > 0 ? 0.0f : PI_F) + frange(g, -0.5f, 0.5f), frange(g, 200, 420), 0.1f, 2.0f, COL_A);
    sfxp(g, SFX_SHOT, 118 + (int)(rnd(g) % 22));
    g->heat += 0.042f;
    g->pvx -= (float)g->face * 6.0f;
    g->wpn_flash[0] = 1.0f;
    if (g->heat >= 1.0f) {
        g->heat = 1.0f; g->overheat = 1;
        sfx(g, SFX_OVERHEAT);
        popup(g, sxof(g, g->px), g->py - 34.0f, "overheat!", 0xFF7040FFu, 0);
        for (int k = 0; k < 10; k++)
            newpart(g, P_SMOKE, g->px, g->py, frange(g, -60, 60), frange(g, -160, -60), frange(g, 0.5f, 0.9f), frange(g, 8, 14), 0xFFFFFF90u);
    }
}


static void fire_missiles(Game *g)
{
    /* the swarm only locks on to 3 out of every 4 baddies on screen (nearest first) */
    int onscreen = 0;
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->warp) continue;
        float sx = sxof(g, e->x);
        if (sx > -10.0f && sx < VW + 10.0f && e->y > -10.0f && e->y < VH + 10.0f) onscreen++;
    }
    int want = (onscreen * 3) / 4;
    if (onscreen > 0 && want < 1) want = 1;
    if (want > 8) want = 8;
    int targets[8], nt = 0;
    int used[MAX_EN];
    memset(used, 0, sizeof used);
    for (int k = 0; k < want; k++) {
        int best = -1;
        float bd = 1e12f;
        for (int i = 0; i < MAX_EN; i++) {
            const Enemy *e = &g->en[i];
            if (!e->alive || e->warp || used[i]) continue;
            float sx = sxof(g, e->x);
            if (sx < -10.0f || sx > VW + 10.0f) continue;
            float dx = wd(g, e->x - g->px), dy = e->y - g->py, d = dx * dx + dy * dy;
            if (dx * (float)g->face < 0) d *= 1.6f;       /* prefer what's in front */
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) break;
        used[best] = 1;
        targets[nt++] = best;
    }
    int launch = nt > 0 ? nt : 3;                         /* nothing to lock on to: a few go out anyway */
    float fwd = g->face > 0 ? 0.0f : PI_F;
    for (int k = 0; k < launch; k++) {
        for (int i = 0; i < MAX_MS; i++) {
            Missile *m = &g->ms[i];
            if (m->alive) continue;
            float side = (k & 1) ? 1.0f : -1.0f;
            float a = fwd - (float)g->face * side * (1.1f + 0.14f * (float)(k / 2));
            m->alive = 1;
            m->x = g->px; m->y = g->py + side * 6.0f;
            m->speed = 330.0f;
            m->vx = cosf(a) * m->speed + g->pvx * 0.5f; m->vy = sinf(a) * m->speed;
            m->life = 2.6f;
            m->target = k < nt ? targets[k] : -1;
            m->wob = frange(g, 0, TAU);
            break;
        }
    }
    g->energy -= COST_MISSILE;
    g->wpn_flash[2] = 1.0f;
    sfx(g, SFX_MISSILE);
    ring(g, g->px, g->py, 70.0f, 0.25f, 6.0f, COL_X);
    shake(g, 3.0f);
}

static void missile_explode(Game *g, Missile *m)
{
    m->alive = 0;
    explode(g, m->x, m->y, 1.2f, COL_X, 0xFFD15CFFu);
    sfxp(g, SFX_EXPL_S, 100 + (int)(rnd(g) % 20));
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive || e->warp) continue;
        float dx = wd(g, e->x - m->x), dy = e->y - m->y;
        float rr = 40.0f + e->r;
        if (e->type == EN_BOSS3) { if (hits_enemy(g, e, m->x, m->y, 40.0f)) hurt_enemy(g, i, 6.0f, 0, 0); continue; }
        if (dx * dx + dy * dy < rr * rr) hurt_enemy(g, i, 6.0f, dx * 2.0f, dy * 2.0f);
    }
}

static int lance_hits(const Game *g, float x, float y, float r)
{
    float rel = wd(g, x - g->px) * (float)g->lance_dir;
    return rel > -r - 10.0f && rel < LANCE_LEN && fabsf(y - g->py) < LANCE_W * 0.5f + r;
}

static void lance_damage(Game *g)
{
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive || e->warp) continue;
        int lh = lance_hits(g, e->x, e->y, e->r);
        if (!lh && e->type == EN_BOSS3) {
            float tx, ty, ex, ey;
            pencil_ends(e, &tx, &ty, &ex, &ey);
            lh = lance_hits(g, tx, ty, e->r) || lance_hits(g, ex, ey, e->r);
        }
        if (lh) {
            hit_sparks(g, e->x, e->y, g->lance_dir > 0 ? 0.0f : PI_F, COL_Y, 6);
            hurt_enemy(g, i, 9.0f, (float)g->lance_dir * 120.0f, 0);
        }
    }
    for (int i = 0; i < MAX_EB; i++) {
        EBullet *b = &g->eb[i];
        if (b->alive && lance_hits(g, b->x, b->y, 6.0f)) {
            b->alive = 0;
            spark(g, b->x, b->y, frange(g, 0, TAU), 250, 0.2f, 2.0f, COL_Y);
        }
    }
}

static void fire_lance(Game *g)
{
    g->lance_on = 1;
    g->lance_t = 0.5f;
    g->lance_tick = 0;
    g->lance_dir = g->face;
    g->pvx -= (float)g->face * 260.0f;
    shake(g, 13.0f);
    g->zoom_kick = 0.045f;
    flash(g, 0.35f, 0.4f, 0.9f, 1.0f);
    sfx(g, SFX_LANCE);
    ring(g, g->px + (float)g->face * 42.0f, g->py, 150.0f, 0.3f, 14.0f, COL_Y);
    g->wpn_flash[3] = 1.0f;
}

/* flying the rocket: stick or d-pad, L / R to turn around, ZL / ZR to boost. On the title screen (title = 1)
 * the ship flies freely around the screen and L / R are left for picking the level. */
static void move_ship(Game *g, const Input *in, uint32_t pressed, float dt, int title)
{
    uint32_t held = in->held;
    float ix = in->sx, iy = in->sy;
    if (held & BTN_LEFT)  ix = -1;
    if (held & BTN_RIGHT) ix = 1;
    if (held & BTN_UP)    iy = -1;
    if (held & BTN_DOWN)  iy = 1;
    float im = sqrtf(ix * ix + iy * iy);
    if (im > 1.0f) { ix /= im; iy /= im; im = 1.0f; }
    float spd = SHIP_SPEED * (g->blade_on ? 0.8f : 1.0f);
    float acc = 9.0f;
    g->pvx = approach(g->pvx, ix * spd, dt * acc);
    g->pvy = approach(g->pvy, iy * spd, dt * acc);

    /* --- boost: a quick dash, two charges that each come back on their own --- */
    if ((pressed & BTN_BOOST) && g->boost_charges > 0) {
        float bx = ix, by = iy;
        if (im < 0.2f) { bx = (float)g->face; by = 0; }
        float bl = sqrtf(bx * bx + by * by);
        g->boost_vx = bx / bl * BOOST_SPEED; g->boost_vy = by / bl * BOOST_SPEED;
        g->boost_t = BOOST_TIME;
        g->boost_charges--;
        if (g->boost_charges < BOOST_MAX && g->boost_recharge <= 0) g->boost_recharge = 0;
        sfxp(g, SFX_BLADE, 175);
        sfxp(g, SFX_FLIP, 90);
        shake(g, 3.0f);
        for (int k = 0; k < 4; k++)
            newpart(g, P_SMOKE, g->px - bx / bl * 30.0f, g->py - by / bl * 30.0f, frange(g, -60, 60) - bx / bl * 80.0f, frange(g, -60, 60) - by / bl * 80.0f, 0.5f, frange(g, 8, 12), 0xFFFFFFC0u);
    }
    if (g->boost_t > 0) {
        float k = g->boost_t / BOOST_TIME;
        g->pvx = lerpf(g->pvx, g->boost_vx, k);
        g->pvy = lerpf(g->pvy, g->boost_vy, k);
        g->boost_t -= dt;
        if ((g->frame & 1) == 0)                          /* speed lines */
            spark(g, g->px + frange(g, -10, 10), g->py + frange(g, -14, 14), atan2f(-g->boost_vy, -g->boost_vx), frange(g, 300, 500), 0.18f, 2.0f, 0xFFFFFFFFu);
    }
    if (g->boost_charges < BOOST_MAX) {
        g->boost_recharge += dt;
        if (g->boost_recharge >= BOOST_RECHARGE) {
            g->boost_charges++;
            g->boost_recharge = 0;
            sfxp(g, SFX_CRYSTAL, 100);
        }
    } else g->boost_recharge = 0;

    if (title) {                                         /* free flight around the title screen */
        g->title_sx = clampf(g->title_sx + g->pvx * dt, 45.0f, VW - 45.0f);
        g->py = clampf(g->py + g->pvy * dt, 40.0f, VH - 40.0f);
        g->px = wrapx(g, g->camx - VW * 0.5f + g->title_sx);
        if (ix * (float)g->face < -0.5f && g->flip_t <= 0) {    /* turn to face the way you're flying */
            g->face = -g->face; g->flip_t = 1.0f; g->flip_dir = 1.0f;
            sfxp(g, SFX_FLIP, 130);
        }
    } else {
        g->px = wrapx(g, g->px + g->pvx * dt);
        g->py += g->pvy * dt;
        float top = terr_top(g, g->px) + 16.0f, bot = terr_bot(g, g->px) - 16.0f;
        if (g->py < top) {
            if (g->pvy < -150.0f) for (int k = 0; k < 5; k++) spark(g, g->px, top - 10, frange(g, 0.2f, 2.9f), frange(g, 100, 260), 0.25f, 2.0f, 0xFFE0F0FFu);
            g->py = top; g->pvy = fabsf(g->pvy) * 0.4f;
        }
        if (g->py > bot) {
            if (g->pvy > 150.0f) for (int k = 0; k < 5; k++) spark(g, g->px, bot + 10, frange(g, -2.9f, -0.2f), frange(g, 100, 260), 0.25f, 2.0f, 0xFFE0F0FFu);
            g->py = bot; g->pvy = -fabsf(g->pvy) * 0.4f;
        }
        /* --- turn around --- */
        if (pressed & (BTN_L | BTN_R)) {
            g->face = -g->face;
            g->flip_t = 1.0f;
            g->flip_dir = (pressed & BTN_L) ? -1.0f : 1.0f;
            sfxp(g, SFX_FLIP, (pressed & BTN_L) ? 120 : 136);
            ring(g, g->px, g->py, 46.0f, 0.2f, 4.0f, 0xFFE9A0FFu);
            for (int k = 0; k < 10; k++)
                spark(g, g->px, g->py, frange(g, 0, TAU), frange(g, 120, 300), 0.2f, 2.0f, (k & 1) ? 0xFFE9A0FFu : COL_A);
        }
    }
    g->tilt = approach(g->tilt, clampf(g->pvy / SHIP_SPEED, -1, 1) * 0.32f, dt * 10.0f);
    if (g->flip_t > 0) g->flip_t -= dt / 0.24f;
}

static void update_player(Game *g, const Input *in, uint32_t pressed, float dt)
{
    if (!g->alive) {
        if (g->respawn_t > 0) {
            g->respawn_t -= dt;
            if (g->respawn_t <= 0) respawn(g);
        }
        return;
    }
    uint32_t held = in->held;

    move_ship(g, in, pressed, dt, 0);

    /* --- A: Pulse Blaster --- */
    g->shot_cd -= dt;
    int firing = 0;
    if ((held & BTN_A) && !g->overheat && g->lance_charge <= 0) {
        firing = 1;
        if (g->shot_cd <= 0) { fire_blaster(g); g->shot_cd += 1.0f / 13.0f; }
    }
    if (g->shot_cd < 0) g->shot_cd = 0;
    if (g->overheat) {
        g->heat -= dt * 0.9f;
        if ((g->frame & 3) == 0) newpart(g, P_SMOKE, g->px - (float)g->face * 6.0f, g->py - 6.0f, frange(g, -30, 30), frange(g, -110, -50), 0.6f, 8.0f, 0xFFFFFF70u);
        if (g->heat <= 0) { g->heat = 0; g->overheat = 0; }
    } else if (!firing) {
        g->heat -= dt * 0.75f;
        if (g->heat < 0) g->heat = 0;
    }

    /* --- B: Nova Blade --- */
    if ((pressed & BTN_B) && !g->blade_on) {
        if (g->energy >= COST_BLADE) {
            g->blade_on = 1; g->blade_t = 0;
            g->energy -= COST_BLADE;
            sfx(g, SFX_BLADE);
            ring(g, g->px, g->py, BLADE_R + 20.0f, 0.3f, 8.0f, COL_B);
        } else out_of_energy(g, 1);
    }
    if (g->blade_on) {
        g->blade_t += dt;
        g->blade_ang += dt * 15.0f * (float)g->face;
        g->energy -= DRAIN_BLADE * dt;
        g->wpn_flash[1] = 1.0f;
        sfxp(g, SFX_BLADE, 120 + (int)(rnd(g) % 16));
        if (g->energy <= 0) { g->energy = 0; g->blade_on = 0; }
        if (!(held & BTN_B) && g->blade_t > 0.4f) g->blade_on = 0;
        for (int i = 0; i < MAX_EN; i++) {
            Enemy *e = &g->en[i];
            if (!e->alive || e->warp) continue;
            float dx = wd(g, e->x - g->px), dy = e->y - g->py;
            if (hits_enemy(g, e, g->px, g->py, BLADE_R) && e->hitcd <= 0) {
                e->hitcd = 0.1f;
                float d = sqrtf(dx * dx + dy * dy) + 0.01f;
                hit_sparks(g, e->x, e->y, atan2f(dy, dx), COL_B, 5);
                hurt_enemy(g, i, 3.0f, dx / d * 260.0f, dy / d * 260.0f);
            }
        }
        for (int i = 0; i < MAX_EB; i++) {
            EBullet *b = &g->eb[i];
            if (!b->alive) continue;
            float dx = wd(g, b->x - g->px), dy = b->y - g->py;
            if (dx * dx + dy * dy < (BLADE_R + 8.0f) * (BLADE_R + 8.0f)) {
                b->alive = 0;
                for (int k = 0; k < 4; k++) spark(g, b->x, b->y, atan2f(dy, dx) + frange(g, -0.6f, 0.6f), 300, 0.2f, 2.0f, COL_B);
                g->score += (long)(5.0f * g->mult);
                sfxp(g, SFX_DEFLECT, 120 + (int)(rnd(g) % 30));
            }
        }
    }

    /* --- X: Swarm Missiles --- */
    if (pressed & BTN_X) {
        if (g->energy >= COST_MISSILE) fire_missiles(g);
        else out_of_energy(g, 2);
    }

    /* --- Y: Prism Lance (short charge, then a screen-wide beam) --- */
    if ((pressed & BTN_Y) && !g->lance_on && g->lance_charge <= 0) {
        if (g->energy >= COST_LANCE) {
            g->energy -= COST_LANCE;
            g->lance_charge = 0.3f;
            sfx(g, SFX_CHARGE);
        } else out_of_energy(g, 3);
    }
    if (g->lance_charge > 0) {
        g->lance_charge -= dt;
        g->wpn_flash[3] = 1.0f;
        for (int k = 0; k < 3; k++) {                       /* particles rushing into the nose */
            float a = frange(g, 0, TAU), r = frange(g, 50, 90);
            float nx = g->px + (float)g->face * 42.0f, ny = g->py;
            Part *p = newpart(g, P_SPARK, nx + cosf(a) * r, ny + sinf(a) * r, -cosf(a) * r * 5.0f, -sinf(a) * r * 5.0f, 0.18f, 2.0f,
                              (k & 1) ? COL_Y : col_hsv(frnd(g), 0.6f, 1.0f, 255));
            p->drag = 0;
        }
        if (g->lance_charge <= 0) { g->lance_charge = 0; fire_lance(g); }
    }
    if (g->lance_on) {
        g->lance_t -= dt;
        g->lance_tick -= dt;
        if (g->lance_tick <= 0 && g->lance_t > 0.1f) { lance_damage(g); g->lance_tick = 0.12f; }
        if ((g->frame & 1) == 0) {
            float d = frange(g, 40, LANCE_LEN);
            spark(g, g->px + (float)g->lance_dir * d, g->py + frange(g, -14, 14), frange(g, 0, TAU), frange(g, 100, 300), 0.3f, 2.5f,
                  col_hsv(frnd(g), 0.5f, 1.0f, 255));
        }
        if (g->lance_t <= 0) g->lance_on = 0;
    }

    /* --- engine trail --- */
    g->trail_t -= dt;
    if (g->trail_t <= 0) {
        g->trail_t = 1.0f / 60.0f;
        memmove(&g->trail[1], &g->trail[0], sizeof(g->trail[0]) * (TRAIL_N - 1));
        g->trail[0][0] = g->px - (float)g->face * 40.0f * cosf(g->tilt);
        g->trail[0][1] = g->py - (float)g->face * 40.0f * sinf(g->tilt);
        if (g->trail_n < TRAIL_N) g->trail_n++;
    }
    if (g->inv_t > 0) g->inv_t -= dt;
}

/* ------------------------------------------------------------------ */
/* world update                                                         */
/* ------------------------------------------------------------------ */
static void update_boss(Game *g, int i, float dt)
{
    Enemy *e = &g->en[i];
    int phase2 = e->hp < e->maxhp * 0.5f;
    if (g->boss_dying > 0) return;

    if (phase2 && e->idx == 0) {                         /* entering overdrive */
        e->idx = 1;
        e->attack = 0;
        e->cd = 1.5f;
        popup(g, VW * 0.5f, 160.0f, "GRRR!!", 0xFF4FD8FFu, 1);
        sfx(g, SFX_BOSS);
        ring(g, e->x, e->y, 300.0f, 0.8f, 20.0f, 0xFF4FD8FFu);
        flash(g, 0.5f, 1.0f, 0.3f, 0.8f);
        shake(g, 12.0f);
        spawn_shields(g, i, 0.3f);
    }

    e->phase += dt * (phase2 ? 1.3f : 0.7f);
    float dx = wd(g, g->px - e->x);
    float side = dx > 0 ? -1.0f : 1.0f;
    float tx = g->px + side * 330.0f, ty = 240.0f + sinf(e->t * 0.7f) * 70.0f;
    float mx = wd(g, tx - e->x), my = ty - e->y;
    float tvx = clampf(mx * 1.2f, -130.0f, 130.0f), tvy = clampf(my * 1.5f, -110.0f, 110.0f);
    e->vx = approach(e->vx, tvx, dt * 2.0f);
    e->vy = approach(e->vy, tvy, dt * 2.0f);
    e->x = wrapx(g, e->x + e->vx * dt);
    e->y += e->vy * dt;
    float aim = atan2f(g->py - e->y, dx);

    /* spiral attack in progress */
    if (e->aux > 0) {
        e->aux -= dt;
        e->fuse -= dt;
        if (e->fuse <= 0) {
            e->fuse = 0.07f;
            float a = e->t * 4.2f;
            for (int k = 0; k < 3; k++) fire_ebullet(g, e->x, e->y, a + TAU * (float)k / 3.0f, 170.0f, 0);
            sfxp(g, SFX_ESHOT, 150);
        }
        return;
    }

    e->cd -= dt;
    if (e->cd > 0 || !g->alive) return;
    static const int P1[] = {0, 1, 2, 0, 1};
    static const int P2[] = {3, 4, 1, 2, 3, 1};
    int atk = phase2 ? P2[e->attack % 6] : P1[e->attack % 5];
    e->attack++;
    e->flash = 0.35f;
    switch (atk) {
    case 0:                                              /* ring burst */
        for (int k = 0; k < 16; k++) fire_ebullet(g, e->x, e->y, e->phase + TAU * (float)k / 16.0f, 165.0f, 1);
        ring(g, e->x, e->y, 130.0f, 0.3f, 8.0f, 0xFF4FD8FFu);
        sfx(g, SFX_ESHOT);
        e->cd = phase2 ? 1.5f : 1.9f;
        break;
    case 1:                                              /* aimed fan */
        for (int k = -2; k <= 2; k++) fire_ebullet(g, e->x, e->y, aim + 0.17f * (float)k, 230.0f, 0);
        sfx(g, SFX_ESHOT);
        e->cd = phase2 ? 1.0f : 1.5f;
        break;
    case 2: {                                            /* call in drones */
        int n = 0;
        for (int k = 0; k < MAX_EN; k++) n += g->en[k].alive && g->en[k].type == EN_SHARD;
        for (int k = 0; k < 4 && n < 10; k++, n++) {
            float a = TAU * (float)k / 4.0f + e->phase;
            spawn_enemy(g, EN_SHARD, e->x + cosf(a) * 150.0f, clampf(e->y + sinf(a) * 120.0f, 90.0f, 390.0f), 0.1f * (float)k);
        }
        e->cd = 2.0f;
        break;
    }
    case 3:                                              /* double ring */
        for (int k = 0; k < 22; k++) fire_ebullet(g, e->x, e->y, e->phase + TAU * (float)k / 22.0f, 175.0f, 1);
        for (int k = 0; k < 22; k++) fire_ebullet(g, e->x, e->y, e->phase + TAU * ((float)k + 0.5f) / 22.0f, 120.0f, 0);
        ring(g, e->x, e->y, 160.0f, 0.35f, 10.0f, 0xFF4FD8FFu);
        sfx(g, SFX_ESHOT);
        e->cd = 2.0f;
        break;
    case 4:                                              /* spiral */
        e->aux = 2.0f; e->fuse = 0;
        e->cd = 2.6f;
        break;
    }
}

/* a fuse bomb going off: a ring of bullets (shoot it first and it just pops) */
static void bomb_burst(Game *g, int i)
{
    Enemy *e = &g->en[i];
    e->alive = 0;
    for (int k = 0; k < 10; k++) fire_ebullet(g, e->x, e->y, e->phase + TAU * (float)k / 10.0f, 190.0f, 1);
    explode(g, e->x, e->y, 1.6f, 0xFFD23AFFu, 0xF0483CFFu);
    sfx(g, SFX_EXPL_M);
    shake(g, 6.0f);
}

/* the Big Head: hovers beside you, lashes its tentacles, spits, flings its earrings' worth of bullets */
static void update_boss2(Game *g, int i, float dt)
{
    Enemy *e = &g->en[i];
    if (g->boss_dying > 0) return;
    int phase2 = e->hp < e->maxhp * 0.5f;
    if (phase2 && e->idx == 0) {
        e->idx = 1;
        e->cd = 1.2f;
        popup(g, VW * 0.5f, 160.0f, "now she's MAD!", 0xF0483CFFu, 1);
        sfx(g, SFX_BOSS);
        ring(g, e->x, e->y, 300.0f, 0.8f, 20.0f, 0xF0483CFFu);
        shake(g, 12.0f);
    }
    float dx = wd(g, g->px - e->x), side = dx > 0 ? -1.0f : 1.0f;
    float tx = g->px + side * 300.0f, ty = 168.0f + sinf(e->t * 0.8f) * 32.0f;
    if (e->lash_t > 0) tx = e->x;                         /* plants itself while lashing */
    e->vx = approach(e->vx, clampf(wd(g, tx - e->x) * 1.2f, -130.0f, 130.0f), dt * 2.0f);
    e->vy = approach(e->vy, clampf((ty - e->y) * 1.5f, -100.0f, 100.0f), dt * 2.0f);
    e->x = wrapx(g, e->x + e->vx * dt);
    e->y += e->vy * dt;
    if (e->lash_t > 0) e->lash_t -= dt;

    /* tentacles and the big head hurt to touch */
    if (g->alive && g->inv_t <= 0) {
        float hx = wd(g, g->px - e->x), hy = g->py - e->y;
        if (hx * hx + hy * hy < (e->r * 0.85f + SHIP_R) * (e->r * 0.85f + SHIP_R)) hurt_player(g);
        float tx2[TENT_SEGS + 1], ty2[TENT_SEGS + 1];
        for (int k = 0; k < TENT_N && g->inv_t <= 0; k++) {
            tentacle_points(e, k, tx2, ty2);
            for (int p = 3; p <= TENT_SEGS; p++) {
                float ddx = wd(g, g->px - tx2[p]), ddy = g->py - ty2[p], rr = 9.0f - (float)p * 0.4f + SHIP_R * 0.8f;
                if (ddx * ddx + ddy * ddy < rr * rr) { hurt_player(g); break; }
            }
        }
    }

    e->cd -= dt;
    if (e->cd > 0 || !g->alive) return;
    enum { LASH, LASH2, SPIT, HOOPS, SUMMON };
    static const int P1[] = {LASH, SPIT, HOOPS, LASH, SUMMON, SPIT};
    static const int P2[] = {LASH2, SPIT, LASH, HOOPS, LASH2, SUMMON, SPIT};
    int atk = phase2 ? P2[e->attack % 7] : P1[e->attack % 6];
    e->attack++;
    e->flash = 0.3f;
    switch (atk) {
    case LASH: case LASH2: {
        float best = 1e9f;                                 /* the tentacle nearest the player whips at them */
        float rx[TENT_SEGS + 1], ry[TENT_SEGS + 1];
        for (int k = 0; k < TENT_N; k++) {
            tentacle_points(e, k, rx, ry);
            float d = fabsf(wd(g, g->px - rx[0]));
            if (d < best) { best = d; e->lash_k = k; }
        }
        tentacle_points(e, e->lash_k, rx, ry);
        e->lash_ang = atan2f(g->py - ry[0], wd(g, g->px - rx[0]));
        e->lash_t = LASH_TIME;
        e->lash2 = atk == LASH2;
        sfxp(g, SFX_FLIP, 70);
        e->cd = phase2 ? 1.25f : 1.6f;
        break;
    }
    case SPIT: {
        float mx = e->x + B2X(B2_MOUTH_U), my = e->y + B2Y(B2_MOUTH_V) - 10.0f;
        float a = atan2f(g->py - my, wd(g, g->px - mx));
        int n = phase2 ? 3 : 2;
        for (int k = -n; k <= n; k++) fire_ebullet(g, mx, my, a + 0.16f * (float)k, 225.0f, 1);
        sfx(g, SFX_ESHOT);
        e->cd = phase2 ? 1.2f : 1.5f;
        break;
    }
    case HOOPS: {
        float hx[2] = {e->x + B2X(B2_HOOP_L_U), e->x + B2X(B2_HOOP_R_U)}, hy[2] = {e->y + B2Y(B2_HOOP_L_V), e->y + B2Y(B2_HOOP_R_V)};
        int n = phase2 ? 12 : 10;
        for (int h = 0; h < 2; h++) {
            for (int k = 0; k < n; k++) fire_ebullet(g, hx[h], hy[h], e->t + TAU * (float)k / (float)n, 150.0f, 0);
            ring(g, hx[h], hy[h], 70.0f, 0.3f, 6.0f, 0xFFD23AFFu);
        }
        sfx(g, SFX_ESHOT);
        e->cd = 2.0f;
        break;
    }
    default: {                                             /* SUMMON: hiccups out some little blobs */
        int n = 0;
        for (int k = 0; k < MAX_EN; k++) n += g->en[k].alive && (g->en[k].type == EN_BLOBLET || g->en[k].type == EN_UFO);
        for (int k = 0; k < (phase2 ? 3 : 2) && n < 8; k++, n++) {
            int b = spawn_enemy(g, k == 2 ? EN_UFO : EN_BLOBLET, e->x + frange(g, -60, 60), e->y + 60.0f, 0.1f * (float)k);
            (void)b;
        }
        popup(g, sxof(g, e->x), e->y - 110.0f, "hic!", 0x22307CFFu, 0);
        e->cd = 1.8f;
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* the Pencil (level 3 boss) and its eraser                             */
/* ------------------------------------------------------------------ */
static void erase_crumbs(Game *g, float x, float y, int n)
{
    for (int k = 0; k < n; k++) {
        float a = frange(g, 0, TAU), s = frange(g, 60, 200);
        Part *p = newpart(g, P_DEBRIS, x, y, cosf(a) * s, sinf(a) * s - 60.0f, frange(g, 0.5f, 0.9f), frange(g, 2.0f, 3.5f), (k & 1) ? 0xFF8AB0FFu : 0xF6B8CCFFu);
        p->rot = frange(g, 0, TAU); p->vr = frange(g, -10, 10); p->drag = 2.0f;
    }
}

/* rub out everything hand-drawn in a circle (world x, y): you, the baddies, bullets, pickups, doodles */
static void erase_at(Game *g, float x, float y, float r)
{
    Enemy *pencil = g->boss >= 0 ? &g->en[g->boss] : NULL;
    if (g->alive && g->inv_t <= 0) {
        float dx = wd(g, g->px - x), dy = g->py - y;
        if (dx * dx + dy * dy < (r + SHIP_R) * (r + SHIP_R)) {
            hurt_player(g);
            popup(g, sxof(g, g->px), g->py - 40.0f, "you got erased!", 0x8A8A96FFu, 0);
        }
    }
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive || e->warp || IS_BOSS(e->type) || e == pencil) continue;
        float dx = wd(g, e->x - x), dy = e->y - y, rr = r + e->r * 0.7f;
        if (dx * dx + dy * dy > rr * rr) continue;
        e->alive = 0;                                      /* rubbed out: no points, no crystals, no splitting */
        erase_crumbs(g, e->x, e->y, 8);
        if (rnd(g) % 3 == 0) popup(g, sxof(g, e->x), e->y - 20.0f, "erased!", 0x8A8A96FFu, 0);
        sfxp(g, SFX_DEFLECT, 70);
    }
    for (int i = 0; i < MAX_EB; i++) {
        EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        float dx = wd(g, b->x - x), dy = b->y - y;
        if (dx * dx + dy * dy < (r + 8.0f) * (r + 8.0f)) { b->alive = 0; erase_crumbs(g, b->x, b->y, 2); }
    }
    for (int i = 0; i < MAX_PB; i++) {
        Bolt *b = &g->pb[i];
        if (!b->alive) continue;
        float dx = wd(g, b->x - x), dy = b->y - y;
        if (dx * dx + dy * dy < (r + 8.0f) * (r + 8.0f)) b->alive = 0;
    }
    for (int i = 0; i < MAX_CRY; i++) {
        Crystal *c = &g->cr[i];
        if (!c->alive) continue;
        float dx = wd(g, c->x - x), dy = c->y - y;
        if (dx * dx + dy * dy < (r + 8.0f) * (r + 8.0f)) { c->alive = 0; erase_crumbs(g, c->x, c->y, 2); }
    }
    for (int i = 0; i < 128; i++) {
        float *l = g->lead[i];
        if (l[2] <= 0) continue;
        float dx = wd(g, l[0] - x), dy = l[1] - y;
        if (dx * dx + dy * dy < r * r) l[2] = 0;
    }
    if (g->L->paper == 2) {                                /* the crayon doodles on the page */
        float sx = sxof(g, x);
        for (int i = 0; i < CP_DOODLES; i++) {
            if (g->doodle_gone[i]) continue;
            float dx2, dy2, dr;
            cp_doodle(g, i, &dx2, &dy2, &dr);
            float ddx = dx2 - sx, ddy = dy2 - y;
            if (ddx * ddx + ddy * ddy < (r + dr) * (r + dr)) {
                g->doodle_gone[i] = 1;
                erase_crumbs(g, wrapx(g, g->camx + dx2 - VW * 0.5f), dy2, 4);
            }
        }
    }
    /* a rubbed-out smudge on the paper */
    float *m = g->erase_mark[g->erase_next];
    g->erase_next = (g->erase_next + 1) % 48;
    m[0] = x; m[1] = y; m[2] = r; m[3] = 5.0f;
}

static void lead_point(Game *g, float x, float y, int stroke)
{
    float *l = g->lead[g->lead_next];
    g->lead_next = (g->lead_next + 1) % 128;
    l[0] = x; l[1] = y; l[2] = 2.8f; l[3] = (float)stroke;
}


static void update_boss3(Game *g, int i, float dt)
{
    Enemy *e = &g->en[i];
    if (g->boss_dying > 0) return;
    int mad = e->hp < e->maxhp * 0.5f;
    if (mad && e->idx == 0) {
        e->idx = 1;
        popup(g, VW * 0.5f, 160.0f, "ERASE EVERYTHING!", 0xF0483CFFu, 1);
        sfx(g, SFX_BOSS);
        shake(g, 12.0f);
        e->cd = 0.6f;
    }
    float tx, ty, ex, ey;
    pencil_ends(e, &tx, &ty, &ex, &ey);
    float to_p = atan2f(g->py - e->y, wd(g, g->px - e->x));
    e->mode_t += dt;
    float turn_rate = 5.0f;

    switch (e->mode) {
    case PM_HOVER: {                                       /* hover beside you, pointing its tip at you */
        float side = wd(g, g->px - e->x) > 0 ? -1.0f : 1.0f;
        float hx = g->px + side * 340.0f, hy = 190.0f + sinf(e->t * 0.9f) * 50.0f;
        e->vx = approach(e->vx, clampf(wd(g, hx - e->x) * 1.4f, -170.0f, 170.0f), dt * 2.0f);
        e->vy = approach(e->vy, clampf((hy - e->y) * 1.6f, -140.0f, 140.0f), dt * 2.0f);
        e->x = wrapx(g, e->x + e->vx * dt);
        e->y += e->vy * dt;
        float want = to_p + PI_F;                         /* eraser away, tip toward you */
        float da = want - e->ang;
        while (da > PI_F) da -= TAU;
        while (da < -PI_F) da += TAU;
        e->ang += clampf(da, -turn_rate * dt, turn_rate * dt);
        e->cd -= dt;
        if (e->cd > 0 || !g->alive) break;
        enum { A_ERASE, A_SHOT, A_SCRIB, A_SKETCH };
        static const int P1[] = {A_ERASE, A_SHOT, A_SCRIB, A_SHOT, A_ERASE, A_SKETCH};
        static const int P2[] = {A_ERASE, A_SCRIB, A_SHOT, A_ERASE, A_SKETCH, A_SCRIB, A_SHOT};
        int atk = mad ? P2[e->attack % 7] : P1[e->attack % 6];
        e->attack++;
        if (atk == A_ERASE) {
            e->mode = PM_TURN; e->mode_t = 0; e->lash_t = 0; e->lash2 = mad ? 1 : 0;
            sfxp(g, SFX_FLIP, 80);
        } else if (atk == A_SHOT) {                       /* flicks pencil shavings / graphite at you */
            int n = mad ? 3 : 2;
            float a = atan2f(g->py - ty, wd(g, g->px - tx));
            for (int k = -n; k <= n; k++) fire_ebullet(g, tx, ty, a + 0.17f * (float)k, 235.0f, 0);
            sfx(g, SFX_ESHOT);
            e->cd = mad ? 1.0f : 1.4f;
        } else if (atk == A_SCRIB) {
            e->mode = PM_SCRIBBLE; e->mode_t = 0; e->lash_k++;
            float sa = atan2f(g->py - ty, wd(g, g->px - tx)) + ((e->attack & 2) ? 0.55f : -0.55f);   /* scribbles past you, not into you */
            e->ax = cosf(sa); e->ay = sinf(sa);
            sfxp(g, SFX_BLADE, 60);
        } else {                                           /* sketches some friends into existence */
            int n = 0;
            for (int k = 0; k < MAX_EN; k++) n += g->en[k].alive && !IS_BOSS(g->en[k].type);
            for (int k = 0; k < 3 && n < 12; k++, n++)
                spawn_enemy(g, k == 2 ? EN_SNIP : EN_SHARD, e->x + frange(g, -140, 140), clampf(e->y + frange(g, -120, 160), 80.0f, 400.0f), 0.15f * (float)k);
            popup(g, sxof(g, e->x), e->y - 70.0f, "*scribble scribble*", 0x22307CFFu, 0);
            e->cd = 1.6f;
        }
        break;
    }
    case PM_TURN: {                                        /* flips round, eraser first, lines up on you... then locks on */
        float tt = mad ? 0.75f : 0.95f, lock = tt - 0.4f;
        if (e->mode_t < lock) {
            float da = to_p - e->ang;
            while (da > PI_F) da -= TAU;
            while (da < -PI_F) da += TAU;
            e->ang += clampf(da, -8.0f * dt, 8.0f * dt);
            e->lash_t = 0;
        } else if (e->lash_t == 0) {                      /* aim is now fixed: the dotted line turns red - move! */
            e->lash_t = 1;
            pencil_ends(e, &tx, &ty, &ex, &ey);
            float dx = wd(g, g->px - ex), dy = g->py - ey, l = sqrtf(dx * dx + dy * dy) + 0.01f;
            float over = 170.0f;
            e->ax = wrapx(g, ex + dx + dx / l * over); e->ay = clampf(ey + dy + dy / l * over, 50.0f, 430.0f);
            e->ang = atan2f(dy, dx);
            sfxp(g, SFX_FLIP, 120);
        }
        e->x = wrapx(g, e->x + sinf(e->mode_t * 60.0f) * 1.5f);      /* shaking with excitement */
        if (e->mode_t >= tt) {
            pencil_ends(e, &tx, &ty, &ex, &ey);
            e->mode = PM_SWEEP; e->mode_t = 0;
            e->aux = ex; e->fuse = ey;                     /* where the rub starts */
            sfx(g, SFX_BLADE);
        }
        break;
    }
    case PM_SWEEP: {                                       /* rubs a line right through you (and anything else) */
        float T = mad ? 0.7f : 0.9f, u = clampf(e->mode_t / T, 0, 1);
        float k = u * u * (3 - 2 * u);
        float sx0 = e->aux, sy0 = e->fuse;
        float dx = wd(g, e->ax - sx0), dy = e->ay - sy0, l = sqrtf(dx * dx + dy * dy) + 0.01f;
        float scrub = sinf(e->mode_t * 42.0f) * 11.0f;
        float rx = wrapx(g, sx0 + dx * k - dy / l * scrub), ry = sy0 + dy * k + dx / l * scrub;
        e->ang = atan2f(dy, dx);
        e->x = wrapx(g, rx - cosf(e->ang) * BOSS3_LEN * 0.5f);
        e->y = ry - sinf(e->ang) * BOSS3_LEN * 0.5f;
        if ((g->frame & 1) == 0) erase_at(g, rx, ry, ERASER_R);
        erase_crumbs(g, rx, ry, 1);
        sfxp(g, SFX_BLADE, 140 + (int)(rnd(g) % 30));
        shake(g, 2.0f);
        if (u >= 1.0f) {
            if (e->lash2) { e->lash2 = 0; e->mode = PM_TURN; e->mode_t = 0.2f; e->lash_t = 0; }   /* mad: comes straight back for another go */
            else { e->mode = PM_RECOVER; e->mode_t = 0; }
        }
        break;
    }
    case PM_RECOVER:
        e->vx *= expf(-dt * 3.0f); e->vy *= expf(-dt * 3.0f);
        if (e->mode_t > 0.5f) { e->mode = PM_HOVER; e->cd = mad ? 0.7f : 1.1f; }
        break;
    case PM_SCRIBBLE: {                                    /* zig-zags its tip about, leaving a pencil line that hurts */
        float T = mad ? 1.5f : 1.2f;
        float px = -e->ay, py = e->ax;
        float zig = sinf(e->mode_t * 16.0f) * 260.0f;
        float vx = e->ax * 260.0f + px * zig, vy = e->ay * 260.0f + py * zig;
        e->x = wrapx(g, e->x + vx * dt);
        e->y = clampf(e->y + vy * dt, 60.0f, 420.0f);
        float want = atan2f(-vy, -vx);                     /* writing with the tip */
        float da = want - e->ang;
        while (da > PI_F) da -= TAU;
        while (da < -PI_F) da += TAU;
        e->ang += clampf(da, -14.0f * dt, 14.0f * dt);
        pencil_ends(e, &tx, &ty, &ex, &ey);
        lead_point(g, tx, clampf(ty, 40.0f, 440.0f), e->lash_k);
        sfxp(g, SFX_HIT, 70);
        if (e->mode_t > T) { e->mode = PM_HOVER; e->cd = 0.9f; }
        break;
    }
    }
    /* bumping into the pencil hurts too - but only while it's hovering or rubbing (turning, scribbling and
     * getting its breath back it's too busy to notice you) */
    if ((e->mode == PM_HOVER || e->mode == PM_SWEEP) && g->alive && g->inv_t <= 0 && hits_enemy(g, e, g->px, g->py, SHIP_R * 0.8f)) hurt_player(g);
}

static void update_level_bits(Game *g, float dt)
{
    for (int i = 0; i < 48; i++) if (g->erase_mark[i][3] > 0) g->erase_mark[i][3] -= dt;
    for (int i = 0; i < 128; i++) {
        float *l = g->lead[i];
        if (l[2] <= 0) continue;
        l[2] -= dt;
        if (!g->alive || g->inv_t > 0) continue;
        float dx = wd(g, g->px - l[0]), dy = g->py - l[1];
        if (dx * dx + dy * dy < (6.0f + SHIP_R * 0.7f) * (6.0f + SHIP_R * 0.7f) && l[2] < 2.6f) hurt_player(g);
    }
}

static void update_enemies(Game *g, float dt)
{
    for (int i = 0; i < MAX_EN; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) continue;
        e->flash -= dt * 9.0f;
        if (e->flash < 0) e->flash = 0;
        e->hitcd -= dt;

        if (e->warp) {
            float before = e->warp_t;
            e->warp_t += dt;
            if (before < 0 && e->warp_t >= 0) sfxp(g, SFX_WARP, 110 + (int)(rnd(g) % 40));
            if (e->type == EN_SHIELD && e->parent >= 0 && g->en[e->parent].alive) {
                Enemy *b = &g->en[e->parent];
                float a = b->phase + TAU * (float)e->idx / 6.0f;
                e->x = wrapx(g, b->x + cosf(a) * 118.0f); e->y = b->y + sinf(a) * 118.0f;
            }
            if (e->warp_t >= 0.7f) {
                e->warp = 0;
                const EnemyStats *s = &ESTAT[e->type];
                for (int k = 0; k < 8; k++) spark(g, e->x, e->y, TAU * (float)k / 8.0f, 260.0f, 0.25f, 2.0f, s->c_light);
                pglow(g, e->x, e->y, 0, 0, 0.25f, e->r * 3.0f, col_a(s->c_light, 0.8f));
                if (IS_BOSS(e->type)) { sfx(g, SFX_BOSS); shake(g, 10.0f); ring(g, e->x, e->y, 260.0f, 0.6f, 16.0f, 0xFF4FD8FFu); g->boss_intro = 2.5f; }
            }
            continue;
        }
        e->t += dt;
        float dx = wd(g, g->px - e->x), dy = g->py - e->y;
        float dist = sqrtf(dx * dx + dy * dy) + 0.001f;

        switch (e->type) {
        case EN_SHARD: {
            float spd = 140.0f + 9.0f * (float)g->wave;
            /* close in, flash, then lunge */
            if (e->aux <= 0 && dist < 170.0f && g->alive && e->cd <= 0) { e->aux = 0.9f; e->cd = 3.0f; }
            if (e->aux > 0) {
                e->aux -= dt;
                if (e->aux < 0.5f) spd *= 2.3f; else spd *= 0.3f;
            }
            e->cd -= dt;
            float px = -dy / dist, py = dx / dist, w = sinf(e->t * 3.0f + e->phase) * 70.0f;
            float tvx = dx / dist * spd + px * w, tvy = dy / dist * spd + py * w;
            if (!g->alive) { tvx = cosf(e->phase + e->t) * 60.0f; tvy = sinf(e->phase + e->t * 1.3f) * 60.0f; }
            for (int k = 0; k < MAX_EN; k++) {             /* keep a little personal space */
                const Enemy *o = &g->en[k];
                if (k == i || !o->alive || o->type != EN_SHARD) continue;
                float ox = wd(g, e->x - o->x), oy = e->y - o->y, od = ox * ox + oy * oy;
                if (od < 30.0f * 30.0f && od > 0.01f) { tvx += ox * 4.0f; tvy += oy * 4.0f; }
            }
            float steer = (e->aux > 0 && e->aux < 0.5f) ? 8.0f : 2.5f;
            e->vx = approach(e->vx, tvx, dt * steer);
            e->vy = approach(e->vy, tvy, dt * steer);
            float ta = atan2f(e->vy, e->vx), da = ta - e->ang;
            while (da > PI_F) da -= TAU;
            while (da < -PI_F) da += TAU;
            e->ang += da * clampf(dt * 8.0f, 0, 1);
            break;
        }
        case EN_ZIP: {
            float dir = e->vx >= 0 ? 1.0f : -1.0f;
            if (dx * dir < -650.0f) dir = -dir;              /* overshot: swing back round */
            e->vx = approach(e->vx, dir * 290.0f, dt * 1.5f);
            e->aux = approach(e->aux, g->py, dt * 0.35f);
            float ty = e->aux + sinf(e->t * 3.2f + e->phase) * 75.0f;
            e->vy = (ty - e->y) * 4.0f;
            e->ang = atan2f(e->vy, e->vx);
            break;
        }
        case EN_SPORE: {
            float want = dist < 220.0f ? -1.0f : (dist > 300.0f ? 1.0f : 0.0f);
            e->vx = approach(e->vx, dx / dist * 75.0f * want, dt * 1.5f);
            e->vy = approach(e->vy, dy / dist * 75.0f * want + cosf(e->t * 1.7f + e->phase) * 40.0f, dt * 1.5f);
            e->ang += dt * 0.9f;
            e->cd -= dt;
            if (e->cd <= 0) {
                e->cd = frange(g, 1.8f, 2.5f);
                float sx = sxof(g, e->x);
                if (g->alive && sx > -20.0f && sx < VW + 20.0f) {
                    float a = atan2f(dy, dx);
                    int n = g->wave >= 4 ? 2 : 1;
                    for (int k = -n; k <= n; k++) fire_ebullet(g, e->x, e->y, a + 0.26f * (float)k, 225.0f, 0);
                    ring(g, e->x, e->y, 46.0f, 0.2f, 5.0f, 0xFF5FA8FFu);
                    sfx(g, SFX_ESHOT);
                }
            }
            break;
        }
        case EN_SERPENT: {
            float want = atan2f(dy, dx);
            float lo, hi;
            corridor(g, e->x, 60.0f, &lo, &hi);
            if (e->y < lo) want = PI_F * 0.5f;
            if (e->y > hi) want = -PI_F * 0.5f;
            if (!g->alive) want = e->ang + 0.3f;
            float da = want - e->ang;
            while (da > PI_F) da -= TAU;
            while (da < -PI_F) da += TAU;
            e->ang += clampf(da, -1.7f * dt, 1.7f * dt);
            float mv = e->ang + sinf(e->t * 4.5f) * 0.55f;
            e->vx = cosf(mv) * 175.0f; e->vy = sinf(mv) * 175.0f;
            break;
        }
        case EN_SEG: {
            if (e->fuse > 0) { e->fuse -= dt; if (e->fuse <= 0) { kill_enemy(g, i, 1); continue; } }
            const Enemy *p = (e->parent >= 0 && g->en[e->parent].alive) ? &g->en[e->parent] : NULL;
            e->vx = e->vy = 0;
            if (p) {
                float sp = (p->type == EN_SERPENT ? 30.0f : 24.0f) * (1.0f - 0.03f * (float)e->idx);
                float ddx = wd(g, p->x - e->x), ddy = p->y - e->y, d = sqrtf(ddx * ddx + ddy * ddy) + 0.001f;
                if (d > sp) { e->x = wrapx(g, e->x + ddx / d * (d - sp)); e->y += ddy / d * (d - sp); }
                e->ang = atan2f(ddy, ddx);
            } else if (e->fuse <= 0) {
                e->fuse = 0.1f;                         /* orphaned piece: let it pop */
            }
            break;
        }
        case EN_BOSS:
            update_boss(g, i, dt);
            continue;
        case EN_BOSS2:
            update_boss2(g, i, dt);
            continue;
        case EN_BOSS3:
            update_boss3(g, i, dt);
            continue;
        case EN_SNIP: {                                     /* creeps up, opens wide, then SNAPS forward */
            e->mode_t += dt;
            switch (e->mode) {
            case 0:
                e->vx = approach(e->vx, dx / dist * 150.0f, dt * 2.5f);
                e->vy = approach(e->vy, dy / dist * 150.0f, dt * 2.5f);
                e->ang = atan2f(dy, dx);
                if (g->alive && dist < 220.0f && e->mode_t > 0.8f) { e->mode = 1; e->mode_t = 0; }
                break;
            case 1:
                e->vx *= expf(-dt * 6.0f); e->vy *= expf(-dt * 6.0f);
                e->ang = atan2f(dy, dx);
                if (e->mode_t > 0.45f) { e->mode = 2; e->mode_t = 0; e->ax = dx / dist; e->ay = dy / dist; sfxp(g, SFX_FLIP, 160); }
                break;
            case 2:
                e->vx = e->ax * 720.0f; e->vy = e->ay * 720.0f;
                if (e->mode_t > 0.32f) { e->mode = 3; e->mode_t = 0; }
                break;
            default:
                e->vx *= expf(-dt * 4.0f); e->vy *= expf(-dt * 4.0f);
                if (e->mode_t > 0.9f) { e->mode = 0; e->mode_t = 0; }
                break;
            }
            break;
        }
        case EN_PLANE: {                                    /* zooms across, loops back, drops bullets on you */
            float dir = e->vx >= 0 ? 1.0f : -1.0f;
            if (dx * dir < -700.0f) dir = -dir;
            e->vx = approach(e->vx, dir * 380.0f, dt * 1.2f);
            e->vy = approach(e->vy, clampf((g->py - e->y) * 0.6f, -90.0f, 90.0f) + sinf(e->t * 3.0f + e->phase) * 40.0f, dt * 2.0f);
            e->ang = atan2f(e->vy, e->vx);
            e->cd -= dt;
            if (e->cd <= 0) {
                e->cd = frange(g, 1.2f, 1.7f);
                float sx = sxof(g, e->x);
                if (g->alive && sx > 20 && sx < VW - 20) { fire_ebullet(g, e->x, e->y, atan2f(dy, dx), 230.0f, 0); sfxp(g, SFX_ESHOT, 170); }
            }
            break;
        }
        case EN_PINWHEEL: {                                 /* drifts into place, then spins out a spiral of bullets */
            float sd = wd(g, e->x - g->px) > 0 ? 1.0f : -1.0f;
            float hx = g->px + sd * 260.0f, hy = clampf(g->py + sinf(e->phase) * 110.0f, 90.0f, 390.0f);
            e->vx = approach(e->vx, clampf(wd(g, hx - e->x), -70.0f, 70.0f), dt * 1.5f);
            e->vy = approach(e->vy, clampf(hy - e->y, -70.0f, 70.0f), dt * 1.5f);
            e->ang += dt * (e->idx ? 5.0f : 1.6f);
            if (e->idx) {
                e->fuse -= dt; e->aux -= dt;
                if (e->aux <= 0) {
                    e->aux = 0.12f;
                    for (int k = 0; k < 2; k++) fire_ebullet(g, e->x, e->y, e->ang + PI_F * (float)k, 150.0f, 0);
                    sfxp(g, SFX_ESHOT, 190);
                }
                if (e->fuse <= 0) { e->idx = 0; e->cd = 2.2f; }
            } else {
                e->cd -= dt;
                float sx = sxof(g, e->x);
                if (e->cd <= 0 && g->alive && sx > 0 && sx < VW) { e->idx = 1; e->fuse = 2.3f; e->aux = 0; }
            }
            break;
        }
        case EN_UFO: {                                      /* hovers above or below you, fires 3-shot bursts */
            float sd = wd(g, e->x - g->px) > 0 ? 1.0f : -1.0f;
            float tx = g->px + sd * 260.0f, ty = clampf(g->py + (e->phase > PI_F ? -115.0f : 115.0f), 80.0f, 400.0f) + sinf(e->t * 2.0f) * 20.0f;
            e->vx = approach(e->vx, clampf(wd(g, tx - e->x) * 1.5f, -220.0f, 220.0f), dt * 2.0f);
            e->vy = approach(e->vy, clampf((ty - e->y) * 1.8f, -160.0f, 160.0f), dt * 2.0f);
            e->ang = clampf(e->vx / 500.0f, -0.3f, 0.3f);
            if (e->idx > 0) {
                e->fuse -= dt;
                if (e->fuse <= 0) {
                    fire_ebullet(g, e->x, e->y + 6.0f, atan2f(g->py - e->y, wd(g, g->px - e->x)), 250.0f, 0);
                    sfxp(g, SFX_ESHOT, 150);
                    e->idx--;
                    e->fuse = 0.16f;
                }
            }
            e->cd -= dt;
            if (e->cd <= 0) {
                e->cd = frange(g, 2.4f, 3.2f);
                float sx = sxof(g, e->x);
                if (g->alive && sx > 0 && sx < VW) { e->idx = 3; e->fuse = 0.0f; }
            }
            break;
        }
        case EN_BOMB: {                                     /* drifts at you with a lit fuse */
            e->fuse -= dt;
            if (dist < 95.0f && e->fuse > 0.9f) e->fuse = 0.9f;
            if (e->fuse <= 0) { bomb_burst(g, i); continue; }
            float spd = 85.0f;
            e->vx = approach(e->vx, dx / dist * spd + cosf(e->t * 2.0f + e->phase) * 30.0f, dt * 1.5f);
            e->vy = approach(e->vy, dy / dist * spd + sinf(e->t * 2.3f + e->phase) * 30.0f, dt * 1.5f);
            e->ang = sinf(e->t * 3.0f) * 0.2f;
            break;
        }
        case EN_BLOB: case EN_BLOBLET: {                    /* boing, boing */
            int small = e->type == EN_BLOBLET;
            float spd = small ? 150.0f : 70.0f, hop = small ? 2.6f : 1.5f;
            if (!g->alive) { dx = cosf(e->phase) * 100.0f; dy = 0; dist = 100.0f; }
            e->vx = approach(e->vx, dx / dist * spd, dt * (small ? 3.0f : 2.0f));
            e->vy = approach(e->vy, dy / dist * spd + cosf(e->t * hop * TAU) * (small ? 70.0f : 90.0f), dt * 3.0f);
            break;
        }
        case EN_SHIELD: {
            if (e->parent < 0 || !g->en[e->parent].alive) { kill_enemy(g, i, 0); continue; }
            Enemy *b = &g->en[e->parent];
            float a = b->phase + TAU * (float)e->idx / 6.0f;
            float rad = 118.0f + sinf(e->t * 2.0f) * 6.0f;
            e->x = wrapx(g, b->x + cosf(a) * rad); e->y = b->y + sinf(a) * rad;
            e->ang = a;
            break;
        }
        default: break;
        }
        if (e->type != EN_SEG && e->type != EN_SHIELD) {
            e->x = wrapx(g, e->x + e->vx * dt);
            e->y += e->vy * dt;
            float lo, hi;
            corridor(g, e->x, e->r + 6.0f, &lo, &hi);
            if (e->y < lo) { e->y = lo; if (e->vy < 0) e->vy *= -0.5f; }
            if (e->y > hi) { e->y = hi; if (e->vy > 0) e->vy *= -0.5f; }
        }
        /* contact with the ship */
        if (g->alive && g->inv_t <= 0) {
            float cx = wd(g, e->x - g->px), cy = e->y - g->py, rr = e->r * 0.85f + SHIP_R;
            if (cx * cx + cy * cy < rr * rr) {
                hurt_player(g);
                if (e->type == EN_SHARD || e->type == EN_ZIP || e->type == EN_SEG || e->type == EN_BLOBLET || e->type == EN_PLANE) hurt_enemy(g, i, 99.0f, 0, 0);
                if (e->type == EN_BOMB && e->alive) bomb_burst(g, i);
            }
        }
    }
}

static void update_projectiles(Game *g, float dt)
{
    /* player bolts */
    for (int i = 0; i < MAX_PB; i++) {
        Bolt *b = &g->pb[i];
        if (!b->alive) continue;
        b->life -= dt;
        b->x = wrapx(g, b->x + b->vx * dt);
        b->y += b->vy * dt;
        if (b->life <= 0) { b->alive = 0; continue; }
        if (b->y < terr_top(g, b->x) || b->y > terr_bot(g, b->x)) {
            b->alive = 0;
            float up = b->y < 240.0f ? 1.0f : -1.0f;
            for (int k = 0; k < 3; k++) spark(g, b->x, b->y, up * frange(g, 0.4f, 2.7f), frange(g, 80, 220), 0.2f, 2.0f, COL_A);
            continue;
        }
        for (int k = 0; k < MAX_EN; k++) {
            Enemy *e = &g->en[k];
            if (!e->alive || e->warp) continue;
            float dx = wd(g, e->x - b->x), dy = e->y - b->y, rr = e->r + 9.0f;
            if (fabsf(dx) > rr + 22.0f && e->type != EN_BOSS3) continue;
            /* swept test along the bolt's last step so fast bolts can't tunnel */
            float step = b->vx * dt, best = 1e9f;
            for (int s = 0; s <= 2; s++) {
                float ox = dx + step * (float)s * 0.5f;
                float d = ox * ox + dy * dy;
                if (d < best) best = d;
            }
            if (e->type == EN_BOSS3) best = hits_enemy(g, e, b->x, b->y, 9.0f) ? 0.0f : 1e9f;
            if (best < rr * rr) {
                b->alive = 0;
                hit_sparks(g, b->x, b->y, b->vx > 0 ? PI_F : 0.0f, ESTAT[e->type].c_light, 3);
                hurt_enemy(g, k, b->dmg, b->vx * 0.05f, 0);
                break;
            }
        }
    }

    /* missiles */
    for (int i = 0; i < MAX_MS; i++) {
        Missile *m = &g->ms[i];
        if (!m->alive) continue;
        m->life -= dt;
        float age = 2.6f - m->life;
        if (m->target >= 0 && (!g->en[m->target].alive || g->en[m->target].warp)) m->target = -1;   /* its target's gone: fly on */
        float ang = atan2f(m->vy, m->vx);
        if (m->target >= 0 && age > 0.12f) {
            const Enemy *e = &g->en[m->target];
            float want = atan2f(e->y - m->y, wd(g, e->x - m->x));
            float da = want - ang;
            while (da > PI_F) da -= TAU;
            while (da < -PI_F) da += TAU;
            float rate = (5.0f + age * 10.0f) * dt;
            ang += clampf(da, -rate, rate);
        }
        ang += sinf(age * 18.0f + m->wob) * 0.04f;
        m->speed = m->speed + 1700.0f * dt;
        if (m->speed > 980.0f) m->speed = 980.0f;
        if (age < 0.12f) m->speed = 330.0f - age * 1200.0f;   /* a little hang-time puff before igniting */
        m->vx = cosf(ang) * m->speed; m->vy = sinf(ang) * m->speed;
        m->x = wrapx(g, m->x + m->vx * dt);
        m->y += m->vy * dt;
        m->smoke_t -= dt;
        if (m->smoke_t <= 0) {
            m->smoke_t = 0.016f;
            newpart(g, P_SMOKE, m->x, m->y, frange(g, -20, 20), frange(g, -20, 20), frange(g, 0.35f, 0.6f), frange(g, 5, 8), 0xF0B8FFA0u);
            spark(g, m->x, m->y, ang + PI_F + frange(g, -0.3f, 0.3f), frange(g, 60, 160), 0.12f, 2.0f, 0xFFD15CFFu);
        }
        if (m->life <= 0 || m->y < terr_top(g, m->x) || m->y > terr_bot(g, m->x)) { missile_explode(g, m); continue; }
        for (int k = 0; k < MAX_EN; k++) {
            const Enemy *e = &g->en[k];
            if (!e->alive || e->warp) continue;
            float dx = wd(g, e->x - m->x), dy = e->y - m->y, rr = e->r + 13.0f;
            if (e->type == EN_BOSS3) { if (hits_enemy(g, e, m->x, m->y, 13.0f)) { missile_explode(g, m); break; } continue; }
            if (dx * dx + dy * dy < rr * rr) { missile_explode(g, m); break; }
        }
    }

    /* enemy bullets */
    for (int i = 0; i < MAX_EB; i++) {
        EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        b->life -= dt;
        b->x = wrapx(g, b->x + b->vx * dt);
        b->y += b->vy * dt;
        if (b->life <= 0) { b->alive = 0; continue; }
        if (b->y < terr_top(g, b->x) || b->y > terr_bot(g, b->x)) {
            b->alive = 0;
            pglow(g, b->x, b->y, 0, 0, 0.15f, 18.0f, 0xFF5FA8C0u);
            continue;
        }
        if (g->alive && g->inv_t <= 0) {
            float dx = wd(g, b->x - g->px), dy = b->y - g->py, rr = (b->big ? 8.0f : 6.0f) + SHIP_R * 0.7f;
            if (dx * dx + dy * dy < rr * rr) { b->alive = 0; hurt_player(g); }
        }
    }
}

static void update_pickups(Game *g, float dt)
{
    g->cry_streak_t -= dt;
    if (g->cry_streak_t <= 0) g->cry_streak = 0;
    for (int i = 0; i < MAX_CRY; i++) {
        Crystal *c = &g->cr[i];
        if (!c->alive) continue;
        c->life -= dt;
        c->spin += dt * 4.0f;
        if (c->life <= 0) { c->alive = 0; continue; }
        float dx = wd(g, g->px - c->x), dy = g->py - c->y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
        float age = (c->kind ? 10.0f : 8.0f) - c->life;
        float pull = g->blade_on ? 260.0f : 170.0f;
        if (g->alive && age > 0.3f && d < pull) {
            float s = 1500.0f * (1.0f - d / pull) + 300.0f;
            c->vx = approach(c->vx, dx / d * s, dt * 10.0f);
            c->vy = approach(c->vy, dy / d * s, dt * 10.0f);
        } else {
            c->vx *= expf(-dt * 3.0f);
            c->vy *= expf(-dt * 3.0f);
            if (c->kind) c->vy += 30.0f * dt;
        }
        c->x = wrapx(g, c->x + c->vx * dt);
        c->y += c->vy * dt;
        c->y = clampf(c->y, terr_top(g, c->x) + 8.0f, terr_bot(g, c->x) - 8.0f);
        if (g->alive && d < 22.0f) {
            c->alive = 0;
            if (c->kind == 0) {
                g->energy = clampf(g->energy + 4.0f, 0, ENERGY_MAX);
                g->score += (long)(10.0f * g->mult);
                g->cry_streak++; g->cry_streak_t = 0.6f;
                sfxp(g, SFX_CRYSTAL, 128 + (g->cry_streak > 20 ? 20 : g->cry_streak) * 6);
                pglow(g, c->x, c->y, 0, 0, 0.15f, 20.0f, 0xFFE070D0u);
                for (int k = 0; k < 3; k++) spark(g, c->x, c->y, frange(g, 0, TAU), 160, 0.18f, 2.0f, 0xFFE070FFu);
            } else {
                if (g->shields < SHIELDS_MAX) g->shields++;
                else g->score += (long)(500.0f * g->mult);
                sfx(g, SFX_POWERUP);
                popup(g, sxof(g, g->px), g->py - 36.0f, "+1 heart", 0x6BFF8AFFu, 0);
                ring(g, g->px, g->py, 70.0f, 0.35f, 6.0f, 0x6BFF8AFFu);
            }
        }
    }
}

static void update_particles(Game *g, float dt)
{
    for (int i = 0; i < MAX_PT; i++) {
        Part *p = &g->pt[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0) { p->alive = 0; continue; }
        float k = expf(-p->drag * dt);
        p->vx *= k; p->vy *= k;
        if (p->kind == P_DEBRIS) p->vy += 160.0f * dt;
        if (p->kind == P_CONF) p->vy += 220.0f * dt;
        if (p->kind == P_SMOKE) p->size += dt * 14.0f;
        p->x = wrapx(g, p->x + p->vx * dt);
        p->y += p->vy * dt;
        p->rot += p->vr * dt;
    }
    for (int i = 0; i < MAX_RING; i++) {
        Ring *r = &g->rg[i];
        if (!r->alive) continue;
        r->life -= dt;
        if (r->life <= 0) { r->alive = 0; continue; }
        float t = 1.0f - r->life / r->max_life;
        r->r = r->maxr * (1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t));
    }
}

/* ------------------------------------------------------------------ */
/* waves & level flow                                                   */
/* ------------------------------------------------------------------ */
static void start_wave(Game *g, int n)
{
    g->wave = n;
    g->wave_t = 0;
    g->wave_hit = 0;
    memset(g->wave_spawned, 0, sizeof g->wave_spawned);
    const WaveDef *w = &g->L->waves[n];
    int boss = 0;
    for (int k = 0; k < w->ngroups; k++) boss |= IS_BOSS(w->g[k].type);
    g->banner_t = 2.4f;
    if (boss) {
        snprintf(g->banner, sizeof g->banner, "WARNING");
        snprintf(g->sub, sizeof g->sub, "something BIG is coming...");
        sfx(g, SFX_BOSS);
    } else {
        snprintf(g->banner, sizeof g->banner, "Wave %d", n + 1);
        snprintf(g->sub, sizeof g->sub, n + 1 == g->L->nwaves - 1 ? "last wave before the boss!" : "here they come!");
        sfx(g, SFX_WAVE);
    }
}

static void wave_complete(Game *g)
{
    int boss_wave = 0;
    const WaveDef *w = &g->L->waves[g->wave];
    for (int k = 0; k < w->ngroups; k++) boss_wave |= IS_BOSS(w->g[k].type);
    if (!g->wave_hit) {
        long bonus = 2500L * (long)(g->wave + 1);
        g->score += bonus;
        char buf[24];
        snprintf(buf, sizeof buf, "+%ld", bonus);
        popup(g, VW * 0.5f, 200.0f, "PERFECT!", 0xFFE070FFu, 1);
        popup(g, VW * 0.5f, 240.0f, buf, 0xFFFFFFFFu, 0);
        sfx(g, SFX_PERFECT);
        confetti(g, g->px, g->py, 40, 500.0f);
    } else if (!boss_wave) {
        popup(g, VW * 0.5f, 200.0f, "wave clear!", 0x9CFFD8FFu, 1);
        sfx(g, SFX_CLEAR);
    }
    g->wave++;
    if (g->wave >= g->L->nwaves) {
        g->level_done = 1;
        g->state_t = 0;
        g->banner_t = 3.0f;
        snprintf(g->banner, sizeof g->banner, "Level clear!");
        snprintf(g->sub, sizeof g->sub, "%s", g->L->name);
        sfx(g, SFX_CLEAR);
    } else {
        g->wave_gap = 2.3f;
    }
}

static void update_waves(Game *g, float dt)
{
    if (g->intro_t > 0) {
        g->intro_t -= dt;
        if (g->intro_t <= 0) start_wave(g, 0);
        return;
    }
    if (g->level_done) return;
    if (g->wave_gap > 0) {
        g->wave_gap -= dt;
        if (g->wave_gap <= 0) start_wave(g, g->wave);
        return;
    }
    g->wave_t += dt;
    const WaveDef *w = &g->L->waves[g->wave];
    int pending = 0, alive = alive_enemies(g);
    float next = 1e9f;
    for (int k = 0; k < w->ngroups; k++) {
        if (g->wave_spawned[k]) continue;
        if (w->g[k].delay <= g->wave_t) { spawn_group(g, &w->g[k]); g->wave_spawned[k] = 1; alive = 1; }
        else { pending = 1; if (w->g[k].delay < next) next = w->g[k].delay; }
    }
    if (pending && !alive && g->wave_t < next - 0.6f) g->wave_t = next - 0.6f;   /* nothing to shoot: hurry up */
    if (!pending && !alive && g->boss_dying <= 0) wave_complete(g);
}

static void update_boss_death(Game *g, float dt)
{
    if (g->boss_dying <= 0 || g->boss < 0) return;
    Enemy *e = &g->en[g->boss];
    g->boss_dying -= dt;
    e->flash = 0.5f + 0.5f * sinf(g->time * 40.0f);
    e->phase += dt * 3.0f;
    if ((g->frame % 5) == 0) {
        float a = frange(g, 0, TAU), r = frange(g, 0, 70);
        explode(g, e->x + cosf(a) * r, e->y + sinf(a) * r, frange(g, 0.8f, 1.6f), (rnd(g) & 1) ? 0xFF4FD8FFu : 0xA57BFFFFu, 0xFFE070FFu);
        sfxp(g, SFX_EXPL_M, 100 + (int)(rnd(g) % 60));
    }
    shake(g, 6.0f);
    if (g->boss_dying <= 0) {
        /* final detonation */
        explode(g, e->x, e->y, 5.0f, 0xFF4FD8FFu, 0x5CE1FFFFu);
        explode(g, e->x, e->y, 3.5f, 0xFFE070FFu, 0xFFFFFFFFu);
        for (int k = 0; k < 5; k++) ring(g, e->x, e->y, 300.0f + 140.0f * (float)k, 0.6f + 0.2f * (float)k, 26.0f - 4.0f * (float)k, col_hsv(0.15f * (float)k + 0.8f, 0.6f, 1.0f, 255));
        for (int k = 0; k < 60; k++) debris(g, e->x, e->y, 700.0f, frange(g, 8, 16), (k & 1) ? 0xA57BFFFFu : 0x3CC6E8FFu);
        confetti(g, e->x, e->y, 80, 700.0f);
        flash(g, 1.0f, 1, 1, 1);
        shake(g, 26.0f);
        g->zoom_kick = 0.08f;
        sfx(g, SFX_EXPL_L);
        drop_crystals(g, e->x, e->y, 40, 0);
        long pts = (long)(ESTAT[e->type].score * g->mult + 0.5f);
        g->score += pts;
        char buf[24];
        snprintf(buf, sizeof buf, "+%ld", pts);
        popup(g, VW * 0.5f, 250.0f, buf, 0xFFE070FFu, 1);
        g->kills++; g->chain++; g->chain_t = 2.4f;
        e->alive = 0;
        g->boss = -1;
        g->boss_dying = 0;
    }
}

static void update_scoring(Game *g, float dt)
{
    if (g->chain_t > 0) {
        g->chain_t -= dt;
        if (g->chain_t <= 0) {
            if (g->chain >= 5) {
                g->chain_bonus = (long)g->chain * (long)g->chain * 8L;
                g->score += g->chain_bonus;
                g->chain_show = 1.6f;
                sfxp(g, SFX_COMBO, 110);
            }
            g->chain = 0;
            g->milestone = 0;
        }
    }
    if (g->chain_show > 0) g->chain_show -= dt;
    long diff = g->score - g->shown_score;
    if (diff > 0) {
        long step = (long)((float)diff * clampf(dt * 12.0f, 0, 1)) + 1;
        g->shown_score += step > diff ? diff : step;
    }
}

static void update_camera(Game *g, float dt)
{
    float before = g->camx;
    g->cam_lead = approach(g->cam_lead, (float)g->face * 150.0f, dt * 3.0f);
    float target = g->px + g->cam_lead + g->pvx * 0.25f;
    g->camx = wrapx(g, g->camx + wd(g, target - g->camx) * clampf(dt * 5.0f, 0, 1));
    float sx = sxof(g, g->px);                             /* never let the ship leave the screen */
    if (sx < 110.0f) g->camx = wrapx(g, g->camx - (110.0f - sx));
    if (sx > VW - 110.0f) g->camx = wrapx(g, g->camx + (sx - (VW - 110.0f)));
    g->scroll += wd(g, g->camx - before);
}

static void update_feel(Game *g, float dt)
{
    g->shake *= expf(-dt * 7.0f);
    if (g->shake < 0.05f) g->shake = 0;
    g->flash -= dt * 2.6f;
    if (g->flash < 0) g->flash = 0;
    g->zoom_kick *= expf(-dt * 8.0f);
    g->mult_pulse -= dt * 3.0f;
    if (g->mult_pulse < 0) g->mult_pulse = 0;
    for (int k = 0; k < 4; k++) { g->wpn_flash[k] -= dt * 4.0f; if (g->wpn_flash[k] < 0) g->wpn_flash[k] = 0; }
    if (g->empty_t > 0) g->empty_t -= dt;
    if (g->banner_t > 0) g->banner_t -= dt;
    if (g->boss_intro > 0) g->boss_intro -= dt;
    if (g->fade > 0) g->fade -= dt * 2.5f;
    for (int i = 0; i < MAX_POP; i++) {
        Popup *p = &g->pp[i];
        if (!p->alive) continue;
        p->life -= dt;
        p->y += p->vy * dt;
        if (p->life <= 0) p->alive = 0;
    }
    if (g->boss >= 0 && g->en[g->boss].alive) {
        float hp = g->en[g->boss].hp / g->en[g->boss].maxhp;
        g->boss_shown_hp = approach(g->boss_shown_hp, hp, dt * 4.0f);
    }
}

/* ------------------------------------------------------------------ */
/* public                                                               */
/* ------------------------------------------------------------------ */
static void rank_run(Game *g)
{
    long s = g->score;
    g->rank = s >= 650000 ? 0 : s >= 450000 ? 1 : s >= 250000 ? 2 : 3;
    if (g->level < 8 && s > best_scores[g->level]) best_scores[g->level] = s;
    if (s > g->best_score) g->best_score = s;
}

void game_init(Game *g)
{
    int  sound = g->inited ? g->sound_on : 1;
    long best = g->best_score;
    float t = g->time;
    memset(g, 0, sizeof *g);
    g->inited = 1;
    g->sound_on = sound;
    g->best_score = best;
    g->time = t;
    g->rng = 0x9E3779B9u;
    g->state = ST_TITLE;
    g->level = 0;
    g->L = &LEVELS[0];
    g->W = g->L->width;
    build_terrain(g);
    g->boss = -1;
    g->fade = 1.0f;
    g->face = 1;
    g->title_sx = VW * 0.3f; g->py = 336.0f;
    g->boost_charges = BOOST_MAX;
    g->camx = 0;
    audio_init();
}

void game_start(Game *g, int level)
{
    int   sound = g->sound_on;
    uint32_t rng = g->rng;
    float t = g->time;
    long  best = g->best_score;
    memset(g, 0, sizeof *g);
    g->inited = 1;
    g->sound_on = sound;
    g->rng = rng ? rng : 1u;
    g->time = t;
    g->best_score = best;
    if (level < 0 || level >= NUM_LEVELS) level = 0;
    g->level = level;
    g->L = &LEVELS[level];
    g->W = g->L->width;
    build_terrain(g);
    g->state = ST_PLAY;
    g->face = 1;
    g->px = 0; g->py = 240;
    g->camx = g->px + 150.0f; g->cam_lead = 150.0f;
    g->shields = SHIELDS_MAX; g->lives = 2;
    g->boost_charges = BOOST_MAX;
    g->alive = 1;
    g->energy = ENERGY_MAX;
    g->mult = 1.0f; g->best_mult = 1.0f;
    g->boss = -1;
    g->intro_t = 2.6f;
    g->inv_t = 2.6f;
    g->banner_t = 2.6f;
    snprintf(g->banner, sizeof g->banner, "Level %d", level + 1);
    snprintf(g->sub, sizeof g->sub, "%s", g->L->name);
    g->fade = 1.0f;
    sfx(g, SFX_START);
}

void game_update(Game *g, const Input *in, float dt)
{
    uint32_t pressed = in->pressed;
    g->time += dt;
    g->frame++;
    g->state_t += dt;
    for (int k = 0; k < SFX_COUNT; k++) if (g->sfx_cd[k] > 0) g->sfx_cd[k] -= dt;

    switch (g->state) {
    case ST_TITLE: {
        g->camx = wrapx(g, g->camx + dt * 90.0f);
        g->scroll += dt * 90.0f;
        g->alive = 1;
        move_ship(g, in, pressed, dt, 1);
        if (g->trail_t -= dt, g->trail_t <= 0) {
            g->trail_t = 1.0f / 60.0f;
            memmove(&g->trail[1], &g->trail[0], sizeof(g->trail[0]) * (TRAIL_N - 1));
            g->trail[0][0] = g->px - (float)g->face * 40.0f; g->trail[0][1] = g->py;
            if (g->trail_n < TRAIL_N) g->trail_n++;
        }
        if (NUM_LEVELS > 1 && (pressed & (BTN_L | BTN_R))) {
            g->level = (g->level + ((pressed & BTN_R) ? 1 : NUM_LEVELS - 1)) % NUM_LEVELS;
            g->L = &LEVELS[g->level]; g->W = g->L->width; build_terrain(g);
            sfx(g, SFX_MOVE);
        }
        if ((g->frame % 4) == 0)
            spark(g, g->px - (float)g->face * 42.0f, g->py, (g->face > 0 ? PI_F : 0.0f) + frange(g, -0.2f, 0.2f), frange(g, 200, 400), 0.4f, 3.0f, (g->frame & 4) ? 0xFF9A2AFFu : 0xFFD23AFFu);
        if (pressed & (BTN_A | BTN_PLUS)) { game_start(g, g->level); return; }
        update_particles(g, dt);
        update_feel(g, dt);
        break;
    }
    case ST_PAUSE:
        if (pressed & BTN_UP)   { g->menu_sel = (g->menu_sel + 3) % 4; sfx(g, SFX_MOVE); }
        if (pressed & BTN_DOWN) { g->menu_sel = (g->menu_sel + 1) % 4; sfx(g, SFX_MOVE); }
        if (pressed & BTN_B)    { g->state = ST_PLAY; sfx(g, SFX_PAUSE); break; }
        if (pressed & (BTN_A | BTN_PLUS)) {
            if ((pressed & BTN_PLUS) && !(pressed & BTN_A)) { g->state = ST_PLAY; sfx(g, SFX_PAUSE); break; }
            switch (g->menu_sel) {
            case 0: g->state = ST_PLAY; sfx(g, SFX_PAUSE); break;
            case 1: game_start(g, g->level); break;
            case 2: g->sound_on = !g->sound_on; sfx(g, SFX_SELECT); break;
            case 3: { int lv = g->level; game_init(g); g->level = lv; sfx(g, SFX_SELECT); break; }
            }
        }
        break;
    case ST_PLAY: {
        if (pressed & BTN_PLUS) { g->state = ST_PAUSE; g->menu_sel = 0; sfx(g, SFX_PAUSE); return; }
        g->inbuf |= pressed;
        float wdt = dt;
        if (g->hitstop > 0) { g->hitstop -= dt; wdt = 0; }
        else if (g->slowmo_t > 0) { g->slowmo_t -= dt; wdt *= g->slowmo; }
        if (wdt > 0) {
            update_player(g, in, g->inbuf, wdt);
            g->inbuf = 0;
            update_enemies(g, wdt);
            update_level_bits(g, wdt);
            update_projectiles(g, wdt);
            update_pickups(g, wdt);
            update_particles(g, wdt);
            update_boss_death(g, wdt);
            update_waves(g, wdt);
            if (!g->level_done) g->play_time += wdt;
        }
        update_scoring(g, dt);
        update_camera(g, dt);
        update_feel(g, dt);
        if (!g->alive && g->lives == 0 && g->respawn_t <= 0 && g->dead_t > 0) {
            g->dead_t -= dt;
            if (g->dead_t <= 0) { g->state = ST_OVER; g->state_t = 0; rank_run(g); sfx(g, SFX_OVER); }
        }
        if (g->level_done && g->banner_t <= 0 && g->state_t > 3.0f) {
            g->state = ST_CLEAR; g->state_t = 0; g->fade = 0.6f;
            rank_run(g);
            sfx(g, SFX_START);
        }
        break;
    }
    case ST_OVER:
        update_particles(g, dt);
        update_feel(g, dt);
        if (g->state_t > 1.2f) {
            if (pressed & BTN_A) game_start(g, g->level);
            else if (pressed & BTN_B) { int lv = g->level; game_init(g); g->level = lv; }
        }
        break;
    case ST_CLEAR:
        update_particles(g, dt);
        update_feel(g, dt);
        if ((g->frame % 20) == 0 && g->state_t > 2.6f) confetti(g, frange(g, 100, VW - 100) + g->camx - VW * 0.5f, -20.0f, 6, 200.0f);
        if (g->state_t > 2.6f && g->state_t - dt <= 2.6f) { sfx(g, SFX_RANK); flash(g, 0.5f, 1, 1, 1); shake(g, 10.0f); }
        if (g->state_t > 3.0f && (pressed & (BTN_A | BTN_PLUS))) {
            if (g->level + 1 < NUM_LEVELS) game_start(g, g->level + 1);
            else { int lv = 0; game_init(g); g->level = lv; }
        }
        break;
    }
}

long game_best_score(int level) { return (level >= 0 && level < 8) ? best_scores[level] : 0; }
