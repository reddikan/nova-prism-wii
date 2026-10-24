/*
 * NOVA PRISM - level definitions.
 *
 * To add a level, copy the LEVEL 1 block, change the name / colours / terrain seed and write its waves
 * (style: STYLE_SKYWAY = open sky over a racetrack, STYLE_CAVE = rocky crystal cave with a bumpy ceiling and floor),
 * then bump nothing else: NUM_LEVELS is computed automatically. Each wave is a list of spawn groups:
 *     { type, count, side, delay }
 * type  : EN_SHARD (blue drills that chase you), EN_ZIP (fast green swoopers, come in flocks),
 *         EN_SPORE (purple spiked orbs that shoot), EN_SERPENT (segmented crystal worm),
 *         EN_BOSS (the Hexacore - put it alone in the last wave)
 * side  : 0 = random side of the player, 1 = in front of the ship, -1 = behind it
 * delay : seconds after the wave starts
 * A wave ends when everything in it has been destroyed.
 */
#include "game.h"

#define C(r, g, b) ((uint32_t)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | 255u))

const LevelDef LEVELS[] = {
    /* ---------------------------------------------------------------- LEVEL 1 */
    {
        .name    = "NEON SKYWAY",
        .boss_sub = "GUARDIAN OF THE SKYWAY",
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
};

const int NUM_LEVELS = (int)(sizeof LEVELS / sizeof LEVELS[0]);
