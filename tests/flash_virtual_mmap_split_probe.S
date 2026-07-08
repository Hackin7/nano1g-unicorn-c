    .syntax unified
    .arm
    .global _start

_start:
    b flash_body

flash_body:
    adr r4, low_code
    ldr r5, =0x10000000
    ldr r6, =low_code_size

copy_low_code:
    ldr r7, [r4], #4
    str r7, [r5], #4
    subs r6, r6, #4
    bne copy_low_code

    adr r4, fast_code
    ldr r5, =0x40001000
    ldr r6, =fast_code_size

copy_fast_code:
    ldr r7, [r4], #4
    str r7, [r5], #4
    subs r6, r6, #4
    bne copy_fast_code

    ldr pc, =0x40001000

fast_code:
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
    .ltorg
fast_code_end:

low_code:
    mov r0, #0
    ldr r1, [r0]
    mov r2, #0x40000000
    add r2, r2, #0x104
    str r1, [r2]
1:
    b 1b
low_code_end:

    .equ low_code_size, low_code_end - low_code
    .equ fast_code_size, fast_code_end - fast_code

    .ltorg
