/*
 * DOODLE SHOOTER - level definitions.
 *
 * To add a level, copy the LEVEL 1 block, change the name / colours / terrain seed and write its waves
 * (style: STYLE_SKYWAY = open sky over a racetrack, STYLE_CAVE = rocky crystal cave with a bumpy ceiling and floor),
 * then bump nothing else: NUM_LEVELS is computed automatically. Each wave is a list of spawn groups:
 *     { type, count, side, delay }
 * type  : EN_SHARD (blue spiky fish that chase you), EN_ZIP (fast mint swoopers, come in flocks),
 *         EN_SPORE (grumpy puffballs that spit), EN_SERPENT (caterpillar that pops segment by segment),
 *         EN_UFO (flying saucer, fires 3-shot bursts), EN_BOMB (fuse bomb: bursts into bullets unless popped),
 *         EN_BLOB (splits into two little blobs),
 *         EN_BOSS (the Hexacore) / EN_BOSS2 (the Big Head) - put a boss alone in the last wave
 * paper : 0 = ruled notebook paper, 1 = graph paper
 * side  : 0 = random side of the player, 1 = in front of the ship, -1 = behind it
 * delay : seconds after the wave starts
 * A wave ends when everything in it has been destroyed.
 */
#include "game.h"

#define C(r, g, b) ((uint32_t)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | 255u))

