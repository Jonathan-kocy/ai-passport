// Host tests for main/flappy_model.c (no ESP-IDF, no LVGL).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flappy_model.h"

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        s_failures++; \
    } \
} while (0)

static uint32_t s_seen;   // every event observed by run()

static void press(flappy_model_t *m, flappy_key_t k) {
    flappy_input(m, k, FLAPPY_KEY_PRESS);
}

static void release(flappy_model_t *m, flappy_key_t k) {
    flappy_input(m, k, FLAPPY_KEY_RELEASE);
}

static void tap(flappy_model_t *m, flappy_key_t k) {
    press(m, k);
    release(m, k);
}

static void hold(flappy_model_t *m, flappy_key_t k) {
    press(m, k);
    flappy_input(m, k, FLAPPY_KEY_LONG);
    release(m, k);
}

static void run(flappy_model_t *m, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        flappy_advance(m, 10);
        s_seen |= flappy_take_events(m);
    }
}

// Next pipe the bird still has to clear, or NULL.
static const flappy_pipe_t *next_pipe(const flappy_model_t *m) {
    const flappy_pipe_t *best = NULL;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        const flappy_pipe_t *p = &m->pipes[i];
        if (!p->active) continue;
        if (flappy_pipe_x(p) + FLAPPY_PIPE_W < FLAPPY_BIRD_X - FLAPPY_HIT_HALF_W) continue;
        if (!best || p->x_q8 < best->x_q8) best = p;
    }
    return best;
}

// Simple pilot: flap when the bird sinks below a point just under the gap centre.
static void autopilot_step(flappy_model_t *m) {
    const flappy_pipe_t *p = next_pipe(m);
    int target = p ? p->gap_y + 12 : 160;
    if (flappy_bird_y(m) > target && m->bird_vy_q8 >= 0) {
        flappy_input(m, FLAPPY_KEY_UP, FLAPPY_KEY_PRESS);
        flappy_input(m, FLAPPY_KEY_UP, FLAPPY_KEY_RELEASE);
    }
    flappy_advance(m, 10);
    s_seen |= flappy_take_events(m);
}

static void test_first_boot_starts_tutorial(void) {
    flappy_model_t m;
    flappy_init(&m, 0, false, 1);
    CHECK(m.state == FLAPPY_TUTORIAL);
    CHECK(m.tut_step == FLAPPY_TUT_WELCOME);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_STATE);

    flappy_init(&m, 7, true, 1);
    CHECK(m.state == FLAPPY_TITLE);
    CHECK(m.best == 7);
}

static void test_tutorial_full_flow(void) {
    flappy_model_t m;
    flappy_init(&m, 0, false, 42);
    s_seen = 0;

    tap(&m, FLAPPY_KEY_UP);                 // wrong key on welcome: ignored
    CHECK(m.tut_step == FLAPPY_TUT_WELCOME);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.tut_step == FLAPPY_TUT_FLAP);

    tap(&m, FLAPPY_KEY_DOWN);               // must flap first
    CHECK(m.tut_step == FLAPPY_TUT_FLAP);
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.tut_step == FLAPPY_TUT_PAUSE);
    CHECK(m.bird_vy_q8 == FLAPPY_FLAP_VY_Q8);

    run(&m, 300);
    tap(&m, FLAPPY_KEY_DOWN);               // volume belongs to settings, not the tutorial
    CHECK(m.tut_step == FLAPPY_TUT_PAUSE);

    // Practice autopilot keeps the bird off the ground while reading.
    run(&m, 5000);
    CHECK(flappy_bird_y(&m) < FLAPPY_GROUND_Y - 40);

    // Pause with OK; the release of that same press must not resume.
    press(&m, FLAPPY_KEY_OK);
    CHECK(m.tut_step == FLAPPY_TUT_RESUME);
    CHECK(m.tut_paused);
    flappy_input(&m, FLAPPY_KEY_OK, FLAPPY_KEY_LONG);   // same press held: no skip
    CHECK(m.state == FLAPPY_TUTORIAL);
    release(&m, FLAPPY_KEY_OK);
    CHECK(m.tut_step == FLAPPY_TUT_RESUME);

    int y = flappy_bird_y(&m);
    run(&m, 500);
    CHECK(flappy_bird_y(&m) == y);          // frozen while paused

    tap(&m, FLAPPY_KEY_OK);                 // fresh tap resumes
    CHECK(m.tut_step == FLAPPY_TUT_GATE);
    CHECK(!m.tut_paused);
    CHECK(m.pipes[0].active && m.pipes[0].gap_h == FLAPPY_TUTORIAL_GAP);

    // The gate waits for the first flap.
    run(&m, 1000);
    CHECK(!m.tut_flying);
    for (int i = 0; i < 1000 && m.tut_step == FLAPPY_TUT_GATE; i++) {
        if (i == 0) tap(&m, FLAPPY_KEY_UP);
        autopilot_step(&m);
    }
    CHECK(m.tut_step == FLAPPY_TUT_SETTINGS);
    CHECK(m.tut_attempts == 0);
    CHECK(!m.tutorial_done);

    // The settings card waits for OK, then the short celebration runs.
    run(&m, FLAPPY_TUTORIAL_DONE_MS + 20);
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.tut_step == FLAPPY_TUT_SETTINGS);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.tut_step == FLAPPY_TUT_DONE);
    CHECK(!m.tutorial_done);
    CHECK(m.volume == FLAPPY_VOL_HIGH);
    CHECK(!(s_seen & FLAPPY_EV_VOLUME));

    run(&m, FLAPPY_TUTORIAL_DONE_MS + 20);
    CHECK(m.state == FLAPPY_READY);
    CHECK(m.tutorial_done);
    CHECK(s_seen & FLAPPY_EV_TUTORIAL_DONE);
}

