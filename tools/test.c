/*
 * NOVA PRISM - native test harness (no Wii toolchain needed).
 *
 *  - software-rasterizes the exact triangle list the Wii's GPU gets (alpha + additive blending,
 *    clamped like the Wii's 8-bit framebuffer) and saves screenshots
 *  - plays the level with bots (several skill levels and random seeds) to check it can be beaten,
 *    that doing nothing loses, and to tune difficulty
 *  - measures CPU time and vertex counts per frame
 *
 *  usage: test <outdir> [quick]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../source/game.h"
#include "../source/game_int.h"
#include "../source/gfx.h"

#define OW 1280
#define OH 720
static float fb[OH][OW][3];
static const char *outdir = ".";

/* ------------------------------------------------------------------ */
/* software rasterizer                                                  */
/* ------------------------------------------------------------------ */
static void raster(void)
{
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) fb[y][x][0] = fb[y][x][1] = fb[y][x][2] = 0;
    const float S = (float)OW / VW;
    for (int s = 0; s < gfx_nseg; s++) {
        int add = gfx_seg[s].mode == BL_ADD;
        for (int i = gfx_seg[s].start; i + 2 < gfx_seg[s].start + gfx_seg[s].count; i += 3) {
            const GVtx *v = &gfx_v[i];
            float x0 = v[0].x * S, y0 = v[0].y * S, x1 = v[1].x * S, y1 = v[1].y * S, x2 = v[2].x * S, y2 = v[2].y * S;
            float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
            if (fabsf(area) < 1e-6f) continue;
            int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
            int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
            if (minx < 0) minx = 0; if (miny < 0) miny = 0;
            if (maxx > OW - 1) maxx = OW - 1; if (maxy > OH - 1) maxy = OH - 1;
            float c[3][4];
            for (int k = 0; k < 3; k++) {
                uint32_t col = v[k].c;
                c[k][0] = (float)(col >> 24) / 255.0f; c[k][1] = (float)((col >> 16) & 255) / 255.0f;
                c[k][2] = (float)((col >> 8) & 255) / 255.0f; c[k][3] = (float)(col & 255) / 255.0f;
            }
            float inv = 1.0f / area;
            for (int py = miny; py <= maxy; py++) {
                float fy = (float)py + 0.5f;
                for (int px = minx; px <= maxx; px++) {
                    float fx = (float)px + 0.5f;
                    float w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) * inv;
                    float w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) * inv;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float a = c[0][3] * w0 + c[1][3] * w1 + c[2][3] * w2;
                    for (int ch = 0; ch < 3; ch++) {
                        float src = c[0][ch] * w0 + c[1][ch] * w1 + c[2][ch] * w2;
                        float d = add ? fb[py][px][ch] + src * a : fb[py][px][ch] * (1 - a) + src * a;
                        fb[py][px][ch] = d > 1 ? 1 : (d < 0 ? 0 : d);
                    }
                }
            }
        }
    }
}

static void shot(const Game *g, const char *name)
{
    game_render(g);
    raster();
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", OW, OH);
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) {
        unsigned char p[3] = {(unsigned char)(fb[y][x][0] * 255.0f + 0.5f), (unsigned char)(fb[y][x][1] * 255.0f + 0.5f), (unsigned char)(fb[y][x][2] * 255.0f + 0.5f)};
        fwrite(p, 1, 3, f);
    }
    fclose(f);
    printf("  screenshot %s\n", name);
}

/* ------------------------------------------------------------------ */
/* bots                                                                 */
/* ------------------------------------------------------------------ */
typedef struct { const char *name; int skill; } Bot;   /* skill 0 = idle, 1 = average, 2 = keen */

static uint32_t brng = 12345;
static float bfr(void) { brng ^= brng << 13; brng ^= brng >> 17; brng ^= brng << 5; return (float)(brng >> 8) / 16777216.0f; }

