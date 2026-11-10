// main/flappy_text.h -- every user-visible string of the game.
//
// The UI is English only and uses the built-in Montserrat fonts, which cover
// printable ASCII (0x20-0x7E) plus LVGL symbols. tests/test_flappy_fonts.py
// fails if a non-ASCII character or an unconfigured font size appears.
#pragma once

#include "flappy_model.h"

#define FLAPPY_TXT_TITLE        "FLAPPY"
#define FLAPPY_TXT_SUBTITLE     "SKY  HOPPER"
#define FLAPPY_TXT_PLAY         "PLAY"
#define FLAPPY_TXT_TUTORIAL     "TUTORIAL"
#define FLAPPY_TXT_SETTINGS     "SETTINGS"
#define FLAPPY_TXT_BEST_FMT     "BEST %u"
#define FLAPPY_TXT_TITLE_HINT   "UP/DOWN choose, OK go"

#define FLAPPY_TXT_STEP_FMT     "STEP %d / %d"
#define FLAPPY_TXT_RETRY_FMT    "Oops! Try again  (x%u)"

#define FLAPPY_TXT_READY        "GET READY"
#define FLAPPY_TXT_READY_BODY   "Press UP to fly"
#define FLAPPY_TXT_READY_HINT   "DOWN vol, hold OK menu"

#define FLAPPY_TXT_PAUSED       "PAUSED"
#define FLAPPY_TXT_PAUSED_BODY  "Tap OK to resume"
#define FLAPPY_TXT_PAUSED_HINT  "DOWN vol, hold OK quit"

#define FLAPPY_TXT_GAME_OVER    "GAME OVER"
#define FLAPPY_TXT_SCORE        "SCORE"
#define FLAPPY_TXT_BEST         "BEST"
#define FLAPPY_TXT_NEW_BEST     "NEW BEST!"
#define FLAPPY_TXT_OVER_HINT    "OK retry, DOWN menu"

#define FLAPPY_TXT_SETTINGS_HINT "UP/DOWN pick, OK change"

static const char *const FLAPPY_VOLUME_NAMES[FLAPPY_VOL_COUNT] = { "MUTE", "LOW", "MED", "HIGH" };
static const char *const FLAPPY_FLAP_NAMES[FLAPPY_FLAP_COUNT] = { "SOFT", "NORMAL", "STRONG" };
static const char *const FLAPPY_DIFFICULTY_NAMES[FLAPPY_DIFF_COUNT] = { "EASY", "NORMAL", "HARD" };
static const char *const FLAPPY_THEME_NAMES[FLAPPY_THEME_COUNT] = { "DAY", "SUNSET", "NIGHT" };
static const char *const FLAPPY_SETTING_NAMES[FLAPPY_SET_COUNT] = {
    "VOLUME", "FLAP", "DIFFICULTY", "THEME", "BACK",
};

typedef struct {
    const char *title;   // big heading
    const char *body;    // what to do
    const char *hint;    // secondary line
} flappy_tutorial_text_t;

// Numbered steps are FLAP..SETTINGS (1..FLAPPY_TUTORIAL_NUMBERED_STEPS).
#define FLAPPY_TUTORIAL_NUMBERED_STEPS 5

static const flappy_tutorial_text_t FLAPPY_TUTORIAL_TEXT[FLAPPY_TUT_STEP_COUNT] = {
    [FLAPPY_TUT_WELCOME] = { "WELCOME!", "Let's learn to fly.", "Press OK to start" },
    [FLAPPY_TUT_FLAP]    = { "FLAP", "Press UP to flap", "Hold OK: skip tutorial" },
    [FLAPPY_TUT_PAUSE]   = { "PAUSE", "Press OK to pause", "Hold OK: skip tutorial" },
    [FLAPPY_TUT_RESUME]  = { "PAUSED", "Tap OK to resume", "In a game, hold OK quits" },
    [FLAPPY_TUT_GATE]    = { "TRY IT", "Fly through the gate!", "UP to start flying" },
    [FLAPPY_TUT_SETTINGS] = { "SETTINGS", "Sound, flap, level, theme", "Open from the title menu" },
    [FLAPPY_TUT_DONE]    = { "NICE!", "You're ready to fly.", "Get ready..." },
};

// Footer while flying through the practice gate (the card hides so the pipe
// stays visible).
#define FLAPPY_TXT_GATE_FLYING  "Tap UP to stay in the air"
