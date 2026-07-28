#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Wake-word detector (sub-project #3, "Jarvis"). Always-listening WakeNet task
 * feeding on the ES7210 mic path; wake-when-quiet v1 — deaf during TTS playback.
 *
 * Concurrency contract:
 *  - The detector owns the mic between detections. Anything else that reads the
 *    mic (ask, mic_test) must bracket with wake_mic_acquire()/wake_mic_release().
 *    Both are no-ops when called from the wake task itself or when no detector runs.
 *  - TTS playback brackets with wake_mute(true/false) — detector keeps draining
 *    the mic but discards audio, so it cannot hear the speaker (no AEC needed). */

/* Start the detector task (needs codec init done; loads the wakenet model from
 * the `model` partition). Logs + returns silently if no model is present. */
void wake_init(void);

bool wake_available(void);                 /* detector task exists              */
void wake_set_enabled(bool on);            /* runtime toggle, persisted in NVS  */
bool wake_get_enabled(void);

void wake_mute(bool on);                   /* deafen while the speaker plays    */
bool wake_mic_acquire(uint32_t timeout_ms);/* park detector; grants mic access  */
void wake_mic_release(void);

void wake_stats(uint32_t *detections, uint32_t *last_uptime_ms, const char **model_name);