static Input bot_input(const Game *g, int skill, uint32_t prev_held)
{
    Input in;
    memset(&in, 0, sizeof in);
    if (skill == 0 || g->state != ST_PLAY || !g->alive) return in;
    /* target: nearest live enemy, boss core preferred over its shields */
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->warp) continue;
        float dx = wd(g, e->x - g->px), dy = e->y - g->py, d = dx * dx + dy * dy;
        if (e->type == EN_SHIELD) d *= 3.0f;
        if (d < bd) { bd = d; best = i; }
    }
    float tx = g->px, ty = 240.0f;
    uint32_t held = 0;
    int near = 0, inline_n = 0, onscreen = 0;
    for (int i = 0; i < MAX_EN; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || e->warp) continue;
        float dx = wd(g, e->x - g->px), dy = e->y - g->py;
        if (dx * dx + dy * dy < 130.0f * 130.0f) near++;
        if (fabsf(dy) < 40.0f && dx * (float)g->face > 0 && fabsf(dx) < 900.0f) inline_n++;
        if (fabsf(dx) < 430.0f) onscreen++;
    }
    if (best >= 0) {
        const Enemy *e = &g->en[best];
        float dx = wd(g, e->x - g->px);
        float side = dx > 0 ? -1.0f : 1.0f;
        float keep = skill == 2 ? 230.0f : 260.0f;
        tx = g->px + dx + side * keep;
        ty = e->y;
        if ((dx > 0 ? 1 : -1) != g->face && !(prev_held & (BTN_L | BTN_R)) && fabsf(dx) > 60.0f) held |= BTN_L;
        if (fabsf(e->y - g->py) < 70.0f && fabsf(dx) < 800.0f) held |= BTN_A;
    }
    /* dodge enemy bullets */
    float ax = 0, ay = 0;
    for (int i = 0; i < MAX_EB; i++) {
        const EBullet *b = &g->eb[i];
        if (!b->alive) continue;
        float dx = wd(g, g->px - b->x), dy = g->py - b->y, d2 = dx * dx + dy * dy;
        float range = skill == 2 ? 130.0f : (skill == 3 ? 0.0f : 85.0f);
        if (d2 > range * range) continue;
        float closing = -(dx * b->vx + dy * b->vy);
        if (closing > 0) continue;
        float vl = sqrtf(b->vx * b->vx + b->vy * b->vy) + 0.01f;
        float px = -b->vy / vl, py = b->vx / vl;
        float s = (px * dx + py * dy) >= 0 ? 1.0f : -1.0f;
        ax += px * s * 2.0f; ay += py * s * 2.0f;
    }
    float mx = clampf(wd(g, tx - g->px) / 80.0f, -1, 1) + ax;
    float my = clampf((ty - g->py) / 50.0f, -1, 1) + ay;
    if (skill == 1) { mx += (bfr() - 0.5f) * 0.6f; my += (bfr() - 0.5f) * 0.6f; }
    if (skill == 3) {                      /* clumsy: wobbly, slow to line up, sometimes stops shooting */
        static float lagx, lagy;
        lagx += (mx - lagx) * 0.08f; lagy += (my - lagy) * 0.08f;
        mx = lagx + (bfr() - 0.5f) * 1.2f; my = lagy + (bfr() - 0.5f) * 1.2f;
        if (bfr() < 0.3f) held &= ~BTN_A;
    }
    in.sx = clampf(mx, -1, 1); in.sy = clampf(my, -1, 1);

    if (skill == 2 && g->heat > 0.9f) held &= ~BTN_A;
    if (skill == 3 && bfr() < 0.97f) { in.held = held; in.pressed = held & ~prev_held; return in; }
    if (g->energy >= 55.0f && onscreen >= 4 && !(prev_held & BTN_X)) held |= BTN_X;
    if (g->energy >= 45.0f && inline_n >= 3 && !(prev_held & BTN_Y)) held |= BTN_Y;
    if (g->boss >= 0 && g->energy >= 70.0f && inline_n >= 1 && !(prev_held & BTN_Y)) held |= BTN_Y;
    if ((near >= 2 || (skill == 2 && near >= 1 && g->shields <= 2)) && g->energy >= 25.0f) held |= BTN_B;
    if (g->blade_on && near >= 1) held |= BTN_B;
    in.held = held;
    in.pressed = held & ~prev_held;
    return in;
}

