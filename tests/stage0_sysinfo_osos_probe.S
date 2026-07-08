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

sector_loop:
    mov r0, #1
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
    mov r0, #0x20
    str r0, [r12, #0x1c]

wait_drq:
    ldr r0, [r12, #0x1c]
    tst r0, #0x08
    beq wait_drq

    mov r7, #256
copy_words:
    ldrh r0, [r12]
    strh r0, [r5], #2
    subs r7, r7, #1
    bne copy_words

    add r4, r4, #1
    subs r6, r6, #1
    bne sector_loop

    pop {r4-r8, lr}
    bx lr
