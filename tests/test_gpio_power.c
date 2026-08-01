#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define GPIOA_INPUT_VAL 0x030u
#define GPIOA_INT_STAT  0x040u
#define GPIOA_INT_EN    0x050u
#define GPIOA_INT_LEV   0x060u
#define GPIOA_INT_CLR   0x070u
#define GPIOA_HOLD      0x20u

#define GPIOL_INPUT_VAL 0x13cu
#define GPIOL_INT_STAT  0x14cu
#define GPIOL_INT_EN    0x15cu
#define GPIOL_INT_LEV   0x16cu
#define GPIOL_INT_CLR   0x17cu
#define GPIOL_MAIN      0x08u
#define GPIOL_USB       0x10u

#define GPIO_HI_IRQ     (1u << 0u)

static int expect_true(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    int failed = expect_true((n1g_dev_gpio_read(&s, GPIOA_INPUT_VAL, 4) & GPIOA_HOLD) != 0u,
                             "hold switch did not default to disengaged");

    n1g_dev_gpio_write(&s, GPIOA_INT_LEV, 4, 0u);
    n1g_dev_gpio_write(&s, GPIOA_INT_EN, 4, GPIOA_HOLD);
    (void)n1g_dev_opto_button(&s, "select", true);
    failed |= expect_true(s.opto.button_bits != 0u, "pre-hold button press was lost");

    (void)n1g_dev_gpio_set_hold(&s, true);
    failed |= expect_true((n1g_dev_gpio_read(&s, GPIOA_INPUT_VAL, 4) & GPIOA_HOLD) == 0u,
                          "engaged hold switch was not active-low") |
              expect_true((n1g_dev_gpio_read(&s, GPIOA_INT_STAT, 4) & GPIOA_HOLD) != 0u,
                          "hold engagement did not latch GPIOA interrupt status") |
              expect_true((s.intc.hi_cpu_status & GPIO_HI_IRQ) != 0u,
                          "hold engagement did not assert PP502x GPIO IRQ 32") |
              expect_true(s.opto.button_bits == 0u,
                          "hold engagement did not release a pressed button");

    uint8_t old_wheel = s.opto.wheel_pos;
    uint64_t old_suppressed = s.opto.suppressed_events;
    (void)n1g_dev_opto_tap(&s, "menu", 2000u);
    (void)n1g_dev_opto_wheel(&s, 4);
    failed |= expect_true(s.opto.button_bits == 0u && s.opto.wheel_pos == old_wheel,
                          "hold switch did not suppress wheel-controller input") |
              expect_true(s.opto.suppressed_events == old_suppressed + 2u,
                          "hold-suppressed input count was incorrect");

    n1g_dev_gpio_write(&s, GPIOA_INT_LEV, 4, GPIOA_HOLD);
    n1g_dev_gpio_write(&s, GPIOA_INT_CLR, 4, GPIOA_HOLD);
    failed |= expect_true((s.intc.hi_cpu_status & GPIO_HI_IRQ) == 0u,
                          "GPIO interrupt acknowledgement did not clear IRQ 32");
    (void)n1g_dev_gpio_set_hold(&s, false);
    failed |= expect_true((n1g_dev_gpio_read(&s, GPIOA_INT_STAT, 4) & GPIOA_HOLD) != 0u,
                          "hold release did not latch the opposite GPIO level");
    n1g_dev_gpio_write(&s, GPIOA_INT_CLR, 4, GPIOA_HOLD);

    uint32_t charger_input = n1g_dev_gpio_read(&s, GPIOL_INPUT_VAL, 4);
    failed |= expect_true((charger_input & GPIOL_MAIN) != 0u &&
                          (charger_input & GPIOL_USB) == 0u,
                          "charger pins did not default to disconnected levels");
    n1g_dev_gpio_write(&s, GPIOL_INT_LEV, 4, GPIOL_USB);
    n1g_dev_gpio_write(&s, GPIOL_INT_EN, 4, GPIOL_MAIN | GPIOL_USB);
    (void)n1g_dev_gpio_set_chargers(&s, true, true);
    charger_input = n1g_dev_gpio_read(&s, GPIOL_INPUT_VAL, 4);
    failed |= expect_true((charger_input & GPIOL_MAIN) == 0u &&
                          (charger_input & GPIOL_USB) != 0u,
                          "runtime charger state did not reach GPIOL input pins") |
              expect_true((n1g_dev_gpio_read(&s, GPIOL_INT_STAT, 4) &
                           (GPIOL_MAIN | GPIOL_USB)) == (GPIOL_MAIN | GPIOL_USB),
                          "charger changes did not latch both GPIO interrupt bits") |
              expect_true((s.intc.hi_cpu_status & GPIO_HI_IRQ) != 0u,
                          "charger changes did not assert PP502x GPIO IRQ 32");
    n1g_dev_gpio_write(&s, GPIOL_INT_CLR, 4, GPIOL_MAIN | GPIOL_USB);
    failed |= expect_true((s.intc.hi_cpu_status & GPIO_HI_IRQ) == 0u,
                          "charger GPIO interrupt acknowledgement left IRQ 32 asserted");

    if (!failed) {
        puts("gpio power unit ok");
    }
    return failed ? 1 : 0;
}
