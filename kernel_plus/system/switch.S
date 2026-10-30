.text

.globl arctian_sched_switch
.type arctian_sched_switch, @function
arctian_sched_switch:
    pushfq
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    fxsave (%rdx)
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    fxrstor (%rcx)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popfq
    ret
.size arctian_sched_switch, .-arctian_sched_switch

.globl arctian_task_entry
.type arctian_task_entry, @function
arctian_task_entry:
    movq %r13, %rdi
    call *%r12
    call sched_exit
    hlt
    jmp arctian_task_entry
.size arctian_task_entry, .-arctian_task_entry

.section .note.GNU-stack,"",@progbits
