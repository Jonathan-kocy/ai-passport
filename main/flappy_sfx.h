// main/flappy_sfx.h -- tiny procedural sound-effect synthesiser.
//
// Pure C: produces 16-bit mono PCM in chunks so the audio worker can stream it
// and switch to a newer effect between chunks. No ESP-IDF dependency.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLAPPY_SFX_RATE_HZ 16000

typedef enum {
    FLAPPY_SFX_NONE = 0,
    FLAPPY_SFX_FLAP,
    FLAPPY_SFX_VOLUME,
    FLAPPY_SFX_SCORE,
    FLAPPY_SFX_HIT,
    FLAPPY_SFX_FALL,
    FLAPPY_SFX_NEW_BEST,
    FLAPPY_SFX_MENU,
    FLAPPY_SFX_SELECT,
    FLAPPY_SFX_STEP,
    FLAPPY_SFX_PAUSE,
    FLAPPY_SFX_COUNT,
} flappy_sfx_t;

typedef struct {
    flappy_sfx_t sfx;
    uint16_t segment;      // index into the effect's segment list
    uint32_t seg_pos;      // samples rendered in the current segment
    uint32_t phase;        // oscillator phase, Q32 of one cycle
    uint32_t noise;        // LFSR state
} flappy_sfx_voice_t;

// Begin an effect from its start (NONE stops the voice).
void flappy_sfx_start(flappy_sfx_voice_t *v, flappy_sfx_t sfx);

// Render up to `max` samples. Returns the number written; 0 when finished.
size_t flappy_sfx_render(flappy_sfx_voice_t *v, int16_t *out, size_t max);

bool flappy_sfx_active(const flappy_sfx_voice_t *v);

// Total length of an effect in samples (for tests and budgeting).
uint32_t flappy_sfx_length(flappy_sfx_t sfx);
