/*
 * NOVA PRISM - a bright, low-poly side-view space shooter for the Wii.
 *
 * game.c / levels.c / gfx.c / audio.c are plain C99 and know nothing about the
 * console. The game takes its input as a BTN_* bitmask plus an analog stick,
 * draws into the gfx triangle list, and emits sound events that the platform
 * layer (main.c) plays.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "audio.h"

/* ---- input ---- */
enum {
    BTN_UP    = 1 << 0,
    BTN_DOWN  = 1 << 1,
    BTN_LEFT  = 1 << 2,
    BTN_RIGHT = 1 << 3,
    BTN_A     = 1 << 4,     /* Pulse Blaster  */
    BTN_B     = 1 << 5,     /* Nova Blade     */
    BTN_X     = 1 << 6,     /* Swarm Missiles */
    BTN_Y     = 1 << 7,     /* Prism Lance    */
    BTN_L     = 1 << 8,     /* turn around    */
    BTN_R     = 1 << 9,     /* turn around    */
    BTN_PLUS  = 1 << 10     /* pause          */
};

typedef struct {
    uint32_t held, pressed;
    float    sx, sy;        /* left stick, -1..1 (y down is +) */
} Input;

/* ---- world ---- */
#define MAX_EN      96
#define MAX_PB      160     /* player bolts */
#define MAX_MS      24      /* missiles */
#define MAX_EB      240     /* enemy bullets */
#define MAX_PT      1400    /* particles */
#define MAX_RING    48
#define MAX_CRY     160     /* crystals */
#define MAX_POP     32      /* floating text */
#define TRAIL_N     28
#define TERR_N      64      /* terrain control points per level (max) */
#define MAX_GROUPS  12
#define MAX_WAVES   12

enum { EN_SHARD, EN_ZIP, EN_SPORE, EN_SERPENT, EN_SEG, EN_BOSS, EN_SHIELD, EN_TYPES };

/* A spawn group inside a wave: `count` enemies of `type` appear `delay` seconds after the wave starts.
 * side: 0 = either side of the player (random), 1 = ahead, -1 = behind. */
typedef struct { uint8_t type, count; int8_t side; float delay; } SpawnGroup;
typedef struct { int ngroups; SpawnGroup g[MAX_GROUPS]; } WaveDef;

typedef struct {
    const char *name;
    uint32_t    seed;                    /* terrain shape */
    float       width;                   /* world circumference (the level wraps around) */
    int         nterr;                   /* terrain control points */
    float       rough;                   /* 0..1 terrain bumpiness */
    /* colours (0xRRGGBBAA) */
    uint32_t    sky_top, sky_bot;        /* background gradient */
    uint32_t    blob, blob_hi;           /* far parallax shapes */
    uint32_t    terr, terr_hi, terr_lo;  /* terrain base / lit bevel / shadow facet */
    uint32_t    glyph;                   /* drifting background confetti */
    int         nwaves;
    WaveDef     waves[MAX_WAVES];
} LevelDef;

extern const LevelDef LEVELS[];
extern const int      NUM_LEVELS;

typedef struct {
    uint8_t alive, type, warp;           /* warp: still materialising */
    float   x, y, vx, vy, ang, hp, maxhp;
    float   t, warp_t, flash, cd, phase, hitcd, fuse, r;
    int     parent, head, idx;           /* serpent: link to the piece in front, head, segment number; shield: boss + slot */
    int     attack;                      /* boss: attack pattern counter */
    float   aux;                         /* per-type scratch (zip: cruise height, boss: spiral timer) */
} Enemy;

typedef struct { uint8_t alive; float x, y, vx, vy, life, dmg; } Bolt;
typedef struct { uint8_t alive; float x, y, vx, vy, life, speed; int target; float smoke_t, wob; } Missile;
typedef struct { uint8_t alive, big; float x, y, vx, vy, life; } EBullet;
typedef struct { uint8_t alive, kind; float x, y, vx, vy, life, max_life, size, rot, vr, drag; uint32_t col; } Part;
typedef struct { uint8_t alive; float x, y, r, maxr, life, max_life, thick; uint32_t col; } Ring;
typedef struct { uint8_t alive, kind; float x, y, vx, vy, life, spin; } Crystal;   /* kind 0 energy, 1 shield */
typedef struct { uint8_t alive, big; float x, y, vy, life, max_life; uint32_t col; char txt[24]; } Popup;

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER, ST_CLEAR } GameState;

typedef struct {
    GameState state;
    float     state_t, fade;
    uint32_t  rng, frame;
    float     time;                       /* total real time (for animation) */

    /* level */
    int   level;
    const LevelDef *L;
    float W;                              /* world width */
    float ttop[TERR_N], tbot[TERR_N];     /* terrain heights at control points */
    int   wave;                           /* 0-based; == nwaves means done */
    float wave_t;                         /* time since wave start */
    int   wave_spawned[MAX_GROUPS];
    float wave_gap;                       /* countdown between waves */
    int   wave_hit;                       /* player damaged during this wave */
    float banner_t; char banner[32]; char sub[40];
    int   level_done;
    float intro_t;

    /* player */
    float px, py, pvx, pvy;
    int   face;                           /* +1 right, -1 left */
    float flip_t, flip_dir;               /* turn-around animation 1 -> 0 */
    float tilt;
    int   shields, lives;
    float inv_t, dead_t, respawn_t;
    int   alive;
    float heat; int overheat; float shot_cd; int shot_side;
    float energy;
    int   blade_on; float blade_ang, blade_t, blade_spin;
    float lance_charge; int lance_on; float lance_t, lance_tick, lance_y; int lance_dir;
    float trail[TRAIL_N][2]; int trail_n; float trail_t;
    float wpn_flash[4];                   /* HUD tile flash for A B X Y */
    float empty_t, empty_wpn;
    uint32_t inbuf;                       /* button presses buffered across hit-stop frames */
    int   cry_streak; float cry_streak_t;  /* rising pitch when hoovering up crystals */
    int   milestone;                      /* last combo milestone announced */
    float boss_intro;                     /* boss name card */

    /* camera & feel */
    float camx, cam_lead;
    float shake, shake_t, zoom_kick;
    float flash, flash_r, flash_g, flash_b;
    float hitstop, slowmo, slowmo_t;

    /* scoring */
    long  score, shown_score;
    float mult, mult_pulse, best_mult;
    int   chain; float chain_t; long chain_bonus; float chain_show;
    int   kills, max_chain;
    float play_time;

    /* boss */
    int   boss;                           /* enemy index or -1 */
    float boss_dying, boss_shown_hp;

    Enemy   en[MAX_EN];
    Bolt    pb[MAX_PB];
    Missile ms[MAX_MS];
    EBullet eb[MAX_EB];
    Part    pt[MAX_PT];
    Ring    rg[MAX_RING];
    Crystal cr[MAX_CRY];
    Popup   pp[MAX_POP];
    int     pt_next;

    /* menus */
    int   menu_sel;
    int   rank;                           /* results: 0 S .. 3 C */
    long  best_score;

    /* sound events */
    uint8_t sfxq[48], sfxp[48];
    int     sfx_head, sfx_tail;
    float   sfx_cd[SFX_COUNT];
    int     sound_on, inited;
} Game;

void game_init(Game *g);
void game_start(Game *g, int level);
void game_update(Game *g, const Input *in, float dt);
void game_render(const Game *g);           /* fills the gfx triangle list */
/* next sound to play: returns SFX id or -1; *pitch = playback speed in 1/128ths (128 = normal) */
int  game_pop_sound(Game *g, int *pitch);
long game_best_score(int level);           /* best score on a level this session */

#endif
