#include "voice/audio_codec.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_log.h"
#include <math.h>

/* Pins from Waveshare factory_01/bsp_board.h (ESP32-S3-AUDIO-Board). */
#define I2C_PORT      I2C_NUM_0
#define PIN_I2C_SCL   GPIO_NUM_10
#define PIN_I2C_SDA   GPIO_NUM_11
#define PIN_I2S_MCLK  GPIO_NUM_12
#define PIN_I2S_BCLK  GPIO_NUM_13
#define PIN_I2S_WS    GPIO_NUM_14
#define PIN_I2S_DIN   GPIO_NUM_15   /* mic in — unused for output (sub-project 2) */
#define PIN_I2S_DOUT  GPIO_NUM_16

static const char *TAG = "audio";
static i2c_master_bus_handle_t s_i2c;
static i2s_chan_handle_t       s_tx;
static esp_codec_dev_handle_t  s_play;

static esp_err_t i2c_setup(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

static esp_err_t i2s_setup(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, NULL), TAG, "i2s_new");
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN,
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   /* ES8311 mclk_div default = 256 */
    /* (esp_codec_dev_open reconfigs the slot to BOTH from sample_info, so no manual slot_mask.) */
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &sc), TAG, "i2s_std");
    return i2s_channel_enable(s_tx);
}

esp_err_t audio_codec_init(void)
{
    if (s_play) return ESP_OK;   /* idempotent */
    ESP_RETURN_ON_ERROR(i2c_setup(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(i2s_setup(), TAG, "i2s");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = s_tx, .rx_handle = NULL };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = { .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,            /* no separate amp-enable line (GPIO_PWR_CTRL = -1) */
        .use_mclk = false,       /* matches Waveshare demo — ES8311 clocks off SCLK, not MCLK */
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!data_if || !ctrl_if || !codec_if) {
        ESP_LOGE(TAG, "codec interface alloc failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_play = esp_codec_dev_new(&dev_cfg);
    if (!s_play) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    esp_codec_dev_set_out_vol(s_play, 70);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = MIMI_AUDIO_BITS,
        .channel = MIMI_AUDIO_CHANNELS,
        .sample_rate = MIMI_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_play, &fs) != 0) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES8311 ready (%d Hz, %d-bit, mono)", MIMI_AUDIO_SAMPLE_RATE, MIMI_AUDIO_BITS);
    return ESP_OK;
}

esp_err_t audio_play_pcm(const uint8_t *data, size_t len)
{
    if (!s_play) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_write(s_play, (void *)data, (int)len) == 0 ? ESP_OK : ESP_FAIL;
}

void audio_play_volume(int vol)
{
    if (s_play) esp_codec_dev_set_out_vol(s_play, vol);
}

void audio_play_tone(int freq_hz, int ms)
{
    if (!s_play) return;
    const int sr = MIMI_AUDIO_SAMPLE_RATE;
    int total = sr * ms / 1000;
    int16_t buf[256];
    double ph = 0.0, step = 2.0 * M_PI * freq_hz / sr;
    int done = 0;
    while (done < total) {
        int n = (total - done > 256) ? 256 : (total - done);
        for (int i = 0; i < n; i++) {
            buf[i] = (int16_t)(8000.0 * sin(ph));
            ph += step;
        }
        audio_play_pcm((uint8_t *)buf, (size_t)n * sizeof(int16_t));
        done += n;
    }
}
