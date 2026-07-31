    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x60004000
    ldr r2, =0x40000100

    mov r3, #0xec
    str r3, [r0, #0x1fc]

    ldr r3, [r1]
    str r3, [r2]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r2, #4]

    ldr r3, [r0, #0x3f8]
    str r3, [r2, #8]
    ldr r3, [r1]
    str r3, [r2, #12]

    ldr r3, [r0, #0x1fc]
    str r3, [r2, #16]
    ldr r3, [r1]
    str r3, [r2, #20]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r2, #24]

1:
    b 1b
