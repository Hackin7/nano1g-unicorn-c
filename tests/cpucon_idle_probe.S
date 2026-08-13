    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x60000000
    ldr r1, [r0]
    ldr r0, =0x60007000
    ldr r2, =0x55555555
    cmp r1, r2
    bne cop_sleep

    mov r2, #0
    str r2, [r0, #4]
    ldr r2, =0x80000000
    str r2, [r0]
cpu_loop:
    b cpu_loop

cop_sleep:
    ldr r2, =0x80000000
    str r2, [r0, #4]
cop_loop:
    b cop_loop
