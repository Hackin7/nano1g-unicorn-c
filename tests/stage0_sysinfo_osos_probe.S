    .syntax unified
    .arm
    .global _start

_start:
    ldr sp, =0x40017f00
    ldr r12, =0xc3000000

    ldr r0, =0x00011f8d      @ FAT SysInfo sector
    ldr r1, =0x40018000
    mov r2, #1
    bl read_sectors

    ldr r6, =0x4001ff18
    ldr r7, =0x53797349      @ "IsyS"
    ldr r8, =0x40018000
    str r7, [r6]
    str r8, [r6, #4]

    ldr r0, =0x00000823      @ Apple firmware partition osos payload sector
    ldr r1, =0x10000000
    ldr r2, =10872           @ whole 0x54ef30-byte osos payload rounded up
    bl read_sectors

    ldr r0, =0x10000000
    bx r0

read_sectors:
    push {r4-r8, lr}
    mov r4, r0               @ current LBA
    mov r5, r1               @ destination
    mov r6, r2               @ sectors remaining

dma_chunk:
    cmp r6, #256
    movhi r7, #256
    movls r7, r6             @ ATA sector count is eight bits; zero means 256

    ldr r0, =0x0003000a
    str r0, [r12, #0x400]    @ DMA direction/config, engine stopped
    mov r0, r7, lsl #9
    sub r0, r0, #4
    str r0, [r12, #0x408]    @ PP502x DMA length is byte count minus four
    str r5, [r12, #0x40c]

    and r0, r7, #0xff
    str r0, [r12, #0x08]
    and r0, r4, #0xff
    str r0, [r12, #0x0c]
    mov r0, r4, lsr #8
    and r0, r0, #0xff
    str r0, [r12, #0x10]
    mov r0, r4, lsr #16
    and r0, r0, #0xff
    str r0, [r12, #0x14]
    mov r0, r4, lsr #24
    and r0, r0, #0x0f
    orr r0, r0, #0xe0
    str r0, [r12, #0x18]
    mov r0, #0xc8            @ READ DMA
    str r0, [r12, #0x1c]

    ldr r0, [r12, #0x400]
    orr r0, r0, #1
    str r0, [r12, #0x400]

wait_dma:
    ldr r0, [r12, #0x1c]
    tst r0, #0x88            @ wait while BSY or DRQ remains asserted
    bne wait_dma

    add r4, r4, r7
    add r5, r5, r7, lsl #9
    subs r6, r6, r7
    bne dma_chunk

    pop {r4-r8, lr}
    bx lr