typedef struct { int cleared, deaths, wave; long score; float time; int rank; float max_mult; int max_chain; } Result;

static Game G;

static Result play(int skill, uint32_t seed, float max_time, int shots, const char *tag)
{
    Result r;
    memset(&r, 0, sizeof r);
    game_init(&G);
    G.rng = seed;
    brng = seed * 2654435761u + 1;
    game_start(&G, 0);
    uint32_t prev = 0;
    int lives0 = G.lives;
    float t = 0;
    int shot_n = 0, did_blade = 0, did_lance = 0, did_ms = 0, did_boss = 0, did_bossdie = 0, did_combo = 0;
    int was_alive = 1;
    while (t < max_time) {
        Input in = bot_input(&G, skill, prev);
        prev = in.held;
        game_update(&G, &in, 1.0f / 60.0f);
        while (game_pop_sound(&G, NULL) >= 0) {}
        t += 1.0f / 60.0f;
        if (!G.alive && was_alive) r.deaths++;
        was_alive = G.alive;
        if (shots) {
            char nm[64];
            if (!did_blade && G.blade_on && G.blade_t > 0.15f) { snprintf(nm, sizeof nm, "%s_blade", tag); shot(&G, nm); did_blade = 1; }
            if (!did_lance && G.lance_on && G.lance_t < 0.38f) { snprintf(nm, sizeof nm, "%s_lance", tag); shot(&G, nm); did_lance = 1; }
            int nm_ms = 0;
            for (int i = 0; i < MAX_MS; i++) nm_ms += G.ms[i].alive;
            if (!did_ms && nm_ms >= 6) { /* a few frames after launch */
                for (int k = 0; k < 10; k++) { Input z = bot_input(&G, skill, prev); prev = z.held; z.pressed = 0; game_update(&G, &z, 1.0f / 60.0f); t += 1.0f / 60.0f; }
                snprintf(nm, sizeof nm, "%s_missiles", tag); shot(&G, nm); did_ms = 1;
            }
            if (!did_boss && G.boss >= 0 && G.boss_intro > 0 && G.boss_intro < 1.5f) { snprintf(nm, sizeof nm, "%s_boss", tag); shot(&G, nm); did_boss = 1; }
            if (!did_bossdie && G.boss_dying > 0 && G.boss_dying < 2.4f) { snprintf(nm, sizeof nm, "%s_bossdeath", tag); shot(&G, nm); did_bossdie = 1; }
            if (!did_combo && G.milestone >= 10) {
                for (int k = 0; k < 12; k++) { Input z = bot_input(&G, skill, prev); prev = z.held; z.pressed = 0; game_update(&G, &z, 1.0f / 60.0f); t += 1.0f / 60.0f; }
                snprintf(nm, sizeof nm, "%s_combo", tag); shot(&G, nm); did_combo = 1;
            }
            if (shot_n < 4 && t > 12.0f + 25.0f * (float)shot_n) { snprintf(nm, sizeof nm, "%s_t%02d", tag, (int)t); shot(&G, nm); shot_n++; }
        }
        if (G.state == ST_CLEAR || G.state == ST_OVER) break;
    }
    if (shots && G.state == ST_CLEAR) {
        Input z; memset(&z, 0, sizeof z);
        for (int k = 0; k < 60 * 4; k++) game_update(&G, &z, 1.0f / 60.0f);
        char nm[64]; snprintf(nm, sizeof nm, "%s_results", tag); shot(&G, nm);
    }
    if (shots && G.state == ST_OVER) {
        Input z; memset(&z, 0, sizeof z);
        for (int k = 0; k < 90; k++) game_update(&G, &z, 1.0f / 60.0f);
        char nm[64]; snprintf(nm, sizeof nm, "%s_gameover", tag); shot(&G, nm);
    }
    r.cleared = G.state == ST_CLEAR;
    r.wave = G.wave;
    r.score = G.score;
    r.time = t;
    r.rank = G.rank;
    r.max_mult = G.best_mult;
    r.max_chain = G.max_chain;
    (void)lives0;
    return r;
}


