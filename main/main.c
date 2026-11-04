// main/main.c -- Flappy Bird for FoloToy AI Passport: start-up and game loop.
//
// Ownership:
//   * Button callbacks (esp_timer task) only translate and enqueue key events.
//   * The game loop is an LVGL timer, so the model and every LVGL object are
//     touched only inside the LVGL task (no extra locking needed there).
//   * Sound, NVS writes and battery polling run in flappy_services workers.
//
// Controls (see docs/flappy-bird.md):
//   UP    press  flap (title/menu: move up)
//   DOWN  press  dive (title/menu: move down; game over: back to title)
//   OK    press  pause in game, select in menus; tap to resume when paused;
//         hold   quit to title while paused, skip the tutorial
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "flappy_model.h"
#include "flappy_services.h"
#include "flappy_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "key_guard.h"
#include "lvgl.h"

static const char *TAG = "flappy";

#define INPUT_QUEUE_DEPTH 16
#define GAME_TIMER_MS     16

typedef struct {
    flappy_key_t key;
    flappy_key_ev_t ev;
} input_event_t;

static flappy_model_t s_model;           // owned by the LVGL task after start-up
static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;
static int64_t s_last_us;

// Waking from deep sleep by key reboots the chip while that key is still held:
// drop key actions until it comes up (deadline is only a stuck-key safety net).
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// ---------------------------------------------------------------------------
// Input: runs on the shared esp_timer task; enqueue only, never block.
// ---------------------------------------------------------------------------
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check so an early release still
    // disarms it (tests/test_key_wake_contract.py).
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) return;
    if (!s_input_ready || !s_input_queue) return;

    input_event_t in;
    switch (ev) {
    case BSP_BTN_PRESS:   in.ev = FLAPPY_KEY_PRESS; break;
    case BSP_BTN_LONG:    in.ev = FLAPPY_KEY_LONG; break;
    case BSP_BTN_RELEASE: in.ev = FLAPPY_KEY_RELEASE; break;
    default:              return;   // CLICK/DOUBLE: the game acts on press edges
    }
    switch (btn) {
    case BSP_BTN_UP:   in.key = FLAPPY_KEY_UP; break;
    case BSP_BTN_DOWN: in.key = FLAPPY_KEY_DOWN; break;
    case BSP_BTN_OK:   in.key = FLAPPY_KEY_OK; break;
    default:           return;
    }
    (void)xQueueSend(s_input_queue, &in, 0);
}

// ---------------------------------------------------------------------------
// Game loop (LVGL task)
// ---------------------------------------------------------------------------
static void play_sounds(uint32_t ev) {
    if (ev & FLAPPY_EV_NEW_BEST)           flappy_sound_play(FLAPPY_SFX_NEW_BEST);
    else if (ev & FLAPPY_EV_HIT)           flappy_sound_play(FLAPPY_SFX_HIT);
    else if ((ev & FLAPPY_EV_GROUND))      flappy_sound_play(FLAPPY_SFX_FALL);
    else if (ev & FLAPPY_EV_TUTORIAL_RETRY) flappy_sound_play(FLAPPY_SFX_HIT);
    else if (ev & FLAPPY_EV_SCORE)         flappy_sound_play(FLAPPY_SFX_SCORE);
    else if (ev & FLAPPY_EV_PAUSE)         flappy_sound_play(FLAPPY_SFX_PAUSE);
    else if (ev & (FLAPPY_EV_SELECT | FLAPPY_EV_RESUME)) flappy_sound_play(FLAPPY_SFX_SELECT);
    else if (ev & FLAPPY_EV_TUTORIAL_STEP) flappy_sound_play(FLAPPY_SFX_STEP);
    else if (ev & FLAPPY_EV_DIVE)          flappy_sound_play(FLAPPY_SFX_DIVE);
    else if (ev & FLAPPY_EV_FLAP)          flappy_sound_play(FLAPPY_SFX_FLAP);
    else if (ev & FLAPPY_EV_MENU_MOVE)     flappy_sound_play(FLAPPY_SFX_MENU);
}

static void game_tick(lv_timer_t *t) {
    (void)t;
    input_event_t in;
    while (xQueueReceive(s_input_queue, &in, 0) == pdTRUE) {
        flappy_input(&s_model, in.key, in.ev);
    }

    const int64_t now = esp_timer_get_time();
    uint32_t elapsed = (uint32_t)((now - s_last_us) / 1000);
    s_last_us += (int64_t)elapsed * 1000;   // keep the sub-millisecond remainder
    flappy_advance(&s_model, elapsed);

    const uint32_t ev = flappy_take_events(&s_model);
    play_sounds(ev);
    if (ev & FLAPPY_EV_NEW_BEST) flappy_store_save_best(s_model.best);
    if (ev & FLAPPY_EV_TUTORIAL_DONE) flappy_store_save_tutorial_done();

    flappy_ui_sync(&s_model, ev, elapsed);
    flappy_ui_set_battery(flappy_battery_soc());
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------
void app_main(void) {
    ESP_LOGI(TAG, "Flappy Bird starting");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
    }

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    uint16_t best = 0;
    bool tutorial_done = false;
    if (flappy_store_load(&best, &tutorial_done) != ESP_OK) {
        ESP_LOGW(TAG, "storage unavailable; best score will not persist");
    }
    flappy_init(&s_model, best, tutorial_done, esp_random());

    const bool audio_ok = bsp_audio_init() == ESP_OK;
    const bool battery_ok = bsp_battery_init() == ESP_OK;
    if (flappy_services_start(audio_ok, battery_ok) != ESP_OK) {
        ESP_LOGW(TAG, "service workers unavailable");
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock timeout during start-up");
        return;
    }
    flappy_ui_create(&s_model);
    flappy_ui_set_battery(flappy_battery_soc());
    s_last_us = esp_timer_get_time();
    lv_timer_t *timer = lv_timer_create(game_tick, GAME_TIMER_MS, NULL);
    bsp_lvgl_unlock();
    bsp_display_backlight(100);
    if (!timer) {
        ESP_LOGE(TAG, "game timer allocation failed");
        return;
    }

    esp_err_t e = bsp_button_init(on_key, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(e));
        return;
    }
    if (s_key_guard.armed) {
        // A key released before the button driver started never reports a release.
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "ready: audio=%d battery=%d best=%u tutorial_done=%d",
             audio_ok, battery_ok, (unsigned)best, tutorial_done);
}
