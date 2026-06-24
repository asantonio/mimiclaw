#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stddef.h>
#include <stdbool.h>

#include "mimi_config.h"
#include "llm_provider.h"

/**
 * Initialize the LLM proxy. Reads API key and model from build-time secrets, then NVS.
 */
esp_err_t llm_proxy_init(void);

/**
 * Save the LLM API key to NVS.
 */
esp_err_t llm_set_api_key(const char *api_key);

/**
 * Save the LLM provider to NVS. (e.g. "anthropic", "openai")
 */
esp_err_t llm_set_provider(const char *provider);

/**
 * Save the model identifier to NVS (writes the active provider's per-brain key + legacy mirror).
 */
esp_err_t llm_set_model(const char *model);

/**
 * Override the Ollama base URL, persisted to NVS key MIMI_NVS_KEY_OLLAMA_URL.
 * Takes effect immediately (updates the in-memory static used by llm_api_url()).
 */
esp_err_t llm_set_ollama_url(const char *url);

/**
 * Return the currently active provider name (e.g. "anthropic", "openai", "ollama").
 */
const char *llm_get_provider(void);

/**
 * Return the currently active model string.
 */
const char *llm_get_model(void);

/**
 * Return the currently active provider descriptor (never NULL; falls back to anthropic).
 */
const llm_provider_t *llm_active_provider(void);

/**
 * Return true if an API key has been set (non-empty).
 * Ollama (auth NONE) does not need a key, but callers can use this to warn when
 * switching to a provider that does.
 */
bool llm_has_api_key(void);

/* ── Tool Use Support ──────────────────────────────────────────── */

typedef struct {
    char id[64];        /* "toolu_xxx" */
    char name[32];      /* "web_search" */
    char *input;        /* heap-allocated JSON string */
    size_t input_len;
} llm_tool_call_t;

typedef struct {
    char *text;                                  /* accumulated text blocks */
    size_t text_len;
    llm_tool_call_t calls[MIMI_MAX_TOOL_CALLS];
    int call_count;
    bool tool_use;                               /* stop_reason == "tool_use" */
} llm_response_t;

void llm_response_free(llm_response_t *resp);

/**
 * Send a chat completion request with tools to the configured LLM API (non-streaming).
 *
 * @param system_prompt  System prompt string
 * @param messages       cJSON array of messages (caller owns)
 * @param tools_json     Pre-built JSON string of tools array, or NULL for no tools
 * @param resp           Output: structured response with text and tool calls
 * @return ESP_OK on success
 */
esp_err_t llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp);