/* ------------------------------------------------------------------ */
/* preview reel: renders a highlight video with the game's own sounds  */
/* ------------------------------------------------------------------ */
#define REEL_RATE 32000
static float *amix;
static int    amix_len;
static int    reel_frame;
static double reel_t;

static void reel_sound(int id, int pitch)
{
    const int16_t *d = audio_data(id);
    int n = audio_len(id);
    float vol = (float)audio_volume(id) / 255.0f * 0.5f;
    double step = (double)pitch / 128.0;
    int start = (int)(reel_t * REEL_RATE);
    for (int i = 0;; i++) {
        double src = (double)i * step;
        int k = (int)src;
        if (k >= n - 1 || start + i >= amix_len) break;
        float fr = (float)(src - k);
        float v = ((float)d[k] * (1 - fr) + (float)d[k + 1] * fr) / 32768.0f;
        amix[start + i] += v * vol;
    }
}

static void reel_step(Game *g, const Input *in, int record)
{
    game_update(g, in, 1.0f / 60.0f);
    int id, pitch;
    while ((id = game_pop_sound(g, &pitch)) >= 0) if (record) reel_sound(id, pitch);
    if (!record) return;
    if (g->frame & 1) {
        static FILE *pipe;
        static int done;
        if (done) return;
        if (!pipe) {
            char cmd[1024];
            snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - -vf scale=960:540 -c:v libx264 -pix_fmt yuv420p -crf 20 %s/reel/video.mp4", OW, OH, outdir);
            pipe = popen(cmd, "w");
        }
        reel_frame++;
        game_render(g);
        raster();
        static unsigned char row[OW * 3];
        for (int y = 0; y < OH; y++) {
            for (int x = 0; x < OW; x++) for (int c = 0; c < 3; c++) row[x * 3 + c] = (unsigned char)(fb[y][x][c] * 255.0f + 0.5f);
            fwrite(row, 1, sizeof row, pipe);
        }
        if (g->state == ST_CLEAR && g->state_t > 4.9f) { pclose(pipe); pipe = NULL; done = 1; }
    }
    reel_t += 1.0 / 60.0;
}

static void record_reel(void)
{
    amix_len = REEL_RATE * 90;
    amix = calloc((size_t)amix_len, sizeof(float));
    Game *g = &G;
    Input z; memset(&z, 0, sizeof z);
    game_init(g);
    g->rng = 1; brng = 2654435762u;
    for (int i = 0; i < 60 * 4; i++) { if (i == 200) { z.held = z.pressed = BTN_A; } else z.held = z.pressed = 0; reel_step(g, &z, 1); }
    uint32_t prev = 0;
    float t = 0;
    int seg = 0, boss_seen = 0;
    while (g->state == ST_PLAY && t < 900) {
        Input in = bot_input(g, 2, prev);
        prev = in.held;
        int rec = 0;
        if (t < 16.0f) rec = 1;                                  /* opening */
        else if (t > 92.0f && t < 106.0f) rec = 1, seg = 1;     /* mid-level chaos */
        if (g->boss >= 0 && !g->en[g->boss].warp) boss_seen = 1;
        if (boss_seen && g->en[g->boss >= 0 ? g->boss : 0].hp < g->en[g->boss >= 0 ? g->boss : 0].maxhp * 0.25f) rec = 1;
        if (g->boss_dying > 0 || g->level_done) rec = 1;
        reel_step(g, &in, rec);
        t += 1.0f / 60.0f;
    }
    for (int i = 0; i < 60 * 5 && g->state == ST_CLEAR; i++) reel_step(g, &z, 1);
    (void)seg;
    /* write the audio as a 16-bit WAV */
    char path[512];
    snprintf(path, sizeof path, "%s/reel/audio.wav", outdir);
    FILE *f = fopen(path, "wb");
    int n = (int)(reel_t * REEL_RATE);
    if (n > amix_len) n = amix_len;
    int bytes = n * 2;
    uint32_t hdr[11] = {0x46464952u, (uint32_t)(36 + bytes), 0x45564157u, 0x20746d66u, 16, 0x00010001u, REEL_RATE, REEL_RATE * 2, 0x00100002u, 0x61746164u, (uint32_t)bytes};
    fwrite(hdr, 4, 11, f);
    for (int i = 0; i < n; i++) {
        float v = amix[i];
        v = v / (1.0f + fabsf(v) * 0.6f) * 1.3f;                /* gentle limiter */
        if (v > 1) v = 1; if (v < -1) v = -1;
        int16_t s16 = (int16_t)(v * 30000.0f);
        fwrite(&s16, 2, 1, f);
    }
    fclose(f);
    printf("reel: %d frames, %.1f s of audio\n", reel_frame, reel_t);
}

