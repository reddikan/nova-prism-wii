/*
 * NOVA PRISM - procedurally synthesized sound effects.
 *
 * No audio files: every effect is generated at start-up from FM / band-limited
 * oscillators and filtered noise. Samples are 16-bit signed mono at SFX_RATE Hz
 * in the host's byte order (big-endian on the Wii, which is what ASND wants).
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define SFX_RATE 32000

enum {
    SFX_SHOT, SFX_OVERHEAT, SFX_BLADE, SFX_MISSILE, SFX_CHARGE, SFX_LANCE,
    SFX_HIT, SFX_EXPL_S, SFX_EXPL_M, SFX_EXPL_L, SFX_CRYSTAL, SFX_HURT, SFX_DIE,
    SFX_FLIP, SFX_WARP, SFX_ESHOT, SFX_DEFLECT, SFX_WAVE, SFX_BOSS, SFX_CLEAR,
    SFX_PERFECT, SFX_COMBO, SFX_EMPTY, SFX_POWERUP, SFX_SELECT, SFX_MOVE,
    SFX_START, SFX_PAUSE, SFX_OVER, SFX_RANK,
    SFX_COUNT
};

void           audio_init(void);               /* synthesize every effect (call once) */
const int16_t *audio_data(int id);             /* 32-byte aligned sample buffer */
int            audio_len(int id);              /* length in samples (multiple of 16) */
int            audio_volume(int id);           /* suggested playback volume 0..255 */

#endif
