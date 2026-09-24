; Sets up memory, video, and protected mode before kernel entry.
%define BI      0x7000
%define DIAG    (BI+16)
%define STAGE   (BI+17)
%define CFG     0x7100
%define VBEINFO 0x0500
%define MODEINFO 0x0800

section .stub progbits alloc exec nowrite align=16

[bits 16]

global _entry16
extern kmain
extern __bss_start
extern __bss_end

_entry16:
    jmp short entry_start
    times 4-($-$$) db 0
img_sum  dw 0
img_sect dw 0
    db 'FXK1'
    dd KERNEL_API
    db KERNEL_VERSION, 0
    times 40-($-$$) db 0

entry_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov byte [DIAG], 0
    mov byte [STAGE], 1
    mov si, msg_hdr
    call sputs

    mov di, [img_sum]
    mov word [img_sum], 0
    mov bp, [img_sect]
    xor ax, ax
    mov bx, 0x0800
.cs_sect:
    mov ds, bx
    xor si, si
    mov cx, 256
.cs_word:
    rol ax, 1
    add ax, [si]
    add si, 2
    loop .cs_word
    add bx, 32
    dec bp
    jnz .cs_sect
    xor bx, bx
    mov ds, bx
    cmp ax, di
    je .cs_ok
    mov si, msg_badimg
    jmp halt_msg
.cs_ok:
    mov [img_sum], di
    mov si, msg_imgok
    call sputs

    mov [boot_drv], dl
    call wd_install

    push ds
    mov ax, 0x27E0
    mov ds, ax
    xor si, si
    mov ax, 0x0710
    mov es, ax
    xor di, di
    mov cx, 256
    rep movsw
    pop ds
    xor ax, ax
    mov es, ax
    or byte [DIAG], 0x01
    mov byte [STAGE], 2

    mov bx, .font_hung
    mov cx, 37
    call wd_arm
    push ds
    mov ax, 0x1130
    mov bh, 6
    int 0x10
    mov si, bp
    mov ax, es
    mov ds, ax
    mov ax, 0x0500
    mov es, ax
    xor di, di
    mov cx, 2048
    rep movsw
    pop ds
    call wd_off
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, msg_font
    call sputs
    jmp .font_done
.font_hung:
    or byte [DIAG], 0x84
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, msg_fonthang
    call sputs
.font_done:
    mov byte [STAGE], 3

    mov bx, .mem_hung
    mov cx, 37
    call wd_arm
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .mem88
    cmp ax, 0
    jne .memax
    mov ax, cx
    mov bx, dx
.memax:

    cli
    movzx eax, ax
    movzx ebx, bx
    shl ebx, 6
    add eax, ebx
    add eax, 1024
    mov [BI+12], eax
    sti
    jmp .memdone
.mem88:
    mov ah, 0x88
    int 0x15
    jc .memguess
    cli
    movzx eax, ax
    add eax, 1024
    mov [BI+12], eax
    sti
    jmp .memdone
.mem_hung:
    or byte [DIAG], 0x88
.memguess:
    mov dword [BI+12], 4096
.memdone:
    call wd_off
    mov byte [STAGE], 4
    test byte [DIAG], 0x08
    jz .memprint
    mov si, msg_memhang
    call sputs
    jmp .memchk
.memprint:
    mov si, msg_mem
    call sputs
    mov eax, [BI+12]
    shr eax, 10
    call print_u16
    mov si, msg_mb
    call sputs
.memchk:

    mov bx, .a20bios_hung
    mov cx, 37
    call wd_arm
    mov ax, 0x2401
    int 0x15
    call wd_off
    jmp .a20_check
.a20bios_hung:
    or byte [DIAG], 0xC0
.a20_check:
    call a20_wait
    jnz .a20_ok

    call kbc_wait
    mov al, 0xD1
    out 0x64, al
    call kbc_wait
    mov al, 0xDF
    out 0x60, al
    call kbc_wait
    call a20_wait
    jnz .a20_ok

    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    call a20_wait
    jnz .a20_ok

    mov si, msg_a20
    jmp halt_msg
.a20_ok:
    mov si, msg_a20ok
    call sputs

%ifdef FORCE_VGA
    jmp .novbe
%endif
    mov word [vmode], 0x0101
    cmp dword [CFG], 0x47464346
    jne .have_pref
    mov al, [CFG+4]
    cmp al, 4
    je .novbe
    cmp al, 2
    jne .chk1024
    mov word [vmode], 0x0103
    jmp .have_pref
.chk1024:
    cmp al, 3
    jne .have_pref
    mov word [vmode], 0x0105
.have_pref:
    cmp dword [BI+12], 9216
    jae .ram_video_ok
    mov word [vmode], 0x0101
.ram_video_ok:
    mov byte [banked], 0
