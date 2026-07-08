    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0xc5
    str r2, [r0, #0x1c]

    ldr r3, [r0, #0x1c]
    str r3, [r1]

    ldr r3, =0x33441122
    str r3, [r0]
    mov r4, #254
1:
    mov r3, #0
    strh r3, [r0]
    subs r4, r4, #1
    bne 1b

    ldr r3, [r0, #0x1c]
    str r3, [r1, #4]

    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0x20
    str r2, [r0, #0x1c]

    ldr r3, [r0]
    str r3, [r1, #8]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #12]

2:
    b 2b
