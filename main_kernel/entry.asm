; ============================================
; ARCTIAN KERNEL ENTRY (64-bit)
; Arctian
; Bootloader'dan 0x100000 adresine yuklenir
; ============================================

[BITS 64]

section .text.entry
global kernel_main_asm
extern kernel_main
extern bss_start
extern bss_end

kernel_main_asm:
    ; Stack pointer'i ayarla (kernel + masaustu icin genis stack)
    mov rsp, stack_top

    ; BSS segmentini sifirla (linker.ld'deki bss_start/bss_end kullanilir)
    mov rdi, bss_start
    mov rcx, bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    ; C kernel_main'a git
    call kernel_main

    ; Donus olursa sonsuz dongu
.halt:
    cli
    hlt
    jmp .halt

; ============================================
; STACK alani (BSS icinde)
; ============================================
section .bss
align 16
stack_bottom:
    resb 524288     ; 512KB stack (LVGL + SVG + masaustu)
stack_top:
