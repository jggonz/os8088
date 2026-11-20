; Public directory-mutation bench for SPEC.md18.5.3, located in B:/CASE.
; r=Rename OLD.TXT to NEW.TXT; m=New Folder ADDED; e=remove EMPTY;
; t=recursively remove TREE. Each callback snapshots API outputs before
; doing anything that could overwrite CF or the non-AX registers.
%include "os88api.inc"

    OS88_HEADER 'DISKMUT', dm_entry, 0, OS88_HIGH | OS88_HIGHMEM

dm_entry:
    mov si, dm_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
.out:
    ret

dm_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov [dm_op], al
    cmp al, 'r'
    je .run
    cmp al, 'm'
    je .run
    cmp al, 'e'
    je .run
    cmp al, 't'
    jne .out
.run:
    call OSAPI_FILE_HERE
    mov [dm_cwd0], dx
    mov [dm_drive0], bl
    mov ax, ds
    mov [dm_ds0], ax
    mov ax, ss
    mov [dm_ss0], ax
    mov ax, cs
    mov [dm_cs0], ax
    mov ax, sp
    mov [dm_sp0], ax
    mov ax, 0x1234
    mov es, ax
    mov bx, 0xB123
    mov cx, 0xC234
    mov dx, 0xD345
    mov bp, 0x5678
    mov di, 0xD567
    cmp byte [dm_op], 'r'
    je .rename
    cmp byte [dm_op], 'm'
    je .mkdir
    cmp byte [dm_op], 'e'
    je .rmdir
    mov si, dm_tree
    mov al, 1
    call OSAPI_FILE_RMDIR
    jmp short .capture
.rename:
    mov si, dm_old
    mov di, dm_new
    call OSAPI_FILE_RENAME
    jmp short .capture
.mkdir:
    mov si, dm_add
    call OSAPI_FILE_MKDIR
    jmp short .capture
.rmdir:
    mov si, dm_empty
    xor al, al
    call OSAPI_FILE_RMDIR
.capture:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    push ss
    push cs
    ; CS is readable even if the API broke DS. Restore our real DS only
    ; after snapshotting every output; never write through a damaged DS.
    mov ax, [cs:dm_ds0]
    mov ds, ax
    mov ax, sp
    add ax, 24
    mov [dm_sp1], ax
    mov bp, sp
    mov di, dm_regs
    mov cx, 12
.copy:
    mov ax, [ss:bp]
    mov [di], ax
    add bp, 2
    add di, 2
    loop .copy
    add sp, 24
    call OSAPI_FILE_HERE
    mov [dm_cwd1], dx
    mov [dm_drive1], bl
    inc byte [dm_serial]         ; publish last, after the complete record
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dm_tpl:
    dw 140, 70, 240, 60, dm_title, dm_cls
    OS88_CLASS dm_cls, 0, ONKEY, dm_key
dm_title: db 'Disk mutation fixture', 0
dm_old:   db 'OLD.TXT', 0
dm_new:   db 'NEW.TXT', 0
dm_add:   db 'ADDED', 0
dm_empty: db 'EMPTY', 0
dm_tree:  db 'TREE', 0

    OS88_BSS 42
    OS88_IMAGE_END
dm_serial equ os88_image_end + 0
dm_op     equ os88_image_end + 1
dm_sp0    equ os88_image_end + 2
dm_sp1    equ os88_image_end + 4
dm_ds0    equ os88_image_end + 6
dm_cwd0   equ os88_image_end + 8
dm_cwd1   equ os88_image_end + 10
dm_drive0 equ os88_image_end + 12
dm_drive1 equ os88_image_end + 13
dm_ss0    equ os88_image_end + 14
dm_cs0    equ os88_image_end + 16
dm_regs   equ os88_image_end + 18 ; CS,SS,DS,ES,BP,DI,SI,DX,CX,BX,AX,FLAGS
