#include "wav.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

size_t wav_write_header(uint8_t *buf, uint32_t sample_rate, uint16_t bits,
                        uint16_t channels, uint32_t data_bytes)
{
    uint16_t block_align = (uint16_t)(channels * (bits / 8));
    uint32_t byte_rate   = sample_rate * block_align;
    memcpy(buf, "RIFF", 4);
    wr32(buf + 4, 36 + data_bytes);          /* RIFF chunk size = 4 + (8+16) + (8+data) */
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    wr32(buf + 16, 16);                       /* PCM fmt chunk size */
    wr16(buf + 20, 1);                        /* audio format = PCM */
    wr16(buf + 22, channels);
    wr32(buf + 24, sample_rate);
    wr32(buf + 28, byte_rate);
    wr16(buf + 32, block_align);
    wr16(buf + 34, bits);
    memcpy(buf + 36, "data", 4);
    wr32(buf + 40, data_bytes);
    return WAV_HEADER_SIZE;
}

bool wav_parse_header(const uint8_t *b, size_t len, wav_info_t *out)
{
    if (!b || !out || len < 44) return false;
    if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return false;
    size_t i = 12;
    while (i + 8 <= len) {
        uint32_t sz = rd32(b + i + 4);
        if (memcmp(b + i, "fmt ", 4) == 0 && i + 8 + 16 <= len) {
            out->channels    = rd16(b + i + 8 + 2);
            out->sample_rate = rd32(b + i + 8 + 4);
            out->bits        = rd16(b + i + 8 + 14);
        } else if (memcmp(b + i, "data", 4) == 0) {
            out->data_offset = i + 8;
            return out->sample_rate != 0;
        }
        i += 8 + sz + (sz & 1);   /* chunks are word-aligned */
    }
    return false;
}
