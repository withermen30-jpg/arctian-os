ENTRY(desktop_main)

SECTIONS
{
    . = 0x00800000;

    .text : ALIGN(4K) { *(.text.entry) *(.text) *(.text.*) }
    .rodata : ALIGN(4K) { *(.rodata) *(.rodata.*) }
    .data : ALIGN(4K) { *(.data) *(.data.*) }
    .bss : ALIGN(4K)
    {
        bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
