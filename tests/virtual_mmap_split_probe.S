    .syntax unified
    .arm
    .global _start

_start:
    adr r4, low_code
    ldr r5, =0x10000000
    ldr r6, =low_code_size

copy_low_code:
    ldr r7, [r4], #4
    str r7, [r5], #4
    subs r6, r6, #4
    bne copy_low_code

    ldr r0, =0xf000f000
    ldr r1, =0x00003a00
    str r1, [r0]
    ldr r1, =0x00003b88
    str r1, [r0, #4]

    ldr r0, =0xf000f008
    ldr r1, =0x00003a00
    str r1, [r0]
    ldr r1, =0x10003f84
    str r1, [r0, #4]

    ldr pc, =0x00000000

low_code:
    mov r0, #0
    ldr r1, [r0]
    mov r2, #0x40000000
    add r2, r2, #0x100
    str r1, [r2]
1:
    b 1b
low_code_end:

    .equ low_code_size, low_code_end - low_code

    .ltorg
