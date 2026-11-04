// main/flappy_sfx.c -- procedural sound effects. See flappy_sfx.h.
#include "flappy_sfx.h"

typedef enum { WAVE_SQUARE, WAVE_TRIANGLE, WAVE_NOISE, WAVE_REST } wave_t;

typedef struct {
    uint16_t f0;       // start frequency (Hz)
    uint16_t f1;       // end frequency (Hz), linear sweep
    uint16_t ms;
    uint8_t wave;
    uint8_t level;     // 0..100 % of full amplitude budget
} segment_t;

typedef struct {
    const segment_t *seg;
    uint8_t count;
} effect_t;

#define SEGS(...) { (const segment_t[]){ __VA_ARGS__ }, \
    (uint8_t)(sizeof((const segment_t[]){ __VA_ARGS__ }) / sizeof(segment_t)) }

// Peak amplitude used at level 100; leaves headroom so the codec never clips.
#define AMP_FULL 9000
#define FADE_SAMPLES 48   // 3 ms attack/release removes clicks at segment edges

static const effect_t EFFECTS[FLAPPY_SFX_COUNT] = {
    [FLAPPY_SFX_NONE]     = { 0, 0 },
    [FLAPPY_SFX_FLAP]     = SEGS({ 520, 900, 70, WAVE_SQUARE, 55 }),
    [FLAPPY_SFX_DIVE]     = SEGS({ 760, 280, 90, WAVE_TRIANGLE, 80 }),
    [FLAPPY_SFX_SCORE]    = SEGS({ 988, 988, 60, WAVE_SQUARE, 50 },
                                 { 1319, 1319, 110, WAVE_SQUARE, 50 }),
    [FLAPPY_SFX_HIT]      = SEGS({ 0, 0, 90, WAVE_NOISE, 85 },
                                 { 180, 120, 60, WAVE_SQUARE, 60 }),
    [FLAPPY_SFX_FALL]     = SEGS({ 440, 110, 320, WAVE_TRIANGLE, 80 }),
    [FLAPPY_SFX_NEW_BEST] = SEGS({ 784, 784, 80, WAVE_SQUARE, 50 },
                                 { 988, 988, 80, WAVE_SQUARE, 50 },
                                 { 1175, 1175, 80, WAVE_SQUARE, 50 },
                                 { 1568, 1568, 200, WAVE_SQUARE, 50 }),
    [FLAPPY_SFX_MENU]     = SEGS({ 1200, 1200, 25, WAVE_SQUARE, 35 }),
    [FLAPPY_SFX_SELECT]   = SEGS({ 880, 880, 40, WAVE_SQUARE, 45 },
                                 { 1320, 1320, 60, WAVE_SQUARE, 45 }),
    [FLAPPY_SFX_STEP]     = SEGS({ 660, 660, 50, WAVE_TRIANGLE, 70 },
                                 { 990, 990, 80, WAVE_TRIANGLE, 70 }),
    [FLAPPY_SFX_PAUSE]    = SEGS({ 660, 440, 90, WAVE_TRIANGLE, 70 }),
};

static uint32_t seg_samples(const segment_t *s) {
    return (uint32_t)s->ms * FLAPPY_SFX_RATE_HZ / 1000u;
}

void flappy_sfx_start(flappy_sfx_voice_t *v, flappy_sfx_t sfx) {
    if ((unsigned)sfx >= FLAPPY_SFX_COUNT) sfx = FLAPPY_SFX_NONE;
    v->sfx = sfx;
    v->segment = 0;
    v->seg_pos = 0;
    v->phase = 0;
    if (v->noise == 0) v->noise = 0xACE1u;
}

bool flappy_sfx_active(const flappy_sfx_voice_t *v) {
    return v->sfx != FLAPPY_SFX_NONE && v->segment < EFFECTS[v->sfx].count;
}

uint32_t flappy_sfx_length(flappy_sfx_t sfx) {
    if ((unsigned)sfx >= FLAPPY_SFX_COUNT) return 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < EFFECTS[sfx].count; i++) total += seg_samples(&EFFECTS[sfx].seg[i]);
    return total;
}

static int32_t oscillator(flappy_sfx_voice_t *v, uint8_t wave) {
    switch (wave) {
    case WAVE_SQUARE:
        return (v->phase & 0x80000000u) ? 32767 : -32767;
    case WAVE_TRIANGLE: {
        uint32_t p = v->phase >> 16;               // 0..65535
        int32_t t = p < 32768u ? (int32_t)p : (int32_t)(65535u - p);
        return t * 2 - 32767;
    }
    case WAVE_NOISE: {
        uint32_t bit = ((v->noise >> 0) ^ (v->noise >> 2) ^ (v->noise >> 3) ^ (v->noise >> 5)) & 1u;
        v->noise = (v->noise >> 1) | (bit << 15);
        return (v->noise & 1u) ? 32767 : -32767;
    }
    default:
        return 0;
    }
}

size_t flappy_sfx_render(flappy_sfx_voice_t *v, int16_t *out, size_t max) {
    size_t n = 0;
    while (n < max && flappy_sfx_active(v)) {
        const segment_t *s = &EFFECTS[v->sfx].seg[v->segment];
        const uint32_t len = seg_samples(s);
        if (v->seg_pos >= len) {
            v->segment++;
            v->seg_pos = 0;
            continue;
        }
        // Linear frequency sweep across the segment.
        int32_t f = (int32_t)s->f0 + ((int32_t)s->f1 - (int32_t)s->f0) *
                    (int32_t)v->seg_pos / (int32_t)len;
        uint32_t inc = (uint32_t)(((uint64_t)(uint32_t)f << 32) / FLAPPY_SFX_RATE_HZ);
        int32_t sample = oscillator(v, s->wave);
        v->phase += inc;

        // Envelope: short fade in/out per segment.
        int32_t env = 256;
        if (v->seg_pos < FADE_SAMPLES) env = (int32_t)(v->seg_pos * 256u / FADE_SAMPLES);
        uint32_t left = len - v->seg_pos;
        if (left < FADE_SAMPLES) {
            int32_t e2 = (int32_t)(left * 256u / FADE_SAMPLES);
            if (e2 < env) env = e2;
        }
        int32_t amp = AMP_FULL * s->level / 100;
        // |sample * amp| <= 32767 * 9000 fits int32; scale first, then envelope.
        out[n++] = (int16_t)((sample * amp / 32767) * env / 256);
        v->seg_pos++;
    }
    return n;
}
