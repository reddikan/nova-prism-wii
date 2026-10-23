/*
 * NOVA PRISM - sound effect synthesis. See audio.h.
 *
 * Every effect is built at start-up from a small synth (band-limited and FM oscillators, resonant filtered
 * noise and a short echo), so no audio files are needed. Bright, punchy arcade-sci-fi sounds.
 */
#include "audio.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXLEN 64000                       /* 2.0 s per effect at 32 kHz, a multiple of 16 so rows stay 32-byte aligned */

static int16_t SFX[SFX_COUNT][MAXLEN] __attribute__((aligned(32)));
static int     sfx_len[SFX_COUNT];
static float   mixbuf[MAXLEN];
static float   outbuf[MAXLEN];
static int     mixend;
static uint32_t rng = 0x2545F491u;
static int     ready;

/* playback volume per effect (every effect is normalised first, then scaled by this to balance loudness) */
static const uint8_t VOL[SFX_COUNT] = {
    /* SHOT */ 70,  /* OVERHEAT */ 170, /* BLADE */ 120, /* MISSILE */ 170, /* CHARGE */ 150, /* LANCE */ 235,
    /* HIT */ 60,   /* EXPL_S */ 150,   /* EXPL_M */ 210, /* EXPL_L */ 250,  /* CRYSTAL */ 90,  /* HURT */ 220,
    /* DIE */ 240,  /* FLIP */ 90,      /* WARP */ 80,    /* ESHOT */ 90,    /* DEFLECT */ 90,  /* WAVE */ 190,
    /* BOSS */ 240, /* CLEAR */ 200,    /* PERFECT */ 210, /* COMBO */ 190,  /* EMPTY */ 140,   /* POWERUP */ 190,
    /* SELECT */ 120, /* MOVE */ 80,    /* START */ 190,  /* PAUSE */ 120,   /* OVER */ 220,    /* RANK */ 230
};

/* ------------------------------------------------------------------ */
/* Building blocks                                                     */
/* ------------------------------------------------------------------ */
enum { W_SIN, W_TRI, W_SOFT, W_FM };           /* oscillator shapes */
enum { F_LP, F_BP, F_HP };                      /* noise filter types */

#define SR ((float)SFX_RATE)
#define TABN 4096
static float sine_tab[TABN + 1];

static inline float sine01(float ph)            /* ph in [0,1) */
{
    float x = ph * (float)TABN;
    int i = (int)x;
    float fr = x - (float)i;
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * fr;
}

static float frand(void)                        /* -1 .. 1 */
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

/*
 * One pitched voice mixed into the effect being built.
 *   start, dur : seconds
 *   f0 -> f1   : exponential pitch glide in Hz
 *   atk        : attack time (s);  tau : decay time constant (s), 0 = sustained
 *   W_SIN      : pure sine
 *   W_TRI      : soft triangle (sine + a few odd harmonics)
 *   W_SOFT     : warm band-limited saw; a = extra high-harmonic roll-off (0 bright .. 2 dark)
 *   W_FM       : two-operator FM; a = modulator/carrier ratio, b = peak modulation index (fades with the note)
 */
static void tone(float start, float dur, float f0, float f1, float vol, float atk, float tau, int wave, float a, float b)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float ph = 0.0f, pm = 0.0f;
    float glide = (f1 == f0) ? 1.0f : powf(f1 / f0, 1.0f / (float)(n > 1 ? n : 1));
    float f = f0, hw[10];
    for (int h = 1; h <= 9; h++) hw[h] = 1.0f / powf((float)h, 1.0f + a);
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        float v;
        switch (wave) {
        case W_TRI:
            v = sine01(ph) + 0.111f * sine01(ph * 3.0f - floorf(ph * 3.0f)) * -1.0f + 0.04f * sine01(ph * 5.0f - floorf(ph * 5.0f));
            break;
        case W_SOFT: {
            v = 0.0f;
            int nh = (int)(0.42f * SR / (f > 1.0f ? f : 1.0f));
            if (nh > 9) nh = 9;
            for (int h = 1; h <= nh; h++) {
                float x = ph * (float)h;
                v += sine01(x - floorf(x)) * hw[h];
            }
            break;
        }
        case W_FM: {
            float idx = b * env;
            float m = sine01(pm);
            float x = ph + idx * m / (2.0f * (float)M_PI) * 2.0f;
            x -= floorf(x);
            v = sine01(x);
            break;
        }
        default:
            v = sine01(ph);
            break;
        }
        mixbuf[s0 + i] += v * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
        pm += f * a / SR; pm -= floorf(pm);
        f *= glide;
        env *= k;
    }
}

