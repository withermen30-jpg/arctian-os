; ============================================
; ARCTIAN STAGE 2 (16->32->64 bit)
; Arctian
; 0x1000:0x0000 adresinde calisir
; Page tables + 64-bit gecis + kernel yukleme
; ============================================

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
    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xF000

    ; VBE ile grafik modu kur (QEMU / VirtualBox / gercek donanim).
    call setup_vesa


    ; A20 kapisi
    call enable_a20

    ; GDT yukle
    lgdt [gdt32_ptr]

    ; 32-bit protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword gdt32_code:0x10000 + protected_mode_start

; 16-bit functions
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

    ; Boot-info alanini sifirla.
    xor ax, ax
    mov ds, ax
    mov dword [0x7000], 0

    ; ---- 1) QEMU/Bochs hizli yol: mod 0x144 ----
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
    ; ---- 2) Standart VBE sorgu (VirtualBox / gercek donanim) ----
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov di, 0x5000
    mov dword [es:di], 0x32454256       ; 'VBE2'
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

    mov word [0x51F4], 0xFFFF
    mov word [0x51F6], 0

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
    je .mode_pick

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

    mov ax, [0x5200]                    ; attributes
    test ax, 0x0081                     ; destekli + LFB
    jz .mode_loop
    cmp byte [0x5219], 32               ; 32bpp
    jne .mode_loop
    cmp word [0x5212], 1024
    jb .mode_loop
    cmp word [0x5214], 768
    jb .mode_loop

    mov dx, 1
    cmp word [0x5212], 1920
    jne .score_ratio
    cmp word [0x5214], 1080
    jne .score_ratio
    mov dx, 3
    jmp .score_cmp
.score_ratio:
    mov ax, [0x5212]
    mov bx, 9
    mul bx
    mov si, ax
    mov ax, [0x5214]
    mov bx, 16
    mul bx
    cmp si, ax
    jne .score_cmp
    mov dx, 2
.score_cmp:
    cmp dx, [0x51F6]
    jbe .mode_loop
    mov [0x51F6], dx
    mov [0x51F4], cx
    jmp .mode_loop

.mode_pick:
    mov cx, [0x51F4]
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
    jne .done

    mov bx, [0x51F4]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    xor bx, bx
    mov ds, bx
    cmp ax, 0x004F
    jne .done

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

; ============================================
; 32-BIT
; ============================================
[BITS 32]
protected_mode_start:
    mov ax, gdt32_data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    ; PAE paging - page tables fiziksel adres = 0x10000 + (offset - ORG)
    ; stage2 0x1000 segmentinde = 0x10000 fiziksel adres
    ; page_table_l4 label = offset from start of stage2 binary
    ; Fiziksel adres = 0x10000 + (page_table_l4 - 0x0000)
    call setup_paging

    ; IA32_EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; PAE + OSFXSR + OSXMMEXCPT (SSE kullanimi icin)
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; 64-bit GDT yukle. Protected mode'da DS flat oldugu icin
    ; pseudo-descriptor adresini fiziksel adres olarak vermeliyiz.
    lgdt [0x10000 + gdt64_ptr]

    ; CR3
    mov eax, page_table_l4
    add eax, 0x10000
    mov cr3, eax

    ; Paging enable
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; 64-bit
    jmp gdt64_code:0x10000 + long_mode_start

[BITS 32]
setup_paging:
    ; L4 + L3 + L2 = 3 pages
    mov edi, page_table_l4
    add edi, 0x10000
    xor eax, eax
    mov ecx, (4096 * 3) / 4      ; L4 + L3 + L2
    rep stosd

    ; L4[0] -> L3
    mov edi, page_table_l4
    add edi, 0x10000
    mov eax, page_table_l3
    add eax, 0x10000
    or eax, 0x3
    mov [edi], eax

    ; L3[0] -> L2
    mov edi, page_table_l3
    add edi, 0x10000
    mov eax, page_table_l2
    add eax, 0x10000
    or eax, 0x3
    mov [edi], eax

    ; L2[0..511] = 512 x 2MB = 1GB identity map.
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

; ============================================
; 64-BIT
; ============================================
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

    ; Kernel binary, stage2'nin hemen arkasinda (LBA 1+offset)
    ; Stage2 boyutu bilinmiyor, kernel stage2'den sonra
    ; build_img.py: stage2 + kernel = tek bir binary ("stage2.bin")
    ;   LBA 0 = boot.bin (512 byte)
    ;   LBA 1..N = stage2 + kernel (birlesik)
    ; Stage2 kendi boyutunu bilmeli.
    ;
    ; Ama daha basit: kernel ayri bir LBA bolgesinde.
    ; build_img.py soyle yapacak:
    ;   LBA 0 = boot.bin (512 byte)
    ;   LBA 1..N = stage2.bin (stage2 + page tables)
    ;   LBA N+1.. = kernel.bin
    ;
    ; Stage2, kernel'i diskten okuyup 0x100000'e kopyalayamaz
    ; cunku INT 0x13 BIOS call'lari real mode gerektirir ve
    ; biz zaten 64-bit modundayiz.
    ;
    ; Bunun yerine: stage2, kernel'i 0x100000'e KOPYALAR
    ; (hafizada zaten var, cunku LBA 1'den itibaren
    ; stage2 + kernel ayni buffer'da)
    ;
    ; stage2_size_bytes = (stage2_end - start)
    ; kernel_start_phys = 0x10000 + stage2_size_bytes
    ; kernel_start_phys = 0x10000 + (stage2_end - 0x0000)

    mov rsi, 0x10000                     ; stage2 basi = 0x1000:0x0000
    add rsi, [rel stage2_size_bytes]     ; kernel basi (stage2'den hemen sonra)
    mov rdi, 0x100000                    ; kernel hedef adresi
    mov rcx, [rel kernel_size_bytes]
    shr rcx, 2                           ; /4 (dword)
    rep movsd

    ; Kernel'e atla
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    mov rax, 0x100000
    jmp rax

.halt:
    cli
    hlt
    jmp .halt

; ============================================
; PAGE TABLES
; ============================================
align 4096
page_table_l4:
    times 4096 db 0
page_table_l3:
    times 4096 db 0
page_table_l2:
    times 4096 db 0

; ============================================
; GDT
; ============================================
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

; ============================================
; BOYUT BILGILERI (link sirasinda doldurulacak)
; ============================================
stage2_size_bytes: dq stage2_end - start
kernel_size_bytes: dq 0    ; build_img.py bu degeri dolduracak

; Stage2'nin bittigi yer
stage2_end:
