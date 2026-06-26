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

/* ---- Capture / mic input (sub-project 2: ES7210 ADC on the shared I2S bus) ---- */

/* Bring up the ES7210 ADC + the RX side of the (full-duplex) I2S bus. Call after
 * audio_codec_init(). Idempotent. Returns ESP_OK on success; on failure capture is
 * unavailable but playback/text still work. */
esp_err_t audio_codec_record_init(void);

/* Read exactly `len` bytes of 16-bit mono PCM (at MIMI_AUDIO_SAMPLE_RATE) into `buf`.
 * Blocks until satisfied. Returns ESP_OK on success. */
esp_err_t audio_record_pcm(uint8_t *buf, size_t len);

/* Mic self-test: capture `ms` ms and log peak/RMS amplitude. The capture analogue of
 * audio_play_tone() — proves the I2S RX + ES7210 path before STT is layered on. */
void audio_record_test(int ms);

/* Diagnostic: probe the I2C bus and log every responding 7-bit address. Reveals the
 * ES7210 address alongside the known ES8311 (0x18) and TCA9555 (0x20). */
void audio_i2c_scan(void);
