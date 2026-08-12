    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0x40000200
    ldr r1, =0x07e0f800
    mov r2, #64

fill_loop:
    str r1, [r0], #4
    subs r2, r2, #1
    bne fill_loop

    ldr r0, =0x6000b000
    ldr r1, =0x40000200
    str r1, [r0, #0x10]
    ldr r1, =0x70008b00
    str r1, [r0, #0x18]
    ldr r1, =0x010000fc
    str r1, [r0]

    ldr r7, =0x7000a000
    ldr r8, =0x80800014
    str r8, [r7]
    mov r8, #0
    str r8, [r7]

    ldr r4, =0x6000d13c
    ldr r5, =0x7000a010
    mvn r6, #0

poll_charger:
    ldr r0, [r4]
    tst r0, #8
    ldreq r1, =0x00000000
    ldrne r1, =0x80200000
    cmp r1, r6
    beq poll_charger
    str r1, [r5]
    mov r6, r1
    b poll_charger
