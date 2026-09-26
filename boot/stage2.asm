
[BITS 16]
[ORG 0x0000]

start:
    cli
    mov dx, 0x3F8
    mov al, 'S'
    out dx, al
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    call setup_vesa


    call enable_a20

    lgdt [gdt32_ptr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword gdt32_code:0x10000 + protected_mode_start

enable_a20:
    pusha
    call .w
    mov al, 0xAD
    out 0x64, al
    call .w
    mov al, 0xD0
    out 0x64, al
    call .w2
    in al, 0x60
    push eax
    call .w
    mov al, 0xD1
    out 0x64, al
    call .w
    pop eax
    or al, 2
    out 0x60, al
    call .w
    mov al, 0xAE
    out 0x64, al
    call .w
    popa
    ret
.w:
    in al, 0x64
    test al, 2
    jnz .w
    ret
.w2:
    in al, 0x64
    test al, 1
    jz .w2
    ret

setup_vesa:
    pusha
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov dword [0x7000], 0

    mov es, ax
    mov di, 0x8000
    mov ax, 0x4F01
    mov cx, 0x144
    int 0x10
    cmp ax, 0x004F
    jne .try_vbe
    test word [es:di], 0x0080
    jz .try_vbe
    mov ax, 0x4F02
    mov bx, 0x4144
    int 0x10
    cmp ax, 0x004F
    jne .try_vbe
    mov dword [0x7000], 0x41534556
    mov eax, [es:di + 0x28]
    mov [0x7004], eax
    mov ax, [es:di + 0x10]
    mov [0x7008], ax
    mov ax, [es:di + 0x12]
    mov [0x700C], ax
    mov ax, [es:di + 0x14]
    mov [0x700E], ax
    mov al, [es:di + 0x19]
    mov [0x7010], al
    jmp .done

.try_vbe:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, 0x5000
    mov dword [es:di], 0x32454256
    mov ax, 0x4F00
    int 0x10
    xor bx, bx
    mov ds, bx
    cmp ax, 0x004F
    jne .done

    mov ax, [0x500E]
    mov [0x51F0], ax
    mov ax, [0x5010]
    mov [0x51F2], ax

.mode_loop:
    xor ax, ax
    mov ds, ax
    mov ax, [0x51F2]
    mov es, ax
    mov si, [0x51F0]
    mov cx, [es:si]
    add si, 2
    mov [0x51F0], si
    cmp cx, 0xFFFF
    je .done

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, 0x5200
    mov ax, 0x4F01
    int 0x10
    xor bx, bx
    mov ds, bx
    cmp ax, 0x004F
    jne .mode_loop

    mov ax, [0x5200]
    test ax, 0x0081
    jz .mode_loop
    cmp byte [0x5219], 32
    jne .mode_loop
    cmp word [0x5212], 1024
    jb .mode_loop
    cmp word [0x5214], 768
    jb .mode_loop

    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    xor bx, bx
    mov ds, bx
    cmp ax, 0x004F
    jne .mode_loop

    mov dword [0x7000], 0x41534556
    mov eax, [0x5228]
    mov [0x7004], eax
    mov ax, [0x5210]
    mov [0x7008], ax
    mov ax, [0x5212]
    mov [0x700C], ax
    mov ax, [0x5214]
    mov [0x700E], ax
    mov al, [0x5219]
    mov [0x7010], al

.done:
    pop es
    pop ds
    popa
    ret

[BITS 32]
protected_mode_start:
    mov ax, gdt32_data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    call setup_paging

    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    lgdt [0x10000 + gdt64_ptr]

    mov eax, page_table_l4
    add eax, 0x10000
    mov cr3, eax

    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    jmp gdt64_code:0x10000 + long_mode_start

[BITS 32]
setup_paging:
    mov edi, page_table_l4
    add edi, 0x10000
    xor eax, eax
    mov ecx, (4096 * 3) / 4
    rep stosd

    mov edi, page_table_l4
    add edi, 0x10000
    mov eax, page_table_l3
    add eax, 0x10000
    or eax, 0x3
    mov [edi], eax

    mov edi, page_table_l3
    add edi, 0x10000
    mov eax, page_table_l2
    add eax, 0x10000
    or eax, 0x3
    mov [edi], eax

    mov edi, page_table_l2
    add edi, 0x10000
    mov eax, 0x83
    xor ecx, ecx
.l2:
    mov [edi + ecx*8], eax
    add eax, 0x200000
    inc ecx
    cmp ecx, 512
    jne .l2

    ret

[BITS 64]
long_mode_start:
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    mov ax, gdt64_data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x8FF00


    mov rsi, 0x10000
    add rsi, [rel stage2_size_bytes]
    mov rdi, 0x100000
    mov rcx, [rel kernel_size_bytes]
    shr rcx, 2
    rep movsd

    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    mov rax, 0x100000
    jmp rax

.halt:
    cli
    hlt
    jmp .halt

align 4096
page_table_l4:
    times 4096 db 0
page_table_l3:
    times 4096 db 0
page_table_l2:
    times 4096 db 0

align 8
gdt32_start:
gdt32_null: dq 0
gdt32_code: equ $ - gdt32_start
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
gdt32_data: equ $ - gdt32_start
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt32_ptr:
    dw $ - gdt32_start - 1
    dd 0x10000 + gdt32_start

align 8
gdt64_start:
gdt64_null: dq 0
gdt64_code: equ $ - gdt64_start
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10011010b
    db 10101111b
    db 0x00
gdt64_data: equ $ - gdt64_start
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10010010b
    db 00000000b
    db 0x00
gdt64_ptr:
    dw $ - gdt64_start - 1
    dq 0x10000 + gdt64_start

stage2_size_bytes: dq stage2_end - start
kernel_size_bytes: dq 0

stage2_end:
