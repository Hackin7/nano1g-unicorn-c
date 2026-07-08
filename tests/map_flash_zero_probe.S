    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x00000555
    mov r1, #0x90
    strb r1, [r0]

    mov r2, #0
    ldrh r3, [r2]
    ldr r4, =0x40000100
    str r3, [r4]

    mov r1, #0xff
    strb r1, [r0]
    ldr r5, [r2]
    str r5, [r4, #4]

1:
    b 1b

    .ltorg