static void test_tutorial_gate_crash_retries(void) {
    flappy_model_t m;
    flappy_init(&m, 0, false, 3);
    tap(&m, FLAPPY_KEY_OK);
    tap(&m, FLAPPY_KEY_UP);
    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_OK);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.tut_step == FLAPPY_TUT_GATE);
    s_seen = 0;
    tap(&m, FLAPPY_KEY_UP);
    run(&m, 4000);                          // no more flaps: falls to the ground
    CHECK(m.state == FLAPPY_TUTORIAL);      // never a game over in the tutorial
    CHECK(m.tut_step == FLAPPY_TUT_GATE);
    CHECK(m.tut_attempts >= 1);
    CHECK(s_seen & FLAPPY_EV_TUTORIAL_RETRY);
    CHECK(!m.tut_flying);                   // waits for the next flap again
}

static void test_tutorial_skip(void) {
    flappy_model_t m;
    flappy_init(&m, 0, false, 5);
    // Holding OK on the welcome card is the start press; it must not skip.
    hold(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_TUTORIAL);
    CHECK(m.tut_step == FLAPPY_TUT_FLAP);

    flappy_take_events(&m);
    hold(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_READY);
    CHECK(m.tutorial_done);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_TUTORIAL_DONE);
}

static void test_title_menu(void) {
    flappy_model_t m;
    flappy_init(&m, 0, true, 9);
    CHECK(m.menu_sel == FLAPPY_MENU_PLAY);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.menu_sel == FLAPPY_MENU_TUTORIAL);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.menu_sel == FLAPPY_MENU_SETTINGS);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.menu_sel == FLAPPY_MENU_PLAY);  // wraps
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.menu_sel == FLAPPY_MENU_SETTINGS);
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.menu_sel == FLAPPY_MENU_TUTORIAL);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_TUTORIAL && m.tut_step == FLAPPY_TUT_WELCOME);

    flappy_init(&m, 0, true, 9);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_READY);

    // Hold OK on READY returns to the title.
    hold(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_TITLE);
}

static void start_game(flappy_model_t *m, uint32_t seed, uint16_t best) {
    flappy_init(m, best, true, seed);
    tap(m, FLAPPY_KEY_OK);
    CHECK(m->state == FLAPPY_READY);
    run(m, 2000);
    CHECK(m->state == FLAPPY_READY);        // waits for UP
    tap(m, FLAPPY_KEY_UP);
    CHECK(m->state == FLAPPY_PLAYING);
    CHECK(m->bird_vy_q8 == FLAPPY_FLAP_VY_Q8);
}

