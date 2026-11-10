// main/flappy_model.c -- portable Flappy Bird rules. See flappy_model.h.
#include "flappy_model.h"

#include <string.h>

#define HOVER_Y        150
#define PRACTICE_FLOOR 190   // tutorial autopilot catches the bird here
#define PRACTICE_HOP_Q8 (-560)
#define PRACTICE_CEIL  40
#define GROUND_WRAP_Q8 FLAPPY_Q8(1200)
#define GAP_STEP_LIMIT 70    // max change of gap centre between neighbours
#define TUT_GATE_Y     HOVER_Y

typedef struct {
    int16_t gap_max;
    int16_t gap_min;
    int16_t speed_base_q8;
    int16_t speed_step_q8;
} difficulty_t;

static const difficulty_t DIFFICULTY[FLAPPY_DIFF_COUNT] = {
    [FLAPPY_DIFF_EASY]   = { 116, 96, 270, 3 },
    [FLAPPY_DIFF_NORMAL] = { FLAPPY_GAP_MAX, FLAPPY_GAP_MIN, FLAPPY_SPEED_BASE_Q8, FLAPPY_SPEED_STEP_Q8 },
    [FLAPPY_DIFF_HARD]   = { 92, 76, 370, 5 },
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static uint32_t rng_next(flappy_model_t *m) {
    uint32_t x = m->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m->rng = x;
    return x;
}

static int rng_range(flappy_model_t *m, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng_next(m) % (uint32_t)(hi - lo + 1));
}

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void set_state(flappy_model_t *m, flappy_state_t s) {
    m->state = s;
    m->state_ms = 0;
    m->ok_armed = false;
    m->events |= FLAPPY_EV_STATE;
}

static void set_step(flappy_model_t *m, flappy_tutorial_step_t step) {
    m->tut_step = step;
    m->state_ms = 0;
    m->events |= FLAPPY_EV_STATE | FLAPPY_EV_TUTORIAL_STEP;
}

static void reset_bird(flappy_model_t *m) {
    m->bird_y_q8 = FLAPPY_Q8(HOVER_Y);
    m->bird_vy_q8 = 0;
    m->flap_anim_ms = 1000;
}

static void clear_pipes(flappy_model_t *m) {
    memset(m->pipes, 0, sizeof(m->pipes));
}

static void flap(flappy_model_t *m) {
    m->bird_vy_q8 = flappy_flap_vy_q8(m->flap);
    m->flap_anim_ms = 0;
    m->events |= FLAPPY_EV_FLAP;
}

static void volume_down(flappy_model_t *m) {
    m->volume = m->volume == FLAPPY_VOL_MUTE ? FLAPPY_VOL_HIGH
                                             : (flappy_volume_t)(m->volume - 1);
    m->events |= FLAPPY_EV_VOLUME;
}

static int hover_bob(uint32_t world_ms) {
    // Triangle wave, 1 s period, -5..+5 px.
    int t = (int)(world_ms % 1000u);
    int v = t < 500 ? t : 1000 - t;
    return v * 10 / 500 - 5;
}

static void hover(flappy_model_t *m) {
    m->bird_y_q8 = FLAPPY_Q8(HOVER_Y + hover_bob(m->world_ms));
    m->bird_vy_q8 = 0;
}

// Gravity + clamp. Returns true when the bird touches the ground.
static bool apply_physics(flappy_model_t *m, int ceiling_px) {
    m->bird_vy_q8 += FLAPPY_GRAVITY_Q8;
    if (m->bird_vy_q8 > FLAPPY_MAX_VY_Q8) m->bird_vy_q8 = FLAPPY_MAX_VY_Q8;
    m->bird_y_q8 += m->bird_vy_q8;
    const int32_t top_limit = FLAPPY_Q8(ceiling_px + FLAPPY_HIT_HALF_H);
    if (m->bird_y_q8 < top_limit) {
        m->bird_y_q8 = top_limit;
        if (m->bird_vy_q8 < 0) m->bird_vy_q8 = 0;
    }
    const int32_t ground_limit = FLAPPY_Q8(FLAPPY_GROUND_Y - FLAPPY_HIT_HALF_H);
    if (m->bird_y_q8 >= ground_limit) {
        m->bird_y_q8 = ground_limit;
        return true;
    }
    return false;
}

