; Loads the kernel and settings from the floppy.
[org 0x7C00]
[bits 16]

    jmp short start
    nop
    ; BIOS-compatible disk geometry; file storage uses FLOPFS.
    db "FLOPNIX "
    dw 512
    db 1
    dw 1
    db 2
    dw 224
    dw 2880
    db 0xF0
    dw 9
    dw 18
    dw 2
    dd 0
    dd 0
    db 0
    db 0
    db 0x29
    dd 0x464C5058
    db "FLOPNIX 0.4"
    db "NONE    "

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    cmp dl, 0x02
    jb .dl_ok
    xor dl, dl
.dl_ok:
    mov [boot_drive], dl

    mov si, msg_boot
    call print

    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

    mov ax, 0x0800
    mov es, ax
    mov bp, 256
    mov word [lba], 1

.load_loop:

    mov ax, [lba]
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    mov ch, al
    shr ch, 1

    mov di, 5
.try:
    mov ax, 0x0201
    xor bx, bx
    mov dl, [boot_drive]
    push cx
    push dx
    int 0x13
    pop dx
    pop cx
    jnc .ok
    mov [err], ah
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .try
    mov si, msg_err
    call print
    mov al, [err]
    call phex8
    mov al, 'L'
    call putc
    mov ax, [lba]
    call phex16
.halt:
    hlt
    jmp .halt

.ok:
    inc word [lba]
    mov al, 13
    call putc
    mov si, msg_ld
    call print
    mov ax, [lba]
    dec ax
    shr ax, 1
    call pdec
    mov si, msg_kb
    call print
    mov ax, es
    add ax, 32
    mov es, ax
    dec bp
    jnz .load_loop

    mov dl, [boot_drive]
    jmp 0x0000:0x8000

print:
    lodsb
    or al, al
    jz .done
    call putc
    jmp print
.done:
    ret

putc:
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop bx
    ret

phex16:
    push ax
    mov al, ah
    call phex8
    pop ax
phex8:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call .nib
    pop cx
    pop ax
    and al, 0x0F
.nib:
    cmp al, 10
    jb .dig
    add al, 7
.dig:
    add al, '0'
    jmp putc

pdec:
    push cx
    push dx
    xor cx, cx
.dv: xor dx, dx
    div word [ten]
    push dx
    inc cx
    test ax, ax
    jnz .dv
.pr: pop ax
    add al, '0'
    call putc
    loop .pr
    pop dx
    pop cx
    ret

msg_boot db "FLOPNIX", 13, 10, 0
msg_ld   db "kernel ", 0
msg_kb   db " KB", 0
msg_err  db " E", 0
ten      dw 10
lba      dw 0
err      db 0

times 505-($-$$) db 0
boot_drive   db 0
kern_sectors dw 128
dw 0
dw 0xAA55
