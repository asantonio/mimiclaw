#pragma once

/* Physical BOOT (GPIO0) button -> long-press latching toggle of the "Jarvis"
 * wake word (always-listening). Routes through wake_set_enabled(), the same
 * NVS-persisted, thread-safe path as the `set_wakeword on|off` CLI command, so
 * the button, the CLI and `wake_status` stay consistent and the choice survives
 * reboot. A short confirmation beep + a distinct LED indicator accompany each
 * toggle. Safe to call once at startup (after wake_init()); no-ops on failure. */
void button_init(void);
