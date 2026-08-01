    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    bl identify_word59
    str r2, [r1]

    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0xc4
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #4]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #8]
    bl wait_not_busy
    str r3, [r1, #12]
    ldr r3, [r0, #0x04]
    str r3, [r1, #16]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #20]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #24]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #28]

    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0xc6
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #32]
    bl wait_not_busy
    str r3, [r1, #36]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #40]
    ldr r3, [r0, #0x1c]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #44]

    bl identify_word59
    str r2, [r1, #48]

    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0xc4
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #52]
    bl wait_not_busy
    str r3, [r1, #56]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #60]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #64]
    ldrh r3, [r0]
    str r3, [r1, #68]
    mov r4, #255
1:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 1b
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #72]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #76]

    mov r2, #2
    str r2, [r0, #0x08]
    mov r2, #0xc6
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #80]
    bl wait_not_busy
    str r3, [r1, #84]
    ldr r3, [r0, #0x04]
    str r3, [r1, #88]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #92]
    ldr r3, [r0, #0x1c]

    bl identify_word59
    str r2, [r1, #96]

2:
    b 2b

wait_not_busy:
3:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 3b
    bx lr

identify_word59:
    mov r5, lr
    mov r2, #0xec
    str r2, [r0, #0x1c]
    bl wait_not_busy
    ldr r3, [r0, #0x1c]
    mov r4, #59
4:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 4b
    ldrh r2, [r0]
    mov r4, #196
5:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 5b
    bx r5
