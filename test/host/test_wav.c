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
    printf("all wav tests passed\n");
    return 0;
}
