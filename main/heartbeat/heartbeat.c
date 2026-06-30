#include "heartbeat/heartbeat.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "heartbeat";

#define HEARTBEAT_PROMPT \
    "Read " MIMI_HEARTBEAT_FILE " and follow any instructions or tasks listed there. " \
    "If nothing needs attention, reply with just: HEARTBEAT_OK"

/*
 * Heartbeat runs on its OWN FreeRTOS task — NOT a software-timer callback.
 * The timer-daemon task ("Tmr Svc") has a small shared stack (ESP-IDF default
 * 2048 B) and must never do file I/O; heartbeat_has_tasks() doing fopen/fgets
 * there overflowed it and crash-rebooted the board every 30 min. A dedicated
 * task gives this work a private, adequately sized stack and matches how
 * cron/agent/telegram already run.
 *
 * Stack sized generously (6 KiB) on purpose: newlib stdio + the SPIFFS/VFS read
 * path are stack-hungry, internal RAM is plentiful (~250 KiB free), and this
 * board is painful to reflash (remote + physical BOOT/RESET buttons), so we err
 * toward "definitely won't overflow." Trim later against uxTaskGetStackHighWaterMark().
 */
#define HEARTBEAT_TASK_STACK   6144    /* bytes */
#define HEARTBEAT_TASK_PRIO    4

static TaskHandle_t s_heartbeat_task = NULL;

/* ── Content check ────────────────────────────────────────────── */

/**
 * Check if HEARTBEAT.md has actionable content.
 * Returns true if any line is NOT:
 *   - empty / whitespace-only
 *   - a markdown header (starts with #)
 *   - a completed checkbox (- [x] or * [x])
 */
static bool heartbeat_has_tasks(void)
{
    FILE *f = fopen(MIMI_HEARTBEAT_FILE, "r");
    if (!f) {
        return false;
    }

    char line[256];
    bool found_task = false;

    while (fgets(line, sizeof(line), f)) {
        /* Skip leading whitespace */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        /* Skip empty lines */
        if (*p == '\0') {
            continue;
        }

        /* Skip markdown headers */
        if (*p == '#') {
            continue;
        }

        /* Skip completed checkboxes: "- [x]" or "* [x]" */
        if ((*p == '-' || *p == '*') && *(p + 1) == ' ' && *(p + 2) == '[') {
            char mark = *(p + 3);
            if ((mark == 'x' || mark == 'X') && *(p + 4) == ']') {
                continue;
            }
        }

        /* Found an actionable line */
        found_task = true;
        break;
    }

    fclose(f);
    return found_task;
}

/* ── Send heartbeat to agent ──────────────────────────────────── */

static bool heartbeat_send(void)
{
    if (!heartbeat_has_tasks()) {
        ESP_LOGD(TAG, "No actionable tasks in HEARTBEAT.md");
        return false;
    }

    mimi_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "heartbeat", sizeof(msg.chat_id) - 1);
    msg.content = strdup(HEARTBEAT_PROMPT);

    if (!msg.content) {
        ESP_LOGE(TAG, "Failed to allocate heartbeat prompt");
        return false;
    }

    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to push heartbeat message: %s", esp_err_to_name(err));
        free(msg.content);
        return false;
    }

    ESP_LOGI(TAG, "Triggered agent check");
    return true;
}

/* ── Heartbeat task ───────────────────────────────────────────── */

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MIMI_HEARTBEAT_INTERVAL_MS));
        heartbeat_send();   /* runs on THIS task's stack, not the timer daemon's */
    }
}

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t heartbeat_init(void)
{
    ESP_LOGI(TAG, "Heartbeat service initialized (file: %s, interval: %ds)",
             MIMI_HEARTBEAT_FILE, MIMI_HEARTBEAT_INTERVAL_MS / 1000);
    return ESP_OK;
}

esp_err_t heartbeat_start(void)
{
    if (s_heartbeat_task) {
        ESP_LOGW(TAG, "Heartbeat task already running");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(
        heartbeat_task,
        "heartbeat",
        HEARTBEAT_TASK_STACK,
        NULL,
        HEARTBEAT_TASK_PRIO,
        &s_heartbeat_task
    );

    if (ok != pdPASS) {
        s_heartbeat_task = NULL;
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Heartbeat started (every %d min)", MIMI_HEARTBEAT_INTERVAL_MS / 60000);
    return ESP_OK;
}

void heartbeat_stop(void)
{
    if (s_heartbeat_task) {
        vTaskDelete(s_heartbeat_task);
        s_heartbeat_task = NULL;
        ESP_LOGI(TAG, "Heartbeat stopped");
    }
}

bool heartbeat_trigger(void)
{
    return heartbeat_send();
}
