/* Switches to the emergency stack and page table, then halts. */
.text
.globl emergency_enter
.type emergency_enter,@function
emergency_enter:
    cli
    cld
    movl 4(%esp), %eax
    movl 8(%esp), %ebx
    movl 12(%esp), %ecx
    movl 16(%esp), %edx
    movl 20(%esp), %esi
    movw $0x10, %di
    movw %di, %ds
    movw %di, %es
    movw %di, %ss
    movl $emergency_stack+8192, %esp

    movl %cr0, %edi
    testl $0x80000000, %edi
    jz 1f
    movl %cr4, %edi
    andl $~0x80, %edi
    movl %edi, %cr4
    movl $emergency_pd, %edi
    movl %edi, %cr3
1:
    subl $12, %esp
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call emergency_render
2:  cli
    hlt
    jmp 2b
.size emergency_enter,.-emergency_enter

.globl emergency_df_entry
.type emergency_df_entry,@function
emergency_df_entry:
    cld
    andl $-16, %esp
    call emergency_double_fault
    jmp 2b
.size emergency_df_entry,.-emergency_df_entry