static void test_fall_to_ground_is_game_over(void) {
    flappy_model_t m;
    start_game(&m, 11, 0);
    s_seen = 0;
    run(&m, 3000);
    CHECK(m.state == FLAPPY_GAME_OVER);
    CHECK(s_seen & FLAPPY_EV_HIT);
    CHECK(s_seen & FLAPPY_EV_GROUND);
    CHECK(!(s_seen & FLAPPY_EV_NEW_BEST));  // score 0 is not a record
    CHECK(flappy_bird_y(&m) == FLAPPY_GROUND_Y - FLAPPY_HIT_HALF_H);
    CHECK(flappy_bird_angle_deci(&m) == 900);
}

static void test_autopilot_scores_and_levels_are_fair(void) {
    for (uint32_t seed = 1; seed <= 20; seed++) {
        flappy_model_t m;
        start_game(&m, seed * 2654435761u, 0);
        int prev_gap = -1;
        const flappy_pipe_t *prev = NULL;
        for (int i = 0; i < 6000 && m.state == FLAPPY_PLAYING; i++) {   // 60 s
            autopilot_step(&m);
            for (int k = 0; k < FLAPPY_PIPE_COUNT; k++) {
                const flappy_pipe_t *p = &m.pipes[k];
                if (!p->active) continue;
                CHECK(p->gap_y - p->gap_h / 2 >= FLAPPY_GAP_MARGIN);
                CHECK(p->gap_y + p->gap_h / 2 <= FLAPPY_GROUND_Y - FLAPPY_GAP_MARGIN);
                CHECK(p->gap_h >= FLAPPY_GAP_MIN && p->gap_h <= FLAPPY_GAP_MAX);
            }
            const flappy_pipe_t *n = next_pipe(&m);
            if (n && n != prev) {
                if (prev_gap >= 0) CHECK(abs(n->gap_y - prev_gap) <= 70);
                prev_gap = n->gap_y;
                prev = n;
            }
        }
        if (m.state != FLAPPY_PLAYING || m.score < 25) {
            fprintf(stderr, "seed %u: state=%d score=%u\n", (unsigned)seed, m.state, m.score);
        }
        CHECK(m.state == FLAPPY_PLAYING);   // autopilot survives a minute
        CHECK(m.score >= 25);
        CHECK(flappy_gap_for_score(m.difficulty, m.score) == FLAPPY_GAP_MIN);
    }
}

// Beam search over flap decisions, limited to a tap rate a player can reach. The
// world is deterministic for a seed, so copies of the model share the same pipes.
#define BEAM_WIDTH 48
#define BEAM_TAP_STEPS 12    // one decision per 120 ms

static int beam_distance(const flappy_model_t *m) {
    const flappy_pipe_t *p = next_pipe(m);
    return abs(flappy_bird_y(m) - (p ? p->gap_y : 160));
}

static bool beam_survives_a_minute(const flappy_model_t *start) {
    static flappy_model_t beam[BEAM_WIDTH];
    static flappy_model_t next[BEAM_WIDTH * 2];
    int count = 1;
    beam[0] = *start;
    for (int t = 0; t < 6000 / BEAM_TAP_STEPS; t++) {
        int n = 0;
        for (int i = 0; i < count; i++) {
            for (int do_flap = 0; do_flap < 2; do_flap++) {
                flappy_model_t c = beam[i];
                if (do_flap) tap(&c, FLAPPY_KEY_UP);
                flappy_advance(&c, BEAM_TAP_STEPS * FLAPPY_STEP_MS);
                flappy_take_events(&c);
                if (c.state != FLAPPY_PLAYING) continue;
                bool duplicate = false;
                for (int k = 0; k < n && !duplicate; k++) {
                    duplicate = flappy_bird_y(&next[k]) == flappy_bird_y(&c) &&
                                next[k].bird_vy_q8 / 32 == c.bird_vy_q8 / 32;
                }
                if (!duplicate) next[n++] = c;
            }
        }
        if (n == 0) return false;
        // Keep the states closest to the next gap.
        for (int i = 0; i < n && i < BEAM_WIDTH; i++) {
            int best = i;
            for (int k = i + 1; k < n; k++) {
                if (beam_distance(&next[k]) < beam_distance(&next[best])) best = k;
            }
            flappy_model_t tmp = next[i];
            next[i] = next[best];
            next[best] = tmp;
            beam[i] = next[i];
        }
        count = n < BEAM_WIDTH ? n : BEAM_WIDTH;
    }
    return true;
}

