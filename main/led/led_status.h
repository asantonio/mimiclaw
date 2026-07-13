#pragma once

/* Agent activity states shown on the WS2812 status ring. */
typedef enum {
    LED_STATE_IDLE,     /* waiting for a message  -> dim white      */
    LED_STATE_LISTENING,/* recording mic (STT)    -> pulsing purple  */
    LED_STATE_THINKING, /* LLM call in progress   -> pulsing blue   */
    LED_STATE_TOOL,     /* executing a tool       -> amber          */
    LED_STATE_REPLY,    /* reply sent             -> brief green     */
    LED_STATE_ERROR,    /* LLM/HTTP failure       -> brief red       */
    LED_STATE_SPEAKING, /* speaking a reply (TTS) -> teal            */
} led_state_t;

/* Initialise the ring (GPIO 38, 7x WS2812) and start the render task.
 * Safe to call once at startup; logs and no-ops on driver failure. */
void led_status_init(void);

/* Fire-and-forget: set the current activity state. Non-blocking.
 * REPLY and ERROR auto-revert to IDLE after a short hold. */
void led_status_set(led_state_t state);
