// main/flappy_services.c -- audio, NVS and battery workers. See flappy_services.h.
#include "flappy_services.h"

#include <stdatomic.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "flappy_svc";

#define NVS_NAMESPACE      "flappy"
#define NVS_KEY_TUTORIAL   "tut_done"
#define AUDIO_CHUNK        256          // 16 ms at 16 kHz
#define AUDIO_STACK        3072
#define STORE_STACK        3072
#define BATTERY_PERIOD_MS  15000

// NORMAL keeps the original key so existing records carry over.
static const char *const BEST_KEYS[FLAPPY_DIFF_COUNT] = { "best_e", "best", "best_h" };
static const char *const SETTING_KEYS[FLAPPY_SET_BACK] = { "volume", "flap", "diff", "theme" };

typedef struct {
    const char *key;
    uint16_t value;
    bool wide;       // u16 instead of u8
} store_cmd_t;

static QueueHandle_t s_sound_q;
static QueueHandle_t s_store_q;
static bool s_battery_ok;
static atomic_int s_soc = -1;
static atomic_bool s_sound_on;
static atomic_int s_volume_pct;
static atomic_uint s_stat_lvgl_used, s_stat_lvgl_total, s_stat_frame_ms;

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
static esp_err_t nvs_ready(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // The partition is full or from an incompatible NVS version: it cannot be
        // read anyway, so reformat it (standard ESP-IDF recovery).
        ESP_LOGW(TAG, "NVS needs reformat (%s)", esp_err_to_name(e));
        e = nvs_flash_erase();
        if (e == ESP_OK) e = nvs_flash_init();
    }
    return e;
}

esp_err_t flappy_store_load(flappy_saved_t *saved, bool *tutorial_done) {
    *saved = (flappy_saved_t){
        .volume = FLAPPY_VOL_HIGH,
        .flap = FLAPPY_FLAP_NORMAL,
        .difficulty = FLAPPY_DIFF_NORMAL,
        .theme = FLAPPY_THEME_DAY,
    };
    *tutorial_done = false;
    esp_err_t e = nvs_ready();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(e));
        return e;
    }
    nvs_handle_t h;
    e = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;   // first boot
    if (e != ESP_OK) return e;
    uint8_t t = 0;
    if (nvs_get_u8(h, NVS_KEY_TUTORIAL, &t) == ESP_OK) *tutorial_done = t != 0;
    // A missing key keeps its default.
    for (int i = 0; i < FLAPPY_DIFF_COUNT; i++) (void)nvs_get_u16(h, BEST_KEYS[i], &saved->best[i]);
    (void)nvs_get_u8(h, SETTING_KEYS[FLAPPY_SET_VOLUME], &saved->volume);
    (void)nvs_get_u8(h, SETTING_KEYS[FLAPPY_SET_FLAP], &saved->flap);
    (void)nvs_get_u8(h, SETTING_KEYS[FLAPPY_SET_DIFFICULTY], &saved->difficulty);
    (void)nvs_get_u8(h, SETTING_KEYS[FLAPPY_SET_THEME], &saved->theme);
    nvs_close(h);
    ESP_LOGI(TAG, "loaded best=%u/%u/%u tutorial_done=%d volume=%u flap=%u diff=%u theme=%u",
             (unsigned)saved->best[FLAPPY_DIFF_EASY], (unsigned)saved->best[FLAPPY_DIFF_NORMAL],
             (unsigned)saved->best[FLAPPY_DIFF_HARD], *tutorial_done, (unsigned)saved->volume,
             (unsigned)saved->flap, (unsigned)saved->difficulty, (unsigned)saved->theme);
    return ESP_OK;
}

static void store_apply(const store_cmd_t *cmd) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(e));
        return;
    }
    if (cmd->wide) e = nvs_set_u16(h, cmd->key, cmd->value);
    else e = nvs_set_u8(h, cmd->key, (uint8_t)cmd->value);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "NVS write failed: %s", esp_err_to_name(e));
    else ESP_LOGI(TAG, "saved %s=%u", cmd->key, (unsigned)cmd->value);
}

static void poll_battery(void) {
    if (!s_battery_ok) return;
    int soc = bsp_battery_soc();
    atomic_store(&s_soc, soc >= 0 && soc <= 100 ? soc : -1);
}

// Logged here, not in the game loop: console output can stall without a USB host.
static void log_stats(void) {
    ESP_LOGI(TAG, "stats: lvgl_max_used=%u/%u free_heap=%u min_free_heap=%u max_frame_ms=%u",
             atomic_load(&s_stat_lvgl_used), atomic_load(&s_stat_lvgl_total),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             atomic_load(&s_stat_frame_ms));
}

static void store_task(void *arg) {
    (void)arg;
    poll_battery();
    TickType_t next = xTaskGetTickCount() + pdMS_TO_TICKS(BATTERY_PERIOD_MS);
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next - now) > 0 ? next - now : 0;
        store_cmd_t cmd;
        if (xQueueReceive(s_store_q, &cmd, wait) == pdTRUE) {
            store_apply(&cmd);
            continue;
        }
        poll_battery();
        log_stats();
        next = xTaskGetTickCount() + pdMS_TO_TICKS(BATTERY_PERIOD_MS);
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static int sfx_priority(flappy_sfx_t s) {
    switch (s) {
    case FLAPPY_SFX_HIT:
    case FLAPPY_SFX_FALL:
    case FLAPPY_SFX_NEW_BEST: return 3;
    case FLAPPY_SFX_SCORE:
    case FLAPPY_SFX_STEP:
    case FLAPPY_SFX_SELECT:
    case FLAPPY_SFX_PAUSE:    return 2;
    default:                  return 1;
    }
}

