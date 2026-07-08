    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x60007000
    ldr r1, =0x40000100

    ldr r2, =0x48000003
    str r2, [r0]

    mov r3, #0x11
    str r3, [r1]

    ldr r4, [r0]
    str r4, [r1, #4]

1:
    b 1b
