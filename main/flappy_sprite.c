// main/flappy_sprite.c -- procedural pixel-art bird. See flappy_sprite.h.
#include "flappy_sprite.h"

// Integer ellipse test scaled by 64: returns d*64 where d = (dx/rx)^2 + (dy/ry)^2.
// Coordinates are doubled so centres can sit on half pixels.
static int ellipse64(int x2, int y2, int cx2, int cy2, int rx2, int ry2) {
    long dx = x2 - cx2;
    long dy = y2 - cy2;
    return (int)((dx * dx * 64) / ((long)rx2 * rx2) + (dy * dy * 64) / ((long)ry2 * ry2));
}

static flappy_px_t beak_px(int x, int y) {
    // Wedge pointing right: rows 11..17, tip narrows towards x = 33.
    if (y < 11 || y > 17 || x < 25) return FLAPPY_PX_CLEAR;
    const int mid2 = 28;                       // centre row * 2 (= 14)
    int half_h2 = (33 - x) + 1;                // shrinks one doubled px per column
    int dy2 = 2 * y - mid2;
    if (dy2 < 0) dy2 = -dy2;
    if (dy2 > half_h2 + 3) return FLAPPY_PX_CLEAR;
    if (dy2 > half_h2 + 1 || x == 33) return FLAPPY_PX_OUTLINE;
    if (y == 14) return FLAPPY_PX_OUTLINE;     // mouth line
    return y < 14 ? FLAPPY_PX_BEAK_TOP : FLAPPY_PX_BEAK_LOW;
}

flappy_px_t flappy_sprite_px(int frame, int x, int y) {
    if (x < 0 || y < 0 || x >= FLAPPY_SPRITE_W || y >= FLAPPY_SPRITE_H) return FLAPPY_PX_CLEAR;
    if (frame < 0) frame = 0;
    if (frame >= FLAPPY_SPRITE_FRAMES) frame = FLAPPY_SPRITE_FRAMES - 1;
    const int x2 = 2 * x + 1;
    const int y2 = 2 * y + 1;

    // Eye sits in front of everything.
    int eye = ellipse64(x2, y2, 2 * 21 + 1, 2 * 8, 2 * 5, 2 * 5);
    if (eye <= 64) {
        int pupil = ellipse64(x2, y2, 2 * 23, 2 * 8 + 1, 2 * 2, 2 * 3);
        if (pupil <= 64) return FLAPPY_PX_PUPIL;
        return eye > 44 ? FLAPPY_PX_OUTLINE : FLAPPY_PX_EYE;
    }

    flappy_px_t beak = beak_px(x, y);
    if (beak != FLAPPY_PX_CLEAR) return beak;

    // Wing: offset per frame (up, middle, down).
    static const int wing_dy[FLAPPY_SPRITE_FRAMES] = { -4, 0, 3 };
    static const int wing_ry[FLAPPY_SPRITE_FRAMES] = { 4, 3, 4 };
    int wing = ellipse64(x2, y2, 2 * 8, 2 * (13 + wing_dy[frame]), 2 * 7, 2 * wing_ry[frame]);
    if (wing <= 64) return wing > 40 ? FLAPPY_PX_OUTLINE : FLAPPY_PX_WING;

    // Body: round, slightly wider than tall.
    int body = ellipse64(x2, y2, 2 * 15, 2 * 12, 2 * 14, 2 * 10 + 1);
    if (body > 64) return FLAPPY_PX_CLEAR;
    if (body > 50) return FLAPPY_PX_OUTLINE;
    int belly = ellipse64(x2, y2, 2 * 18, 2 * 17, 2 * 9, 2 * 5);
    if (belly <= 64) return FLAPPY_PX_BELLY;
    int cheek = ellipse64(x2, y2, 2 * 24, 2 * 13, 2 * 2, 2 * 2);
    if (cheek <= 64) return FLAPPY_PX_CHEEK;
    return FLAPPY_PX_BODY;
}

uint32_t flappy_sprite_rgb(flappy_px_t px) {
    switch (px) {
    case FLAPPY_PX_OUTLINE:  return 0x3A2418;
    case FLAPPY_PX_BODY:     return 0xFF8B3D;
    case FLAPPY_PX_BELLY:    return 0xFFE2B0;
    case FLAPPY_PX_WING:     return 0xE0582A;
    case FLAPPY_PX_EYE:      return 0xFFFFFF;
    case FLAPPY_PX_PUPIL:    return 0x1A1A2E;
    case FLAPPY_PX_BEAK_TOP: return 0xFFC93C;
    case FLAPPY_PX_BEAK_LOW: return 0xF0A020;
    case FLAPPY_PX_CHEEK:    return 0xFF6F8E;
    case FLAPPY_PX_CLEAR:
    default:                 return 0;
    }
}
