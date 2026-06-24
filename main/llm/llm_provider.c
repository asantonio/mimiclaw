#include "llm_provider.h"
#include "mimi_config.h"
#include <string.h>
#include <stddef.h>

static const llm_provider_t k_providers[] = {
    { "anthropic", LLM_DIALECT_ANTHROPIC, LLM_AUTH_ANTHROPIC, MIMI_LLM_API_URL,    MIMI_NVS_KEY_MODEL_ANTHROPIC },
    { "openai",    LLM_DIALECT_OPENAI,    LLM_AUTH_BEARER,    MIMI_OPENAI_API_URL, MIMI_NVS_KEY_MODEL_OPENAI    },
    { "ollama",    LLM_DIALECT_OPENAI,    LLM_AUTH_NONE,      MIMI_OLLAMA_API_URL, MIMI_NVS_KEY_MODEL_OLLAMA    },
};

const llm_provider_t *llm_provider_lookup(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(k_providers) / sizeof(k_providers[0]); i++) {
        if (strcmp(name, k_providers[i].name) == 0) {
            return &k_providers[i];
        }
    }
    return NULL;
}
