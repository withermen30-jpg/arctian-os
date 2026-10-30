; ============================================
; ARCTIAN ISR/IRQ STUBS (64-bit)
; Her exception (0-31) ve IRQ (32-47) icin stub
; ============================================

[BITS 64]

extern isr_handler

; ============================================
; ORTAK STUB: tum registerlari kaydet,
; C handler'i cagir, geri yukle, iretq
; ============================================
isr_common_stub:
    ; Push in reverse order of registers_t struct
    ; (first push = highest offset after all pushes)
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
    push r15     ; RSP now points to r15 = offset 0 of registers_t

    ; rdi = pointer to registers_t
    mov rdi, rsp
    call isr_handler

    ; Pop registers in reverse
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

    ; Skip int_no and err_code
    add rsp, 16
    iretq

; ============================================
; Makro: error code'suz exception/IRQ'lar
; CPU sadece RIP, CS, RFLAGS push'lar
; Biz: push 0 (dummy err_code), push int_no
; ============================================
%macro ISR_NOERROR 1
global isr_stub_%1
isr_stub_%1:
    push 0            ; dummy err_code
    push %1           ; int_no
    jmp isr_common_stub
%endmacro

; ============================================
; Makro: error code'u OLAN exception'lar
; CPU zaten error code push'lamis
; Biz: sadece int_no push'la
; Stack: int_no, err_code(CPU), RIP, CS, RFLAGS
; ============================================
%macro ISR_ERROR 1
global isr_stub_%1
isr_stub_%1:
    push %1           ; int_no (error code already below by CPU)
    jmp isr_common_stub
%endmacro

; ============================================
; Exception 0-31
; ============================================
ISR_NOERROR 0     ; Division By Zero
ISR_NOERROR 1     ; Debug
ISR_NOERROR 2     ; Non Maskable Interrupt
ISR_NOERROR 3     ; Breakpoint
ISR_NOERROR 4     ; Overflow
ISR_NOERROR 5     ; Bound Range Exceeded
ISR_NOERROR 6     ; Invalid Opcode
ISR_NOERROR 7     ; Device Not Available
ISR_ERROR   8     ; Double Fault (HAS error code)
ISR_NOERROR 9     ; Coprocessor Segment Overrun
ISR_ERROR   10    ; Invalid TSS (HAS error code)
ISR_ERROR   11    ; Segment Not Present (HAS error code)
ISR_ERROR   12    ; Stack-Segment Fault (HAS error code)
ISR_ERROR   13    ; General Protection Fault (HAS error code)
ISR_ERROR   14    ; Page Fault (HAS error code)
ISR_NOERROR 15    ; Reserved
ISR_NOERROR 16    ; x87 FPU Error
ISR_ERROR   17    ; Alignment Check (HAS error code)
ISR_NOERROR 18    ; Machine Check
ISR_NOERROR 19    ; SIMD Floating-Point Exception
ISR_NOERROR 20    ; Virtualization Exception
ISR_ERROR   21    ; Control Protection (HAS error code)
ISR_NOERROR 22    ; Reserved
ISR_NOERROR 23    ; Reserved
ISR_NOERROR 24    ; Reserved
ISR_NOERROR 25    ; Reserved
ISR_NOERROR 26    ; Reserved
ISR_NOERROR 27    ; Reserved
ISR_NOERROR 28    ; Hypervisor Injection
ISR_ERROR   29    ; VMM Communication (HAS error code)
ISR_ERROR   30    ; Security Exception (HAS error code)
ISR_NOERROR 31    ; Reserved

; ============================================
; IRQ 32-47 (remapped from PIC IRQ0-15)
; ============================================
ISR_NOERROR 32    ; IRQ0: Timer
ISR_NOERROR 33    ; IRQ1: Keyboard
ISR_NOERROR 34    ; IRQ2: Cascade
ISR_NOERROR 35    ; IRQ3: COM2
ISR_NOERROR 36    ; IRQ4: COM1
ISR_NOERROR 37    ; IRQ5: LPT2
ISR_NOERROR 38    ; IRQ6: Floppy
ISR_NOERROR 39    ; IRQ7: LPT1
ISR_NOERROR 40    ; IRQ8: CMOS RTC
ISR_NOERROR 41    ; IRQ9: Free
ISR_NOERROR 42    ; IRQ10: Free
ISR_NOERROR 43    ; IRQ11: Free
ISR_NOERROR 44    ; IRQ12: Mouse
ISR_NOERROR 45    ; IRQ13: FPU
ISR_NOERROR 46    ; IRQ14: Primary ATA
ISR_NOERROR 47    ; IRQ15: Secondary ATA

; ============================================
; System call (int 0x80)
; ============================================
ISR_NOERROR 128

; ============================================
; Stub tablosu (C tarafindan erisilecek)
; 48 entry: [0-31] exceptions, [32-47] IRQs
; ============================================
section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr_stub_%+i
%assign i i+1
%endrep
