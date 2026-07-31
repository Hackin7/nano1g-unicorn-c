    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100
    ldr r2, =0x60004000
    ldr r3, =0x40000200

    ldr r4, =0x0003000a
    str r4, [r0, #0x400]
    ldr r4, =508
    str r4, [r0, #0x408]
    str r3, [r0, #0x40c]

    mov r4, #1
    str r4, [r0, #0x1e8]
    mov r4, #0
    str r4, [r0, #0x1ec]
    str r4, [r0, #0x1f0]
    str r4, [r0, #0x1f4]
    mov r4, #0xe0
    str r4, [r0, #0x1f8]
    mov r4, #0xc8
    str r4, [r0, #0x1fc]

    ldr r4, [r0, #0x400]
    orr r4, r4, #1
    str r4, [r0, #0x400]

    ldr r4, [r2]
    str r4, [r1]
    ldr r4, [r0, #0x28]
    and r4, r4, #0x18
    str r4, [r1, #4]
    ldr r4, [r0, #0x400]
    str r4, [r1, #8]
    ldr r4, [r0, #0x404]
    str r4, [r1, #12]
    ldr r4, [r0, #0x408]
    str r4, [r1, #16]
    ldr r4, [r0, #0x40c]
    str r4, [r1, #20]
    ldr r4, [r3]
    str r4, [r1, #24]
    ldr r4, [r0, #0x1fc]
    str r4, [r1, #28]
    ldr r4, [r2]
    str r4, [r1, #32]

1:
    b 1b
