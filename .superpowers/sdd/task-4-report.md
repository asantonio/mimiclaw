# Task 4 Report: `/brain` command (parser TDD + agent-loop interception)

## What Was Implemented

1. **`main/agent/brain_cmd.h`** — Public header defining `brain_action_t`, `brain_cmd_t`, and `brain_cmd_parse()`.
2. **`main/agent/brain_cmd.c`** — Pure libc parser (no ESP-IDF deps). Maps friendly names `claude→anthropic`, `codex→openai`, `local→ollama`. Rejects `/brainstorm` via whole-token check (`input[6]` must be `\0`, space, or tab). Returns `BRAIN_STATUS` for bare `/brain` (whitespace trimmed), `BRAIN_SWITCH` for known names, `BRAIN_USAGE` for unknown.
3. **`test/host/test_brain_cmd.c`** — Host-native test (gcc, no ESP-IDF). All 9 assertions covering negative cases, status, all three switches, and unknown-name fallback.
4. **`test/host/Makefile`** — Plain `make -f test/host/Makefile test` runner.
5. **`main/llm/llm_proxy.h`** / **`main/llm/llm_proxy.c`** — Added two public accessors:
   - `const llm_provider_t *llm_active_provider(void)` — delegates to the existing static `active_provider()`, exposing provider descriptor to callers outside llm_proxy.c.
   - `bool llm_has_api_key(void)` — returns `s_api_key[0] != '\0'`, exposing key-presence without leaking the key.
   - Also added `#include "llm_provider.h"` to `llm_proxy.h` so callers get `llm_provider_t` transitively.
6. **`main/CMakeLists.txt`** — Added `"agent/brain_cmd.c"` to SRCS.
7. **`main/agent/agent_loop.c`** — Intercept block inserted immediately after `ESP_LOGI("Processing message from...")` at line 195, before the ReAct loop.

---

## TDD Evidence

### RED (brain_cmd.c/h do not exist)

Command:
```
cd /home/alastaira/code/mimiclaw && make -f test/host/Makefile test --always-make
```

Output:
```
gcc -std=c11 -Wall -Wextra -o /tmp/test_brain_cmd \
    test/host/test_brain_cmd.c main/agent/brain_cmd.c
test/host/test_brain_cmd.c:1:10: fatal error: ../../main/agent/brain_cmd.h: No such file or directory
    1 | #include "../../main/agent/brain_cmd.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
compilation terminated.
cc1: fatal error: main/agent/brain_cmd.c: No such file or directory
compilation terminated.
make: *** [test/host/Makefile:2: test] Error 1
```

Expected: compile error because brain_cmd.h and brain_cmd.c did not yet exist. ✓

### GREEN (after implementing brain_cmd.h and brain_cmd.c)

Command:
```
cd /home/alastaira/code/mimiclaw && make -f test/host/Makefile test --always-make
```

Output:
```
gcc -std=c11 -Wall -Wextra -o /tmp/test_brain_cmd \
    test/host/test_brain_cmd.c main/agent/brain_cmd.c
/tmp/test_brain_cmd
all brain_cmd tests passed
```

All 9 assertions passed, zero warnings. ✓

---

## agent_loop Integration

### Where inserted

After `ESP_LOGI(TAG, "Processing message from %s:%s", ...)` at line 195, before `/* 1. Build system prompt */`. This means any `/brain` command is handled before session loading, context building, and the ReAct loop — no LLM call is made.

### How the reply mirrors existing outbound push code

The existing pattern (lines ~289-301 before insertion):
```c
mimi_msg_t out = {0};
strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
out.content = final_text;  /* transfer ownership */
if (message_bus_push_outbound(&out) != ESP_OK) {
    free(final_text);
}
```

The normal final path assigns `final_text` directly (transfer ownership) and frees it only on queue-full. The working-status path (lines ~222-234) uses `strdup`. For the `/brain` reply, `reply` is a stack buffer, so `strdup` is required — this matches the working-status pattern and is safe.

### `msg.content` ownership finding

The inbound message bus allocates `msg.content` (heap). In the **normal path**, `free(msg.content)` is called unconditionally at line 362 (after the refactored insertion), at the bottom of every loop iteration. The `/brain` early-out path must free it before `continue` to avoid a leak — done at line 237:
```c
free(msg.content);   /* match normal path ownership — always freed at end of loop */
continue;
```

There is no double-free risk: the normal `free(msg.content)` at the bottom of the loop is only reached if `brain_cmd_parse()` returns false (i.e., the `/brain` branch was NOT taken). The `continue` skips to the next loop iteration, bypassing that free.

---

## Missing-Key Warning (addition from plan self-review)

After a successful `llm_set_provider`, the code calls `llm_active_provider()` to get the new provider's `auth` field:
```c
const llm_provider_t *prov = llm_active_provider();
if ((prov->auth == LLM_AUTH_BEARER || prov->auth == LLM_AUTH_ANTHROPIC)
        && !llm_has_api_key()) {
    strncat(reply, " (no API key set)", sizeof(reply) - strlen(reply) - 1);
}
```

Ollama (`LLM_AUTH_NONE`) is silently skipped. anthropic and openai get the warning if the shared `s_api_key` is empty.