// Every difficulty and flap strength must stay passable, including at top speed.
static void test_all_settings_are_passable(void) {
    for (int d = 0; d < FLAPPY_DIFF_COUNT; d++) {
        for (int f = 0; f < FLAPPY_FLAP_COUNT; f++) {
            for (uint32_t seed = 1; seed <= 4; seed++) {
                flappy_model_t m;
                flappy_init(&m, 0, true, seed * 2654435761u);
                const flappy_saved_t saved = {
                    .volume = FLAPPY_VOL_HIGH, .flap = (uint8_t)f,
                    .difficulty = (uint8_t)d, .theme = FLAPPY_THEME_DAY,
                };
                flappy_restore(&m, &saved);
                tap(&m, FLAPPY_KEY_OK);
                tap(&m, FLAPPY_KEY_UP);
                const bool ok = beam_survives_a_minute(&m);
                if (!ok) fprintf(stderr, "difficulty %d flap %d seed %u: no path\n", d, f, (unsigned)seed);
                CHECK(ok);
            }
        }
    }
    CHECK(flappy_flap_vy_q8(FLAPPY_FLAP_SOFT) > flappy_flap_vy_q8(FLAPPY_FLAP_NORMAL));
    CHECK(flappy_flap_vy_q8(FLAPPY_FLAP_STRONG) < flappy_flap_vy_q8(FLAPPY_FLAP_NORMAL));
    CHECK(flappy_gap_for_score(FLAPPY_DIFF_EASY, 0) > flappy_gap_for_score(FLAPPY_DIFF_HARD, 0));
    CHECK(flappy_speed_q8_for_score(FLAPPY_DIFF_EASY, 0) < flappy_speed_q8_for_score(FLAPPY_DIFF_HARD, 0));
}

static void test_settings_screen(void) {
    flappy_model_t m;
    flappy_init(&m, 5, true, 50);
    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_SETTINGS);
    CHECK(m.settings_sel == FLAPPY_SET_VOLUME);
    flappy_take_events(&m);

    // OK steps the selected row; UP and DOWN only move, and wrap.
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.volume == FLAPPY_VOL_MED);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_VOLUME);
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.settings_sel == FLAPPY_SET_BACK);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.settings_sel == FLAPPY_SET_VOLUME);
    CHECK(m.volume == FLAPPY_VOL_MED);

    tap(&m, FLAPPY_KEY_DOWN);
    flappy_take_events(&m);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.flap == FLAPPY_FLAP_STRONG);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_SETTING);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.flap == FLAPPY_FLAP_SOFT);

    // Each difficulty has its own record.
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.best == 5);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.difficulty == FLAPPY_DIFF_HARD);
    CHECK(m.best == 0);

    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.theme == FLAPPY_THEME_SUNSET);

    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_TITLE);
    CHECK(m.menu_sel == FLAPPY_MENU_SETTINGS);

    // Settings apply to play, and a record lands on its own difficulty.
    tap(&m, FLAPPY_KEY_DOWN);
    tap(&m, FLAPPY_KEY_OK);
    tap(&m, FLAPPY_KEY_UP);
    CHECK(m.bird_vy_q8 == flappy_flap_vy_q8(FLAPPY_FLAP_SOFT));
    CHECK(m.pipes[0].gap_h == flappy_gap_for_score(FLAPPY_DIFF_HARD, 0));
    m.score = 2;
    flappy_take_events(&m);
    run(&m, 4000);
    CHECK(m.state == FLAPPY_GAME_OVER);
    CHECK(s_seen & FLAPPY_EV_NEW_BEST);
    CHECK(m.bests[FLAPPY_DIFF_HARD] == 2);
    CHECK(m.bests[FLAPPY_DIFF_NORMAL] == 5);

    // Corrupt stored values fall back to defaults.
    const flappy_saved_t bad = { .best = { 1, 2, 3 }, .volume = 200, .flap = 9, .difficulty = 9, .theme = 9 };
    flappy_restore(&m, &bad);
    CHECK(m.volume == FLAPPY_VOL_HIGH && m.flap == FLAPPY_FLAP_NORMAL);
    CHECK(m.difficulty == FLAPPY_DIFF_NORMAL && m.theme == FLAPPY_THEME_DAY);
    CHECK(m.best == 2);
}

