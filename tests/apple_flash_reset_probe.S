    .syntax unified
    .arm
    .global _start

_start:
    mrs r8, cpsr
    ldr r4, =0x40000100
    str r0, [r4, #0]
    str sp, [r4, #4]
    str lr, [r4, #8]
    str r8, [r4, #12]

    adr r5, fast_code
    ldr r6, =0x40001000
    ldr r7, =fast_image_size

copy_fast_code:
    ldr r8, [r5], #4
    str r8, [r6], #4
    subs r7, r7, #4
    bne copy_fast_code

    ldr pc, =0x40001000

fast_code:
    ldr r4, =0x40000100

    ldr r0, =0x00000555
    mov r1, #0x90
    strb r1, [r0]
    mov r2, #0
    ldrh r3, [r2]
    str r3, [r4, #16]
    mov r1, #0xff
    strb r1, [r0]

    adr r5, low_code
    ldr r6, =0x10000000
    ldr r7, =low_code_size

copy_low_code:
    ldr r8, [r5], #4
    str r8, [r6], #4
    subs r7, r7, #4
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
    .ltorg
fast_code_end:

low_code:
    mov r0, #0
    ldr r1, [r0]
    mov r2, #0x40000000
    add r2, r2, #0x100
    add r2, r2, #0x14
    str r1, [r2]
1:
    b 1b
low_code_end:

    .equ fast_image_size, low_code_end - fast_code
    .equ low_code_size, low_code_end - low_code

    .ltorg
