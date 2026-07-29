OUTPUT_FORMAT(pei-i386)
ENTRY(_stage2_start)

STAGE2_BASE = 0x00010000;
STAGE2_BYTES = 127 * 512;

SECTIONS
{
    . = STAGE2_BASE;
    __stage2_load_start = .;

    .text : ALIGN(16)
    {
        KEEP(*(.text.stage2_entry))
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
        *(.rdata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    __stage2_code_end = .;
    /* Real mode runs with DS=CS=0x1000, so the VBE scratch buffers must live
       below offset 0xfe00. Stage 1 already loads this raw-image padding as
       zeroes; protected mode clears the range again before entering C++. */
    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss.low*)
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    ASSERT(__bss_end - __stage2_load_start <= STAGE2_BYTES,
           "stage2 code, data and BSS exceed the 127-sector real-mode window")
    ASSERT(__bss_end - STAGE2_BASE <= 0xfe00,
           "stage2 real-mode offsets must remain below 0xfe00")
    ASSERT(__bss_end < 0x00080000,
           "stage2 BSS must remain clear of the 0x90000 protected-mode stack")

    /* objcopy --pad-to uses this same end address for the raw artifact. */
    . = STAGE2_BASE + STAGE2_BYTES;
    __stage2_load_end = .;

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
        *(.gcc_except_table*)
        *(.pdata*)
        *(.xdata*)
        *(.drectve*)
        *(.idata*)
        *(.edata*)
        *(.reloc*)
    }
}
