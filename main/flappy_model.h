// main/flappy_model.h -- portable Flappy Bird game model.
//
// Pure C11 with no ESP-IDF or LVGL dependency: physics, pipes, collisions,
// scoring, the interactive tutorial, and every button rule live here so the
// same code runs in host tests and on the device. The LVGL page only reads
// this state and draws it; it never moves the bird or decides a collision.
//
// Time advances in fixed FLAPPY_STEP_MS steps. Positions and velocities are
// Q8 fixed point (value * 256) in screen pixels.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// World geometry (portrait 240 x 320 panel).
// ---------------------------------------------------------------------------
#define FLAPPY_W            240
#define FLAPPY_H            320
#define FLAPPY_GROUND_Y     280      // first ground pixel row
#define FLAPPY_BIRD_X       64       // bird centre x, fixed
#define FLAPPY_BIRD_W       34       // sprite size
#define FLAPPY_BIRD_H       24
#define FLAPPY_HIT_HALF_W   11       // forgiving hitbox, smaller than the sprite
#define FLAPPY_HIT_HALF_H   8
#define FLAPPY_PIPE_W       44
#define FLAPPY_PIPE_COUNT   3
#define FLAPPY_PIPE_SPACING 150      // horizontal distance between pipe pairs
#define FLAPPY_GAP_MAX      100      // gap height at score 0
#define FLAPPY_GAP_MIN      80       // narrowest gap (reached at score 20)
#define FLAPPY_GAP_MARGIN   40       // minimum pipe length above/below a gap
#define FLAPPY_TUTORIAL_GAP 112      // generous gap for the practice gate

// ---------------------------------------------------------------------------
// Physics (per FLAPPY_STEP_MS, Q8 pixels).
// ---------------------------------------------------------------------------
#define FLAPPY_STEP_MS        10
#define FLAPPY_Q8(px)         ((int32_t)((px) * 256))
#define FLAPPY_GRAVITY_Q8     31     // ~0.12 px/step^2
#define FLAPPY_FLAP_VY_Q8     (-760) // ~-2.97 px/step: ~36 px rise
#define FLAPPY_DIVE_VY_Q8     820    // ~3.2 px/step downward kick
#define FLAPPY_MAX_VY_Q8      1280   // 5 px/step terminal velocity
#define FLAPPY_DIVE_COOLDOWN_MS 450
#define FLAPPY_SPEED_BASE_Q8  320    // 1.25 px/step = 125 px/s
#define FLAPPY_SPEED_STEP_Q8  4      // added per point...
#define FLAPPY_SPEED_CAP_SCORE 30    // ...up to this score
#define FLAPPY_GAME_OVER_LOCK_MS 700 // ignore retry presses right after a crash
#define FLAPPY_TUTORIAL_DONE_MS  1500
#define FLAPPY_MAX_ELAPSED_MS    100 // clamp long frames instead of tunnelling

typedef enum {
    FLAPPY_TITLE = 0,
    FLAPPY_TUTORIAL,
    FLAPPY_READY,
    FLAPPY_PLAYING,
    FLAPPY_PAUSED,
    FLAPPY_DYING,      // hit something; bird falls to the ground
    FLAPPY_GAME_OVER,
} flappy_state_t;

typedef enum {
    FLAPPY_TUT_WELCOME = 0, // press OK to begin
    FLAPPY_TUT_FLAP,        // press UP
    FLAPPY_TUT_DIVE,        // press DOWN
    FLAPPY_TUT_PAUSE,       // press OK to pause
    FLAPPY_TUT_RESUME,      // tap OK again to resume
    FLAPPY_TUT_GATE,        // fly through one practice gate
    FLAPPY_TUT_DONE,        // short celebration, then READY
    FLAPPY_TUT_STEP_COUNT,
} flappy_tutorial_step_t;

typedef enum {
    FLAPPY_MENU_PLAY = 0,
    FLAPPY_MENU_TUTORIAL,
    FLAPPY_MENU_COUNT,
} flappy_menu_t;