// Only the audio worker touches the codec, so volume changes apply here.
static int apply_volume(int *applied) {
    const int want = atomic_load(&s_volume_pct);
    if (want != *applied) {
        bsp_audio_set_volume((uint8_t)want);
        *applied = want;
    }
    return want;
}

static void audio_task(void *arg) {
    (void)arg;
    static int16_t pcm[AUDIO_CHUNK];
    flappy_sfx_voice_t voice = { 0 };
    esp_err_t e = bsp_audio_set_format(FLAPPY_SFX_RATE_HZ, 16, 1);
    if (e != ESP_OK) {
        // Keep the queue alive (posters may hold it); just stop accepting effects.
        ESP_LOGE(TAG, "audio format failed: %s; sound disabled", esp_err_to_name(e));
        atomic_store(&s_sound_on, false);
        vTaskDelete(NULL);
        return;
    }
    int applied = -1;
    (void)apply_volume(&applied);

    for (;;) {
        flappy_sfx_t next;
        if (!flappy_sfx_active(&voice)) {
            if (xQueueReceive(s_sound_q, &next, portMAX_DELAY) == pdTRUE) {
                flappy_sfx_start(&voice, next);
            }
            continue;
        }
        if (apply_volume(&applied) == 0) {
            flappy_sfx_start(&voice, FLAPPY_SFX_NONE);
            continue;
        }
        // Between chunks, let a newer effect of equal or higher priority cut in.
        while (xQueueReceive(s_sound_q, &next, 0) == pdTRUE) {
            if (sfx_priority(next) >= sfx_priority(voice.sfx)) flappy_sfx_start(&voice, next);
        }
        size_t n = flappy_sfx_render(&voice, pcm, AUDIO_CHUNK);
        if (n == 0) continue;
        for (size_t i = n; i < AUDIO_CHUNK; i++) pcm[i] = 0;   // pad the last chunk
        if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) {
            ESP_LOGW(TAG, "audio write failed");
            flappy_sfx_start(&voice, FLAPPY_SFX_NONE);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t flappy_services_start(bool audio_ok, bool battery_ok) {
    s_battery_ok = battery_ok;
    s_store_q = xQueueCreate(8, sizeof(store_cmd_t));
    if (!s_store_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(store_task, "flappy_store", STORE_STACK, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(s_store_q);
        s_store_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (audio_ok) {
        s_sound_q = xQueueCreate(6, sizeof(flappy_sfx_t));
        atomic_store(&s_sound_on, s_sound_q != NULL);
        if (s_sound_q &&
            xTaskCreate(audio_task, "flappy_audio", AUDIO_STACK, NULL, 6, NULL) != pdPASS) {
            atomic_store(&s_sound_on, false);
            vQueueDelete(s_sound_q);
            s_sound_q = NULL;
        }
        if (!atomic_load(&s_sound_on)) ESP_LOGW(TAG, "audio worker unavailable; playing silently");
    }
    return ESP_OK;
}

void flappy_sound_play(flappy_sfx_t sfx) {
    QueueHandle_t q = s_sound_q;
    if (q && atomic_load(&s_sound_on) && sfx != FLAPPY_SFX_NONE &&
        atomic_load(&s_volume_pct) > 0) {
        (void)xQueueSend(q, &sfx, 0);
    }
}

void flappy_sound_set_volume(int percent) {
    atomic_store(&s_volume_pct, percent);
}

void flappy_store_save_best(flappy_difficulty_t difficulty, uint16_t best) {
    if (!s_store_q) return;
    const store_cmd_t cmd = { .key = BEST_KEYS[difficulty], .value = best, .wide = true };
    if (xQueueSend(s_store_q, &cmd, 0) != pdTRUE) ESP_LOGW(TAG, "best score not queued");
}

void flappy_store_save_tutorial_done(void) {
    if (!s_store_q) return;
    const store_cmd_t cmd = { .key = NVS_KEY_TUTORIAL, .value = 1 };
    if (xQueueSend(s_store_q, &cmd, 0) != pdTRUE) ESP_LOGW(TAG, "tutorial flag not queued");
}

void flappy_store_save_setting(flappy_setting_t setting, uint8_t value) {
    if (!s_store_q || setting >= FLAPPY_SET_BACK) return;
    const store_cmd_t cmd = { .key = SETTING_KEYS[setting], .value = value };
    if (xQueueSend(s_store_q, &cmd, 0) != pdTRUE) ESP_LOGW(TAG, "setting not queued");
}

int flappy_battery_soc(void) {
    return atomic_load(&s_soc);
}

void flappy_stats_note(uint32_t lvgl_max_used, uint32_t lvgl_total, uint32_t max_frame_ms) {
    atomic_store(&s_stat_lvgl_used, lvgl_max_used);
    atomic_store(&s_stat_lvgl_total, lvgl_total);
    atomic_store(&s_stat_frame_ms, max_frame_ms);
}
