#include "voice/wake.h"
#include "voice/audio_codec.h"
#include "voice/voice_in.h"
#include "mimi_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "model_path.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include <string.h>

static const char *TAG = "wake";

#define WAKE_DST_RATE  16000     /* WakeNet models are 16 kHz */
#define SRC_CHUNK      512       /* bus-rate samples per mic read (~23 ms @ 22050) */

/* run/park handshake with wake_mic_acquire() */
#define BIT_RUN     (1u << 0)
#define BIT_PARKED  (1u << 1)

static EventGroupHandle_t s_eg;
static TaskHandle_t       s_task;
static volatile int       s_mute;          /* >0: discard mic audio (TTS playing) */
static volatile bool      s_enabled = true;
static volatile uint32_t  s_detections;
static volatile uint32_t  s_last_ms;
static char               s_model_name[64];

/* --- incremental linear resampler: bus rate (22050) -> 16000, Q16 phase.
 * The bus stays at MIMI_AUDIO_SAMPLE_RATE for everything (TTS playback and the
 * whisper clip both want it); only WakeNet needs 16 k, and linear interpolation
 * is plenty for wake-word detection. This avoids reconfiguring the shared
 * full-duplex I2S clock + both codec devs around every utterance. --- */
static int32_t  s_rs_prev;
static uint32_t s_rs_frac;

static void resample_reset(void)
{
    s_rs_prev = 0;
    s_rs_frac = 1u << 16;        /* first iteration consumes a real source sample */
}

static size_t resample_16k(const int16_t *src, size_t nsrc, int16_t *dst)
{
    const uint32_t step = (uint32_t)(((uint64_t)MIMI_AUDIO_SAMPLE_RATE << 16) / WAKE_DST_RATE);
    size_t si = 0, di = 0;
    for (;;) {
        while (s_rs_frac >= (1u << 16)) {
            if (si >= nsrc) return di;
            s_rs_prev = src[si++];
            s_rs_frac -= (1u << 16);
        }
        if (si >= nsrc) return di;            /* need lookahead; state carries over */
        int32_t a = s_rs_prev, b = src[si];
        dst[di++] = (int16_t)(a + (((b - a) * (int32_t)s_rs_frac) >> 16));
        s_rs_frac += step;
    }
}