/* resonant filtered noise: fc0 -> fc1 is the filter cutoff sweep in Hz, q the resonance */
static void noise(float start, float dur, float fc0, float fc1, float q, float vol, float atk, float tau, int mode)
{
    int s0 = (int)(start * SR), n = (int)(dur * SR);
    float z1 = 0.0f, z2 = 0.0f;                  /* transposed direct form II state */
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float env = 1.0f, k = (tau > 0.0f) ? expf(-1.0f / (tau * SR)) : 1.0f;
    float atk_n = atk * SR > 1.0f ? atk * SR : 1.0f;
    float rel_n = 0.006f * SR;
    float ratio = (fc1 == fc0) ? 1.0f : powf(fc1 / fc0, 1.0f / (float)(n > 1 ? n : 1));
    float fc = fc0;
    for (int i = 0; i < n && s0 + i < MAXLEN; i++) {
        if ((i & 7) == 0) {                       /* retune the filter every 8 samples */
            float w = 2.0f * (float)M_PI * (fc < 0.45f * SR ? fc : 0.45f * SR) / SR;
            float cw = cosf(w), alpha = sinf(w) / (2.0f * q), a0 = 1.0f + alpha;
            switch (mode) {
            case F_LP: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; break;
            case F_HP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; break;
            default:   b0 = alpha; b1 = 0; b2 = -alpha; break;
            }
            a1 = -2.0f * cw / a0; a2 = (1.0f - alpha) / a0;
            b0 /= a0; b1 /= a0; b2 /= a0;
        }
        float x = frand();
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        float amp = env;
        if ((float)i < atk_n) amp *= (float)i / atk_n;
        if ((float)(n - i) < rel_n) amp *= (float)(n - i) / rel_n;
        mixbuf[s0 + i] += y * amp * vol;
        if (s0 + i + 1 > mixend) mixend = s0 + i + 1;
        env *= k;
        fc *= ratio;
    }
}

static void begin(void)
{
    memset(mixbuf, 0, sizeof mixbuf);
    mixend = 0;
}

/* mix down: a clean two-tap echo for a little space (no reverb noise), normalise, 16-bit. No clipping or saturation. */
static void finish_fx(int id, float wet, float room)
{
    int d1 = (int)((0.045f + 0.07f * room) * SR), d2 = d1 * 2 + (int)(0.013f * SR);
    int tail = (wet > 0.0f) ? d2 + (int)(0.05f * SR) : (int)(0.03f * SR);
    int len = mixend + tail;
    if (len > MAXLEN) len = MAXLEN;
    float peak = 0.0f;
    for (int i = 0; i < len; i++) {
        float x = (i < mixend) ? mixbuf[i] : 0.0f;
        if (i >= d1 && i - d1 < mixend) x += wet * 0.6f * mixbuf[i - d1];
        if (i >= d2 && i - d2 < mixend) x += wet * 0.3f * mixbuf[i - d2];
        outbuf[i] = x;
        if (fabsf(x) > peak) peak = fabsf(x);
    }
    int last = len;                                    /* trim the silent tail */
    while (last > 1 && fabsf(outbuf[last - 1]) < peak * 0.003f) last--;
    last += (int)(0.02f * SR);
    if (last < len) len = last;
    int fade = (int)(0.02f * SR);
    float scale = peak > 0.0f ? 0.85f * 32000.0f / peak : 0.0f;
    int outlen = (len + 15) & ~15;
    if (outlen > MAXLEN) outlen = MAXLEN;
    memset(SFX[id], 0, sizeof SFX[id]);
    for (int i = 0; i < len && i < MAXLEN; i++) {
        float x = outbuf[i];
        if (len - i < fade) x *= (float)(len - i) / (float)fade;
        SFX[id][i] = (int16_t)(x * scale);
    }
    sfx_len[id] = outlen;
}

