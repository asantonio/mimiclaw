#include "control/button.h"
#include "voice/wake.h"
#include "voice/audio_codec.h"
#include "led/led_status.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "button";

/* On-board BOOT button = GPIO0. It is free at runtime: the board function keys
 * (K1-K3) are wired to the TCA9555 I2C expander (P1.1-P1.3), NOT to a GPIO, and
 * nothing else in the app reads GPIO0. BOOT/GPIO0 is only sampled at reset
 * (held -> download mode), so using it as a runtime input does not interfere
 * with USB-JTAG `app-flash`. Active-LOW: idle HIGH (pull-up), pressed -> GND. */
#define BUTTON_GPIO        GPIO_NUM_0
#define BUTTON_POLL_MS     20      /* poll cadence; also debounces (bounce resets count) */
#define BUTTON_HOLD_MS     1000    /* long-press threshold to toggle (avoids stray taps) */
#define BUTTON_HOLD_TICKS  (BUTTON_HOLD_MS / BUTTON_POLL_MS)

/* Latching long-press: fires ONCE when the hold reaches the threshold, then
 * waits for release before it can fire again. A short tap never toggles. */
static void button_task(void *arg)
{
    int  held  = 0;        /* consecutive pressed (LOW) samples */
    bool fired = false;    /* this hold already toggled         */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        if (gpio_get_level(BUTTON_GPIO) == 0) {            /* pressed */
            if (held < BUTTON_HOLD_TICKS) held++;
            if (held >= BUTTON_HOLD_TICKS && !fired) {
                fired = true;
                /* SAME code path as `set_wakeword on|off`: thread-safe flag +
                 * NVS write, arbitrated with ask/mic_test via the wake tasks
                 * own park handshake. We only flip the published state. */
                bool on = !wake_get_enabled();
                wake_set_enabled(on);
                ESP_LOGI(TAG, "BOOT long-press: wake word %s (persisted)",
                         on ? "ENABLED" : "DISABLED");
                if (on) {
                    audio_play_tone(880, 150);       /* high beep  = listening ON  */
                    led_status_set(LED_STATE_IDLE);  /* resting ring -> faint white */
                } else {
                    audio_play_tone(330, 220);       /* low beep   = listening OFF */
                    led_status_set(LED_STATE_IDLE);  /* IDLE render shows dim RED
                                                      * while wake is disabled     */
                }
            }
        } else {                                          /* released / bounce */
            held  = 0;
            fired = false;
        }
    }
}

void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d config failed; button wake-toggle unavailable", BUTTON_GPIO);
        return;
    }
    xTaskCreate(button_task, "button", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "BOOT-button wake toggle ready (long-press %d ms on GPIO%d)",
             BUTTON_HOLD_MS, BUTTON_GPIO);
}
