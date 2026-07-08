    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x6000c000
    ldr r1, =0xf0000000
    ldr r2, =0x40000100

    ldr r3, =0x11223344
    str r3, [r0]
    ldr r3, =0xa55a
    strh r3, [r0, #2]
    ldr r4, [r0]
    str r4, [r2]
    ldrh r4, [r0, #2]
    str r4, [r2, #4]

    ldr r3, =0x55667788
    str r3, [r1]
    mov r3, #0x99
    strb r3, [r1, #1]
    ldr r4, [r1]
    str r4, [r2, #8]
    ldrb r4, [r1, #1]
    str r4, [r2, #12]

1:
    b 1b
