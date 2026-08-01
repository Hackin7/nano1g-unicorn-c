#include "nano1g/input_script.h"

#include <stdio.h>
#include <string.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    n1g_input_script_t script;
    int failed = expect_true(
        n1g_input_script_load(
            &script,
            "wait:7,frame:main,wheel:+4,select-down,select-up"),
        "valid checkpoint script was rejected");
    failed |= expect_true(script.count == 5u,
                          "checkpoint script produced the wrong event count") |
              expect_true(script.events[0].kind == N1G_INPUT_EV_WAIT &&
                          script.events[0].ticks == 7u,
                          "wait event was parsed incorrectly") |
              expect_true(script.events[1].kind == N1G_INPUT_EV_FRAME &&
                          strcmp(script.events[1].name, "main") == 0,
                          "frame checkpoint was parsed incorrectly") |
              expect_true(script.events[2].kind == N1G_INPUT_EV_WHEEL &&
                          script.events[2].delta == 4,
                          "wheel event was parsed incorrectly") |
              expect_true(script.events[3].kind == N1G_INPUT_EV_DOWN &&
                          script.events[4].kind == N1G_INPUT_EV_UP,
                          "button events were parsed incorrectly");

    failed |= expect_true(!n1g_input_script_load(&script, "frame:"),
                          "empty frame checkpoint label was accepted") |
              expect_true(!n1g_input_script_load(&script,
                                                  "frame:label-that-is-too-long"),
                          "overlong frame checkpoint label was truncated");

    failed |= expect_true(n1g_input_script_load(&script, "select") &&
                          script.count == 3u &&
                          script.events[0].kind == N1G_INPUT_EV_DOWN &&
                          script.events[1].kind == N1G_INPUT_EV_WAIT &&
                          script.events[2].kind == N1G_INPUT_EV_UP,
                          "bare button expansion regressed");

    if (!failed) {
        puts("input script unit ok");
    }
    return failed ? 1 : 0;
}