const LevelDef LEVELS[] = {
    /* ---------------------------------------------------------------- LEVEL 1 */
    {
        .name    = "NOTEBOOK NEBULA",
        .boss_sub = "the grumpiest shape in space",
        .style   = STYLE_SKYWAY,
        .seed    = 0x5EED1234u,
        .width   = 3416.0f,             /* four screens around */
        .nterr   = 28,
        .rough   = 0.75f,
        .sky_top = C(66, 10, 94),  .sky_bot = C(62, 14, 78),
        .blob    = C(30, 6, 46),   .blob_hi = C(48, 14, 70),
        .terr    = C(232, 46, 110), .terr_hi = C(255, 104, 150), .terr_lo = C(170, 26, 92),
        .glyph   = C(84, 102, 230),
        /* skyway look: magenta dusk sky, violet mountains, a city skyline, and a lamp-lined track */
        .sky_mid = C(140, 30, 140), .horizon = C(196, 86, 178), .cloud = C(255, 150, 214),
        .mountain = C(66, 40, 128), .mountain_hi = C(98, 62, 162),
        .city    = C(40, 26, 92),  .city_hi = C(62, 42, 124), .window = C(255, 214, 110),
        .grass   = C(24, 92, 84),  .grass_hi = C(70, 170, 120),
        .track   = C(92, 86, 116), .track_hi = C(108, 102, 134), .rail = C(46, 36, 82), .lamp = C(255, 214, 64),
        .nwaves  = 10,
        .waves = {
            /* 1: a gentle warm-up from both sides */
            { 2, { {EN_SHARD, 6, 1, 0.0f}, {EN_SHARD, 6, -1, 4.0f} } },
            /* 2: zip flocks */
            { 3, { {EN_ZIP, 8, 1, 0.0f}, {EN_ZIP, 8, -1, 3.0f}, {EN_SHARD, 8, 0, 6.0f} } },
            /* 3: first shooters */
            { 3, { {EN_SPORE, 2, 1, 0.0f}, {EN_SHARD, 10, -1, 2.0f}, {EN_ZIP, 8, 1, 6.0f} } },
            /* 4: pincer */
            { 4, { {EN_SHARD, 10, 1, 0.0f}, {EN_SHARD, 10, -1, 0.5f}, {EN_SPORE, 2, 0, 4.0f}, {EN_ZIP, 10, 0, 8.0f} } },
            /* 5: the serpent */
            { 3, { {EN_SERPENT, 1, 1, 0.0f}, {EN_ZIP, 8, -1, 3.0f}, {EN_ZIP, 8, 1, 8.0f} } },
            /* 6: crossfire */
            { 4, { {EN_SPORE, 4, 0, 0.0f}, {EN_SHARD, 12, 1, 2.0f}, {EN_ZIP, 12, -1, 6.0f}, {EN_SHARD, 10, 0, 10.0f} } },
            /* 7: swarm */
            { 4, { {EN_SHARD, 14, 1, 0.0f}, {EN_ZIP, 12, -1, 1.5f}, {EN_SHARD, 14, -1, 5.0f}, {EN_ZIP, 12, 1, 7.0f} } },
            /* 8: twin serpents */
            { 3, { {EN_SERPENT, 2, 0, 0.0f}, {EN_SPORE, 3, 1, 4.0f}, {EN_SHARD, 10, -1, 8.0f} } },
            /* 9: everything at once */
            { 5, { {EN_SPORE, 4, 1, 0.0f}, {EN_SERPENT, 1, -1, 2.0f}, {EN_SHARD, 14, 0, 4.0f}, {EN_ZIP, 12, 1, 7.0f}, {EN_SPORE, 3, -1, 10.0f} } },
            /* 10: the Hexacore */
            { 1, { {EN_BOSS, 1, 1, 0.0f} } },
        },
    },
    /* ---------------------------------------------------------------- LEVEL 2 */
    {
        .name    = "GRAPH PAPER GALAXY",
        .boss_sub = "watch out for the tentacles!",
        .style   = STYLE_SKYWAY,
        .paper   = 1,
        .seed    = 0x2B0B5u,
        .width   = 3416.0f,
        .nterr   = 28,
        .rough   = 0.75f,
        .nwaves  = 10,
        .waves = {
            /* 1: old friends from both sides */
            { 3, { {EN_SHARD, 8, 1, 0.0f}, {EN_SHARD, 8, -1, 2.5f}, {EN_ZIP, 8, 0, 5.0f} } },
            /* 2: flying saucers! */
            { 3, { {EN_UFO, 2, 1, 0.0f}, {EN_SHARD, 8, -1, 2.0f}, {EN_UFO, 1, -1, 6.0f} } },
            /* 3: fuse bombs - pop them before they go off */
            { 3, { {EN_BOMB, 4, 1, 0.0f}, {EN_ZIP, 10, -1, 2.0f}, {EN_BOMB, 3, -1, 6.0f} } },
            /* 4: blobs that split in two */
            { 3, { {EN_BLOB, 3, 1, 0.0f}, {EN_SHARD, 10, -1, 2.0f}, {EN_BLOB, 2, -1, 6.0f} } },
            /* 5: caterpillar with saucer escorts */
            { 3, { {EN_SERPENT, 1, 1, 0.0f}, {EN_UFO, 2, -1, 3.0f}, {EN_ZIP, 10, 0, 7.0f} } },
            /* 6: bombs, spores and blobs */
            { 4, { {EN_BOMB, 4, 0, 0.0f}, {EN_SPORE, 3, 1, 2.0f}, {EN_BLOB, 3, -1, 5.0f}, {EN_SHARD, 10, 0, 9.0f} } },
            /* 7: swarm */
            { 4, { {EN_ZIP, 14, 1, 0.0f}, {EN_SHARD, 12, -1, 1.5f}, {EN_UFO, 2, 0, 4.0f}, {EN_BOMB, 4, 0, 7.0f} } },
            /* 8: twin caterpillars + bombs */
            { 3, { {EN_SERPENT, 2, 0, 0.0f}, {EN_BOMB, 5, 1, 4.0f}, {EN_BLOB, 2, -1, 8.0f} } },
            /* 9: everything at once */
            { 6, { {EN_UFO, 3, 1, 0.0f}, {EN_BLOB, 3, -1, 1.5f}, {EN_SPORE, 3, 0, 3.0f}, {EN_BOMB, 4, 0, 5.0f}, {EN_SHARD, 12, 0, 7.0f}, {EN_ZIP, 10, 0, 10.0f} } },
            /* 10: the Big Head */
            { 1, { {EN_BOSS2, 1, 1, 0.0f} } },
        },
    },
};

const int NUM_LEVELS = (int)(sizeof LEVELS / sizeof LEVELS[0]);
