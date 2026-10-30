; ============================================
; ARCTIAN Ring3 kullanici payload + CPL3 gecis
; ============================================

[BITS 64]

global ring3_payload_start
global ring3_payload_end
global enter_user

section .text

; void enter_user(uint64_t rip [rdi], uint64_t rsp [rsi], uint64_t rflags [rdx])
enter_user:
    mov ax, 0x1b
    mov ds, ax
    mov es, ax
    cli
    push 0x1b
    push rsi
    push rdx
    push 0x23
    push rdi
    iretq

ring3_payload_start:
    lea rsi, [rel ring3_msg]
    mov edx, ring3_msg_len
    mov edi, 1
    mov eax, 1
    int 0x80
    mov dx, 0x3F8
    mov al, 'X'
    out dx, al
    mov eax, 0
    int 0x80
.hang:
    jmp .hang

ring3_msg:
    db "ring3: merhaba! syscall ABI calisti (CPL3)", 10
ring3_msg_len equ $ - ring3_msg
ring3_payload_end:

global ring3_file_start
global ring3_file_end
ring3_file_start:
    lea rdi, [rel ffname]
    xor esi, esi
    mov eax, 5
    int 0x80
    test eax, eax
    js .bad
    mov edi, eax
    lea rsi, [rel fbuf]
    mov edx, 96
    mov eax, 6
    int 0x80
    mov r13, rax
    mov edi, 1
    lea rsi, [rel fbuf]
    mov edx, r13d
    mov eax, 1
    int 0x80
    mov edi, 3
    mov eax, 7
    int 0x80
.bad:
    mov dx, 0x3F8
    mov al, 'Y'
    out dx, al
.hang2:
    jmp .hang2
ffname:
    db "syscall_test.txt", 0
fbuf:
    times 96 db 0
ring3_file_end:

global ring3_fault_start
global ring3_fault_end
ring3_fault_start:
    xor eax, eax
    mov [rax], rax
.hang3:
    jmp .hang3
ring3_fault_end:

