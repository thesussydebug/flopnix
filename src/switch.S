/* Saves one thread's stack and resumes another. */
.text
    .global thr_switch
thr_switch:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    movl %esp, (%eax)
    movl %edx, %esp
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret

    .global thr_bootstrap
thr_bootstrap:
    popl %eax
    sti
    call *%eax

    call thr_exit
1:  jmp 1b
