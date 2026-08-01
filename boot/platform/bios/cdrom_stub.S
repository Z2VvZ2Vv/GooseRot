/*
 * Read-only El Torito CD stub.
 *
 * Stage 1 cannot be reused from an optical drive: its INT 13h/AH=42h path
 * assumes 512-byte sectors on the boot drive, while a no-emulation CD boot
 * exposes 2048-byte sectors. This stub therefore performs no disk I/O at all.
 * The boot catalog asks the firmware for all 128 virtual sectors, so the
 * stage-2 payload is already in memory directly behind this sector; the stub
 * only moves it to 0x10000 and enters it exactly the way stage 1 does.
 *
 * It relies on the default load address of 0x7C00 alone, so no firmware needs
 * to honour a custom El Torito load segment. There is no INT 13h call, and
 * therefore no read path and no write path, anywhere in this file.
 */

    .code16
    .section .text.cdstub,"ax"
    .global _cdstub_start

    .set PAYLOAD_SEGMENT, 0x07e0 /* 0x7c00 + 512, the byte after this sector. */
    .set STAGE2_SEGMENT, 0x1000
    .set STAGE2_BYTES, 65024

_cdstub_start:
    cli
    ljmp $0x07c0, $_cdstub_normalized

_cdstub_normalized:
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7c00, %sp

    /* Source 0x07e00..0x17bff and destination 0x10000..0x1fdff overlap, so the
       payload is moved from its last byte down. Both offsets stay inside their
       own 64 KiB segment window, which keeps this a single string move. */
    movw $PAYLOAD_SEGMENT, %ax
    movw %ax, %ds
    movw $STAGE2_SEGMENT, %ax
    movw %ax, %es
    movw $(STAGE2_BYTES - 1), %si
    movw $(STAGE2_BYTES - 1), %di
    movw $STAGE2_BYTES, %cx
    std
    rep movsb
    cld

    /* Stage 2 sets up its own segments, stack and interrupt state from CS. */
    ljmp $STAGE2_SEGMENT, $0x0000

    .org 510
    .byte 0x55, 0xaa
