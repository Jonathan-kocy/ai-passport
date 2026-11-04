// main/flappy_ui.h -- LVGL presentation of the Flappy Bird model.
//
// All functions must run with the LVGL lock held (or from an LVGL timer, which
// already runs inside the LVGL task). The UI never mutates the model.
#pragma once

#include <stdint.h>

#include "flappy_model.h"

// Build the single game screen and load it.
void flappy_ui_create(const flappy_model_t *m);

// Redraw from the model. `events` are the FLAPPY_EV_* bits of this frame.
void flappy_ui_sync(const flappy_model_t *m, uint32_t events, uint32_t elapsed_ms);

// Battery state of charge 0..100, or -1 when unavailable.
void flappy_ui_set_battery(int soc);
