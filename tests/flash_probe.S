    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =ram_code_start
    ldr r1, =0x40000000
    ldr r2, =ram_code_end

copy_loop:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r0, r2
    blo copy_loop

    ldr pc, =0x40000000

    .align 2
ram_code_start:
    ldr r0, =0x00000555
    mov r1, #0x90
    strb r1, [r0]

    mov r2, #0
    ldrh r3, [r2]
    ldr r4, =0x40000100
    str r3, [r4]

    mov r1, #0xff
    strb r1, [r0]
    ldrh r3, [r2]
    str r3, [r4, #4]

1:
    b 1b

    .ltorg
ram_code_end:
