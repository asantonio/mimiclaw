#include "../../main/agent/brain_cmd.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    brain_cmd_t c;

    /* Not a /brain command */
    assert(brain_cmd_parse("what's the weather", &c) == false);
    assert(brain_cmd_parse("", &c) == false);
    assert(brain_cmd_parse("/brainstorm ideas", &c) == false); /* must not false-match prefix */

    /* Bare status */
    assert(brain_cmd_parse("/brain", &c) == true && c.action == BRAIN_STATUS);
    assert(brain_cmd_parse("/brain   ", &c) == true && c.action == BRAIN_STATUS);

    /* Friendly-name switches */
    assert(brain_cmd_parse("/brain claude", &c) == true && c.action == BRAIN_SWITCH
           && strcmp(c.provider, "anthropic") == 0 && strcmp(c.friendly, "claude") == 0);
    assert(brain_cmd_parse("/brain codex", &c) == true && c.action == BRAIN_SWITCH
           && strcmp(c.provider, "openai") == 0);
    assert(brain_cmd_parse("/brain local", &c) == true && c.action == BRAIN_SWITCH
           && strcmp(c.provider, "ollama") == 0);

    /* Unknown brain → usage */
    assert(brain_cmd_parse("/brain gpt9", &c) == true && c.action == BRAIN_USAGE);

    /* I1: strengthened edge-case assertions */
    assert(brain_cmd_parse(NULL, &c) == false);
    assert(brain_cmd_parse("/brain\t", &c) == true && c.action == BRAIN_STATUS);
    assert(brain_cmd_parse("/brain Claude", &c) == true && c.action == BRAIN_SWITCH
           && strcmp(c.provider, "anthropic") == 0);
    assert(brain_cmd_parse("/brain gpt9", &c) == true && c.action == BRAIN_USAGE
           && c.friendly[0] == '\0');   /* friendly cleared on USAGE — see M3 */

    printf("all brain_cmd tests passed\n");
    return 0;
}
