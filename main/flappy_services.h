// main/flappy_services.h -- slow work kept off the button and LVGL paths.
//
//  * Audio worker: owns all bsp_audio_* PCM writes; effects are posted without
//    blocking and a newer, equal-or-higher priority effect interrupts the
//    current one between 16 ms chunks.
//  * Storage/battery worker: owns NVS writes and polls the CW2017 gauge.
//
// Every post function is non-blocking and safe from the LVGL task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "flappy_sfx.h"

// Load the saved best score and tutorial flag (defaults: 0 / false). Call once
// from app_main before the UI starts; initialises NVS if needed.
esp_err_t flappy_store_load(uint16_t *best, bool *tutorial_done);

// Start workers. audio_ok/battery_ok reflect the BSP init results; a missing
// peripheral simply disables that part.
esp_err_t flappy_services_start(bool audio_ok, bool battery_ok);

void flappy_sound_play(flappy_sfx_t sfx);
void flappy_store_save_best(uint16_t best);
void flappy_store_save_tutorial_done(void);

// Latest battery state of charge 0..100, or -1 when unavailable/unknown.
int flappy_battery_soc(void);
