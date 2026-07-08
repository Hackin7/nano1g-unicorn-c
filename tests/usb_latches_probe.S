    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc5000000
    add r0, r0, #0x184
    ldr r1, =0x40000100

    ldr r2, =0x11223344
    str r2, [r0]
    ldr r2, =0xabcd
    strh r2, [r0, #2]
    mov r2, #0x5a
    strb r2, [r0, #1]

    ldr r3, [r0]
    str r3, [r1]
    ldrh r3, [r0, #2]
    str r3, [r1, #4]
    ldrb r3, [r0, #1]
    str r3, [r1, #8]

1:
    b 1b
