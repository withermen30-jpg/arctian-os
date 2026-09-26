[BITS 16]
[ORG 0x7C00]

start:
    cli
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 3
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    mov dx, 0x3F8
    mov al, '1'
    out dx, al

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov byte [0x7100], 2

    mov cx, [payload_words]

    mov ax, cx
    shl ax, 1
    sub ax, 2
    mov si, ax
    mov di, ax

    mov bx, 0x07E0
    mov ds, bx
    mov bx, 0x1000
    mov es, bx

    std
    rep movsw
    cld

    mov dx, 0x3F8
    mov al, '2'
    out dx, al
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    jmp 0x1000:0x0000

payload_words:
    dw 0x0000
magic:
    db 'PBYT'

times 510-($-$$) db 0
dw 0xAA55
