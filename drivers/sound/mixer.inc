; Hardware mixer, SPEC.md 34.15. No IRQ/DMA/routing/filter registers here.
; Descriptors: left register, right (0=mono, FF=packed), shift, native max.
; The percentage ABI hides the chip's resolution, never its capabilities.
; Each table is the chip's channels 0..n-1 and NOTHING after them, n in the
; byte IN FRONT of it ([mix_table] - 1): mix_desc refuses a channel at or past
; it and the probe's save and the detach walk exactly n (driver size pass 1:
; the zero entries that used to pad each table to MIX_NCHAN, and the
; `cmp byte [si], 0` that stopped at them, were 32 resident bytes)
MIX_DSZ equ 4
    db (mix_1335e - mix_1335) / MIX_DSZ
mix_1335:
    db 02h,0,1,7, 0Ah,0,1,3, 06h,0,1,7, 08h,0,1,7
mix_1335e:
    db (mix_1345e - mix_1345) / MIX_DSZ
mix_1345:
    db 22h,0FFh,1,7, 04h,0FFh,1,7, 26h,0FFh,1,7
    db 28h,0FFh,1,7, 2Eh,0FFh,1,7, 0Ah,0,0,3
mix_1345e:
    db (mix_1745e - mix_1745) / MIX_DSZ
mix_1745:
    db 30h,31h,3,31, 32h,33h,3,31, 34h,35h,3,31
    db 36h,37h,3,31, 38h,39h,3,31, 3Ah,0,3,31
    db 3Bh,0,6,3, 46h,47h,4,15, 44h,45h,4,15
mix_1745e:
%if (mix_1335e - mix_1335) != 4 * MIX_DSZ || (mix_1345e - mix_1345) != 6 * MIX_DSZ || (mix_1745e - mix_1745) != MIX_NCHAN * MIX_DSZ
  %error "a mixer table's channels are no longer 4, 6 and MIX_NCHAN: mix_probe makes INFO's mask (0Fh, 3Fh, 1FFh) from the count"
%endif

; --- MIX_ONCE: ATTACH-ONLY (SPEC.md 34.15.1) ------------------------------
; mix_probe runs inside the attach and never after.
; Defined as a macro and expanded HERE unless SND_SPLIT is set; SOUND.DRV
; sets it and expands MIX_ONCE past snd_keepmix, the end of what the driver
; keeps once its attach has answered, so these bytes leave the claim.
%macro MIX_ONCE 0
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
    cmp byte [sbl_verhi], 3
    jb .test
    mov si, mix_1345
    inc bl
    cmp byte [sbl_verhi], 4
    jb .test
    mov si, mix_1745
    inc bl
.test:
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
    mov [mix_kind], bl
    mov cl, [si-1]              ; CL = the table's channels, and INFO's mask
    mov ax, 1                   ; one bit each: 0Fh, 3Fh, 1FFh
    shl ax, cl
    dec ax
    mov [mix_mask], ax
    mov [mix_table], si
    ; Preserve the original native registers before the first user write.
    mov di, mix_orig
    mov ch, 0                   ; CX = the channels
.save:
    pushf
    cli
    mov ah, [si]
    call mix_rd
    mov [di], al
    mov al, [si+1]              ; a right register of its own? Not 0 (mono)
    inc ax                      ; and not 0FFh (packed): 1 and 0 after the
    cmp al, 2                   ; INC are the two that are not
    jb .saved
    mov ah, [si+1]
    call mix_rd
    mov [di+1], al
.saved:
    popf
    add si, MIX_DSZ
    add di, 2
    loop .save
    or word [snd_services+DSV_CAPS], SND_CAP_MIXER
    jmp short .out
.bad:
    mov al, bh
    call mix_wr
    popf
    mov word [mix_base], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endmacro
%ifndef SND_SPLIT
    MIX_ONCE
%endif
mix_info:                       ; (no mix_kind test in any verb here:
    xor ax, ax                  ; snd_pkg refuses them all without a mixer)
    mov al, [mix_kind]          ; (CF = 0 from the XOR)
    mov cx, [mix_mask]
    ret
mix_arg:
    mov ax, SNDE_MIXARG
    stc
    ret

; AL=channel -> SI=descriptor, CF; AX = the channel, zero-extended. Callers
; bank SI.
mix_desc:
    mov si, [mix_table]
    cmp al, [si-1]              ; the table's channel count
    jae mix_arg
    cbw                         ; AL < 9: AH = 0
    add si, ax
    add si, ax
    add si, ax
    add si, ax                  ; SI += 4 x channel (MIX_DSZ). CF = 0: the
    ret                         ; image's offsets do not wrap
%if MIX_DSZ != 4
  %error "mix_desc adds the channel MIX_DSZ times"
%endif

mix_get:
    push bx
    push si
    push di
    call mix_desc
    jc .out
    mov di, ax                  ; channel, for the tone flag (zero-extended)
    pushf
    cli
    call mix_rawget             ; AL=left raw, AH=right raw
    popf
    mov bh, ah
    call mix_to_pct
    mov bl, al
    mov al, bh                  ; (packed right is already in the low nibble)
    call mix_to_pct
    mov ch, al
    mov cl, bl
    xor dx, dx
    cmp dl, [si+1]              ; CF = 1: a right register, separate or packed
    adc dl, dl                  ; ...and DL = MIX_STEREO
%if MIX_STEREO != 1
  %error "mix_get takes MIX_STEREO from the carry"
%endif
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

; Native register image AL (still shifted) -> percentage, nearest step.
; AX/CL/DX scratch.
mix_to_pct:
    mov cl, [si+2]
    shr al, cl
    and al, [si+3]
    mov dl, 100
    mul dl                      ; AX = AL * 100
    mov dl, [si+3]              ; DL = the native max...
    mov dh, dl
    shr dh, 1                   ; ...and half of it, to round
    add al, dh
    adc ah, 0
    div dl                      ; <= 100: it fits AL
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
    call mix_idx
    in al, dx
    pop dx
    ret
mix_wr:
    push dx
    push ax
    call mix_idx
    pop ax
    out dx, al
    pop dx
    ret
; mix_idx - AH to the index port (2x4h); out DX = the data port, AL = AH
mix_idx:
    mov dx, [mix_base]
    add dx, 4
    mov al, ah
    out dx, al
    inc dx
    ret

mix_detach:
    push ax
    push cx
    push si
    push di                     ; (snd_detach tests mix_kind for us)
    mov si, [mix_table]
    mov di, mix_orig
    mov cl, [si-1]              ; CX = the table's channels
    mov ch, 0
.more:
    pushf
    cli
    mov ah, [si]
    mov al, [di]
    call mix_wr
    mov al, [si+1]              ; separate right register: not 0, not 0FFh
    inc ax                      ; (mix_probe's .save test)
    cmp al, 2
    jb .done
    mov ah, [si+1]
    mov al, [di+1]
    call mix_wr
.done:
    popf
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

%macro MIX_STATE 0               ; (mix_kind is the core's: sound.asm)
mix_base:  dw 0
mix_mask:  dw 0
mix_table: dw 0
mix_orig:  times MIX_NCHAN*2 db 0
%endmacro
