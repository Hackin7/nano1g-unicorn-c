    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    /* An initial LBA beyond the one-sector disk fails without DRQ. */
    mov r2, #1
    str r2, [r0, #0x08]
    str r2, [r0, #0x0c]
    mov r2, #0
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0x20
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1]
    bl wait_not_busy
    str r3, [r1, #4]
    ldr r3, [r0, #0x04]
    str r3, [r1, #8]
    ldr r3, [r0, #0x0c]
    str r3, [r1, #12]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #16]
    ldr r3, [r0, #0x1c]

    /* A two-sector read transfers LBA 0, then reports LBA 1 as IDNF. */
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
    str r3, [r1, #20]
    bl wait_not_busy
    str r3, [r1, #24]
    ldr r3, [r0, #0x1c]
    ldrh r3, [r0]
    str r3, [r1, #28]
    mov r4, #255
1:
    ldrh r3, [r0]
    subs r4, r4, #1
    bne 1b
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #32]
    ldr r3, [r0, #0x0c]
    str r3, [r1, #36]
    ldr r3, [r0, #0x08]
    str r3, [r1, #40]
    bl wait_not_busy
    str r3, [r1, #44]
    ldr r3, [r0, #0x04]
    str r3, [r1, #48]
    ldr r3, [r0, #0x1c]

    /* The same boundary rule applies to PIO data-out. */
    mov r2, #2
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0x30
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #52]
    bl wait_not_busy
    str r3, [r1, #56]
    ldr r3, =0x5678
    strh r3, [r0]
    mov r3, #0
    mov r4, #255
2:
    strh r3, [r0]
    subs r4, r4, #1
    bne 2b
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #60]
    ldr r3, [r0, #0x0c]
    str r3, [r1, #64]
    ldr r3, [r0, #0x08]
    str r3, [r1, #68]
    bl wait_not_busy
    str r3, [r1, #72]
    ldr r3, [r0, #0x04]
    str r3, [r1, #76]
    ldr r3, [r0, #0x1c]

    /* DMA copies the valid sector and terminates at the first invalid LBA. */
    ldr r2, =0x0003000a
    str r2, [r0, #0x400]
    ldr r2, =1020
    str r2, [r0, #0x408]
    ldr r2, =0x40000200
    str r2, [r0, #0x40c]
    mov r2, #2
    str r2, [r0, #0x08]
    mov r2, #0
    str r2, [r0, #0x0c]
    str r2, [r0, #0x10]
    str r2, [r0, #0x14]
    mov r2, #0xe0
    str r2, [r0, #0x18]
    mov r2, #0xc8
    str r2, [r0, #0x1c]
    ldr r3, [r0, #0x3f8]
    str r3, [r1, #80]
    ldr r2, [r0, #0x400]
    orr r2, r2, #1
    str r2, [r0, #0x400]
    bl wait_busy
    str r3, [r1, #84]
    bl wait_not_busy
    str r3, [r1, #88]
    ldr r3, [r0, #0x04]
    str r3, [r1, #92]
    ldr r3, [r0, #0x0c]
    str r3, [r1, #96]
    ldr r3, [r0, #0x08]
    str r3, [r1, #100]
    ldr r3, [r0, #0x40c]
    str r3, [r1, #104]
    ldr r3, [r0, #0x404]
    str r3, [r1, #108]
    ldr r2, =0x40000200
    ldr r3, [r2]
    str r3, [r1, #112]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #116]

3:
    b 3b

wait_busy:
4:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    beq 4b
    bx lr

wait_not_busy:
5:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 5b
    bx lr
