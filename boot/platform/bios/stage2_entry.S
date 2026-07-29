/*
 * GooseBoot BIOS stage 2 entry.
 *
 * This file performs only pre-game platform setup: select a compatible VBE
 * linear framebuffer, verify minimal RAM, enable A20, enter 32-bit protected
 * mode, clear BSS and call bios_main(). It contains no disk I/O.
 */

    .code16
    .section .text.stage2_entry,"ax"
    .global _stage2_start
    .extern _bios_main
    .extern __bss_start
    .extern __bss_end

    .set CODE_SELECTOR, 0x08
    .set DATA_SELECTOR, 0x10
    .set BOOT_INFO_MAGIC, 0xa067b105
    .set MINIMUM_EXTENDED_KIB, 1024
    .set PROTECTED_STACK_TOP, 0x00090000

_stage2_start:
    cli
    /* Stage 1 enters at 1000:0000. Keep every real-mode reference relative
       to that segment; 32-bit linear offsets exceed the 64 KiB DS limit. */
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    sti

    /* Keep the protected-mode stack below reported conventional memory. */
    int $0x12
    cmpw $576, %ax
    jb _stage2_memory_error

    /* The 640x360 render target occupies 0x100000..0x1e0fff. */
    movb $0x88, %ah
    int $0x15
    jc _stage2_memory_error
    cmpw $MINIMUM_EXTENDED_KIB, %ax
    jb _stage2_memory_error

    call _vbe_select_mode
    jc _stage2_video_error

    /* IRQ1 must not consume the 8042 output-port response while the bounded
       A20 fallback owns the controller. Interrupts stay disabled in protected
       mode; the diagnostic path explicitly reenables them for BIOS teletype. */
    cli
    call _ensure_a20
    jc _stage2_a20_error
    call _disable_nmi

    movw %cs, %ax
    movw %ax, %ds
    movw $(_gdt_descriptor - _stage2_start), %di
    lgdtl (%di)
    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0
    ljmpl $CODE_SELECTOR, $_protected_mode_entry

_stage2_memory_error:
    movw %cs, %ax
    movw %ax, %ds
    movw $(_memory_error_message - _stage2_start), %si
    jmp _print_and_halt

_stage2_video_error:
    movw %cs, %ax
    movw %ax, %ds
    movw $(_video_error_message - _stage2_start), %si
    jmp _print_and_halt

_stage2_a20_error:
    sti
    movw %cs, %ax
    movw %ax, %ds
    movw $(_a20_error_message - _stage2_start), %si

_print_and_halt:
    movw %cs, %ax
    movw %ax, %ds
1:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0e, %ah
    movw $0x0007, %bx
    int $0x10
    /* BIOS calls are not trusted to preserve data segments. */
    pushw %cs
    popw %ds
    jmp 1b
2:
    cli
3:
    hlt
    jmp 3b

/* Return CF clear after selecting and activating a usable 32-bit VBE mode. */
_vbe_select_mode:
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw $(_vbe_controller_info - _stage2_start), %di
    movl $0x32454256, (%di)        /* "VBE2" */
    movw $0x4f00, %ax
    int $0x10
    cmpw $0x004f, %ax
    jne _vbe_failed

    movw %cs, %ax
    movw %ax, %ds
    movw $(_vbe_controller_info - _stage2_start), %di
    cmpw $0x0200, 4(%di)
    jb _vbe_failed

    /* VBE returns a ROM far pointer. Keep it as segment:offset instead of
       flattening it into an out-of-range real-mode DS offset. */
    movw 14(%di), %ax
    movw $(_mode_list_offset - _stage2_start), %di
    movw %ax, (%di)
    movw $(_vbe_controller_info - _stage2_start), %di
    movw 16(%di), %ax
    movw $(_mode_list_segment - _stage2_start), %di
    movw %ax, (%di)
    movw $(_modes_remaining - _stage2_start), %di
    movw $1024, (%di)
    movw $(_best_mode - _stage2_start), %di
    movw $0xffff, (%di)
    movw $(_best_area - _stage2_start), %di
    movl $0xffffffff, (%di)

_vbe_mode_loop:
    movw %cs, %ax
    movw %ax, %ds
    movw $(_modes_remaining - _stage2_start), %di
    cmpw $0, (%di)
    je _vbe_finish_selection
    decw (%di)

    movw $(_mode_list_offset - _stage2_start), %di
    movw 0(%di), %si
    movw 2(%di), %dx
    movw %dx, %fs
    movw %fs:(%si), %cx
    cmpw $0xffff, %cx
    je _vbe_finish_selection
    addw $2, %si
    jnc 1f
    addw $0x1000, %dx
    jc _vbe_failed
