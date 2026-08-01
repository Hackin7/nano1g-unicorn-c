    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x40000100

    ldr r1, =0x7000a000
    ldr r2, =0x80140000
    str r2, [r1, #0x10]
    ldr r2, [r1, #0x10]
    str r2, [r0]

    ldr r1, =0x7000c300
    ldr r2, =0x14000008
    str r2, [r1]
    mov r2, #127
    str r2, [r1, #0x24]
    ldr r2, =0x54000008
    str r2, [r1]
    ldr r2, [r1, #0x04]
    str r2, [r0, #0x04]
    ldr r2, [r1, #0x04]
    str r2, [r0, #0x08]
    ldr r2, =0x60000000
    str r2, [r1, #0x04]

    ldr r2, =0x14000008
    str r2, [r1]
    mov r2, #1
    str r2, [r1, #0x24]
    ldr r2, =0x54000008
    str r2, [r1]

    ldr r1, =0x6000d92c
    ldr r2, =0x00008080
    strh r2, [r1]
    ldr r1, =0x6000d12c
    ldr r2, [r1]
    str r2, [r0, #0x0c]

    ldr r1, =0x7000003c
    ldr r2, =0x00400000
    str r2, [r1]
    ldr r2, [r1]
    str r2, [r0, #0x10]
    mov r2, #0
    str r2, [r1]
    ldr r2, [r1]
    str r2, [r0, #0x14]

1:
    b 1b
