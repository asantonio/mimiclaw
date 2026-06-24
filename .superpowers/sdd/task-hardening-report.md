# Hardening: Per-Provider Default Models

## Changes (4 files)

### 1. `main/mimi_config.h`
Added three default-model macros after the API URL block:
- `MIMI_DEFAULT_MODEL_ANTHROPIC "claude-sonnet-4-6"`
- `MIMI_DEFAULT_MODEL_OPENAI    "gpt-4o"`
- `MIMI_DEFAULT_MODEL_OLLAMA    "qwen3:30b-a3b"`

### 2. `main/llm/llm_provider.h`
Added `default_model` field to `llm_provider_t` struct after `model_nvs_key`:
```c
const char *default_model; /* fallback model when this brain's NVS key is unset */
```

### 3. `main/llm/llm_provider.c`
Added `default_model` initializer to each row of `k_providers[]`:
- anthropic → `MIMI_DEFAULT_MODEL_ANTHROPIC`
- openai    → `MIMI_DEFAULT_MODEL_OPENAI`
- ollama    → `MIMI_DEFAULT_MODEL_OLLAMA`

### 4. `main/llm/llm_proxy.c` — `llm_set_provider()`
Added `else` branch to the NVS model-restore block. When `p->model_nvs_key` is absent
or empty (brain was never configured), falls back to `p->default_model` and persists it
to both the per-brain key and the active `MIMI_NVS_KEY_MODEL` slot so subsequent
`config_show` and API calls see the right value immediately.

## Build result

```
Project build complete.
mimiclaw.bin binary size 0x120cf0 bytes. Smallest app partition is 0x200000 bytes. 0xdf310 bytes (44%) free.
```

Zero new warnings. All 35 build steps passed.

## Files changed
- `main/mimi_config.h`
- `main/llm/llm_provider.h`
- `main/llm/llm_provider.c`
- `main/llm/llm_proxy.c`
