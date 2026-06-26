#include "wav.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

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
