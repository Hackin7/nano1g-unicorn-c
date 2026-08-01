#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void dimmer_pulse(n1g_state_t *s, uint32_t width) {
    n1g_dev_dimmer_write(s, 0x00u, 4u, 0x14000008u);
    n1g_dev_dimmer_write(s, 0x24u, 4u, width);
    n1g_dev_dimmer_write(s, 0x00u, 4u, 0x54000008u);
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    int failed = expect_true(n1g_dev_backlight_powered(&s),
                             "unconfigured backlight did not default on") |
                 expect_true(n1g_dev_backlight_level(&s) == 32u,
                             "unconfigured backlight did not default to full brightness");

    n1g_dev_pwm_write(&s, 0x10u, 4u, 0x80140000u);
    failed |= expect_true(strcmp(n1g_dev_backlight_mode(&s), "pwm") == 0,
                          "PWM write did not select PWM backlight mode") |
              expect_true(n1g_dev_pwm_read(&s, 0x10u, 4u) == 0x80140000u,
                          "PWM channel 1 did not latch its control word") |
              expect_true(n1g_dev_backlight_powered(&s) &&
                          n1g_dev_backlight_level(&s) == 20u,
                          "PWM duty did not produce level 20");
    n1g_dev_pwm_write(&s, 0x10u, 4u, 0x80000000u);
    failed |= expect_true(!n1g_dev_backlight_powered(&s),
                          "zero-duty PWM did not turn the backlight off");

    memset(&s, 0, sizeof(s));
    dimmer_pulse(&s, 127u);
    failed |= expect_true(strcmp(n1g_dev_backlight_mode(&s), "nano") == 0,
                          "dimmer pulse did not select Nano backlight mode") |
              expect_true(n1g_dev_backlight_level(&s) == 17u,
                          "Nano up pulse did not increment the default level") |
              expect_true((n1g_dev_dimmer_read(&s, 0x04u, 4u) & 0x80000000u) != 0u,
                          "Nano dimmer did not expose its busy phase") |
              expect_true((n1g_dev_dimmer_read(&s, 0x04u, 4u) & 0x80000000u) == 0u,
                          "Nano dimmer busy phase did not complete");
    dimmer_pulse(&s, 1u);
    failed |= expect_true(n1g_dev_backlight_level(&s) == 16u,
                          "Nano down pulse did not decrement the level") |
              expect_true(s.backlight.dimmer_pulses == 2u &&
                          s.backlight.dimmer_up_pulses == 1u &&
                          s.backlight.dimmer_down_pulses == 1u,
                          "Nano pulse counters were incorrect");

    n1g_dev_gpio_write(&s, 0x92cu, 2u, 0x8080u);
    failed |= expect_true(n1g_dev_backlight_powered(&s),
                          "Apple GPIO 103 write did not power the Nano backlight") |
              expect_true(n1g_dev_backlight_intensity(&s) > 0u,
                          "powered Nano backlight had zero rendered intensity");
    n1g_dev_gpio_write(&s, 0x92cu, 2u, 0x8000u);
    failed |= expect_true(!n1g_dev_backlight_powered(&s) &&
                          n1g_dev_backlight_intensity(&s) == 0u,
                          "Apple GPIO 103 clear did not extinguish the backlight");

    if (!failed) {
        puts("backlight unit ok");
    }
    return failed ? 1 : 0;
}