static void test_pause_resume_and_quit(void) {
    flappy_model_t m;
    start_game(&m, 21, 0);
    run(&m, 200);
    press(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_PAUSED);
    flappy_input(&m, FLAPPY_KEY_OK, FLAPPY_KEY_LONG);   // same press: no quit
    CHECK(m.state == FLAPPY_PAUSED);
    release(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_PAUSED);                    // that release: no resume

    int y = flappy_bird_y(&m);
    int px = flappy_pipe_x(&m.pipes[0]);
    run(&m, 1000);
    CHECK(flappy_bird_y(&m) == y);
    CHECK(flappy_pipe_x(&m.pipes[0]) == px);

    tap(&m, FLAPPY_KEY_DOWN);                           // DOWN only changes the volume
    CHECK(m.state == FLAPPY_PAUSED);
    CHECK(m.volume == FLAPPY_VOL_MED);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_PLAYING);

    press(&m, FLAPPY_KEY_OK);
    release(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_PAUSED);
    tap(&m, FLAPPY_KEY_UP);                             // UP also resumes (no flap)
    CHECK(m.state == FLAPPY_PLAYING);

    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_PAUSED);
    hold(&m, FLAPPY_KEY_OK);                            // fresh hold quits
    CHECK(m.state == FLAPPY_TITLE);
}

static void test_game_over_retry_rules_and_best(void) {
    flappy_model_t m;
    start_game(&m, 33, 2);
    for (int i = 0; i < 1500 && m.score < 3; i++) autopilot_step(&m);
    CHECK(m.score >= 3);
    s_seen = 0;
    run(&m, 4000);                                       // stop flying: crash
    CHECK(m.state == FLAPPY_GAME_OVER);
    CHECK(m.best == m.score);
    CHECK(m.new_best);
    CHECK(s_seen & FLAPPY_EV_NEW_BEST);

    flappy_take_events(&m);
    m.state_ms = 0;                                      // just crashed
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_GAME_OVER);                  // locked briefly
    run(&m, FLAPPY_GAME_OVER_LOCK_MS);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_READY);
    CHECK(m.score == 0 && !m.new_best);
    CHECK(m.best >= 3);

    // DOWN from game over goes to the title.
    tap(&m, FLAPPY_KEY_UP);
    run(&m, 3000);
    CHECK(m.state == FLAPPY_GAME_OVER);
    run(&m, FLAPPY_GAME_OVER_LOCK_MS);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.state == FLAPPY_TITLE);
}

static void test_volume_control(void) {
    flappy_model_t m;
    flappy_init(&m, 0, true, 44);
    CHECK(m.volume == FLAPPY_VOL_HIGH);
    CHECK(flappy_volume_percent(FLAPPY_VOL_MUTE) == 0);
    CHECK(flappy_volume_percent(FLAPPY_VOL_LOW) < flappy_volume_percent(FLAPPY_VOL_MED));
    CHECK(flappy_volume_percent(FLAPPY_VOL_MED) < flappy_volume_percent(FLAPPY_VOL_HIGH));

    const flappy_saved_t saved = {
        .volume = FLAPPY_VOL_LOW, .flap = FLAPPY_FLAP_NORMAL,
        .difficulty = FLAPPY_DIFF_NORMAL, .theme = FLAPPY_THEME_DAY,
    };
    flappy_take_events(&m);
    flappy_restore(&m, &saved);
    CHECK(m.volume == FLAPPY_VOL_LOW);
    CHECK(!flappy_take_events(&m));                        // restoring is silent
    flappy_init(&m, 0, true, 44);

    // The title keeps DOWN for the menu.
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.volume == FLAPPY_VOL_HIGH);
    CHECK(!(flappy_take_events(&m) & FLAPPY_EV_VOLUME));
    tap(&m, FLAPPY_KEY_UP);
    tap(&m, FLAPPY_KEY_OK);
    CHECK(m.state == FLAPPY_READY);
    flappy_take_events(&m);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.state == FLAPPY_READY);
    CHECK(m.volume == FLAPPY_VOL_MED);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_VOLUME);

    // In play, DOWN never touches the bird and steps down, then wraps.
    start_game(&m, 44, 0);
    flappy_take_events(&m);
    const int32_t vy = m.bird_vy_q8;
    static const flappy_volume_t order[] = {
        FLAPPY_VOL_MED, FLAPPY_VOL_LOW, FLAPPY_VOL_MUTE, FLAPPY_VOL_HIGH, FLAPPY_VOL_MED,
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        tap(&m, FLAPPY_KEY_DOWN);
        CHECK(m.volume == order[i]);
        CHECK(m.bird_vy_q8 == vy);
        CHECK(m.state == FLAPPY_PLAYING);
        CHECK(flappy_take_events(&m) & FLAPPY_EV_VOLUME);
    }
}

