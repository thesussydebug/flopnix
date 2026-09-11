/* Saves and restores the registers used for fault recovery. */
.text
    .global fj_set
fj_set:
    movl 4(%esp), %eax
    movl %ebx,  0(%eax)
    movl %esi,  4(%eax)
    movl %edi,  8(%eax)
    movl %ebp, 12(%eax)
    leal 4(%esp), %ecx
    movl %ecx, 16(%eax)
    movl (%esp), %ecx
    movl %ecx, 20(%eax)
    xorl %eax, %eax
    ret

    .global fj_long
fj_long:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    movl  0(%edx), %ebx
    movl  4(%edx), %esi
    movl  8(%edx), %edi
    movl 12(%edx), %ebp
    movl 16(%edx), %esp
    movl 20(%edx), %ecx
    sti
    jmp *%ecx
