// Host tests for the procedural bird sprite and sound effects.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flappy_sfx.h"
#include "flappy_sprite.h"

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static void test_sprite(void) {
    int counts[FLAPPY_SPRITE_FRAMES][FLAPPY_PX_COUNT];
    memset(counts, 0, sizeof(counts));
    for (int f = 0; f < FLAPPY_SPRITE_FRAMES; f++) {
        for (int y = 0; y < FLAPPY_SPRITE_H; y++) {
            for (int x = 0; x < FLAPPY_SPRITE_W; x++) {
                flappy_px_t p = flappy_sprite_px(f, x, y);
                CHECK((unsigned)p < FLAPPY_PX_COUNT);
                counts[f][p]++;
                // Shapes must not be cut by the image edge: edge pixels are
                // either transparent or outline.
                if (x == 0 || y == 0 || x == FLAPPY_SPRITE_W - 1 || y == FLAPPY_SPRITE_H - 1) {
                    CHECK(p == FLAPPY_PX_CLEAR || p == FLAPPY_PX_OUTLINE);
                }
            }
        }
        CHECK(flappy_sprite_px(f, 0, 0) == FLAPPY_PX_CLEAR);
        CHECK(flappy_sprite_px(f, FLAPPY_SPRITE_W - 1, FLAPPY_SPRITE_H - 1) == FLAPPY_PX_CLEAR);
        CHECK(counts[f][FLAPPY_PX_EYE] > 10);
        CHECK(counts[f][FLAPPY_PX_PUPIL] > 4);
        CHECK(counts[f][FLAPPY_PX_BEAK_TOP] > 4);
        CHECK(counts[f][FLAPPY_PX_WING] > 20);
        CHECK(counts[f][FLAPPY_PX_BODY] > 60);
    }
    CHECK(flappy_sprite_px(0, -1, 5) == FLAPPY_PX_CLEAR);
    CHECK(flappy_sprite_px(0, 5, FLAPPY_SPRITE_H) == FLAPPY_PX_CLEAR);

    // Wing frames really differ.
    int diff01 = 0, diff12 = 0;
    for (int y = 0; y < FLAPPY_SPRITE_H; y++) {
        for (int x = 0; x < FLAPPY_SPRITE_W; x++) {
            diff01 += flappy_sprite_px(0, x, y) != flappy_sprite_px(1, x, y);
            diff12 += flappy_sprite_px(1, x, y) != flappy_sprite_px(2, x, y);
        }
    }
    CHECK(diff01 > 10 && diff12 > 10);

    for (int p = 1; p < FLAPPY_PX_COUNT; p++) CHECK(flappy_sprite_rgb((flappy_px_t)p) != 0);
    CHECK(flappy_sprite_rgb(FLAPPY_PX_CLEAR) == 0);
}

static void test_sfx(void) {
    static int16_t whole[FLAPPY_SFX_RATE_HZ];
    static int16_t chunked[FLAPPY_SFX_RATE_HZ];
    for (int s = 1; s < FLAPPY_SFX_COUNT; s++) {
        uint32_t len = flappy_sfx_length((flappy_sfx_t)s);
        CHECK(len > 0 && len <= FLAPPY_SFX_RATE_HZ);   // every effect under 1 s

        flappy_sfx_voice_t v = { 0 };
        flappy_sfx_start(&v, (flappy_sfx_t)s);
        CHECK(flappy_sfx_active(&v));
        size_t n = flappy_sfx_render(&v, whole, FLAPPY_SFX_RATE_HZ);
        CHECK(n == len);
        CHECK(!flappy_sfx_active(&v));
        CHECK(flappy_sfx_render(&v, whole, 16) == 0);

        int peak = 0;
        for (size_t i = 0; i < n; i++) {
            int a = abs(whole[i]);
            if (a > peak) peak = a;
        }
        CHECK(peak > 1000);          // audible
        CHECK(peak <= 9000);         // headroom: never clips the codec
        CHECK(abs(whole[0]) < 400);  // fades in (no click)
        CHECK(abs(whole[n - 1]) < 400);

        // Streaming in 256-sample chunks produces identical PCM.
        flappy_sfx_voice_t c = { 0 };
        flappy_sfx_start(&c, (flappy_sfx_t)s);
        size_t total = 0, got;
        while ((got = flappy_sfx_render(&c, chunked + total, 256)) > 0) total += got;
        CHECK(total == n);
        CHECK(memcmp(whole, chunked, n * sizeof(int16_t)) == 0);
    }

    flappy_sfx_voice_t v = { 0 };
    flappy_sfx_start(&v, FLAPPY_SFX_NONE);
    CHECK(!flappy_sfx_active(&v));
    flappy_sfx_start(&v, (flappy_sfx_t)99);
    CHECK(!flappy_sfx_active(&v));
    CHECK(flappy_sfx_length((flappy_sfx_t)99) == 0);
}

int main(void) {
    test_sprite();
    test_sfx();
    if (s_failures) {
        fprintf(stderr, "test_flappy_assets: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_flappy_assets: PASS\n");
    return 0;
}
