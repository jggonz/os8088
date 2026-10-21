; Hardware mixer, SPEC.md 34.15. No IRQ/DMA/routing/filter registers here.
; Descriptors: left register, right (0=mono, FF=packed), shift, native max.
; The percentage ABI hides the chip's resolution, never its capabilities.
MIX_DSZ equ 4
mix_1335:
    db 02h,0,1,7, 0Ah,0,1,3, 06h,0,1,7, 08h,0,1,7
    times (MIX_NCHAN-4)*MIX_DSZ db 0
mix_1345:
    db 22h,0FFh,1,7, 04h,0FFh,1,7, 26h,0FFh,1,7
    db 28h,0FFh,1,7, 2Eh,0FFh,1,7, 0Ah,0,0,3
    times (MIX_NCHAN-6)*MIX_DSZ db 0
mix_1745:
    db 30h,31h,3,31, 32h,33h,3,31, 34h,35h,3,31
    db 36h,37h,3,31, 38h,39h,3,31, 3Ah,0,3,31
    db 3Bh,0,6,3, 46h,47h,4,15, 44h,45h,4,15

; Probe after the first successful DSP attach; own the base independently of
; stream tier teardown. Test two patterns so a floating/stuck bus cannot pass.
mix_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [sbl_base]
    mov [mix_base], ax
    mov si, mix_1335
    mov bl, 1
    mov cx, 000Fh
    cmp byte [sbl_verhi], 3
    jb .test
    mov si, mix_1345
    inc bl
    mov cx, 003Fh
    cmp byte [sbl_verhi], 4
    jb .test
    mov si, mix_1745
    inc bl
    mov cx, 01FFh
.test:
    push cx
    pushf
    cli
    mov ah, [si]
    call mix_rd
    mov bh, al                  ; original register, restored even on failure
    mov cl, [si+2]
    mov al, [si+3]
    shl al, cl
    mov ch, al                  ; defined mask (left side is enough to test)
    mov al, [si+3]
    shr al, 1
    shl al, cl
    mov dl, al                  ; pattern one
    call mix_wr
    call mix_rd
    and al, ch
    cmp al, dl
    jne .bad
    mov al, dl
    xor al, ch                  ; pattern two: every defined bit changes
    mov dl, al
    call mix_wr
    call mix_rd
    and al, ch
    cmp al, dl
    jne .bad
    mov al, bh
    call mix_wr
    popf
    pop cx
    mov [mix_kind], bl
    mov [mix_mask], cx
    mov [mix_table], si
    ; Preserve the original native registers before the first user write.
    mov di, mix_orig
    mov cx, MIX_NCHAN
.save:
    cmp byte [si], 0
    je .next
    pushf
    cli
    mov ah, [si]
    call mix_rd
    mov [di], al
    cmp byte [si+1], 0
    je .saved
    cmp byte [si+1], 0FFh
    je .saved
    mov ah, [si+1]
    call mix_rd
    mov [di+1], al
.saved:
    popf
.next:
    add si, MIX_DSZ
    add di, 2
    loop .save
    or word [snd_services+DSV_CAPS], SND_CAP_MIXER
    jmp short .out
.bad:
    mov al, bh
    call mix_wr
    popf
    pop cx
    mov word [mix_base], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mix_info:
    cmp byte [mix_kind], 0
    je mix_none
    xor ax, ax
    mov al, [mix_kind]
    mov cx, [mix_mask]
    clc
    ret
mix_none:
    mov ax, SNDE_NOMIX
    stc
    ret
mix_arg:
    mov ax, SNDE_MIXARG
    stc
    ret

; AL=channel -> SI=descriptor, CF. Callers bank SI.
mix_desc:
    cmp byte [mix_kind], 0
    je mix_none
    cmp al, MIX_NCHAN
    jae mix_arg
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    add si, [mix_table]
    cmp byte [si], 0
    je mix_arg
    clc
    ret