1:
    movw %si, 0(%di)
    movw %dx, 2(%di)

    movw $(_candidate_mode - _stage2_start), %di
    movw %cx, (%di)
    call _vbe_consider_candidate
    jmp _vbe_mode_loop

_vbe_finish_selection:
    movw %cs, %ax
    movw %ax, %ds
    movw $(_best_mode - _stage2_start), %di
    movw (%di), %bx
    cmpw $0xffff, %bx
    je _vbe_failed
    orw $0x4000, %bx              /* request linear framebuffer */
    movw $0x4f02, %ax
    int $0x10
    cmpw $0x004f, %ax
    jne _vbe_failed

    movw %cs, %ax
    movw %ax, %ds
    movw $(_bios_boot_info - _stage2_start), %di
    movl $BOOT_INFO_MAGIC, 0(%di)
    clc
    ret

_vbe_failed:
    stc
    ret

/* Query and retain the smallest compatible direct-color mode. */
_vbe_consider_candidate:
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw $(_vbe_mode_info - _stage2_start), %di
    movw $(_candidate_mode - _stage2_start), %si
    movw (%si), %cx
    movw $0x4f01, %ax
    int $0x10
    cmpw $0x004f, %ax
    jne _candidate_done

    movw %cs, %ax
    movw %ax, %ds
    movw $(_vbe_mode_info - _stage2_start), %si

    movw 0(%si), %ax
    andw $0x0091, %ax             /* available, graphics, linear */
    cmpw $0x0091, %ax
    jne _candidate_done
    cmpb $32, 25(%si)
    jne _candidate_done
    cmpb $6, 27(%si)              /* direct-color memory model */
    jne _candidate_done
    cmpw $640, 18(%si)
    jb _candidate_done
    cmpw $360, 20(%si)
    jb _candidate_done

    movzwl 18(%si), %edx
    shll $2, %edx
    movzwl 16(%si), %eax
    cmpl %edx, %eax
    jb _candidate_done
    cmpl $0x00200000, 40(%si)
    jb _candidate_done             /* keep clear of stage2/stack/render RAM */

    cmpb $8, 31(%si)
    jne _candidate_done
    cmpb $8, 33(%si)
    jne _candidate_done
    cmpb $8, 35(%si)
    jne _candidate_done
    cmpb $8, 34(%si)
    jne _candidate_done

    cmpb $16, 32(%si)
    jne 1f
    cmpb $0, 36(%si)
    jne _candidate_done
    jmp 2f
1:
    cmpb $0, 32(%si)
    jne _candidate_done
    cmpb $16, 36(%si)
    jne _candidate_done
2:
    movzwl 18(%si), %eax
    movzwl 20(%si), %edx
    imull %edx, %eax
    movw $(_best_area - _stage2_start), %di
    cmpl (%di), %eax
    jae _candidate_done
    movl %eax, (%di)

    movw $(_candidate_mode - _stage2_start), %di
    movw (%di), %ax
    movw $(_best_mode - _stage2_start), %di
    movw %ax, (%di)

    movw $(_bios_boot_info - _stage2_start), %di
    movl 40(%si), %eax
    movl %eax, 4(%di)
    movw 18(%si), %ax
    movw %ax, 8(%di)
    movw 20(%si), %ax
    movw %ax, 10(%di)
    movw 16(%si), %ax
    movw %ax, 12(%di)
    movb 25(%si), %al
    movb %al, 14(%di)
    movb 32(%si), %al
    movb %al, 15(%di)
    movb 34(%si), %al
    movb %al, 16(%di)
    movb 36(%si), %al
    movb %al, 17(%di)

_candidate_done:
    ret

/*
 * Return CF clear only after proving that addresses 0x000500 and 0x100500
 * no longer alias.  The first address is immediately above the BIOS data
 * area; the second is inside the not-yet-used software framebuffer.  Both
 * bytes are restored before returning.
 */
_check_a20:
    pushfw
    cli
    pushw %ds
    pushw %es
    pushw %si
    pushw %di
    pushw %bx

    xorw %ax, %ax
    movw %ax, %es
    movw $0xffff, %ax
    movw %ax, %ds
    movw $0x0500, %di
    movw $0x0510, %si

    movb %es:(%di), %al
    movb (%si), %bl
    movb $0x00, %es:(%di)
    movb $0xff, (%si)
    xorw %dx, %dx
    cmpb $0xff, %es:(%di)
    je 1f
    movw $1, %dx
1:
    movb %bl, (%si)
    movb %al, %es:(%di)

    popw %bx
    popw %di
    popw %si
    popw %es
    popw %ds
    popfw
    movw %dx, %ax
    ret