static void wake_task(void *arg)
{
    const esp_wn_iface_t *wn = esp_wn_handle_from_name(s_model_name);
    model_iface_data_t *md = wn ? wn->create(s_model_name, DET_MODE_90) : NULL;
    if (!md) {
        ESP_LOGE(TAG, "wakenet create failed for %s", s_model_name);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    int chunk = wn->get_samp_chunksize(md);
    ESP_LOGI(TAG, "WakeNet running: %s (chunk %d @ %d Hz, DET_MODE_90)",
             s_model_name, chunk, wn->get_samp_rate(md));

    int16_t *src = heap_caps_malloc(SRC_CHUNK * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    /* accumulator: one chunk + max resampler yield per src block (+margin) */
    int16_t *acc = heap_caps_malloc(((size_t)chunk + 384) * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!src || !acc) {
        ESP_LOGE(TAG, "buffer alloc failed");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    size_t fill = 0;
    resample_reset();

    for (;;) {
        if (!(xEventGroupGetBits(s_eg) & BIT_RUN) || !s_enabled) {
            xEventGroupSetBits(s_eg, BIT_PARKED);
            while (!(xEventGroupGetBits(s_eg) & BIT_RUN) || !s_enabled) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            xEventGroupClearBits(s_eg, BIT_PARKED);
            resample_reset();
            fill = 0;
        }

        if (audio_record_pcm((uint8_t *)src, SRC_CHUNK * sizeof(int16_t)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));    /* mic hiccup; retry gently */
            continue;
        }
        if (s_mute > 0) {                      /* speaker active: stay deaf, keep draining */
            resample_reset();
            fill = 0;
            continue;
        }

        fill += resample_16k(src, SRC_CHUNK, acc + fill);
        while (fill >= (size_t)chunk) {
            wakenet_state_t st = wn->detect(md, acc);
            memmove(acc, acc + chunk, (fill - chunk) * sizeof(int16_t));
            fill -= chunk;
            if (st == WAKENET_DETECTED) {
                s_detections++;
                s_last_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "\"Jarvis\" detected (#%lu) -> opening listen window",
                         (unsigned long)s_detections);
                /* NOTE: do NOT call wn->clean() — it LoadProhibited-crashes inside
                 * dl_convq_queue_bzero for this quantized wakenet9-lite (esp-sr's
                 * own code never calls it either). Resetting our accumulator +
                 * resampler is enough; the model just keeps streaming. */
                fill = 0;
                /* low half of the double-beep; voice_in_ask() plays the high half */
                audio_play_tone(660, 120);
                char heard[256] = {0};
                voice_in_ask(MIMI_VOICE_REC_SECONDS, heard, sizeof(heard));
                /* reply TTS is gated separately via wake_mute() from voice_out */
                resample_reset();
                break;
            }
        }
    }
}

void wake_init(void)
{
    if (s_task) return;                        /* idempotent */

    nvs_handle_t nvs;
    uint8_t en = 1;
    if (nvs_open(MIMI_NVS_WAKE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "enabled", &en);
        nvs_close(nvs);
    }
    s_enabled = (en != 0);

    srmodel_list_t *models = esp_srmodel_init("model");
    char *name = models ? esp_srmodel_filter(models, ESP_WN_PREFIX, NULL) : NULL;
    if (!name) {
        ESP_LOGW(TAG, "no wakenet model in the model partition — wake word unavailable");
        return;
    }
    strncpy(s_model_name, name, sizeof(s_model_name) - 1);

    if (audio_codec_record_init() != ESP_OK) {
        ESP_LOGW(TAG, "mic capture init failed — wake word unavailable");
        return;
    }

    s_eg = xEventGroupCreate();
    xEventGroupSetBits(s_eg, BIT_RUN);
    /* Dedicated task (never a timer callback — see the 2026-06-30 Tmr Svc
     * stack-overflow postmortem). Generous stack: the detection handler runs
     * the full record->STT HTTP path. APP core, modest priority. */
    xTaskCreatePinnedToCore(wake_task, "wake", 12288, NULL, 5, &s_task, 1);
    ESP_LOGI(TAG, "wake word armed: say \"Jarvis\"%s",
             s_enabled ? "" : " (currently disabled — set_wakeword on)");
}

bool wake_available(void) { return s_task != NULL; }

void wake_set_enabled(bool on)
{
    s_enabled = on;
    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_WAKE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "enabled", on ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

bool wake_get_enabled(void) { return s_enabled; }

void wake_mute(bool on)
{
    if (on) {
        __atomic_add_fetch(&s_mute, 1, __ATOMIC_SEQ_CST);
    } else {
        __atomic_sub_fetch(&s_mute, 1, __ATOMIC_SEQ_CST);
    }
}

bool wake_mic_acquire(uint32_t timeout_ms)
{
    if (!s_task || xTaskGetCurrentTaskHandle() == s_task) return true;
    xEventGroupClearBits(s_eg, BIT_RUN);
    EventBits_t b = xEventGroupWaitBits(s_eg, BIT_PARKED, pdFALSE, pdTRUE,
                                        pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_PARKED) != 0;
}

void wake_mic_release(void)
{
    if (!s_task || xTaskGetCurrentTaskHandle() == s_task) return;
    xEventGroupSetBits(s_eg, BIT_RUN);
}

void wake_stats(uint32_t *detections, uint32_t *last_uptime_ms, const char **model_name)
{
    if (detections) *detections = s_detections;
    if (last_uptime_ms) *last_uptime_ms = s_last_ms;
    if (model_name) *model_name = s_model_name[0] ? s_model_name : NULL;
}
