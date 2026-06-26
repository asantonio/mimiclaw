#pragma once
#include <stdbool.h>

/* Init the codec + the playback task. Safe to call once at startup.
 * If the codec fails to init, voice is disabled and the agent continues text-only. */
void voice_out_init(void);

/* Enqueue text to be spoken via piper -> ES8311. Non-blocking; no-op if disabled. */
void voice_speak(const char *text);

void voice_out_set_enabled(bool on);
bool voice_out_enabled(void);
