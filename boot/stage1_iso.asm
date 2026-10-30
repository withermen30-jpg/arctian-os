; ============================================
; ARCTIAN STAGE1 (ISO, El Torito no-emulation)
; BIOS, tum boot imajini 0x7C00'e yukler.
; Bu kod stage2+main_kernel'i 0x10000'e tasir ve atlar.
; ============================================
[BITS 16]
[ORG 0x7C00]

start:
    cli
    ; COM1 init + '1' isareti
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
    mov byte [0x7100], 2          ; boot medya = CD

    ; payload uzunlugunu oku (ORG=0x7C00, ds=0 => mutlak 0x7Cxx)
    mov eax, [payload_bytes]

    ; Kaynak 0x7E00, hedef 0x10000 (fark 0x8200 => es = ds + 0x820).
    ; Payload >32KB olabildigi icin geriye dogru, 32KB'lik bloklar halinde.
    mov ebp, 0x7E00
    add ebp, eax
    mov edx, eax

.copy_loop:
    test edx, edx
    jz .done
    mov eax, 0x8000
    cmp eax, edx
    jbe .len_ok
    mov eax, edx
.len_ok:
    mov edi, ebp
    sub edi, eax
    mov ebx, edi
    shr ebx, 4
    mov ds, bx
    add bx, 0x820
    mov es, bx
    and edi, 0x0F
    mov si, di
    mov di, si
    mov ecx, eax
    shr ecx, 1
    cld
    rep movsw
    sub ebp, eax
    sub edx, eax
    jmp .copy_loop

.done:
    ; HDD yolunda stage1 diski 0x10000..0x90000'e okuyup ustunu sifirlar.
    ; ISO yolunda BIOS bu bolgeyi doldurmaz; bazi BIOS'lar (VirtualBox)
    ; kalinti birakir. Ayni durumu saglamak icin payload ustunu sifirla.
    mov bx, 0x3000
.clear_seg:
    mov es, bx
    xor di, di
    mov cx, 0x8000
    xor ax, ax
    cld
    rep stosw
    add bx, 0x1000
    cmp bx, 0x9000
    jb .clear_seg

    mov ax, 0x1000
    mov ds, ax
    mov es, ax

    mov dx, 0x3F8
    mov al, '2'
    out dx, al
    jmp 0x1000:0x0000

payload_bytes:
    dd 0x00000000
magic:
    db 'PBYT'

times 510-($-$$) db 0
dw 0xAA55
