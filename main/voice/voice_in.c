#include "voice/voice_in.h"
#include "voice/audio_codec.h"
#include "voice/wav.h"
#include "led/led_status.h"
#include "bus/message_bus.h"
#include "mimi_config.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "voice_in";

/* Accumulates the (small) JSON STT response. */
typedef struct { char *buf; size_t cap; size_t len; } resp_ctx_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_ctx_t *c = e->user_data;
    if (!c->buf) return ESP_OK;
    size_t n = e->data_len;
    if (c->len + n >= c->cap) n = (c->cap > c->len + 1) ? (c->cap - c->len - 1) : 0;
    if (n) { memcpy(c->buf + c->len, e->data, n); c->len += n; c->buf[c->len] = '\0'; }
    return ESP_OK;
}

/* POST the WAV at `wav`/`wav_len` to the Whisper service; copy the parsed transcript into
 * out_text. Returns true on a 200 with a non-empty "text". */
static bool stt_transcribe(const uint8_t *wav, size_t wav_len, char *out_text, size_t out_len)
{
    char resp[512];
    resp_ctx_t ctx = { .buf = resp, .cap = sizeof(resp), .len = 0 };
    resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url           = MIMI_WHISPER_URL,
        .method        = HTTP_METHOD_POST,
        .event_handler = on_http,
        .user_data     = &ctx,
        .timeout_ms    = 30000,
        .buffer_size   = 1024,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return false;
    esp_http_client_set_header(cl, "Content-Type", "audio/wav");
    esp_http_client_set_post_field(cl, (const char *)wav, (int)wav_len);
    esp_err_t err = esp_http_client_perform(cl);
    int status = esp_http_client_get_status_code(cl);
    esp_http_client_cleanup(cl);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "STT failed: %s status=%d", esp_err_to_name(err), status);
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) { ESP_LOGW(TAG, "STT bad JSON: %.96s", resp); return false; }
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    bool ok = false;
    if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
        snprintf(out_text, out_len, "%s", text->valuestring);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

bool voice_in_ask(int seconds, char *out_text, size_t out_len)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 15) seconds = 15;
    if (audio_codec_record_init() != ESP_OK) {
        ESP_LOGE(TAG, "capture init failed");
        return false;
    }

    size_t pcm_bytes = (size_t)seconds * MIMI_AUDIO_SAMPLE_RATE * (MIMI_AUDIO_BITS / 8) * MIMI_AUDIO_CHANNELS;
    size_t total = WAV_HEADER_SIZE + pcm_bytes;
    uint8_t *wav = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!wav) { ESP_LOGE(TAG, "no PSRAM for %u-byte clip", (unsigned)total); return false; }

    wav_write_header(wav, MIMI_AUDIO_SAMPLE_RATE, MIMI_AUDIO_BITS, MIMI_AUDIO_CHANNELS, pcm_bytes);

    ESP_LOGI(TAG, "listening %d s (%u KB)...", seconds, (unsigned)(pcm_bytes / 1024));
    audio_play_tone(880, 150);                          /* audible go-cue right before the window opens */
    led_status_set(LED_STATE_LISTENING);                /* pulsing purple while the mic records */
    esp_err_t rec = audio_record_pcm(wav + WAV_HEADER_SIZE, pcm_bytes);
    if (rec != ESP_OK) {
        ESP_LOGE(TAG, "capture failed");
        led_status_set(LED_STATE_ERROR);
        free(wav);
        return false;
    }

    ESP_LOGI(TAG, "transcribing via %s ...", MIMI_WHISPER_URL);
    char text[256];
    bool ok = stt_transcribe(wav, total, text, sizeof(text));
    free(wav);

    if (!ok) {
        ESP_LOGW(TAG, "no transcript");
        led_status_set(LED_STATE_ERROR);
        return false;
    }
    led_status_set(LED_STATE_IDLE);
    ESP_LOGI(TAG, "heard: \"%s\"", text);
    if (out_text && out_len) snprintf(out_text, out_len, "%s", text);

    /* Inject as a "voice" inbound message; the agent loop answers and voice_out speaks it. */
    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_VOICE, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, MIMI_CHAN_VOICE, sizeof(msg.chat_id) - 1);
    msg.content = strdup(text);
    if (!msg.content) return ok;
    if (message_bus_push_inbound(&msg) != ESP_OK) {
        ESP_LOGW(TAG, "inbound queue full, dropping voice message");
        free(msg.content);
    }
    return ok;
}
