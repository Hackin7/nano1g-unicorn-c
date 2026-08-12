    .syntax unified
    .arm
    .global _start

_start:
    mov r2, #0x0a
    mov r3, #0x58
    bl pcf_write
    mov r2, #0x0b
    mov r3, #0x59
    bl pcf_write
    mov r2, #0x0c
    mov r3, #0x23
    bl pcf_write
    mov r2, #0x0d
    mov r3, #0x01
    bl pcf_write
    mov r2, #0x0e
    mov r3, #0x28
    bl pcf_write
    mov r2, #0x0f
    mov r3, #0x02
    bl pcf_write
    mov r2, #0x10
    mov r3, #0x28
    bl pcf_write

    @ Alarm programming and an unrelated PMU register must persist too.
    mov r2, #0x12
    mov r3, #0x42
    bl pcf_write
    mov r2, #0x3c
    mov r3, #0xab
    bl pcf_write

1:
    b 1b

pcf_write:
    ldr r0, =0x7000c000
    mov r1, #0x10
    str r1, [r0, #0x04]
    orr r2, r2, r3, lsl #8
    strh r2, [r0, #0x0c]
    mov r1, #0x82
    str r1, [r0]
    bx lr
