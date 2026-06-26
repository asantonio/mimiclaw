#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t bits;
    uint16_t channels;
    size_t   data_offset;   /* byte offset of PCM data within the buffer */
} wav_info_t;

/* Parse a canonical PCM WAV header. Returns false on non-WAV / truncated / no data chunk.
 * libc-only (host-testable). */
bool wav_parse_header(const uint8_t *buf, size_t len, wav_info_t *out);

/* Canonical 44-byte PCM WAV header size. */
#define WAV_HEADER_SIZE 44

/* Write a canonical 44-byte PCM WAV header into `buf` (must hold >= WAV_HEADER_SIZE bytes)
 * for the given format and PCM payload size. Returns WAV_HEADER_SIZE. libc-only
 * (host-testable), and round-trips through wav_parse_header. */
size_t wav_write_header(uint8_t *buf, uint32_t sample_rate, uint16_t bits,
                        uint16_t channels, uint32_t data_bytes);
