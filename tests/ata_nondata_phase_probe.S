    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000
    ldr r1, =0x40000100

    mov r2, #0xaa
    str r2, [r0, #0x04]
    mov r2, #0xef
    str r2, [r0, #0x1c]

    ldr r3, [r0, #0x3f8]
    str r3, [r1]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #4]

1:
    ldr r3, [r0, #0x3f8]
    tst r3, #0x80
    bne 1b
    str r3, [r1, #8]

    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #12]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #16]
    ldr r3, [r0, #0x28]
    and r3, r3, #0x18
    str r3, [r1, #20]
    ldr r3, [r0, #0x04]
    str r3, [r1, #24]

2:
    b 2b
