#include "voice/voice_out.h"
#include "voice/audio_codec.h"
#include "led/led_status.h"
#include "mimi_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "voice_out";
static QueueHandle_t s_q;                              /* of char* (heap text) */
static volatile bool s_enabled = MIMI_VOICE_OUT_DEFAULT;

/* Per-request streaming state: skip the canonical 44-byte WAV header, then play PCM. */
typedef struct { size_t skipped; } play_ctx_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    play_ctx_t *c = e->user_data;
    uint8_t *p = e->data;
    size_t n = e->data_len;
    if (c->skipped < 44) {                              /* drop the WAV header */
        size_t s = (44 - c->skipped < n) ? (44 - c->skipped) : n;
        c->skipped += s; p += s; n -= s;
    }
    if (n) audio_play_pcm(p, n);
    return ESP_OK;
}

static void speak_one(const char *text)
{
    play_ctx_t ctx = {0};
    esp_http_client_config_t cfg = {
        .url = MIMI_PIPER_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http,
        .user_data = &ctx,
        .timeout_ms = 30000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return;
    esp_http_client_set_header(cl, "Content-Type", "text/plain");
    esp_http_client_set_post_field(cl, text, strlen(text));
    led_status_set(LED_STATE_SPEAKING);                 /* teal while fetching + playing */
    esp_err_t err = esp_http_client_perform(cl);
    int status = esp_http_client_get_status_code(cl);
    esp_http_client_cleanup(cl);
    led_status_set(LED_STATE_IDLE);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "TTS failed: %s status=%d", esp_err_to_name(err), status);
    }
}

static void task(void *arg)
{
    char *text;
    for (;;) {
        if (xQueueReceive(s_q, &text, portMAX_DELAY) == pdTRUE) {
            if (s_enabled && text) speak_one(text);
            free(text);
        }
    }
}

void voice_out_init(void)
{
    if (audio_codec_init() != ESP_OK) {
        ESP_LOGW(TAG, "codec init failed; voice disabled");
        s_enabled = false;
        return;
    }
    s_q = xQueueCreate(4, sizeof(char *));
    if (!s_q) { s_enabled = false; return; }
    xTaskCreate(task, "voice_out", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "voice_out ready -> %s", MIMI_PIPER_URL);
}

void voice_speak(const char *text)
{
    if (!s_enabled || !s_q || !text || !text[0]) return;
    char *copy = strdup(text);
    if (copy && xQueueSend(s_q, &copy, 0) != pdTRUE) free(copy);   /* drop if backed up */
}

void voice_out_set_enabled(bool on) { s_enabled = on; }
bool voice_out_enabled(void) { return s_enabled; }