mix_get:
    push bx
    push si
    push di
    mov di, ax                  ; channel, for the tone flag
    call mix_desc
    jc .out
    pushf
    cli
    call mix_rawget             ; AL=left raw, AH=right raw
    popf
    mov bh, ah
    mov cl, [si+2]
    shr al, cl
    and al, [si+3]
    call mix_to_pct
    mov bl, al
    mov al, bh
    mov cl, [si+2]
    shr al, cl
    cmp byte [si+1], 0FFh
    jne .right
    and al, [si+3]              ; packed right is already in low nibble
    jmp short .norm
.right:
    and al, [si+3]
.norm:
    call mix_to_pct
    mov ch, al
    mov cl, bl
    xor dx, dx
    cmp byte [si+1], 0
    je .flags
    inc dx
.flags:
    and di, 00FFh
    cmp di, 7
    jb .ok
    or dl, MIX_TONE
.ok:
    mov dh, [si+3]
    clc
.out:
    pop di
    pop si
    pop bx
    ret

; Native register images -> percentages, nearest step. AX/DX scratch.
mix_to_pct:
    xor ah, ah
    mov dx, 100
    mul dx
    xor dx, dx
    mov dl, [si+3]
    shr dx, 1
    add ax, dx
    xor dx, dx
    mov dl, [si+3]
    div dl
    xor ah, ah
    ret

mix_set:
    push bx
    push cx
    push dx
    push si
    call mix_desc
    jc .out
    cmp cl, 100
    ja .arg
    cmp ch, 100
    ja .arg
    mov bl, ch
    mov al, cl
    call mix_from_pct
    mov bh, al                  ; native left, shifted
    mov al, bl
    call mix_from_pct
    mov bl, al                  ; native right, shifted
    pushf
    cli
    mov ah, [si]
    cmp byte [si+1], 0FFh
    jne .separate
    mov al, bh
    mov cl, 4
    shl al, cl
    or al, bl
    call mix_wr
    jmp short .done
.separate:
    mov al, bh
    call mix_wr
    cmp byte [si+1], 0
    je .done
    mov ah, [si+1]
    mov al, bl
    call mix_wr
.done:
    popf
    xor ax, ax
    jmp short .out
.arg:
    call mix_arg
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret
mix_from_pct:
    mul byte [si+3]
    add ax, 50
    mov dl, 100
    div dl
    mov cl, [si+2]
    shl al, cl
    ret

; AL=left raw, AH=right raw; packed left aligned to low nibble first.
mix_rawget:
    mov ah, [si]
    call mix_rd
    mov dh, al
    cmp byte [si+1], 0
    je .mono
    cmp byte [si+1], 0FFh
    je .packed
    mov ah, [si+1]
    call mix_rd
    mov ah, al
    mov al, dh
    ret
.packed:
    mov ah, al
    mov cl, 4
    shr al, cl
    ret
.mono:
    mov ah, al
    ret

; AH=index, AL=value. No implicit interrupt-state change: caller brackets
; the whole stereo transaction. DX is preserved (probe holds pattern in DL).
mix_rd:
    push dx
    mov dx, [mix_base]
    add dx, 4
    mov al, ah
    out dx, al
    inc dx
    in al, dx
    pop dx
    ret
mix_wr:
    push dx
    push ax
    mov dx, [mix_base]
    add dx, 4
    mov al, ah
    out dx, al
    inc dx
    pop ax
    out dx, al
    pop dx
    ret

mix_detach:
    push ax
    push cx
    push si
    push di
    cmp byte [mix_kind], 0
    je .out
    mov si, [mix_table]
    mov di, mix_orig
    mov cx, MIX_NCHAN
.more:
    cmp byte [si], 0
    je .next
    pushf
    cli
    mov ah, [si]
    mov al, [di]
    call mix_wr
    cmp byte [si+1], 0
    je .done
    cmp byte [si+1], 0FFh
    je .done
    mov ah, [si+1]
    mov al, [di+1]
    call mix_wr
.done:
    popf
.next:
    add si, MIX_DSZ
    add di, 2
    loop .more
    mov byte [mix_kind], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

%macro MIX_STATE 0
mix_kind:  db 0
mix_base:  dw 0
mix_mask:  dw 0
mix_table: dw 0
mix_orig:  times MIX_NCHAN*2 db 0
%endmacro
