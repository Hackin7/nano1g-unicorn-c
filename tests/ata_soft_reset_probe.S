    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    @ Enable READ/WRITE MULTIPLE so reset has mode state to clear.
    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0xc6
    str r2, [r0, #0x1c]
    bl wait_not_busy
    ldr r3, [r0, #0x1c]

    @ Begin a PIO write and put one word into the active sector.
    mov r2, #1
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0x30
    str r2, [r0, #0x1c]
    bl wait_not_busy
    ldr r3, =0x1122
    strh r3, [r0]

    @ SRST aborts the transfer, clears IRQ, and holds BSY while asserted.
    mov r2, #4
    str r2, [r0, #0x3f8]
    ldr r3, [r0, #0x3f8]
    str r3, [r1]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #4]

    @ Commands and data writes are ignored until reset completes.
    mov r2, #0xec
    str r2, [r0, #0x1c]
    ldr r3, =0x3344
    strh r3, [r0]

    mov r2, #0
    str r2, [r0, #0x3f8]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #8]
    bl wait_not_busy
    str r3, [r1, #12]
    ldr r3, [r0, #0x04]
    str r3, [r1, #16]
    ldr r3, [r0, #0x08]
    str r3, [r1, #20]
    ldr r3, [r0, #0x0c]
    str r3, [r1, #24]
    ldr r3, [r0, #0x10]
    str r3, [r1, #28]
    ldr r3, [r0, #0x14]
    str r3, [r1, #32]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #36]

    @ IDENTIFY word 59 reports that multiple mode returned to disabled.
    mov r2, #0xec
    str r2, [r0, #0x1c]
    bl wait_not_busy
    ldr r3, [r0, #0x1c]
    mov r4, #59
1:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 1b
    ldrh r3, [r0]
    str r3, [r1, #40]

2:
    b 2b

wait_not_busy:
3:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 3b
    bx lr
