/*
 * Read-only BIOS stage 1.
 *
 * The companion linker script emits exactly one 512-byte boot sector. The
 * sector uses INT 13h extensions to read the fixed-size stage-2 artifact from
 * LBAs 1..127 into 0x10000. There is deliberately no INT 13h write path.
 */

    .code16
    .section .text.stage1,"ax"
    .global _stage1_start

    .set STAGE2_SECTORS, 127
    .set STAGE2_SEGMENT, 0x1000

_stage1_start:
    cli
    ljmp $0x07c0, $_stage1_normalized

_stage1_normalized:
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    cld

    movb %dl, _boot_drive

    /* Require Enhanced Disk Drive reads (AH=42h). */
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc _stage1_disk_error
    cmpw $0xaa55, %bx
    jne _stage1_disk_error
    testw $0x0001, %cx
    jz _stage1_disk_error

    movb $3, _read_attempts
_stage1_read_stage2:
    movw $STAGE2_SECTORS, _disk_address_packet + 2
    movb _boot_drive, %dl
    movw $_disk_address_packet, %si
    movb $0x42, %ah
    int $0x13
    jnc _stage1_read_complete

    /* Reset the same boot drive and retry transient EDD failures. The DAP
       sector count is restored on every attempt because firmware may update
       it with a partial-transfer count. */
    pushw %cs
    popw %ds
    movb _boot_drive, %dl
    xorb %ah, %ah
    int $0x13
    pushw %cs
    popw %ds
    decb _read_attempts
    jnz _stage1_read_stage2
    jmp _stage1_disk_error

_stage1_read_complete:
    pushw %cs
    popw %ds
    movb _boot_drive, %dl
    ljmp $STAGE2_SEGMENT, $0x0000

_stage1_disk_error:
    movw $_disk_error_message, %si
_stage1_print_loop:
    lodsb
    testb %al, %al
    jz _stage1_halt
    movb $0x0e, %ah
    movw $0x0007, %bx
    int $0x10
    jmp _stage1_print_loop

_stage1_halt:
    cli
1:
    hlt
    jmp 1b

_boot_drive:
    .byte 0
_read_attempts:
    .byte 0

    .balign 4
_disk_address_packet:
    .byte 0x10, 0x00
    .word STAGE2_SECTORS
    .word 0x0000
    .word STAGE2_SEGMENT
    .quad 1

_disk_error_message:
    .asciz "AURA 67: stage2 read failed"

    .org 510
    .word 0xaa55
