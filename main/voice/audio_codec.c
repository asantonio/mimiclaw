#include "voice/audio_codec.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "es7210_adc.h"
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
#define PIN_I2S_DIN   GPIO_NUM_15   /* mic in — ES7210 ADC (sub-project 2: capture) */
#define PIN_I2S_DOUT  GPIO_NUM_16

/* ES7210 mic-ADC I2C address (7-bit). esp_codec_dev's ES7210_CODEC_DEFAULT_ADDR is the
 * 8-bit form (0x80 -> 0x40). Confirm against `i2c_scan` if capture init ever fails. */
#define ES7210_ADDR   ES7210_CODEC_DEFAULT_ADDR

/* The speaker amp is gated by the TCA9555 I2C GPIO expander, pin EXIO8 (P1.0), active HIGH.
 * (From the Waveshare schematic PA_CTRL + the Arduino demo Audio_PA_EN -> Set_EXIO(EXIO8, true).) */
#define TCA9555_ADDR         0x20
#define TCA9555_REG_OUTPUT1  0x03   /* output port 1 */
#define TCA9555_REG_CONFIG1  0x07   /* direction port 1: bit=0 -> output */

static const char *TAG = "audio";
static i2c_master_bus_handle_t s_i2c;
static i2s_chan_handle_t       s_tx;
static i2s_chan_handle_t       s_rx;     /* RX side of the full-duplex bus (mic capture) */
static esp_codec_dev_handle_t  s_play;
static esp_codec_dev_handle_t  s_rec;    /* ES7210 capture device */

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

/* Enable the speaker amplifier via TCA9555 EXIO8 (P1.0) -> output, HIGH.
 * Read-modify-write so the expander's other pins (buttons on P1.1-1.3) are untouched. */
static esp_err_t tca9555_enable_amp(void)
{
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9555_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dc, &dev), TAG, "tca add");
    uint8_t reg, val;
    /* config port1: clear bit0 so IO8 is an output */
    reg = TCA9555_REG_CONFIG1;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(dev, &reg, 1, &val, 1, 1000), TAG, "tca rd cfg");
    uint8_t cfg[2] = { TCA9555_REG_CONFIG1, (uint8_t)(val & ~0x01u) };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, cfg, 2, 1000), TAG, "tca wr cfg");
    /* output port1: set bit0 -> IO8 HIGH -> amp enabled */
    reg = TCA9555_REG_OUTPUT1;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(dev, &reg, 1, &val, 1, 1000), TAG, "tca rd out");
    uint8_t out[2] = { TCA9555_REG_OUTPUT1, (uint8_t)(val | 0x01u) };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, out, 2, 1000), TAG, "tca wr out");
    i2c_master_bus_rm_device(dev);
    ESP_LOGI(TAG, "speaker amp enabled (TCA9555 EXIO8 high)");
    return ESP_OK;
}

static esp_err_t i2s_setup(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear_after_cb = true;   /* emit zeros (silence) when no data, else the DMA loops the last buffer */
    /* Allocate both directions up front (full-duplex on one port). The RX channel is left
     * uninitialised/disabled until audio_codec_record_init() — TX behaviour is unchanged. */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "i2s_new");
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
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
    if (tca9555_enable_amp() != ESP_OK) ESP_LOGW(TAG, "amp enable (TCA9555) failed; speaker may stay silent");
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

    esp_codec_dev_set_out_vol(s_play, 80);
    /* Open 32-bit stereo (matches the Waveshare demo's known-good config; the ES8311 in
     * SCLK-clocked mode needs the wider 64*LRCK BCLK that 32-bit slots provide).
     * audio_play_pcm() converts the 16-bit mono source -> 32-bit stereo. */
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 32,
        .channel = 2,
        .sample_rate = MIMI_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_play, &fs) != 0) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES8311 ready (%d Hz, %d-bit, mono)", MIMI_AUDIO_SAMPLE_RATE, MIMI_AUDIO_BITS);
    return ESP_OK;
}

/* Source is 16-bit mono; the codec is opened 32-bit stereo. Convert on the fly: each
 * int16 sample s -> 32-bit (s<<16) duplicated to L+R. A 1-byte carry handles odd-length
 * chunks split across calls (e.g. a streamed HTTP body). */
esp_err_t audio_play_pcm(const uint8_t *data, size_t len)
{
    if (!s_play) return ESP_ERR_INVALID_STATE;
    static uint8_t carry;
    static bool has_carry;
    static bool logged_rc;
    int32_t out[128];          /* 64 stereo frames per flush */
    size_t oi = 0, i = 0;
    int rc = 0;
    while (i < len) {
        int16_t s;
        if (has_carry) { s = (int16_t)(carry | (data[i] << 8)); i += 1; has_carry = false; }
        else if (i + 1 < len) { s = (int16_t)(data[i] | (data[i + 1] << 8)); i += 2; }
        else { carry = data[i]; has_carry = true; break; }
        int32_t v = (int32_t)s << 16;
        out[oi++] = v;     /* left  */
        out[oi++] = v;     /* right */
        if (oi >= 128) { rc = esp_codec_dev_write(s_play, out, (int)(oi * sizeof(int32_t))); oi = 0; }
    }
    if (oi) rc = esp_codec_dev_write(s_play, out, (int)(oi * sizeof(int32_t)));
    if (!logged_rc) { logged_rc = true; ESP_LOGW(TAG, "first codec write rc=%d (0=OK)", rc); }
    return ESP_OK;
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
            buf[i] = (int16_t)(16000.0 * sin(ph));
            ph += step;
        }
        audio_play_pcm((uint8_t *)buf, (size_t)n * sizeof(int16_t));
        done += n;
    }
}

