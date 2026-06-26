#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Voice input (sub-project 2): capture mic audio -> WAV -> faster-whisper STT.
 * The transcript is injected into the agent loop on the "voice" channel, so the
 * spoken reply comes back through the existing voice_out (Piper -> ES8311) path. */

/* Capture `seconds` of mic audio, transcribe it via the Whisper service, and (if the
 * transcript is non-empty) push it onto the inbound bus as a "voice" message. Returns
 * true if a transcript was produced. `out_text` (optional) receives the transcript. */
bool voice_in_ask(int seconds, char *out_text, size_t out_len);