%ifdef FORCE_BANKED
    mov byte [banked], 1
%endif
    mov ax, [vmode]
    mov [vpref], ax
    mov byte [STAGE], 5
    mov byte [vesa_tried], 1

    mov bx, .vbe_hung
    mov cx, 128
    call wd_arm

    mov di, VBEINFO
    mov dword [di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .novbe_off
    cmp dword [VBEINFO], 'VESA'
    jne .novbe_off

.vtry:
    mov ax, 0x4F01
    mov cx, [vmode]
    mov di, MODEINFO
    int 0x10
    cmp ax, 0x004F
    jne .vfail
    cmp byte [MODEINFO+25], 8
    jne .vfail
    cmp byte [banked], 0
    jne .vbank_chk
    mov ax, [MODEINFO]
    test al, 0x80
    jz .vfail
    mov eax, [MODEINFO+40]
    cmp eax, 0x00100000
    jb .vfail
    jmp .vchk_ok
.vbank_chk:
    mov ax, [MODEINFO]
    test al, 0x40
    jnz .vfail
    cmp word [MODEINFO+6], 64
    jne .vfail
    cmp word [MODEINFO+8], 0xA000
    jne .vfail
.vchk_ok:
    mov si, msg_vid
    call sputs
    movzx eax, word [MODEINFO+18]
    call print_u16
    mov al, 'x'
    call sputc
    movzx eax, word [MODEINFO+20]
    call print_u16
    mov si, msg_vesa
    cmp byte [banked], 0
    je .vrep
    mov si, msg_vesab
.vrep:
    call sputs
    call pause_vesa
    jc .novbe_off

    mov ax, 0x4F02
    mov bx, [vmode]
    cmp byte [banked], 0
    jne .vset
    or bh, 0x40
.vset:
    int 0x10
    cmp ax, 0x004F
    jne .vfail

    call wd_off
    mov ax, [MODEINFO+18]
    mov [BI+0], ax
    mov ax, [MODEINFO+20]
    mov [BI+2], ax
    mov ax, [MODEINFO+16]
    cmp ax, [MODEINFO+18]
    jae .pitchok
    mov ax, [MODEINFO+18]
.pitchok:
    mov [BI+4], ax
    mov byte [BI+6], 8
    mov ax, [vmode]
    mov [BI+18], ax
    cmp byte [banked], 0
    jne .bi_banked
    mov byte [BI+7], 1
    mov eax, [MODEINFO+40]
    mov [BI+8], eax
    jmp .viddone
.bi_banked:
    mov byte [BI+7], 2
    mov dword [BI+8], 0xA0000
    jmp .viddone

.vfail:
    cmp word [vmode], 0x0101
    je .vpass_done
    mov word [vmode], 0x0101
    jmp .vtry
.vpass_done:
    cmp byte [banked], 0
    jne .novbe_off
    mov byte [banked], 1
    mov ax, [vpref]
    mov [vmode], ax
    jmp .vtry

.vbe_hung:
    or byte [DIAG], 0xA0
    jmp .novbe
.novbe_off:
    call wd_off
.novbe:
    mov word [BI+0], 320
    mov word [BI+2], 200
    mov word [BI+4], 320
    mov byte [BI+6], 8
    mov byte [BI+7], 0
    mov dword [BI+8], 0xA0000
    mov word [BI+18], 0x0013
    mov si, msg_vga
    call sputs
    call pause_short
    mov bx, .vga_hung
    mov cx, 55
    call wd_arm
    mov ax, 0x0013
    int 0x10
    call wd_off
    jmp .viddone
.vga_hung:
    or byte [DIAG], 0x90
    cmp byte [vesa_tried], 0
    jne .viddone
    mov word [vmode], 0x0101
    jmp .have_pref
.viddone:
    mov byte [STAGE], 7

    cli
    lgdt [gdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:pm_start

wd_install:
    push es
    xor ax, ax
    mov es, ax
    mov eax, [es:8*4]
    mov [old_ivt8], eax
    cli
    mov word [es:8*4], wd_isr
    mov [es:8*4+2], cs
    sti
    pop es
    ret

wd_arm:
    mov [wd_rec], bx
    mov [wd_limit], cx
    mov word [wd_count], 0
    mov [wd_ss], ss
    mov ax, sp
    add ax, 2
    mov [wd_sp], ax
    mov byte [wd_active], 1
    ret

wd_off:
    mov byte [wd_active], 0
    ret

wd_isr:
    push ax
    inc word [cs:wd_count]
    cmp byte [cs:wd_active], 1
    jne .chain
    mov ax, [cs:wd_count]
    cmp ax, [cs:wd_limit]
    jbe .chain

    mov byte [cs:wd_active], 0
    mov al, 0x20
    out 0x20, al
    cli
    mov ss, [cs:wd_ss]
    mov sp, [cs:wd_sp]
    sti
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld
    mov al, '!'
    call sputc
    jmp [cs:wd_rec]
.chain:
    pop ax
    jmp far [cs:old_ivt8]

check_a20:
    push es
    mov ax, 0xFFFF
    mov es, ax
    mov byte [0x04F0], 0x00
    mov byte [es:0x0500], 0xFF
    cmp byte [0x04F0], 0xFF
    pop es
    ret

kbc_wait:
    push cx
    xor cx, cx
.w: in al, 0x64
    test al, 2
    jz .d
    loop .w
.d: pop cx
    ret

sputc:
    pusha
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    popa
    ret

sputs:
    pusha
.l: lodsb
    or al, al
    jz .d
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .l
.d: popa
    ret

print_u16:
    pusha
    cli
    mov ebx, 10
    xor cx, cx
.div:
    xor edx, edx
    div ebx
    push dx
    inc cx
    test eax, eax
    jnz .div
    sti
.out:
    pop ax
    add al, '0'
    call sputc
    loop .out
    popa
    ret

a20_wait:
    push cx
    mov cx, 2048
.t: call check_a20
    jnz .d
    in al, 0x80
    loop .t
.d: pop cx
    ret

halt_msg:
    call sputs
.h: hlt
    jmp .h

pause_short:
    push es
    push ax
    push bx
    xor ax, ax
    mov es, ax
    mov ax, [es:0x046C]
.p: hlt
    mov bx, [es:0x046C]
    sub bx, ax
    cmp bx, 27
    jb .p
    pop bx
    pop ax
    pop es
    ret

pause_vesa:
    push es
    push ax
    push bx
    xor ax, ax
    mov es, ax
    mov bx, [es:0x046C]
.q: mov ah, 1
    int 0x16
    jz .nokey
    xor ah, ah
    int 0x16
    or al, 0x20
    cmp al, 'v'
    je .vhit
.nokey:
    mov ax, [es:0x046C]
    sub ax, bx
    cmp ax, 27
    jb .q
    clc
    jmp .out
.vhit:
    stc
.out:
    pop bx
    pop ax
    pop es
    ret

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdtr:
    dw 23
    dd gdt

msg_hdr      db 13,10,"FLOPNIX: kernel loaded",13,10,0
msg_imgok    db "image ok",13,10,0
msg_badimg   db "HALT: kernel image corrupt (E09)",13,10
             db "bad floppy or drive - rewrite the disk",13,10,0
msg_font     db "font ok",13,10,0
msg_fonthang db "font grab hung - continuing",13,10,0
msg_mem      db "mem ",0
msg_mb       db " MB",13,10,0
msg_memhang  db "mem detect hung - assuming 4 MB",13,10,0
msg_a20      db "HALT: A20 line stuck - this machine"
             db 13,10,"cannot address RAM above 1 MB",13,10,0
msg_a20ok    db "A20 ok",13,10,0
msg_vid      db "video ",0
msg_vesa     db " VESA (press V for safe VGA mode)",13,10,0
msg_vesab    db " VESA banked (press V for safe VGA mode)",13,10,0
msg_vga      db "video 320x200 VGA - starting GUI",13,10,0
boot_drv db 0
vesa_tried db 0
vmode    dw 0x0101
vpref    dw 0x0101
banked   db 0
wd_active db 0
wd_count  dw 0
wd_limit  dw 0
wd_ss     dw 0
wd_sp     dw 0
wd_rec    dw 0
old_ivt8  dd 0

[bits 32]
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x0009E000

    mov byte [BI+17], 8

    mov dword [0x000500], 0x0A20DEAD
    mov dword [0x100500], 0x0BAD0BAD
    cmp dword [0x000500], 0x0A20DEAD
    je .a20_pm_ok
    or byte [BI+16], 0xC0
.freeze:
    cli
    hlt
    jmp .freeze
.a20_pm_ok:

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    cld
    rep stosd

    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .text

extern isr_dispatch

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

%assign i 0
%rep 48
  %if i = 8 || (i >= 10 && i <= 14) || i = 17
    ISR_ERR i
  %else
    ISR_NOERR i
  %endif
%assign i i+1
%endrep

isr_common:
    pushad
    push esp
    call isr_dispatch
    add esp, 4
    popad
    add esp, 8
    iretd

global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep

global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

extern syscall_dispatch
global isr128
isr128:
    pushad
    push esp
    call syscall_dispatch
    add esp, 4
    popad
    iretd

global gdt_load
gdt_load:
    mov eax, [esp+4]
    lgdt [eax]

    jmp 0x08:.reload
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

global tss_load
tss_load:
    mov eax, [esp+4]
    ltr ax
    ret
