    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x6000d030
    ldr r1, =0x40000100

    ldr r2, [r0]
    str r2, [r1]
    ldr r2, [r0, #4]
    str r2, [r1, #4]
    ldr r2, [r0, #8]
    str r2, [r1, #8]
    ldr r2, [r0, #12]
    str r2, [r1, #12]

1:
    b 1b
