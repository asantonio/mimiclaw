#include "led_status.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED_GPIO          38      /* WS2812 data line (validated under MicroPython) */
#define LED_COUNT          7      /* ring-mounted addressable LEDs                  */
#define LED_TICK_MS        40      /* render cadence (~25 fps)                       */
#define REPLY_HOLD_MS     900      /* green hold before reverting to idle           */
#define ERROR_HOLD_MS    2500      /* red hold before reverting to idle             */

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static volatile led_state_t s_state = LED_STATE_IDLE;
static volatile TickType_t  s_state_tick;

/* Brightness is kept low on purpose — WS2812 at full scale is blinding indoors. */
static void fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}

static void led_task(void *arg)
{
    int phase = 0;  /* 0..59, drives the thinking pulse */
    for (;;) {
        led_state_t st = s_state;
        uint32_t elapsed_ms = (xTaskGetTickCount() - s_state_tick) * portTICK_PERIOD_MS;

        switch (st) {
        case LED_STATE_LISTENING: {
            /* triangle pulse in purple — distinct from THINKING's blue, signals
             * the mic is recording (STT capture window). */
            phase = (phase + 1) % 60;
            int tri = (phase < 30) ? phase : (60 - phase);  /* 0..30..0 */
            int lvl = 8 + (tri * 62 / 30);                  /* ~8..70   */
            fill((uint8_t)lvl, 0, (uint8_t)lvl);            /* purple   */
            break;
        }
        case LED_STATE_THINKING: {
            /* triangle pulse: brightness ramps up then down over ~2.4 s */
            phase = (phase + 1) % 60;
            int tri = (phase < 30) ? phase : (60 - phase);  /* 0..30..0 */
            int lvl = 8 + (tri * 62 / 30);                  /* ~8..70   */
            fill(0, 0, (uint8_t)lvl);                       /* blue     */
            break;
        }
        case LED_STATE_TOOL:
            fill(48, 22, 0);   /* amber */
            break;
        case LED_STATE_REPLY:
            if (elapsed_ms > REPLY_HOLD_MS) {
                s_state = LED_STATE_IDLE;
            } else {
                fill(0, 52, 8); /* green */
            }
            break;
        case LED_STATE_ERROR:
            if (elapsed_ms > ERROR_HOLD_MS) {
                s_state = LED_STATE_IDLE;
            } else {
                fill(60, 0, 0); /* red */
            }
            break;
        case LED_STATE_SPEAKING:
            fill(0, 40, 40);   /* teal */
            break;
        case LED_STATE_IDLE:
        default:
            fill(2, 2, 2);     /* faint white = alive + idle */
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

void led_status_set(led_state_t state)
{
    s_state = state;
    s_state_tick = xTaskGetTickCount();
}

void led_status_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    s_state = LED_STATE_IDLE;
    s_state_tick = xTaskGetTickCount();
    xTaskCreate(led_task, "led_status", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "LED status ring ready (GPIO%d x%d)", LED_GPIO, LED_COUNT);
}
