#include "../../main/voice/wav.h"
#include <assert.h>
#include <stdio.h>

/* Canonical 44-byte WAV header (16kHz mono 16-bit PCM) + 4 data bytes. */
static const unsigned char WAV[] = {
  'R','I','F','F', 0x24,0,0,0, 'W','A','V','E',
  'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x80,0x3e,0,0, 0,0x7d,0,0, 2,0, 16,0,
  'd','a','t','a', 4,0,0,0, 0x11,0x22,0x33,0x44 };

int main(void) {
    wav_info_t w;
    assert(wav_parse_header(WAV, sizeof(WAV), &w) == true);
    assert(w.sample_rate == 16000);
    assert(w.bits == 16);
    assert(w.channels == 1);
    assert(w.data_offset == 44);
    /* reject garbage / truncated */
    assert(wav_parse_header((const unsigned char *)"nope", 4, &w) == false);
    assert(wav_parse_header(WAV, 8, &w) == false);
    assert(wav_parse_header(0, 100, &w) == false);

    /* writer round-trips through the parser (puck capture format: 22050 mono 16-bit) */
    unsigned char hdr[64];
    assert(wav_write_header(hdr, 22050, 16, 1, 220500) == WAV_HEADER_SIZE);
    wav_info_t r;
    assert(wav_parse_header(hdr, WAV_HEADER_SIZE, &r) == true);
    assert(r.sample_rate == 22050 && r.bits == 16 && r.channels == 1 && r.data_offset == 44);

    printf("all wav tests passed\n");
    return 0;
}
