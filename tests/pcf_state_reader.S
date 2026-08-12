    .syntax unified
    .arm
    .global _start

_start:
    ldr r4, =0x40000100
    mov r2, #0x0a
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x0b
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x0c
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x0d
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x0e
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x0f
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x10
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x12
    bl pcf_read
    str r3, [r4], #4
    mov r2, #0x3c
    bl pcf_read
    str r3, [r4]

1:
    b 1b

pcf_read:
    ldr r0, =0x7000c000
    mov r1, #0x10
    str r1, [r0, #0x04]
    str r2, [r0, #0x0c]
    mov r1, #0x80
    str r1, [r0]
    mov r1, #0x11
    str r1, [r0, #0x04]
    mov r1, #0xa0
    str r1, [r0]
    ldrb r3, [r0, #0x0c]
    bx lr
