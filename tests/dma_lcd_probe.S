    .syntax unified
    .arm
    .global _start

    /* Arm a flow-controlled LCD2 DMA, then observe completion.
     * CMD is WAIT_REQ|INTR|(256-4). The block request, not a profile
     * flag, is what lets the engine move the 64 words. */
_start:
    ldr r0, =0x40000200
    ldr r1, =0x07e0f800
    mov r2, #64
fill_loop:
    str r1, [r0], #4
    subs r2, r2, #1
    bne fill_loop

    ldr r0, =0x70008a24
    ldr r1, =0xc00100ff
    str r1, [r0]
    ldr r0, =0x70008a20
    ldr r1, =0x34000000
    str r1, [r0]

    ldr r0, =0x6000b000
    ldr r1, =0x40000200
    str r1, [r0, #0x10]
    ldr r1, =0x70008b00
    str r1, [r0, #0x18]
    ldr r1, =0x410000fc
    str r1, [r0]

    ldr r2, =0x40000100
    ldr r4, =0x60004000
    ldr r3, [r4]
    and r3, r3, #0x04000000
    str r3, [r2, #8]
    ldr r3, [r0, #0x04]
    str r3, [r2]
    ldr r3, [r0]
    str r3, [r2, #4]
    ldr r3, [r4]
    and r3, r3, #0x04000000
    str r3, [r2, #12]

1:
    b 1b
