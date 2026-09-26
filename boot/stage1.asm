
[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    mov byte [0x7100], 0
    cmp dl, 0x80
    jb .src_ok
    mov byte [0x7100], 1
.src_ok:

    mov ax, 0x0003
    int 0x10
    mov ah, 0x01
    mov cx, 0x2607
    int 0x10

    mov si, msg_t
    call print
    call nl

    mov si, msg_b
    call print
    call delay
    call poke
    call nl

    mov si, msg_m
    call print
    call delay
    call poke
    call nl

    mov si, msg_d
    call print
    call delay
    call poke
    call nl

    mov si, msg_a
    call print
    call delay
    call poke
    call nl

    mov si, msg_v
    call print
    call delay
    call poke
    call nl

    mov si, msg_p
    call print
    call delay
    call poke
    call nl

    mov si, msg_k
    call print
    call dot
    call dot
    call dot

    cmp byte [boot_drive], 0x80
    jae .lba

    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    xor ch, ch
    xor dh, dh
    mov cl, 2
    mov bp, 1024
.chs:
    mov ax, 0x0201
    mov dl, [boot_drive]
    int 0x13
    jc .fail
    add bx, 512
    jnz .chs_n
    mov ax, es
    add ax, 0x1000
    mov es, ax
.chs_n:
    inc cl
    cmp cl, 19
    jb .chs_nx
    mov cl, 1
    inc dh
    cmp dh, 2
    jb .chs_nx
    xor dh, dh
    inc ch
.chs_nx:
    dec bp
    jnz .chs
    jmp .done

.lba:
    mov word [dap_seg], 0x1000
    mov word [dap_off], 0
    mov dword [dap_lba], 1
    mov bp, 16
.lba_l:
    mov word [dap_cnt], 64
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .fail
    add word [dap_seg], 0x0800
    add dword [dap_lba], 64
    dec bp
    jnz .lba_l

.done:
    call poke
    call nl
    mov si, msg_s
    call print
    call nl
    call nl
    call delay
    call delay

    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    jmp 0x1000:0x0000

.fail:
    call nl
    mov si, msg_f
    call print
    hlt
    jmp $

print:
    pusha
    mov ah, 0x0E
.l:
    lodsb
    test al, al
    jz .d
    int 0x10
    jmp .l
.d:
    popa
    ret

nl:
    pusha
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    popa
    ret

poke:
    pusha
    mov si, msg_ok
    call print
    popa
    ret

dot:
    pusha
    mov ah, 0x0E
    mov al, '.'
    int 0x10
    popa
    ret

delay:
    pusha
    mov cx, 0x0100
.ol:
    mov dx, 0xFFFF
.il:
    dec dx
    jnz .il
    loop .ol
    popa
    ret

msg_t:  db "Arctian x64", 0
msg_b:  db " [Boot]", 0
msg_m:  db " [Mem]", 0
msg_d:  db " [Disk]", 0
msg_a:  db " [A20]", 0
msg_v:  db " [VESA]", 0
msg_p:  db " [Paging]", 0
msg_k:  db " [Kernel]", 0
msg_ok: db "OK", 0
msg_f:  db " HATA", 0x0D, 0x0A, 0
msg_s:  db " -> Yuklendi!", 0

boot_drive: db 0
dap:
    db 0x10, 0
dap_cnt:
    dw 0
dap_off:
    dw 0
dap_seg:
    dw 0
dap_lba:
    dq 0

times 510-($-$$) db 0
dw 0xAA55
