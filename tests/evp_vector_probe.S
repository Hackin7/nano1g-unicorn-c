    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x6000f008
    adr r1, swi_handler
    str r1, [r0]

    ldr r0, =0x6000c000
    mov r1, #0x10
    str r1, [r0]

    svc #0

    ldr r0, =0x40000100
    ldr r1, =0xbad00000
    str r1, [r0, #4]

1:
    b 1b

swi_handler:
    ldr r0, =0x40000100
    ldr r1, =0x5a17e0e5
    str r1, [r0]

2:
    b 2b

    .ltorg
