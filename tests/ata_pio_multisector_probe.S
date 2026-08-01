    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    mov r2, #2
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0x20
    str r2, [r0, #0x1c]

    ldr r3, [r0, #0x3f8]
    str r3, [r1]
1:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 1b
    str r3, [r1, #4]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #8]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #12]

    ldrh r3, [r0]
    str r3, [r1, #16]
    mov r4, #255
2:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 2b

    ldr r3, [r0, #0x3f8]
    str r3, [r1, #20]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #24]
3:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 3b
    str r3, [r1, #28]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #32]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #36]

    ldrh r3, [r0]
    str r3, [r1, #40]
    mov r4, #255
4:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 4b

    ldr r3, [r0, #0x3f8]
    str r3, [r1, #44]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #48]

5:
    b 5b