static void spawn_pipe(flappy_model_t *m, int32_t x_q8, int gap_h, int prev_center) {
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        flappy_pipe_t *p = &m->pipes[i];
        if (p->active) continue;
        int lo = FLAPPY_GAP_MARGIN + gap_h / 2;
        int hi = FLAPPY_GROUND_Y - FLAPPY_GAP_MARGIN - gap_h / 2;
        if (prev_center > 0) {
            lo = clamp_int(prev_center - GAP_STEP_LIMIT, lo, hi);
            hi = clamp_int(prev_center + GAP_STEP_LIMIT, lo, hi);
        }
        p->x_q8 = x_q8;
        p->gap_h = (int16_t)gap_h;
        p->gap_y = (int16_t)rng_range(m, lo, hi);
        p->active = true;
        p->scored = false;
        return;
    }
}

static const flappy_pipe_t *rightmost_pipe(const flappy_model_t *m) {
    const flappy_pipe_t *best = NULL;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        const flappy_pipe_t *p = &m->pipes[i];
        if (p->active && (!best || p->x_q8 > best->x_q8)) best = p;
    }
    return best;
}

static void move_pipes(flappy_model_t *m, int32_t speed_q8) {
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        flappy_pipe_t *p = &m->pipes[i];
        if (!p->active) continue;
        p->x_q8 -= speed_q8;
        if (p->x_q8 < -FLAPPY_Q8(FLAPPY_PIPE_W + 8)) p->active = false;
    }
}

static void maybe_spawn(flappy_model_t *m) {
    const flappy_pipe_t *last = rightmost_pipe(m);
    if (!last) {
        spawn_pipe(m, FLAPPY_Q8(FLAPPY_W + 40), flappy_gap_for_score(m->difficulty, m->score), 0);
    } else if (last->x_q8 <= FLAPPY_Q8(FLAPPY_W - FLAPPY_PIPE_SPACING)) {
        spawn_pipe(m, last->x_q8 + FLAPPY_Q8(FLAPPY_PIPE_SPACING),
                   flappy_gap_for_score(m->difficulty, m->score), last->gap_y);
    }
}

// Returns how many pipes the bird's centre passed this step.
static int update_scoring(flappy_model_t *m) {
    int passed = 0;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        flappy_pipe_t *p = &m->pipes[i];
        if (!p->active || p->scored) continue;
        if (p->x_q8 + FLAPPY_Q8(FLAPPY_PIPE_W) < FLAPPY_Q8(FLAPPY_BIRD_X)) {
            p->scored = true;
            passed++;
        }
    }
    return passed;
}

static bool hits_pipe(const flappy_model_t *m) {
    const int by = flappy_bird_y(m);
    const int left = FLAPPY_BIRD_X - FLAPPY_HIT_HALF_W;
    const int right = FLAPPY_BIRD_X + FLAPPY_HIT_HALF_W;
    const int top = by - FLAPPY_HIT_HALF_H;
    const int bottom = by + FLAPPY_HIT_HALF_H;
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        const flappy_pipe_t *p = &m->pipes[i];
        if (!p->active) continue;
        const int px = flappy_pipe_x(p);
        if (right <= px || left >= px + FLAPPY_PIPE_W) continue;
        const int gap_top = p->gap_y - p->gap_h / 2;
        const int gap_bottom = p->gap_y + p->gap_h / 2;
        if (top < gap_top || bottom > gap_bottom) return true;
    }
    return false;
}

