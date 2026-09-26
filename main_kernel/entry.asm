
[BITS 64]

section .text.entry
global kernel_main_asm
extern kernel_main
extern bss_start
extern bss_end

kernel_main_asm:
    mov rsp, stack_top

    mov rdi, bss_start
    mov rcx, bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