**Design limitation flagged:** The system stores a single shared API key (`s_api_key` in `llm_proxy.c`). This means the warning "no API key set" means "no key configured at all" — not "no key for this specific brain." A user who has an Anthropic key set but switches to `codex` (OpenAI) will NOT see the warning even though their key is wrong for OpenAI. The per-brain model NVS keys exist (Task 3), but there is no per-brain API key storage. This is a known limitation and should be addressed if multi-provider key storage is added in a future task.

---

## idf.py Build

Command:
```
cd /home/alastaira/code/mimiclaw && bash -c 'source ~/esp/esp-idf-5.5.2/export.sh >/dev/null 2>&1 && idf.py build 2>&1'
```

Tail:
```
[5/17] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/agent/brain_cmd.c.obj
[8/17] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/agent/agent_loop.c.obj
[11/17] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/llm/llm_proxy.c.obj
[12/17] Linking C static library esp-idf/main/libmain.a
[14/17] Linking CXX executable mimiclaw.elf
[15/17] Generating binary image from built executable
Generated /home/alastaira/code/mimiclaw/build/mimiclaw.bin
Project build complete.
```

Zero warnings, zero errors. Binary size: 0x1209a0 bytes, 44% free in app partition. ✓

---

## Files Changed

| File | Change |
|------|--------|
| `main/agent/brain_cmd.h` | Created — enum, struct, function declaration |
| `main/agent/brain_cmd.c` | Created — libc-only parser |
| `test/host/test_brain_cmd.c` | Created — host test (9 assertions) |
| `test/host/Makefile` | Created — gcc test runner |
| `main/llm/llm_proxy.h` | Added `#include "llm_provider.h"`, `llm_active_provider()`, `llm_has_api_key()` declarations |
| `main/llm/llm_proxy.c` | Added `llm_active_provider()` and `llm_has_api_key()` implementations |
| `main/CMakeLists.txt` | Added `"agent/brain_cmd.c"` to SRCS |
| `main/agent/agent_loop.c` | Added includes; inserted `/brain` intercept block after inbound pop |

---

## Self-Review

- **TDD evidence real:** RED shows compile error (files didn't exist), GREEN shows all assertions pass. Not faked.
- **Memory ownership correct:** `free(msg.content)` on early-out mirrors end-of-loop free. No double-free (early `continue` skips the tail free). Outbound `out.content = strdup(reply)` — reply is stack, strdup required, freed on queue-full.
- **YAGNI:** No speculative fields or hooks. `llm_active_provider()` and `llm_has_api_key()` are minimal — they expose only what agent_loop.c needs and nothing more.
- **Names:** match brief exactly (`brain_cmd_parse`, `brain_cmd_t`, `brain_action_t`, `BRAIN_*` enum values).
- **No stray warnings:** gcc `-Wall -Wextra` clean; ESP-IDF build clean (zero warning lines).
- **`mimi_secrets.h` not committed:** verified via `git show HEAD --name-only` — only the listed files in both commits.

## Concerns

1. **Single shared API key**: As described above — the missing-key warning checks `llm_has_api_key()` which is global, not per-brain. Switching from anthropic (with a key set) to openai will NOT warn, even though the key is an Anthropic key. Multi-brain key storage is a future task.
2. **`reply` buffer size**: 192 bytes. The status reply format `"🧠 <provider> | model <model> | <url>"` is at most ~130 bytes with real values. The switch reply `"🧠 -> <friendly> (<model>) (no API key set)"` fits well within 192. But if `llm_get_model()` returns a very long model string (e.g., a URL-like identifier) the status line could truncate silently — `snprintf` handles this safely (no overflow), but the reply would be clipped.
3. **`llm_api_url()` removed from public interface**: The brief mentioned consuming `llm_api_url()`, but that function is static. Rather than expose it directly, the integration uses `llm_active_provider()->default_url` via the new `llm_active_provider()` accessor, which is cleaner. No behavior difference.

---

## Fix pass

### Host test

Command:
```
cd /home/alastaira/code/mimiclaw && gcc -std=c11 -Wall -Wextra -o /tmp/test_brain_cmd test/host/test_brain_cmd.c main/agent/brain_cmd.c && /tmp/test_brain_cmd
```

Output:
```
all brain_cmd tests passed
```

Zero gcc warnings. 13 assertions total (9 original + 4 new from I1). ✓

### idf.py build tail

```
[9/14] Linking C static library esp-idf/main/libmain.a
[10/14] Generating esp-idf/esp_system/ld/sections.ld
[11/14] Linking CXX executable mimiclaw.elf
[12/14] Generating binary image from built executable
esptool.py v4.12.dev2
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
Generated /home/alastaira/code/mimiclaw/build/mimiclaw.bin
[13/14] cd /home/alastaira/code/mimiclaw/build/esp-idf/esptool_py && ...
mimiclaw.bin binary size 0x1209b0 bytes. Smallest app partition is 0x200000 bytes. 0xdf650 bytes (44%) free.

Project build complete.
```

Zero warnings, zero errors. ✓

### Files changed

| File | Change |
|------|--------|
| `test/host/test_brain_cmd.c` | Added 4 new assertions (I1: NULL input, tab-only status, capital-C Claude switch, friendly cleared on USAGE) |
| `main/agent/brain_cmd.c` | Added `memset(out->friendly, 0, sizeof(out->friendly))` before `BRAIN_USAGE` (M3) |
| `main/agent/agent_loop.c` | Added one-line comment above `strncat` documenting stack-buffer sizeof intent (M1) |
