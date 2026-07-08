    .syntax unified
    .arm
    .global _start

_start:
    ldr r0, =0xc3000000      @ ATA taskfile base
    ldr r5, =0x40018000      @ native fast-RAM SysInfo destination
    mov r1, r5

    mov r2, #1
    str r2, [r0, #0x08]      @ sector count
    mov r2, #0x8d
    str r2, [r0, #0x0c]      @ LBA low: 0x00011f8d
    mov r2, #0x1f
    str r2, [r0, #0x10]      @ LBA mid
    mov r2, #0x01
    str r2, [r0, #0x14]      @ LBA high
    mov r2, #0xe0
    str r2, [r0, #0x18]      @ LBA mode, device 0, top nibble 0
    mov r2, #0x20
    str r2, [r0, #0x1c]      @ READ SECTORS

wait_drq:
    ldr r2, [r0, #0x1c]
    tst r2, #0x08
    beq wait_drq

    mov r3, #256
copy_sector:
    ldrh r2, [r0]
    strh r2, [r1], #2
    subs r3, r3, #1
    bne copy_sector

    ldr r6, =0x4001ff18
    ldr r7, =0x53797349      @ "IsyS"
    str r7, [r6]
    str r5, [r6, #4]

    ldr r4, =0x40000100
    ldr r2, [r6]
    str r2, [r4]
    ldr r2, [r6, #4]
    str r2, [r4, #4]
    ldr r2, [r5]
    str r2, [r4, #8]
    ldr r2, [r5, #4]
    str r2, [r4, #12]
    ldr r2, [r5, #0xe0]
    str r2, [r4, #16]
    ldr r2, [r5, #0xe4]
    str r2, [r4, #20]

1:
    b 1b
