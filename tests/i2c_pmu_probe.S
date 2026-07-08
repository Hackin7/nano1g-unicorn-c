    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x7000c000
    ldr r1, =0x40000100

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x38
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]
    ldr r3, [r0, #0x1c]
    str r3, [r1]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #4]

    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #8]

    mov r2, #0x10
    str r2, [r0, #0x04]
    ldr r2, =0x5538
    strh r2, [r0, #0x0c]
    mov r2, #0x82
    str r2, [r0]

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x38
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]

    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #12]

1:
    b 1b
