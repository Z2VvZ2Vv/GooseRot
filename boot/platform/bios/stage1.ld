OUTPUT_FORMAT(pei-i386)
ENTRY(_stage1_start)

SECTIONS
{
    . = 0;
    .boot :
    {
        KEEP(*(.text.stage1))
    }

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}

ASSERT(SIZEOF(.boot) == 512, "stage1 must be exactly 512 bytes")
