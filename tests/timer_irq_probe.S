    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x60005000
    ldr r1, =0x60004000
    ldr r2, =0x40000100

    mov r3, #1
    str r3, [r1, #0x24]
    ldr r3, =0x80000003
    str r3, [r0]

    mov r4, #96
1:
    subs r4, r4, #1
    bne 1b

    ldr r3, [r1]
    str r3, [r2]
    ldr r3, [r0, #0x04]
    str r3, [r2, #4]
    ldr r3, [r1]
    str r3, [r2, #8]
    ldrh r3, [r0, #2]
    str r3, [r2, #12]

2:
    b 2b
