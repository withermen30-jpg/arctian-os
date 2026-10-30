; ============================================
; ARCTIAN Cokme Koruma - setjmp / longjmp (x86_64)
; Yapi duzeni include/arctian/recover.h ile birebir ayni olmalidir.
;
;   +0  rbx        +48 rsp
;   +8  rbp        +56 rip
;   +16 r12        +64 rflags
;   +24 r13
;   +32 r14
;   +40 r15
; ============================================

[BITS 64]

global arctian_setjmp
global arctian_longjmp

section .text

; int arctian_setjmp(arctian_recover_t *ctx)   [rdi]
arctian_setjmp:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rax, [rsp + 8]          ; call sonrasi yigin (donus adresi cikarilmis)
    mov [rdi + 48], rax
    mov rax, [rsp]              ; donus adresi = devam edilecek rip
    mov [rdi + 56], rax
    pushfq
    pop rax
    mov [rdi + 64], rax
    xor eax, eax
    ret

; void arctian_longjmp(arctian_recover_t *ctx, int code)   [rdi, esi]
arctian_longjmp:
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    mov eax, esi
    test eax, eax
    jnz .have_code
    mov eax, 1
.have_code:
    mov rcx, [rdi + 64]
    push rcx
    popfq
    jmp qword [rdi + 56]
