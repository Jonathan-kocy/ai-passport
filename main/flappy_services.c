// main/flappy_services.c -- audio, NVS and battery workers. See flappy_services.h.
#include "flappy_services.h"

#include <stdatomic.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "flappy_svc";

#define NVS_NAMESPACE      "flappy"
#define NVS_KEY_BEST       "best"
#define NVS_KEY_TUTORIAL   "tut_done"
#define AUDIO_CHUNK        256          // 16 ms at 16 kHz
#define AUDIO_VOLUME       65
#define AUDIO_STACK        3072
#define STORE_STACK        3072
#define BATTERY_PERIOD_MS  15000

typedef enum { STORE_BEST, STORE_TUTORIAL } store_cmd_kind_t;
typedef struct {
    store_cmd_kind_t kind;
    uint16_t value;
} store_cmd_t;

static QueueHandle_t s_sound_q;
static QueueHandle_t s_store_q;
static bool s_battery_ok;
static atomic_int s_soc = -1;
static atomic_bool s_sound_on;

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

esp_err_t flappy_store_load(uint16_t *best, bool *tutorial_done) {
    *best = 0;
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
    uint16_t b = 0;
    uint8_t t = 0;
    if (nvs_get_u16(h, NVS_KEY_BEST, &b) == ESP_OK) *best = b;
    if (nvs_get_u8(h, NVS_KEY_TUTORIAL, &t) == ESP_OK) *tutorial_done = t != 0;
    nvs_close(h);
    ESP_LOGI(TAG, "loaded best=%u tutorial_done=%d", (unsigned)*best, *tutorial_done);
    return ESP_OK;
}

static void store_apply(const store_cmd_t *cmd) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(e));
        return;
    }
    if (cmd->kind == STORE_BEST) e = nvs_set_u16(h, NVS_KEY_BEST, cmd->value);
    else e = nvs_set_u8(h, NVS_KEY_TUTORIAL, 1);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "NVS write failed: %s", esp_err_to_name(e));
}

static void poll_battery(void) {
    if (!s_battery_ok) return;
    int soc = bsp_battery_soc();
    atomic_store(&s_soc, soc >= 0 && soc <= 100 ? soc : -1);
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
    bsp_audio_set_volume(AUDIO_VOLUME);

    for (;;) {
        flappy_sfx_t next;
        if (!flappy_sfx_active(&voice)) {
            if (xQueueReceive(s_sound_q, &next, portMAX_DELAY) == pdTRUE) {
                flappy_sfx_start(&voice, next);
            }
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
    s_store_q = xQueueCreate(4, sizeof(store_cmd_t));
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
    if (q && atomic_load(&s_sound_on) && sfx != FLAPPY_SFX_NONE) (void)xQueueSend(q, &sfx, 0);
}

void flappy_store_save_best(uint16_t best) {
    if (!s_store_q) return;
    const store_cmd_t cmd = { .kind = STORE_BEST, .value = best };
    if (xQueueSend(s_store_q, &cmd, 0) != pdTRUE) ESP_LOGW(TAG, "best score not queued");
}

void flappy_store_save_tutorial_done(void) {
    if (!s_store_q) return;
    const store_cmd_t cmd = { .kind = STORE_TUTORIAL, .value = 1 };
    if (xQueueSend(s_store_q, &cmd, 0) != pdTRUE) ESP_LOGW(TAG, "tutorial flag not queued");
}

int flappy_battery_soc(void) {
    return atomic_load(&s_soc);
}