static void test_clamps(void) {
    // A long stall advances at most FLAPPY_MAX_ELAPSED_MS of game time.
    flappy_model_t a;
    start_game(&a, 45, 0);
    uint32_t before = a.world_ms;
    flappy_advance(&a, 5000);
    CHECK(a.world_ms - before == FLAPPY_MAX_ELAPSED_MS);

    // Ceiling: rapid flapping never leaves the screen.
    start_game(&a, 46, 0);
    for (int i = 0; i < 100; i++) {
        tap(&a, FLAPPY_KEY_UP);
        flappy_advance(&a, 10);
        CHECK(flappy_bird_y(&a) >= FLAPPY_HIT_HALF_H);
    }
}

static void test_presentation_helpers(void) {
    flappy_model_t m;
    flappy_init(&m, 0, true, 1);
    CHECK(flappy_bird_angle_deci(&m) == 0);            // hovering on the title
    int frames_seen = 0;
    for (int i = 0; i < 100; i++) {
        flappy_advance(&m, 10);
        frames_seen |= 1 << flappy_bird_frame(&m);
    }
    CHECK(frames_seen == 0x7);                         // wing animates while hovering

    start_game(&m, 2, 0);
    CHECK(flappy_bird_angle_deci(&m) < 0);             // nose up after a flap
    run(&m, 600);
    CHECK(flappy_bird_angle_deci(&m) > 0);             // nose down when falling
    CHECK(flappy_bird_angle_deci(&m) <= 700);

    for (int o = 0; o < 2000; o += 7) {
        m.ground_q8 = FLAPPY_Q8(o);
        int g = flappy_ground_offset(&m, 24);
        CHECK(g >= 0 && g < 24);
    }
    flappy_pipe_t p = { .x_q8 = -FLAPPY_Q8(3) - 10 };
    CHECK(flappy_pipe_x(&p) == -4);                    // floors through negatives
    CHECK(flappy_speed_q8_for_score(FLAPPY_DIFF_NORMAL, 0) == FLAPPY_SPEED_BASE_Q8);
    CHECK(flappy_speed_q8_for_score(FLAPPY_DIFF_NORMAL, 1000) ==
          flappy_speed_q8_for_score(FLAPPY_DIFF_NORMAL, FLAPPY_SPEED_CAP_SCORE));
    CHECK(flappy_gap_for_score(FLAPPY_DIFF_NORMAL, 0) == FLAPPY_GAP_MAX);
}

int main(void) {
    test_first_boot_starts_tutorial();
    test_tutorial_full_flow();
    test_tutorial_gate_crash_retries();
    test_tutorial_skip();
    test_title_menu();
    test_fall_to_ground_is_game_over();
    test_autopilot_scores_and_levels_are_fair();
    test_all_settings_are_passable();
    test_settings_screen();
    test_pause_resume_and_quit();
    test_game_over_retry_rules_and_best();
    test_volume_control();
    test_clamps();
    test_presentation_helpers();
    if (s_failures) {
        fprintf(stderr, "test_flappy_model: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_flappy_model: PASS\n");
    return 0;
}
