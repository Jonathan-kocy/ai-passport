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
#include "flappy_model.h"
#include "flappy_sfx.h"

// Load the saved records, settings, and tutorial flag (zero records, default
// settings, and false when nothing is stored). Call once from app_main before
// the UI starts; initialises NVS if needed.
esp_err_t flappy_store_load(flappy_saved_t *saved, bool *tutorial_done);

// Start workers. audio_ok/battery_ok reflect the BSP init results; a missing
// peripheral simply disables that part.
esp_err_t flappy_services_start(bool audio_ok, bool battery_ok);

void flappy_sound_play(flappy_sfx_t sfx);

// Codec output volume 0..100, applied by the audio worker. 0 drops new effects
// and stops the one playing. Safe to call before flappy_services_start().
void flappy_sound_set_volume(int percent);
void flappy_store_save_best(flappy_difficulty_t difficulty, uint16_t best);
void flappy_store_save_tutorial_done(void);
void flappy_store_save_setting(flappy_setting_t setting, uint8_t value);

// Latest battery state of charge 0..100, or -1 when unavailable/unknown.
int flappy_battery_soc(void);

// Latest runtime figures, logged by the storage worker with each battery poll.
void flappy_stats_note(uint32_t lvgl_max_used, uint32_t lvgl_total, uint32_t max_frame_ms);
