// Host tests for main/flappy_model.c (no ESP-IDF, no LVGL).
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
    CHECK(m.tut_step == FLAPPY_TUT_DIVE);
    CHECK(m.bird_vy_q8 == FLAPPY_FLAP_VY_Q8);

    run(&m, 300);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(m.tut_step == FLAPPY_TUT_PAUSE);
    CHECK(m.bird_vy_q8 >= FLAPPY_DIVE_VY_Q8);

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
    CHECK(m.tut_step == FLAPPY_TUT_DONE);
    CHECK(m.tut_attempts == 0);
    CHECK(!m.tutorial_done);

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
    CHECK(m.menu_sel == FLAPPY_MENU_PLAY);  // wraps
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
        CHECK(flappy_gap_for_score(m.score) == FLAPPY_GAP_MIN);
    }
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

    tap(&m, FLAPPY_KEY_DOWN);                           // DOWN does nothing when paused
    CHECK(m.state == FLAPPY_PAUSED);
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

static void test_dive_cooldown_and_clamps(void) {
    flappy_model_t m;
    start_game(&m, 44, 0);
    flappy_take_events(&m);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_DIVE);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(!(flappy_take_events(&m) & FLAPPY_EV_DIVE)); // cooling down
    for (int i = 0; i < FLAPPY_DIVE_COOLDOWN_MS / 10; i++) autopilot_step(&m);
    CHECK(m.state == FLAPPY_PLAYING);
    flappy_take_events(&m);
    tap(&m, FLAPPY_KEY_DOWN);
    CHECK(flappy_take_events(&m) & FLAPPY_EV_DIVE);

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
    CHECK(flappy_speed_q8_for_score(0) == FLAPPY_SPEED_BASE_Q8);
    CHECK(flappy_speed_q8_for_score(1000) == flappy_speed_q8_for_score(FLAPPY_SPEED_CAP_SCORE));
    CHECK(flappy_gap_for_score(0) == FLAPPY_GAP_MAX);
}

int main(void) {
    test_first_boot_starts_tutorial();
    test_tutorial_full_flow();
    test_tutorial_gate_crash_retries();
    test_tutorial_skip();
    test_title_menu();
    test_fall_to_ground_is_game_over();
    test_autopilot_scores_and_levels_are_fair();
    test_pause_resume_and_quit();
    test_game_over_retry_rules_and_best();
    test_dive_cooldown_and_clamps();
    test_presentation_helpers();
    if (s_failures) {
        fprintf(stderr, "test_flappy_model: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_flappy_model: PASS\n");
    return 0;
}
