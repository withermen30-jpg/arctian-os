
[BITS 64]

extern isr_handler

isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

%macro ISR_NOERROR 1
global isr_stub_%1
isr_stub_%1:
    push 0
    push %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERROR 1
global isr_stub_%1
isr_stub_%1:
    push %1
    jmp isr_common_stub
%endmacro

ISR_NOERROR 0
ISR_NOERROR 1
ISR_NOERROR 2
ISR_NOERROR 3
ISR_NOERROR 4
ISR_NOERROR 5
ISR_NOERROR 6
ISR_NOERROR 7
ISR_ERROR   8
ISR_NOERROR 9
ISR_ERROR   10
ISR_ERROR   11
ISR_ERROR   12
ISR_ERROR   13
ISR_ERROR   14
ISR_NOERROR 15
ISR_NOERROR 16
ISR_ERROR   17
ISR_NOERROR 18
ISR_NOERROR 19
ISR_NOERROR 20
ISR_ERROR   21
ISR_NOERROR 22
ISR_NOERROR 23
ISR_NOERROR 24
ISR_NOERROR 25
ISR_NOERROR 26
ISR_NOERROR 27
ISR_NOERROR 28
ISR_ERROR   29
ISR_ERROR   30
ISR_NOERROR 31

ISR_NOERROR 32
ISR_NOERROR 33
ISR_NOERROR 34
ISR_NOERROR 35
ISR_NOERROR 36
ISR_NOERROR 37
ISR_NOERROR 38
ISR_NOERROR 39
ISR_NOERROR 40
ISR_NOERROR 41
ISR_NOERROR 42
ISR_NOERROR 43
ISR_NOERROR 44
ISR_NOERROR 45
ISR_NOERROR 46
ISR_NOERROR 47

section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr_stub_%+i
%assign i i+1
%endrep