_enable_a20_fast:
    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92
    ret

_wait_8042_input_clear:
    pushw %cx
    movw $0xffff, %cx
1:
    inb $0x64, %al
    testb $0x02, %al
    jz 2f
    loop 1b
    popw %cx
    stc
    ret
2:
    popw %cx
    clc
    ret

_wait_8042_output_full:
    pushw %cx
    movw $0xffff, %cx
1:
    inb $0x64, %al
    testb $0x01, %al
    jnz 2f
    loop 1b
    popw %cx
    stc
    ret
2:
    popw %cx
    clc
    ret

_drain_8042_output:
    pushw %cx
    movw $0x0100, %cx
1:
    inb $0x64, %al
    testb $0x01, %al
    jz 2f
    inb $0x60, %al
    loop 1b
    popw %cx
    stc
    ret
2:
    popw %cx
    clc
    ret

/* Fall back to the legacy keyboard-controller output port, with bounded waits. */
_enable_a20_8042:
    pushw %bx
    xorw %bx, %bx
    call _wait_8042_input_clear
    jc 2f
    movb $0xad, %al
    outb %al, $0x64
    movb $1, %bh
    call _wait_8042_input_clear
    jc 1f
    call _drain_8042_output
    jc 1f
    movb $0xd0, %al
    outb %al, $0x64
    call _wait_8042_output_full
    jc 1f
    inb $0x60, %al
    orb $0x02, %al
    movb %al, %bl
    call _wait_8042_input_clear
    jc 1f
    movb $0xd1, %al
    outb %al, $0x64
    call _wait_8042_input_clear
    jc 1f
    movb %bl, %al
    outb %al, $0x60
    call _wait_8042_input_clear
    jc 1f
    movb $0xae, %al
    outb %al, $0x64
    call _wait_8042_input_clear
    jc 2f
    popw %bx
    clc
    ret
1:
    cmpb $0, %bh
    je 2f
    call _wait_8042_input_clear
    jc 2f
    movb $0xae, %al
    outb %al, $0x64
2:
    popw %bx
    stc
    ret

_ensure_a20:
    call _check_a20
    testw %ax, %ax
    jnz 1f
    call _enable_a20_fast
    call _check_a20
    testw %ax, %ax
    jnz 1f
    call _enable_a20_8042
    call _check_a20
    testw %ax, %ax
    jz 2f
1:
    clc
    ret
2:
    stc
    ret

_disable_nmi:
    inb $0x70, %al
    orb $0x80, %al
    outb %al, $0x70
    ret

    .code32
_protected_mode_entry:
    movw $DATA_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $PROTECTED_STACK_TOP, %esp
    xorl %ebp, %ebp
    cld

    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    movl %ecx, %edx
    shrl $2, %ecx
    rep stosl
    movl %edx, %ecx
    andl $3, %ecx
    rep stosb

    call _bios_main
    cli
1:
    hlt
    jmp 1b

    .balign 8
_gdt_start:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff   /* flat 32-bit code */
    .quad 0x00cf92000000ffff   /* flat 32-bit data */
_gdt_end:

_gdt_descriptor:
    .word _gdt_end - _gdt_start - 1
    .long _gdt_start

_memory_error_message:
    .asciz "AURA 67 requires 2 MiB RAM"
_video_error_message:
    .asciz "AURA 67 requires VBE 2.0 640x360x32 LFB"
_a20_error_message:
    .asciz "AURA 67 could not enable the A20 address line"

    /* Real-mode writable state shares the entry section so GAS can resolve
       every 16-bit offset relative to _stage2_start without a truncating PE
       relocation. Stage 1 loads these initialized bytes read-only from its
       own image; they are only mutated in RAM. */
    .section .text.stage2_entry,"ax"
    .global _bios_boot_info
_bios_boot_info:
    .long 0                       /* magic */
    .long 0                       /* physical framebuffer */
    .word 0                       /* width */
    .word 0                       /* height */
    .word 0                       /* pitch */
    .byte 0                       /* bpp */
    .byte 0                       /* red position */
    .byte 0                       /* green position */
    .byte 0                       /* blue position */

_best_area:
    .long 0xffffffff
_mode_list_offset:
    .word 0
_mode_list_segment:
    .word 0
_best_mode:
    .word 0xffff
_candidate_mode:
    .word 0xffff
_modes_remaining:
    .word 0

    /* VBE scratch buffers must also remain in the first 64 KiB segment. They
       occupy zero-filled bytes in stage2 rather than a separately relocated
       BSS section, keeping their offsets link-independent and unambiguous. */
    .balign 16
_vbe_controller_info:
    .skip 512
    .balign 16
_vbe_mode_info:
    .skip 256