/* ---- Capture / mic input (ES7210 ADC, sub-project 2) ---- */

esp_err_t audio_codec_record_init(void)
{
    if (s_rec) return ESP_OK;            /* idempotent */
    if (!s_rx) { ESP_LOGE(TAG, "rx channel not allocated (call audio_codec_init first)"); return ESP_ERR_INVALID_STATE; }

    /* Init + enable the RX side. Same clocks/slots as TX so the shared full-duplex BCLK/WS
     * stays consistent (32-bit stereo @ bus rate; ES8311 TX already runs this). */
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIMI_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN,
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &sc), TAG, "i2s rx std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx en");

    /* ES7210 ADC on the shared I2C bus, two onboard MEMS mics, ESP as I2S master. */
    audio_codec_i2s_cfg_t rx_i2s = { .port = I2S_NUM_0, .tx_handle = NULL, .rx_handle = s_rx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&rx_i2s);
    audio_codec_i2c_cfg_t i2c_cfg = { .addr = ES7210_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es7210_codec_cfg_t es_cfg = {
        .ctrl_if      = ctrl_if,
        .master_mode  = false,                                  /* ESP32 is master (ES8311 drives clocks) */
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,      /* the two onboard mics */
        .mclk_src     = ES7210_MCLK_FROM_PAD,
        .mclk_div     = 256,
    };
    const audio_codec_if_t *codec_if = es7210_codec_new(&es_cfg);
    if (!data_if || !ctrl_if || !codec_if) {
        ESP_LOGE(TAG, "es7210 interface alloc failed (addr 0x%02X — run i2c_scan)", ES7210_ADDR);
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_rec = esp_codec_dev_new(&dev_cfg);
    if (!s_rec) { ESP_LOGE(TAG, "esp_codec_dev_new (rec) failed"); return ESP_FAIL; }

    esp_codec_dev_set_in_gain(s_rec, 36.0f);   /* 30 dB captured couch-distance speech at ~-36 dBFS -> garbled STT */
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 32, .channel = 2, .sample_rate = MIMI_AUDIO_SAMPLE_RATE };
    if (esp_codec_dev_open(s_rec, &fs) != 0) {
        ESP_LOGE(TAG, "esp_codec_dev_open (rec) failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ES7210 ready (%d Hz capture, 2 mics, gain 36 dB)", MIMI_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

/* The codec is opened 32-bit stereo (mic1=L, mic2=R); downconvert to 16-bit mono (left mic,
 * top 16 bits) to match MIMI_AUDIO_BITS for the WAV/STT path. */
esp_err_t audio_record_pcm(uint8_t *buf, size_t len)
{
    if (!s_rec) return ESP_ERR_INVALID_STATE;
    size_t want = len / sizeof(int16_t);      /* int16 mono samples requested */
    int16_t *out = (int16_t *)buf;
    int32_t in[128];                          /* 64 stereo frames per read */
    size_t done = 0;
    while (done < want) {
        size_t batch = want - done;
        if (batch > 64) batch = 64;
        int rc = esp_codec_dev_read(s_rec, in, (int)(batch * 2 * sizeof(int32_t)));
        if (rc != 0) { ESP_LOGW(TAG, "codec read rc=%d", rc); return ESP_FAIL; }
        for (size_t i = 0; i < batch; i++) {
            out[done + i] = (int16_t)(in[i * 2] >> 16);   /* left mic, high 16 bits */
        }
        done += batch;
    }
    return ESP_OK;
}

void audio_record_test(int ms)
{
    if (audio_codec_record_init() != ESP_OK) { ESP_LOGE(TAG, "mic_test: capture init failed"); return; }
    int total = MIMI_AUDIO_SAMPLE_RATE * ms / 1000;
    int16_t chunk[512];
    int32_t peak = 0;
    double sumsq = 0.0;
    int done = 0;
    while (done < total) {
        int n = (total - done > 512) ? 512 : (total - done);
        if (audio_record_pcm((uint8_t *)chunk, (size_t)n * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGE(TAG, "mic_test: read failed at %d/%d", done, total);
            return;
        }
        for (int i = 0; i < n; i++) {
            int32_t a = chunk[i] < 0 ? -(int32_t)chunk[i] : chunk[i];
            if (a > peak) peak = a;
            sumsq += (double)chunk[i] * (double)chunk[i];
        }
        done += n;
    }
    int rms = (total > 0) ? (int)sqrt(sumsq / total) : 0;
    ESP_LOGI(TAG, "mic_test: %d ms / %d samples @ %d Hz -> peak=%ld rms=%d "
                  "(near-0 = silence; speak and expect hundreds–thousands)",
             ms, total, MIMI_AUDIO_SAMPLE_RATE, (long)peak, rms);
}

void audio_i2c_scan(void)
{
    if (!s_i2c) { ESP_LOGE(TAG, "i2c bus not initialised (call audio_codec_init first)"); return; }
    ESP_LOGI(TAG, "I2C scan (SDA=%d SCL=%d):", PIN_I2C_SDA, PIN_I2C_SCL);
    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_i2c, a, 50) == ESP_OK) {
            const char *known =
                (a == 0x18) ? " (ES8311 codec)" :
                (a == 0x20) ? " (TCA9555 expander)" :
                (a >= 0x40 && a <= 0x43) ? " (ES7210 mic-ADC?)" : "";
            ESP_LOGI(TAG, "  0x%02X%s", a, known);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done: %d device(s)", found);
}
