OUTPUT_FORMAT(pei-i386)
ENTRY(_cdstub_start)

SECTIONS
{
    . = 0;
    .boot :
    {
        KEEP(*(.text.cdstub))
    }

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}

ASSERT(SIZEOF(.boot) == 512, "the CD stub must be exactly 512 bytes")
