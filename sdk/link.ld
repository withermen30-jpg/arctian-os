OUTPUT_FORMAT(elf64-x86-64)
ENTRY(wolf_main)

/* .wlf native uygulama yerlesimi.
 * Yukleyici kodu 0x03000000 adresine kopyalar ve entry_off'taki
 * `wolf_main(const wolf_api_t *api)` islevini cagirir.
 * Uygulama cekirdek/LVGL sembolu KULLANMAZ: yalnizca libWolf arayuzu.
 */
SECTIONS
{
    . = 0x03000000;

    .text : ALIGN(16) {
        *(.text.entry)
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(16) {
        *(.data)
        *(.data.*)
        *(.got)
        *(.got.plt)
    }

    .bss : ALIGN(16) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
