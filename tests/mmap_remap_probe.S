    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x10000120
    ldr r1, =0x13572468
    str r1, [r0]

    ldr r0, =0xf000f008
    ldr r1, =0x20003a00
    str r1, [r0]
    ldr r1, =0x00003f84
    str r1, [r0, #4]

    ldr r0, =0xf000f010
    ldr r1, =0x00003a00
    str r1, [r0]
    ldr r1, =0x10003f84
    str r1, [r0, #4]

    ldr r0, =0x00000120
    ldr r2, [r0]
    ldr r3, =0x40000100
    str r2, [r3]

    ldr r0, =0x20000555
    mov r1, #0x90
    strb r1, [r0]

    ldr r0, =0x20000000
    ldrh r2, [r0]
    str r2, [r3, #4]

    mov r1, #0xff
    ldr r0, =0x20000555
    strb r1, [r0]

1:
    b 1b

    .ltorg
