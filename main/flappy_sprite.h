// main/flappy_sprite.h -- procedural pixel-art bird (original artwork).
//
// The sprite is described geometrically so it needs no binary asset and can be
// checked by host tests. The LVGL page converts the palette indices into an
// RGB565A8 image once at start-up.
#pragma once

#include <stdint.h>

#define FLAPPY_SPRITE_W      34
#define FLAPPY_SPRITE_H      24
#define FLAPPY_SPRITE_FRAMES 3    // wing up, middle, down

typedef enum {
    FLAPPY_PX_CLEAR = 0,
    FLAPPY_PX_OUTLINE,
    FLAPPY_PX_BODY,
    FLAPPY_PX_BELLY,
    FLAPPY_PX_WING,
    FLAPPY_PX_EYE,
    FLAPPY_PX_PUPIL,
    FLAPPY_PX_BEAK_TOP,
    FLAPPY_PX_BEAK_LOW,
    FLAPPY_PX_CHEEK,
    FLAPPY_PX_COUNT,
} flappy_px_t;

// Palette index of one sprite pixel. Out-of-range coordinates are CLEAR.
flappy_px_t flappy_sprite_px(int frame, int x, int y);

// RGB888 colour of a palette index (CLEAR returns 0).
uint32_t flappy_sprite_rgb(flappy_px_t px);
