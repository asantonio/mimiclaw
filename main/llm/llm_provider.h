#pragma once
#include <stdbool.h>

/* Wire format of the request/response JSON (NOT the endpoint). */
typedef enum { LLM_DIALECT_ANTHROPIC, LLM_DIALECT_OPENAI } llm_dialect_t;

/* Auth header style for the request. */
typedef enum { LLM_AUTH_ANTHROPIC, LLM_AUTH_BEARER, LLM_AUTH_NONE } llm_auth_t;

typedef struct {
    const char   *name;          /* NVS provider value: "anthropic"|"openai"|"ollama" */
    llm_dialect_t dialect;       /* request/response shape */
    llm_auth_t    auth;          /* auth header style */
    const char   *default_url;   /* full URL incl. scheme/host/port/path */
    const char   *model_nvs_key; /* per-brain model NVS key */
    const char   *default_model; /* fallback model when this brain's NVS key is unset */
} llm_provider_t;

/* Look up a provider by name; returns NULL if unknown. */
const llm_provider_t *llm_provider_lookup(const char *name);