typedef enum {
    FLAPPY_KEY_UP = 0,
    FLAPPY_KEY_DOWN,
    FLAPPY_KEY_OK,
} flappy_key_t;

typedef enum {
    FLAPPY_KEY_PRESS = 0,   // low-latency press edge
    FLAPPY_KEY_LONG,        // held past the long-press threshold
    FLAPPY_KEY_RELEASE,
} flappy_key_ev_t;

// Event bits collected since the last flappy_take_events(); drive sound/storage.
enum {
    FLAPPY_EV_FLAP          = 1u << 0,
    FLAPPY_EV_DIVE          = 1u << 1,
    FLAPPY_EV_SCORE         = 1u << 2,
    FLAPPY_EV_HIT           = 1u << 3,
    FLAPPY_EV_GROUND        = 1u << 4,  // bird reached the ground: game over shown
    FLAPPY_EV_NEW_BEST      = 1u << 5,  // best score changed; persist it
    FLAPPY_EV_TUTORIAL_STEP = 1u << 6,
    FLAPPY_EV_TUTORIAL_DONE = 1u << 7,  // tutorial finished or skipped; persist flag
    FLAPPY_EV_MENU_MOVE     = 1u << 8,
    FLAPPY_EV_SELECT        = 1u << 9,
    FLAPPY_EV_PAUSE         = 1u << 10,
    FLAPPY_EV_RESUME        = 1u << 11,
    FLAPPY_EV_STATE         = 1u << 12, // state or tutorial step changed: redraw overlays
    FLAPPY_EV_TUTORIAL_RETRY = 1u << 13,
};

typedef struct {
    int32_t x_q8;   // left edge
    int16_t gap_y;  // gap centre
    int16_t gap_h;
    bool active;
    bool scored;
} flappy_pipe_t;

typedef struct {
    flappy_state_t state;
    flappy_tutorial_step_t tut_step;
    bool tut_paused;          // pause shown inside the tutorial
    bool tut_flying;          // practice gate: physics started by the first flap
    uint16_t tut_attempts;
    flappy_menu_t menu_sel;

    int32_t bird_y_q8;        // bird centre y
    int32_t bird_vy_q8;
    uint32_t dive_cooldown_ms;
    uint32_t flap_anim_ms;    // time since last flap (wing animation)

    flappy_pipe_t pipes[FLAPPY_PIPE_COUNT];
    int32_t ground_q8;        // scroll offset, wraps in the UI
    uint32_t world_ms;        // ms the world has been animated (hover bobbing)
    uint32_t state_ms;        // ms spent in the current state/step
    uint32_t acc_ms;          // unprocessed time below one step

    uint16_t score;
    uint16_t best;
    bool new_best;
    bool tutorial_done;

    bool ok_armed;            // OK went down while a release-to-act state was shown
    bool ok_long_fired;
    bool key_consumed[3];     // a press already acted; ignore its LONG

    uint32_t rng;
    uint32_t events;
} flappy_model_t;

// Start at the tutorial when it has never been completed, else at the title.
void flappy_init(flappy_model_t *m, uint16_t best, bool tutorial_done, uint32_t seed);

// Feed one physical button event.
void flappy_input(flappy_model_t *m, flappy_key_t key, flappy_key_ev_t ev);

// Advance real time. Long gaps are clamped to FLAPPY_MAX_ELAPSED_MS.
void flappy_advance(flappy_model_t *m, uint32_t elapsed_ms);

// Return and clear accumulated FLAPPY_EV_* bits.
uint32_t flappy_take_events(flappy_model_t *m);

// Presentation helpers (pure; used by the UI and host tests).
int flappy_bird_y(const flappy_model_t *m);           // centre, pixels
int flappy_bird_angle_deci(const flappy_model_t *m);  // 0.1 degree, + = nose down
int flappy_bird_frame(const flappy_model_t *m);       // wing frame 0..2
int flappy_pipe_x(const flappy_pipe_t *p);            // left edge, pixels
int flappy_ground_offset(const flappy_model_t *m, int period); // 0..period-1
int flappy_gap_for_score(uint16_t score);
int32_t flappy_speed_q8_for_score(uint16_t score);
