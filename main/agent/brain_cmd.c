#include "brain_cmd.h"
#include <string.h>
#include <ctype.h>

struct map { const char *friendly; const char *provider; };
static const struct map k_map[] = {
    { "claude", "anthropic" },
    { "codex",  "openai"    },
    { "local",  "ollama"    },
};

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

bool brain_cmd_parse(const char *input, brain_cmd_t *out)
{
    if (!input || !out) return false;
    memset(out, 0, sizeof(*out));

    /* Must start with "/brain" as a whole token (reject "/brainstorm"). */
    if (strncmp(input, "/brain", 6) != 0) return false;
    char after = input[6];
    if (after != '\0' && after != ' ' && after != '\t') return false;

    const char *arg = skip_ws(input + 6);
    if (*arg == '\0') { out->action = BRAIN_STATUS; return true; }

    /* Copy the first whitespace-delimited token as the friendly name. */
    size_t n = 0;
    while (arg[n] && arg[n] != ' ' && arg[n] != '\t' && n < sizeof(out->friendly) - 1) {
        out->friendly[n] = (char)tolower((unsigned char)arg[n]);
        n++;
    }
    out->friendly[n] = '\0';

    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        if (strcmp(out->friendly, k_map[i].friendly) == 0) {
            strncpy(out->provider, k_map[i].provider, sizeof(out->provider) - 1);
            out->action = BRAIN_SWITCH;
            return true;
        }
    }
    out->action = BRAIN_USAGE;   /* recognized /brain, unknown target */
    return true;
}
