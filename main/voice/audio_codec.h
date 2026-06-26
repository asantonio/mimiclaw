#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* Must match the piper service output rate. en_US-lessac-medium is 22050 Hz. */
#define MIMI_AUDIO_SAMPLE_RATE   22050
#define MIMI_AUDIO_BITS          16
#define MIMI_AUDIO_CHANNELS       1

/* Bring up I2C + I2S + ES8311 (DAC/output). Returns ESP_OK on success.
 * On failure the caller should continue text-only (voice disabled). */
esp_err_t audio_codec_init(void);

/* Write 16-bit mono PCM at MIMI_AUDIO_SAMPLE_RATE to the codec. Blocks until written. */
esp_err_t audio_play_pcm(const uint8_t *data, size_t len);

/* Output volume, 0..100. */
void audio_play_volume(int vol);

/* Self-test: synthesize + play a sine tone (proves I2S + codec + amp path). */
void audio_play_tone(int freq_hz, int ms);