#define C4 261.63f
#define D4 293.66f
#define EB4 311.13f
#define F4 349.23f
#define G4 392.00f
#define A4 440.00f
#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define G5 783.99f
#define A5 880.00f
#define B5 987.77f
#define C6 1046.50f
#define E6 1318.51f
#define G6 1567.98f
#define C7 2093.00f

/* a bell/ping: FM with a slightly inharmonic ratio */
static void bell(float t, float f, float vol, float tau)
{
    tone(t, tau * 5.0f, f, f, vol, 0.002f, tau, W_FM, 3.0f, 1.1f);
    tone(t, tau * 3.0f, f * 2.0f, f * 2.0f, vol * 0.25f, 0.002f, tau * 0.5f, W_SIN, 0, 0);
}

void audio_init(void)
{
    if (ready) return;
    ready = 1;
    for (int i = 0; i <= TABN; i++) sine_tab[i] = sinf((float)i * 2.0f * (float)M_PI / (float)TABN);

    /* --- weapons --- */
    begin();                                                         /* blaster: short bright "pew" */
    tone(0, 0.09f, 2400, 520, 0.55f, 0.001f, 0.035f, W_FM, 1.0f, 2.2f);
    tone(0, 0.07f, 1200, 300, 0.3f, 0.001f, 0.025f, W_SIN, 0, 0);
    finish_fx(SFX_SHOT, 0.15f, 0.1f);

    begin();                                                         /* overheat: hiss + falling whine */
    noise(0, 0.6f, 6000, 2000, 0.7f, 0.5f, 0.01f, 0.25f, F_HP);
    tone(0, 0.5f, 1400, 300, 0.4f, 0.005f, 0.2f, W_FM, 1.0f, 2.0f);
    finish_fx(SFX_OVERHEAT, 0.2f, 0.3f);

    begin();                                                         /* blade: airy whoosh with a ring */
    noise(0, 0.26f, 600, 3800, 2.5f, 0.8f, 0.05f, 0.0f, F_BP);
    tone(0, 0.26f, 900, 1400, 0.15f, 0.04f, 0.15f, W_SIN, 0, 0);
    finish_fx(SFX_BLADE, 0.2f, 0.2f);

    begin();                                                         /* missiles: rapid launch thumps + hiss */
    for (int i = 0; i < 4; i++) {
        tone(0.04f * (float)i, 0.12f, 320, 110, 0.5f, 0.001f, 0.05f, W_SIN, 0, 0);
        noise(0.04f * (float)i, 0.25f, 1500, 5000, 1.2f, 0.35f, 0.005f, 0.09f, F_BP);
    }
    finish_fx(SFX_MISSILE, 0.25f, 0.3f);

    begin();                                                         /* lance charge: rising shimmer */
    tone(0, 0.32f, 300, 2400, 0.4f, 0.02f, 0.0f, W_FM, 2.0f, 1.5f);
    tone(0, 0.32f, 600, 4800, 0.2f, 0.02f, 0.0f, W_SIN, 0, 0);
    finish_fx(SFX_CHARGE, 0.2f, 0.2f);

    begin();                                                         /* lance: huge sustained zap */
    tone(0, 0.7f, 110, 55, 0.9f, 0.002f, 0.3f, W_SOFT, 0.3f, 0);
    tone(0, 0.6f, 1800, 400, 0.45f, 0.002f, 0.25f, W_FM, 1.5f, 4.0f);
    tone(0, 0.5f, 3600, 900, 0.2f, 0.002f, 0.2f, W_FM, 1.0f, 2.0f);
    noise(0, 0.5f, 3000, 600, 0.8f, 0.4f, 0.002f, 0.18f, F_LP);
    finish_fx(SFX_LANCE, 0.4f, 0.6f);

    /* --- hits & explosions --- */
    begin();                                                         /* hit: tiny tick */
    tone(0, 0.04f, 1600, 900, 0.5f, 0.001f, 0.012f, W_SIN, 0, 0);
    noise(0, 0.03f, 4000, 4000, 1.0f, 0.3f, 0.001f, 0.01f, F_BP);
    finish_fx(SFX_HIT, 0.0f, 0.0f);

    begin();                                                         /* small explosion: pop + crackle */
    tone(0, 0.22f, 380, 60, 0.8f, 0.001f, 0.07f, W_SIN, 0, 0);
    noise(0, 0.3f, 2400, 300, 0.7f, 0.6f, 0.001f, 0.08f, F_LP);
    tone(0, 0.1f, 2600, 500, 0.25f, 0.001f, 0.03f, W_FM, 1.0f, 3.0f);
    finish_fx(SFX_EXPL_S, 0.2f, 0.3f);

    begin();                                                         /* medium explosion */
    tone(0, 0.5f, 240, 35, 1.0f, 0.001f, 0.16f, W_SIN, 0, 0);
    noise(0, 0.6f, 1800, 120, 0.7f, 0.7f, 0.001f, 0.16f, F_LP);
    tone(0, 0.3f, 1500, 120, 0.35f, 0.001f, 0.08f, W_FM, 1.0f, 4.0f);
    finish_fx(SFX_EXPL_M, 0.35f, 0.5f);

    begin();                                                         /* big explosion: deep rumbling boom */
    tone(0, 1.6f, 120, 22, 1.0f, 0.002f, 0.5f, W_SIN, 0, 0);
    noise(0, 1.6f, 1200, 60, 0.6f, 0.9f, 0.002f, 0.45f, F_LP);
    tone(0, 0.8f, 900, 40, 0.5f, 0.002f, 0.25f, W_FM, 1.0f, 5.0f);
    noise(0.15f, 1.0f, 3000, 200, 0.7f, 0.35f, 0.01f, 0.3f, F_BP);
    finish_fx(SFX_EXPL_L, 0.45f, 0.8f);

    begin();                                                         /* crystal: glassy chime (pitched up as you chain) */
    bell(0, 2093.0f, 0.5f, 0.07f);
    tone(0, 0.08f, 3136, 3136, 0.2f, 0.001f, 0.04f, W_SIN, 0, 0);
    finish_fx(SFX_CRYSTAL, 0.25f, 0.2f);

    /* --- player --- */
    begin();                                                         /* hurt: crunchy alarm */
    tone(0, 0.3f, 600, 150, 0.7f, 0.001f, 0.12f, W_FM, 1.4f, 4.0f);
    noise(0, 0.25f, 2000, 400, 0.8f, 0.5f, 0.001f, 0.08f, F_LP);
    tone(0.12f, 0.2f, 500, 250, 0.4f, 0.002f, 0.08f, W_FM, 1.0f, 2.0f);
    finish_fx(SFX_HURT, 0.3f, 0.3f);

    begin();                                                         /* ship destroyed */
    tone(0, 1.2f, 800, 40, 0.7f, 0.002f, 0.45f, W_FM, 1.0f, 3.5f);
    tone(0, 1.2f, 160, 25, 0.9f, 0.002f, 0.45f, W_SIN, 0, 0);
    noise(0, 1.0f, 2500, 100, 0.6f, 0.7f, 0.002f, 0.3f, F_LP);
    finish_fx(SFX_DIE, 0.4f, 0.6f);

    begin();                                                         /* turn around: quick swish */
    noise(0, 0.14f, 900, 3500, 2.0f, 0.6f, 0.02f, 0.06f, F_BP);
    tone(0, 0.12f, 500, 900, 0.2f, 0.01f, 0.05f, W_SIN, 0, 0);
    finish_fx(SFX_FLIP, 0.1f, 0.1f);

    /* --- enemies --- */
    begin();                                                         /* warp-in: descending sparkle */
    tone(0, 0.3f, 2400, 700, 0.35f, 0.005f, 0.12f, W_FM, 2.5f, 1.2f);
    tone(0, 0.3f, 2410, 720, 0.25f, 0.005f, 0.12f, W_SIN, 0, 0);
    finish_fx(SFX_WARP, 0.3f, 0.4f);

    begin();                                                         /* enemy shot: soft "bwop" */
    tone(0, 0.14f, 700, 260, 0.6f, 0.002f, 0.05f, W_FM, 1.0f, 1.5f);
    finish_fx(SFX_ESHOT, 0.2f, 0.2f);

    begin();                                                         /* bullet destroyed by the blade */
    bell(0, 3520.0f, 0.4f, 0.04f);
    finish_fx(SFX_DEFLECT, 0.15f, 0.1f);

    /* --- flow --- */
    begin();                                                         /* wave start */
    tone(0.00f, 0.3f, 250, 800, 0.35f, 0.02f, 0.2f, W_SIN, 0, 0);
    tone(0.25f, 0.6f, G4, G4, 0.35f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    tone(0.25f, 0.6f, D5, D5, 0.3f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    tone(0.25f, 0.6f, G5, G5, 0.25f, 0.01f, 0.3f, W_FM, 1.0f, 1.2f);
    finish_fx(SFX_WAVE, 0.4f, 0.5f);

    begin();                                                         /* boss / warning siren */
    for (int i = 0; i < 2; i++) {
        tone(0.5f * (float)i, 0.45f, 300, 900, 0.45f, 0.02f, 0.0f, W_FM, 1.0f, 2.0f);
        tone(0.5f * (float)i, 0.45f, 150, 450, 0.3f, 0.02f, 0.0f, W_SOFT, 0.5f, 0);
    }
    tone(0, 1.1f, 45, 40, 0.6f, 0.05f, 0.7f, W_SIN, 0, 0);
    finish_fx(SFX_BOSS, 0.35f, 0.5f);

    begin();                                                         /* wave clear: arpeggio */
    for (int i = 0; i < 5; i++) {
        static const float nf[5] = {C5, E5, G5, C6, E6};
        tone(0.06f * (float)i, 0.2f, nf[i], nf[i], 0.35f, 0.003f, 0.1f, W_FM, 2.0f, 1.0f);
    }
    finish_fx(SFX_CLEAR, 0.45f, 0.5f);

    begin();                                                         /* perfect: sparkling fanfare */
    for (int i = 0; i < 6; i++) {
        static const float nf[6] = {G5, C6, E6, G5 * 2.0f, C6 * 2.0f, E6 * 2.0f};
        bell(0.05f * (float)i, nf[i], 0.35f, 0.12f);
    }
    tone(0.3f, 0.6f, C6, C6, 0.25f, 0.01f, 0.3f, W_FM, 2.0f, 1.0f);
    tone(0.3f, 0.6f, E6, E6, 0.2f, 0.01f, 0.3f, W_FM, 2.0f, 1.0f);
    finish_fx(SFX_PERFECT, 0.45f, 0.6f);

    begin();                                                         /* combo milestone: whoosh-up + chord stab */
    tone(0, 0.2f, 400, 1600, 0.35f, 0.01f, 0.1f, W_SIN, 0, 0);
    tone(0.12f, 0.4f, E5, E5, 0.35f, 0.003f, 0.18f, W_FM, 1.0f, 1.5f);
    tone(0.12f, 0.4f, B5, B5, 0.3f, 0.003f, 0.18f, W_FM, 1.0f, 1.5f);
    tone(0.12f, 0.4f, E6, E6, 0.25f, 0.003f, 0.18f, W_FM, 1.0f, 1.5f);
    finish_fx(SFX_COMBO, 0.4f, 0.5f);

    begin();                                                         /* out of energy: dull buzz */
    tone(0, 0.15f, 140, 120, 0.7f, 0.003f, 0.06f, W_SOFT, 0.0f, 0);
    tone(0.08f, 0.15f, 110, 90, 0.7f, 0.003f, 0.06f, W_SOFT, 0.0f, 0);
    finish_fx(SFX_EMPTY, 0.0f, 0.0f);

    begin();                                                         /* power-up / respawn */
    for (int i = 0; i < 4; i++) {
        static const float nf[4] = {C5, G5, C6, G6};
        tone(0.05f * (float)i, 0.15f, nf[i] * 0.8f, nf[i], 0.4f, 0.003f, 0.07f, W_FM, 1.0f, 1.2f);
    }
    finish_fx(SFX_POWERUP, 0.4f, 0.4f);

    begin();                                                         /* menu select */
    tone(0.00f, 0.07f, 900, 1200, 0.5f, 0.003f, 0.03f, W_SIN, 0, 0);
    tone(0.06f, 0.14f, 1500, 1900, 0.5f, 0.003f, 0.05f, W_SIN, 0, 0);
    finish_fx(SFX_SELECT, 0.3f, 0.3f);

    begin();                                                         /* menu move */
    tone(0, 0.05f, 1800, 2300, 0.6f, 0.002f, 0.014f, W_SIN, 0, 0);
    finish_fx(SFX_MOVE, 0.0f, 0.0f);

    begin();                                                         /* start / launch */
    tone(0.00f, 0.4f, 150, 1600, 0.4f, 0.01f, 0.25f, W_FM, 1.0f, 1.5f);
    noise(0.0f, 0.4f, 500, 6000, 1.5f, 0.3f, 0.05f, 0.15f, F_BP);
    tone(0.32f, 0.5f, C5, C5, 0.3f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    tone(0.32f, 0.5f, G5, G5, 0.25f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    tone(0.32f, 0.5f, C6, C6, 0.2f, 0.01f, 0.25f, W_FM, 2.0f, 1.0f);
    finish_fx(SFX_START, 0.4f, 0.5f);

    begin();                                                         /* pause */
    tone(0, 0.2f, 1500, 500, 0.5f, 0.004f, 0.08f, W_SIN, 0, 0);
    finish_fx(SFX_PAUSE, 0.3f, 0.3f);

    begin();                                                         /* game over */
    tone(0, 1.3f, 700, 35, 0.6f, 0.01f, 0.7f, W_FM, 1.0f, 2.5f);
    tone(0, 1.3f, 350, 30, 0.5f, 0.01f, 0.7f, W_SIN, 0, 0);
    finish_fx(SFX_OVER, 0.4f, 0.5f);

    begin();                                                         /* rank stamp: slam + bright chord */
    tone(0, 0.5f, 200, 40, 1.0f, 0.001f, 0.15f, W_SIN, 0, 0);
    noise(0, 0.3f, 3000, 300, 0.7f, 0.5f, 0.001f, 0.08f, F_LP);
    for (int i = 0; i < 3; i++) {
        static const float nf[3] = {C6, E6, G6};
        tone(0.05f, 0.8f, nf[i], nf[i], 0.25f, 0.003f, 0.35f, W_FM, 2.0f, 1.0f);
    }
    finish_fx(SFX_RANK, 0.45f, 0.6f);
}

const int16_t *audio_data(int id) { return SFX[id]; }
int            audio_len(int id)  { return sfx_len[id]; }
int            audio_volume(int id) { return VOL[id]; }
