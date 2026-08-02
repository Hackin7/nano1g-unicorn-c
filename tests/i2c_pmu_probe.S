    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x7000c000
    ldr r1, =0x40000100

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x38
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]
    ldr r3, [r0, #0x1c]
    str r3, [r1]
    ldr r3, [r0, #0x1c]
    str r3, [r1, #4]

    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #8]

    mov r2, #0x10
    str r2, [r0, #0x04]
    ldr r2, =0x5538
    strh r2, [r0, #0x0c]
    mov r2, #0x82
    str r2, [r0]

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x38
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]

    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #12]

    @ Interrupt status is read-only: a guest write must not create an event.
    mov r2, #0x10
    str r2, [r0, #0x04]
    ldr r2, =0xa502
    strh r2, [r0, #0x0c]
    mov r2, #0x82
    str r2, [r0]

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x02
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]
    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #16]

    @ Interrupt masks remain ordinary read/write registers.
    mov r2, #0x10
    str r2, [r0, #0x04]
    ldr r2, =0xa505
    strh r2, [r0, #0x0c]
    mov r2, #0x82
    str r2, [r0]

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x05
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]
    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #20]

    @ GOSTDBY is retained for readback and reported as a standby request.
    mov r2, #0x10
    str r2, [r0, #0x04]
    ldr r2, =0x6108
    strh r2, [r0, #0x0c]
    mov r2, #0x82
    str r2, [r0]

    mov r2, #0x10
    str r2, [r0, #0x04]
    mov r2, #0x08
    str r2, [r0, #0x0c]
    mov r2, #0x80
    str r2, [r0]
    mov r2, #0x11
    str r2, [r0, #0x04]
    mov r2, #0xa0
    str r2, [r0]
    ldrb r3, [r0, #0x0c]
    str r3, [r1, #24]

1:
    b 1b
