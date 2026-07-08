    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x6000d000
    ldr r1, =0x6000d800
    ldr r2, =0x40000100

    mov r3, #0
    str r3, [r0]

    ldr r3, =0x0000ff55
    str r3, [r1]
    ldr r4, [r0]
    str r4, [r2]

    ldr r3, =0x0000f000
    strh r3, [r1]
    ldr r4, [r0]
    str r4, [r2, #4]

    ldr r3, =0x00000f0f
    strh r3, [r1]
    ldr r4, [r0]
    str r4, [r2, #8]

1:
    b 1b