/* ------------------------------------------------------------------ */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void press(Game *g, uint32_t b)
{
    Input in; memset(&in, 0, sizeof in);
    in.held = b; in.pressed = b;
    game_update(g, &in, 1.0f / 60.0f);
    memset(&in, 0, sizeof in);
    game_update(g, &in, 1.0f / 60.0f);
}
static void idle(Game *g, int frames)
{
    Input in; memset(&in, 0, sizeof in);
    for (int i = 0; i < frames; i++) { game_update(g, &in, 1.0f / 60.0f); while (game_pop_sound(g, NULL) >= 0) {} }
}

static void unit_tests(void)
{
    printf("unit tests\n");
    Game *g = &G;
    game_init(g);
    CHECK(g->state == ST_TITLE, "starts on title");
    idle(g, 90);
    shot(g, "title");
    press(g, BTN_A);
    CHECK(g->state == ST_PLAY, "A starts the game");
    idle(g, 60);
    shot(g, "intro");
    /* flip */
    int f0 = g->face;
    press(g, BTN_R);
    CHECK(g->face == -f0, "R turns around");
    press(g, BTN_L);
    CHECK(g->face == f0, "L turns around");
    /* blaster heat */
    Input in; memset(&in, 0, sizeof in);
    in.held = BTN_A;
    for (int i = 0; i < 120; i++) game_update(g, &in, 1.0f / 60.0f);
    CHECK(g->overheat, "holding A overheats the blaster");
    memset(&in, 0, sizeof in);
    idle(g, 150);
    CHECK(!g->overheat && g->heat < 0.01f, "blaster cools down");
    /* energy weapons */
    g->energy = 100;
    press(g, BTN_X);
    int nms = 0; for (int i = 0; i < MAX_MS; i++) nms += g->ms[i].alive;
    CHECK(nms == 8, "X launches 8 missiles (got %d)", nms);
    CHECK(fabsf(g->energy - 70.0f) < 1.0f, "missiles cost 30 energy (have %.1f)", (double)g->energy);
    press(g, BTN_Y);
    idle(g, 22);
    CHECK(g->lance_on, "Y fires the lance after a short charge");
    idle(g, 40);
    g->energy = 100;
    in.held = BTN_B; in.pressed = BTN_B;
    game_update(g, &in, 1.0f / 60.0f);
    in.pressed = 0;
    for (int i = 0; i < 30; i++) game_update(g, &in, 1.0f / 60.0f);
    CHECK(g->blade_on, "B holds the blade");
    memset(&in, 0, sizeof in);
    idle(g, 40);
    CHECK(!g->blade_on, "releasing B stops the blade");
    g->energy = 5;
    press(g, BTN_Y);
    CHECK(g->lance_charge <= 0 && !g->lance_on, "no lance without energy");
    /* pause */
    press(g, BTN_PLUS);
    CHECK(g->state == ST_PAUSE, "+ pauses");
    shot(g, "pause");
    press(g, BTN_PLUS);
    CHECK(g->state == ST_PLAY, "+ resumes");
    /* wrap-around maths */
    CHECK(fabsf(wd(g, g->W - 10.0f)) < 10.01f, "wrap offset");
    CHECK(wrapx(g, -5.0f) > g->W - 6.0f, "wrapx negative");
    /* sound events exist for every effect id */
    for (int id = 0; id < SFX_COUNT; id++) CHECK(audio_len(id) > 0, "sound %d synthesized", id);
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    if (argc > 2 && strcmp(argv[2], "reel") == 0) { record_reel(); return 0; }
    int quick = argc > 2;
    unit_tests();

    printf("\nbot runs (level 1)\n");
    Result r;
    r = play(0, 77, 400, 1, "idle");
    printf("  idle   : %s wave %d score %ld deaths %d time %.0fs\n", r.cleared ? "CLEARED" : "lost", r.wave + 1, r.score, r.deaths, (double)r.time);
    CHECK(!r.cleared, "doing nothing must lose");

    r = play(2, 1, 900, 1, "keen");
    printf("  keen   : %s wave %d score %ld deaths %d time %.0fs rank %c mult %.1f chain %d\n", r.cleared ? "CLEARED" : "lost", r.wave + 1, r.score, r.deaths,
           (double)r.time, "SABC"[r.rank], (double)r.max_mult, r.max_chain);

    if (!quick) {
        int seeds = 12;
        for (int skill = 1; skill <= 3; skill++) {
            int clears = 0, deaths = 0; long sc = 0; float tm = 0; int ranks[4] = {0};
            for (int s = 0; s < seeds; s++) {
                Result q = play(skill, 1000u + (uint32_t)s * 77u, 900, 0, "x");
                clears += q.cleared; deaths += q.deaths; sc += q.score; tm += q.time;
                if (q.cleared) ranks[q.rank]++;
            }
            printf("  %s x%d: cleared %d/%d, avg deaths %.1f, avg score %ld, avg time %.0fs, ranks S%d A%d B%d C%d\n",
                   skill == 1 ? "average" : (skill == 2 ? "keen   " : "clumsy "), seeds, clears, seeds, (double)deaths / seeds, sc / seeds, (double)tm / seeds,
                   ranks[0], ranks[1], ranks[2], ranks[3]);
        }
    }

    /* performance: worst frames of a keen run */
    printf("\nperformance (keen bot, whole level)\n");
    game_init(&G); G.rng = 5; game_start(&G, 0);
    uint32_t prev = 0;
    double worst_u = 0, worst_r = 0, sum_u = 0, sum_r = 0;
    int maxv = 0, maxseg = 0, frames = 0;
    for (int i = 0; i < 60 * 600 && G.state == ST_PLAY; i++) {
        Input in = bot_input(&G, 2, prev);
        prev = in.held;
        clock_t a = clock();
        game_update(&G, &in, 1.0f / 60.0f);
        clock_t b = clock();
        game_render(&G);
        clock_t c = clock();
        while (game_pop_sound(&G, NULL) >= 0) {}
        double du = (double)(b - a) / CLOCKS_PER_SEC * 1000.0, dr = (double)(c - b) / CLOCKS_PER_SEC * 1000.0;
        if (du > worst_u) worst_u = du;
        if (dr > worst_r) worst_r = dr;
        sum_u += du; sum_r += dr; frames++;
        if (gfx_nv > maxv) maxv = gfx_nv;
        if (gfx_nseg > maxseg) maxseg = gfx_nseg;
    }
    printf("  frames %d  update avg %.3f ms worst %.3f ms   render avg %.3f ms worst %.3f ms\n", frames, sum_u / frames, worst_u, sum_r / frames, worst_r);
    printf("  max vertices %d (%d triangles, limit %d)  max blend segments %d\n", maxv, maxv / 3, GFX_MAX_VTX, maxseg);
    CHECK(maxv < GFX_MAX_VTX - 3, "vertex buffer never full");

    printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "all checks passed", fails);
    return fails ? 1 : 0;
}