static void scroll_ground(flappy_model_t *m, int32_t speed_q8) {
    m->ground_q8 = (m->ground_q8 + speed_q8) % GROUND_WRAP_Q8;
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------
static void enter_title(flappy_model_t *m) {
    set_state(m, FLAPPY_TITLE);
    m->menu_sel = FLAPPY_MENU_PLAY;
    reset_bird(m);
    clear_pipes(m);
}

static void enter_settings(flappy_model_t *m) {
    set_state(m, FLAPPY_SETTINGS);
    m->settings_sel = FLAPPY_SET_VOLUME;
}

static void enter_ready(flappy_model_t *m) {
    set_state(m, FLAPPY_READY);
    reset_bird(m);
    clear_pipes(m);
    m->score = 0;
    m->new_best = false;
}

static void enter_tutorial(flappy_model_t *m) {
    set_state(m, FLAPPY_TUTORIAL);
    reset_bird(m);
    clear_pipes(m);
    m->tut_paused = false;
    m->tut_flying = false;
    m->tut_attempts = 0;
    set_step(m, FLAPPY_TUT_WELCOME);
}

static void tutorial_reset_gate(flappy_model_t *m) {
    reset_bird(m);
    clear_pipes(m);
    m->tut_flying = false;
    flappy_pipe_t *p = &m->pipes[0];
    p->x_q8 = FLAPPY_Q8(FLAPPY_W + 10);
    p->gap_h = FLAPPY_TUTORIAL_GAP;
    p->gap_y = TUT_GATE_Y;
    p->active = true;
    p->scored = false;
}

static void finish_tutorial(flappy_model_t *m) {
    m->tutorial_done = true;
    m->tut_paused = false;
    m->events |= FLAPPY_EV_TUTORIAL_DONE;
    enter_ready(m);
}

static void start_playing(flappy_model_t *m) {
    set_state(m, FLAPPY_PLAYING);
    clear_pipes(m);
    maybe_spawn(m);
    flap(m);
}

static void game_over(flappy_model_t *m) {
    set_state(m, FLAPPY_GAME_OVER);
    m->events |= FLAPPY_EV_GROUND;
    if (m->score > m->best) {
        m->best = m->score;
        m->bests[m->difficulty] = m->score;
        m->new_best = true;
        m->events |= FLAPPY_EV_NEW_BEST;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void flappy_init(flappy_model_t *m, uint16_t best, bool tutorial_done, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->best = best;
    m->bests[FLAPPY_DIFF_NORMAL] = best;
    m->tutorial_done = tutorial_done;
    m->rng = seed ? seed : 0x2545F491u;
    m->volume = FLAPPY_VOL_HIGH;
    m->flap = FLAPPY_FLAP_NORMAL;
    m->difficulty = FLAPPY_DIFF_NORMAL;
    m->theme = FLAPPY_THEME_DAY;
    reset_bird(m);
    if (tutorial_done) {
        enter_title(m);
    } else {
        enter_tutorial(m);
    }
}

void flappy_restore(flappy_model_t *m, const flappy_saved_t *saved) {
    m->volume = saved->volume < FLAPPY_VOL_COUNT ? (flappy_volume_t)saved->volume : FLAPPY_VOL_HIGH;
    m->flap = saved->flap < FLAPPY_FLAP_COUNT ? (flappy_flap_t)saved->flap : FLAPPY_FLAP_NORMAL;
    m->difficulty = saved->difficulty < FLAPPY_DIFF_COUNT ? (flappy_difficulty_t)saved->difficulty
                                                          : FLAPPY_DIFF_NORMAL;
    m->theme = saved->theme < FLAPPY_THEME_COUNT ? (flappy_theme_t)saved->theme : FLAPPY_THEME_DAY;
    memcpy(m->bests, saved->best, sizeof(m->bests));
    m->best = m->bests[m->difficulty];
}

// OK on a settings row: step its value, or leave on BACK.
static void settings_select(flappy_model_t *m) {
    switch (m->settings_sel) {
    case FLAPPY_SET_VOLUME:
        volume_down(m);        // EV_VOLUME previews and persists it
        m->events |= FLAPPY_EV_STATE;
        return;
    case FLAPPY_SET_FLAP:
        m->flap = (flappy_flap_t)((m->flap + 1) % FLAPPY_FLAP_COUNT);
        break;
    case FLAPPY_SET_DIFFICULTY:
        m->difficulty = (flappy_difficulty_t)((m->difficulty + 1) % FLAPPY_DIFF_COUNT);
        m->best = m->bests[m->difficulty];
        break;
    case FLAPPY_SET_THEME:
        m->theme = (flappy_theme_t)((m->theme + 1) % FLAPPY_THEME_COUNT);
        break;
    default:
        m->events |= FLAPPY_EV_SELECT;
        enter_title(m);
        m->menu_sel = FLAPPY_MENU_SETTINGS;
        return;
    }
    m->events |= FLAPPY_EV_SELECT | FLAPPY_EV_SETTING | FLAPPY_EV_STATE;
}

static bool tutorial_press(flappy_model_t *m, flappy_key_t key) {
    switch (m->tut_step) {
    case FLAPPY_TUT_WELCOME:
        if (key != FLAPPY_KEY_OK) return false;
        set_step(m, FLAPPY_TUT_FLAP);
        return true;
    case FLAPPY_TUT_FLAP:
        if (key != FLAPPY_KEY_UP) return false;
        flap(m);
        set_step(m, FLAPPY_TUT_PAUSE);
        return true;
    case FLAPPY_TUT_PAUSE:
        if (key == FLAPPY_KEY_UP) { flap(m); return true; }
        if (key == FLAPPY_KEY_DOWN) return false;
        m->tut_paused = true;
        m->events |= FLAPPY_EV_PAUSE;
        set_step(m, FLAPPY_TUT_RESUME);
        return true;
    case FLAPPY_TUT_RESUME:
        if (key == FLAPPY_KEY_OK) m->ok_armed = true;  // resume on release
        return false;                                  // leave LONG available to skip
    case FLAPPY_TUT_GATE:
        if (key == FLAPPY_KEY_UP) {
            m->tut_flying = true;
            flap(m);
            return true;
        }
        return false;
    case FLAPPY_TUT_SETTINGS:
        if (key == FLAPPY_KEY_UP) { flap(m); return true; }
        if (key == FLAPPY_KEY_DOWN) return false;
        set_step(m, FLAPPY_TUT_DONE);
        return true;
    default:
        return false;
    }
}

static bool handle_press(flappy_model_t *m, flappy_key_t key) {
    switch (m->state) {
    case FLAPPY_TITLE:
        if (key == FLAPPY_KEY_UP || key == FLAPPY_KEY_DOWN) {
            int d = key == FLAPPY_KEY_UP ? FLAPPY_MENU_COUNT - 1 : 1;
            m->menu_sel = (flappy_menu_t)((m->menu_sel + d) % FLAPPY_MENU_COUNT);
            m->events |= FLAPPY_EV_MENU_MOVE | FLAPPY_EV_STATE;
            return true;
        }
        m->events |= FLAPPY_EV_SELECT;
        if (m->menu_sel == FLAPPY_MENU_SETTINGS) {
            enter_settings(m);
        } else if (m->menu_sel == FLAPPY_MENU_PLAY && m->tutorial_done) {
            enter_ready(m);
        } else {
            enter_tutorial(m);   // first play always teaches the controls
        }
        return true;
    case FLAPPY_SETTINGS:
        if (key == FLAPPY_KEY_OK) {
            settings_select(m);
        } else {
            int d = key == FLAPPY_KEY_UP ? FLAPPY_SET_COUNT - 1 : 1;
            m->settings_sel = (flappy_setting_t)((m->settings_sel + d) % FLAPPY_SET_COUNT);
            m->events |= FLAPPY_EV_MENU_MOVE | FLAPPY_EV_STATE;
        }
        return true;
    case FLAPPY_TUTORIAL:
        return tutorial_press(m, key);
    case FLAPPY_READY:
        if (key == FLAPPY_KEY_DOWN) { volume_down(m); return true; }
        if (key != FLAPPY_KEY_UP) return false;
        start_playing(m);
        return true;
    case FLAPPY_PLAYING:
        if (key == FLAPPY_KEY_UP) flap(m);
        else if (key == FLAPPY_KEY_DOWN) volume_down(m);
        else {
            set_state(m, FLAPPY_PAUSED);
            m->events |= FLAPPY_EV_PAUSE;
        }
        return true;
    case FLAPPY_PAUSED:
        if (key == FLAPPY_KEY_OK) {
            m->ok_armed = true;   // tap = resume on release, hold = quit
            return false;
        }
        if (key == FLAPPY_KEY_UP) {
            set_state(m, FLAPPY_PLAYING);
            m->events |= FLAPPY_EV_RESUME;
            return true;
        }
        volume_down(m);
        return true;
    case FLAPPY_GAME_OVER:
        if (m->state_ms < FLAPPY_GAME_OVER_LOCK_MS) return true;
        m->events |= FLAPPY_EV_SELECT;
        if (key == FLAPPY_KEY_DOWN) enter_title(m);
        else enter_ready(m);
        return true;
    case FLAPPY_DYING:
    default:
        return true;
    }
}

static void handle_long(flappy_model_t *m, flappy_key_t key) {
    if (key != FLAPPY_KEY_OK) return;
    if (m->state == FLAPPY_TUTORIAL && m->tut_step != FLAPPY_TUT_DONE) {
        m->ok_long_fired = true;     // skip the tutorial
        finish_tutorial(m);
    } else if (m->state == FLAPPY_PAUSED && m->ok_armed) {
        m->ok_long_fired = true;     // quit the run
        enter_title(m);
    } else if (m->state == FLAPPY_READY) {
        m->ok_long_fired = true;
        enter_title(m);
    }
}

static void handle_release(flappy_model_t *m, flappy_key_t key) {
    if (key != FLAPPY_KEY_OK) return;
    const bool tap = m->ok_armed && !m->ok_long_fired;
    m->ok_armed = false;
    m->ok_long_fired = false;
    if (!tap) return;
    if (m->state == FLAPPY_PAUSED) {
        set_state(m, FLAPPY_PLAYING);
        m->events |= FLAPPY_EV_RESUME;
    } else if (m->state == FLAPPY_TUTORIAL && m->tut_step == FLAPPY_TUT_RESUME) {
        m->tut_paused = false;
        m->events |= FLAPPY_EV_RESUME;
        tutorial_reset_gate(m);
        set_step(m, FLAPPY_TUT_GATE);
    }
}

void flappy_input(flappy_model_t *m, flappy_key_t key, flappy_key_ev_t ev) {
    if ((unsigned)key > FLAPPY_KEY_OK) return;
    switch (ev) {
    case FLAPPY_KEY_PRESS:
        m->key_consumed[key] = handle_press(m, key);
        break;
    case FLAPPY_KEY_LONG:
        if (!m->key_consumed[key]) handle_long(m, key);
        break;
    case FLAPPY_KEY_RELEASE:
        handle_release(m, key);
        m->key_consumed[key] = false;
        break;
    default:
        break;
    }
}

static void tutorial_step_once(flappy_model_t *m) {
    if (m->tut_paused) return;
    const int32_t base = FLAPPY_SPEED_BASE_Q8;
    scroll_ground(m, base);

    if (m->tut_step == FLAPPY_TUT_GATE) {
        if (!m->tut_flying) { hover(m); return; }
        move_pipes(m, base);
        bool crashed = apply_physics(m, 0) || hits_pipe(m);
        if (crashed) {
            m->tut_attempts++;
            m->events |= FLAPPY_EV_TUTORIAL_RETRY;
            tutorial_reset_gate(m);
            return;
        }
        if (update_scoring(m) > 0) {
            m->events |= FLAPPY_EV_SCORE;
            set_step(m, FLAPPY_TUT_SETTINGS);
        }
        return;
    }

    // Practice physics with an autopilot so the bird never crashes while
    // the player is still reading the next instruction.
    move_pipes(m, base);
    (void)apply_physics(m, PRACTICE_CEIL);
    if (m->bird_y_q8 > FLAPPY_Q8(PRACTICE_FLOOR) && m->bird_vy_q8 > 0) {
        m->bird_vy_q8 = PRACTICE_HOP_Q8;
        m->flap_anim_ms = 0;
    }
    if (m->tut_step == FLAPPY_TUT_DONE && m->state_ms >= FLAPPY_TUTORIAL_DONE_MS) {
        finish_tutorial(m);
    }
}

static void step_once(flappy_model_t *m) {
    const bool paused = m->state == FLAPPY_PAUSED ||
                        (m->state == FLAPPY_TUTORIAL && m->tut_paused);
    m->state_ms += FLAPPY_STEP_MS;
    if (!paused) {
        m->world_ms += FLAPPY_STEP_MS;
        if (m->flap_anim_ms < 100000u) m->flap_anim_ms += FLAPPY_STEP_MS;
    }

    switch (m->state) {
    case FLAPPY_TITLE:
    case FLAPPY_SETTINGS:
    case FLAPPY_READY:
        scroll_ground(m, FLAPPY_SPEED_BASE_Q8);
        hover(m);
        break;
    case FLAPPY_TUTORIAL:
        tutorial_step_once(m);
        break;
    case FLAPPY_PLAYING: {
        const int32_t speed = flappy_speed_q8_for_score(m->difficulty, m->score);
        scroll_ground(m, speed);
        move_pipes(m, speed);
        maybe_spawn(m);
        const bool on_ground = apply_physics(m, 0);
        const int passed = update_scoring(m);
        if (passed > 0) {
            m->score = (uint16_t)(m->score + passed);
            m->events |= FLAPPY_EV_SCORE;
        }
        if (on_ground) {
            m->events |= FLAPPY_EV_HIT;
            game_over(m);
        } else if (hits_pipe(m)) {
            m->events |= FLAPPY_EV_HIT;
            if (m->bird_vy_q8 < 0) m->bird_vy_q8 = 0;
            set_state(m, FLAPPY_DYING);
        }
        break;
    }
    case FLAPPY_DYING:
        if (apply_physics(m, 0)) game_over(m);
        break;
    case FLAPPY_PAUSED:
    case FLAPPY_GAME_OVER:
    default:
        break;
    }
}

void flappy_advance(flappy_model_t *m, uint32_t elapsed_ms) {
    if (elapsed_ms > FLAPPY_MAX_ELAPSED_MS) elapsed_ms = FLAPPY_MAX_ELAPSED_MS;
    m->acc_ms += elapsed_ms;
    while (m->acc_ms >= FLAPPY_STEP_MS) {
        m->acc_ms -= FLAPPY_STEP_MS;
        step_once(m);
    }
}

uint32_t flappy_take_events(flappy_model_t *m) {
    uint32_t ev = m->events;
    m->events = 0;
    return ev;
}

// ---------------------------------------------------------------------------
// Presentation helpers
// ---------------------------------------------------------------------------
int flappy_bird_y(const flappy_model_t *m) {
    // Round to nearest pixel (arithmetic shift floors negatives consistently).
    return (int)((m->bird_y_q8 + 128) >> 8);
}

static bool bird_is_hovering(const flappy_model_t *m) {
    return m->state == FLAPPY_TITLE || m->state == FLAPPY_SETTINGS || m->state == FLAPPY_READY ||
           (m->state == FLAPPY_TUTORIAL && m->tut_step == FLAPPY_TUT_GATE && !m->tut_flying);
}

int flappy_bird_angle_deci(const flappy_model_t *m) {
    if (bird_is_hovering(m)) return 0;
    if (m->state == FLAPPY_GAME_OVER) return 900;
    int a = (int)(m->bird_vy_q8 * 30 / 64);   // ~120 deci-degrees per px/step
    int max_down = m->state == FLAPPY_DYING ? 900 : 700;
    return clamp_int(a, -250, max_down);
}

int flappy_bird_frame(const flappy_model_t *m) {
    static const uint8_t cycle[4] = { 0, 1, 2, 1 };
    if (m->state == FLAPPY_DYING || m->state == FLAPPY_GAME_OVER ||
        m->state == FLAPPY_PAUSED || (m->state == FLAPPY_TUTORIAL && m->tut_paused)) {
        return 1;
    }
    if (bird_is_hovering(m)) return cycle[(m->world_ms / 110u) % 4u];
    if (m->flap_anim_ms < 300u) return cycle[(m->flap_anim_ms / 75u) % 4u];
    return 1;
}

int flappy_pipe_x(const flappy_pipe_t *p) {
    // Floor division so pixels keep moving smoothly through negative x.
    return (int)(p->x_q8 >> 8);
}

int flappy_ground_offset(const flappy_model_t *m, int period) {
    if (period <= 0) return 0;
    return (int)((m->ground_q8 >> 8) % period);
}

int flappy_gap_for_score(flappy_difficulty_t difficulty, uint16_t score) {
    const difficulty_t *d = &DIFFICULTY[difficulty];
    int gap = d->gap_max - (int)score;
    return gap < d->gap_min ? d->gap_min : gap;
}

int flappy_volume_percent(flappy_volume_t level) {
    // HIGH is the original fixed level. The codec curve is about 0.5 dB per percent.
    static const uint8_t percent[FLAPPY_VOL_COUNT] = { 0, 35, 50, 65 };
    return (unsigned)level < FLAPPY_VOL_COUNT ? percent[level] : percent[FLAPPY_VOL_HIGH];
}

int32_t flappy_speed_q8_for_score(flappy_difficulty_t difficulty, uint16_t score) {
    const difficulty_t *d = &DIFFICULTY[difficulty];
    int s = score > FLAPPY_SPEED_CAP_SCORE ? FLAPPY_SPEED_CAP_SCORE : score;
    return d->speed_base_q8 + (int32_t)s * d->speed_step_q8;
}

int32_t flappy_flap_vy_q8(flappy_flap_t flap) {
    // Rise is about 26, 36, or 49 px.
    static const int16_t vy[FLAPPY_FLAP_COUNT] = { -640, FLAPPY_FLAP_VY_Q8, -880 };
    return vy[flap];
}
