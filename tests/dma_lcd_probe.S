    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x40000200
    ldr r1, =0x07e0f800
    mov r2, #64

fill_loop:
    str r1, [r0], #4
    subs r2, r2, #1
    bne fill_loop

    ldr r0, =0x6000b000
    ldr r1, =0x40000200
    str r1, [r0, #0x10]
    ldr r1, =0x70008b00
    str r1, [r0, #0x18]
    ldr r1, =0x010000fc
    str r1, [r0]

    ldr r2, =0x40000100
    ldr r3, [r0, #0x04]
    str r3, [r2]
    ldr r3, [r0]
    str r3, [r2, #4]

1:
    b 1b
