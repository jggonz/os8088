; =============================================================================
; os8088 - tests/megahw/megahw.asm
;
; MEGAHW, a TEST instrument (nothing under tests/ ships): apps/os88huge.inc,
; the huge claim's walker (SPEC.md 50.3.8, docs/plans/MEGA-HUGE-PLAN.md F5),
; IN A GUEST - one build, every kernel. It claims the largest OSAPI_MEM_HUGE
; claim a 64KB step can be given (the AX = 0 ask; on kern_big and kern_small,
; which answer the ask CF = 1, MH_BIGKB) through hw_claim, takes the boundary
; B to test at - the bridge's CUT when it bridged (kern_mega, HW_CUT), else
; window 2's start - and with byte X of the claim meant to hold
; pat(X) = X0 xor X1 xor X2 xor 5Ah (X's three low bytes, so two windows
; never agree on a byte):
;
;   1. spans put from a near buffer and got back (hw_put, hw_get): across B,
;      from B's last byte, odd lengths, a window's end, the claim's two ends;
;   2. hw_aim at B - 1 answers a reach of ONE byte, at B a whole window, each
;      ES:DI reading the pattern;
;   3. [B - 32K, B + 32K) filled, then written by the KERNEL's own single
;      OSAPI_FILE_WRITE (which steps across a cut itself, KERN-MEGA-PLAN
;      14.169) to HWK.DAT and by hw_fwrite (os88_wseq) to HWW.DAT, and both
;      files read back with os88_rseq and held to the pattern - the kernel's
;      view of where the walker put the bytes;
;   4. the span zeroed, then hw_fread of HWK.DAT into it with HW_FOFF making
;      claim byte B - 32K the file's byte 0, and hw_get holding it to the
;      pattern again.
;
; Answers in MH_RES, an 'HWRES001' block in its own image (86Box and MartyPC
; read it), and as one line on port E9 (Bochs):
;
;   +8  KB claimed        +10 the claim's CF   +12 HW_STEP   +14 HW_CUT
;   +16 B (64KB units)    +18 spans: 1 every one the pattern, 2 + the index
;   +20 aim: 1 both right, 2 B - 1's reach, 3 B's reach, 4 a byte wrong
;   +22 fill: 1 put       +24 the kernel's write: 1 ok, 2 + AX refused
;   +26 hw_fwrite: 1 ok, 2 open refused, 3 put refused, 4 close refused
;   +28 HWK.DAT read back: 1 the pattern, 2 a byte wrong, 3 refused, 4 short
;   +30 HWW.DAT read back: the same
;   +32 hw_fread: 1 64KB delivered, 2 refused, 3 short
;   +34 its span got back: 1 the pattern, 2 a byte wrong (+36 where)
;   +38 1 = done (written last)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAHW', mh_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

%define HW_CLAIM
%define HW_GET
%define HW_PUT
%define HW_RSEQ
%define HW_WSEQ

MH_BIGKB    equ 200                 ; the claim where the ask is refused
MH_BUF      equ 0x2000              ; each near buffer

mh_entry:
    push si
    push di
    push bp
    mov si, mh_tpl                  ; the window FIRST, so a row on 86Box or
    call OSAPI_WM_CREATE            ; MartyPC sees the program up
    jc .out0
    mov [mh_win], bx                ; the entry RETURNS it (SPEC.md 20.2):
                                    ; the loader's step 9 binds BX as the
                                    ; window, and BX is everybody's below
    OS88_REGION_MOVABLE
    xor ax, ax                      ; the ask: the most a 64KB step's claim
    xor bx, bx                      ; is given, a bridge allowed
    call OSAPI_MEM_HUGE
    jnc .ask
    mov ax, MH_BIGKB                ; (kern_big, kern_small: no ask)
.ask:
    mov [mh_res+8], ax
    xor cx, cx
    mov bx, mh_rec
    call hw_claim                   ; DX = the claim
    sbb si, si
    mov [mh_res+10], si
    jnc .got
    jmp .done
.got:
    mov [mh_dx], dx
    mov ax, [mh_rec+HW_STEP]
    mov [mh_res+12], ax
    mov ax, [mh_rec+HW_CUT]
    mov [mh_res+14], ax
    or ax, ax                       ; B: the cut, else window 2
    jnz .b
    mov ax, 2
.b:
    mov [mh_res+16], ax
    mov [mh_b], ax
    mov bx, mh_rec                  ; BX -> the record from here on
    ; --- 1. spans --------------------------------------------------------
    mov si, mh_spans
    xor bp, bp                      ; BP = the span's index
.sp:
    cmp si, mh_spans_end
    jae .spok
    call mh_spanat                  ; DX:AX = its offset, CX = its bytes
    add si, 6
    mov di, mh_src
    call mh_fill
    push ds
    pop es
    call hw_put
    jc .spbad
    mov di, mh_dst
    call mh_zero
    call hw_get
    jc .spbad
    call mh_cmp
    jc .spbad
    inc bp
    jmp short .sp
.spbad:
    add bp, 2
    mov [mh_res+18], bp
    jmp short .aim
.spok:
    mov word [mh_res+18], 1
    ; --- 2. the aim ------------------------------------------------------
.aim:
    mov dx, [mh_b]
    xor ax, ax
    sub ax, 1
    sbb dx, 0                       ; DX:AX = B - 1
    mov cx, MH_BUF                  ; (its pattern, to be read through it)
    mov di, mh_src
    call mh_fill
    push ds
    pop es
    mov cx, 2
    call hw_put                     ; B - 1 and B: the pattern there
    mov cx, 0xFFFF
    call hw_aim
    mov word [mh_res+20], 2
    jc .aimo
    cmp cx, 1
    jne .aimo
    mov word [mh_res+20], 4
    push ax
    call mh_pat                     ; AL = pat(B - 1)
    cmp [es:di], al
    pop ax
    jne .aimo
    add ax, 1
    adc dx, 0                       ; B
    mov cx, 0xFFFF
    call hw_aim
    mov word [mh_res+20], 3
    jc .aimo
    cmp cx, 0xFFFF
    jne .aimo
    mov word [mh_res+20], 4
    push ax
    call mh_pat
    cmp [es:di], al
    pop ax
    jne .aimo
    mov word [mh_res+20], 1
.aimo:
    push ds
    pop es
    ; --- 3. the 64KB about B, written two ways ---------------------------
    call mh_lo                      ; DX:AX = B - 32K
    mov bp, 8
.f:
    mov cx, MH_BUF
    mov di, mh_src
    call mh_fill
    call hw_put
    add ax, MH_BUF
    adc dx, 0
    dec bp
    jnz .f
    mov word [mh_res+22], 1
    call mh_kwrite                  ; the kernel's, HWK.DAT
    mov [mh_res+24], ax
    call mh_wwrite                  ; hw_fwrite's, HWW.DAT
    mov [mh_res+26], ax
    mov si, mh_s_k
    call mh_check
    mov [mh_res+28], ax
    mov si, mh_s_w
    call mh_check
    mov [mh_res+30], ax
    ; --- 4. zeroed, then hw_fread from HWK.DAT ---------------------------
    push ds
    pop es
    mov di, mh_src
    mov cx, MH_BUF
    call mh_zero
    call mh_lo
    mov bp, 8
.z:
    call hw_put
    add ax, MH_BUF
    adc dx, 0
    dec bp
    jnz .z
    call mh_lo                      ; HW_FOFF = -(B - 32K): claim byte B - 32K
    xor si, si                      ; is the file's byte 0
    xor di, di
    sub si, ax
    sbb di, dx
    mov [mh_rec+HW_FOFF], si
    mov [mh_rec+HW_FOFF+2], di
    mov di, mh_cur
    call os88_rseq_new
    mov si, mh_s_k
    mov cx, 0x8000
    call hw_fread                   ; DX:AX = delivered
    mov word [mh_res+32], 2
    jc .fr
    or dx, dx
    jnz .fr3
    cmp ax, 0x8000
    jne .fr3
    call mh_lo
    add ax, 0x8000
    adc dx, 0
    mov cx, 0x8000
    call hw_fread
    mov word [mh_res+32], 2
    jc .fr
    or dx, dx
    jnz .fr3
    cmp ax, 0x8000
    jne .fr3
    mov word [mh_res+32], 1
    jmp short .fr
.fr3:
    mov word [mh_res+32], 3
.fr:
    call mh_lo                      ; ...got back in 8 KB pieces
    mov bp, 8
    xor bx, bx                      ; BX = where in the 64KB
.g:
    mov cx, MH_BUF
    mov di, mh_dst
    push bx
    mov bx, mh_rec
    call hw_get
    pop bx
    call mh_cmp                     ; CF = 1: AX... (DX:AX kept)
    jc .gbad
    add bx, MH_BUF
    add ax, MH_BUF
    adc dx, 0
    dec bp
    jnz .g
    mov word [mh_res+34], 1
    jmp short .gend
.gbad:
    mov word [mh_res+34], 2
    mov [mh_res+36], bx
.gend:
    mov dx, [mh_dx]
    call OSAPI_MEM_FREE
.done:
    mov word [mh_res+38], 1
    call mh_dump
    mov bx, [mh_win]                ; BX = the window again: the 8KB walk
                                    ; above leaves 0 here, and step 9 bound
                                    ; that (wm_ptr2idx's divide, FAULT 00)
.out0:
    pop bp
    pop di
    pop si
    clc
    ret

mh_paint:
    ret

; mh_lo - DX:AX = B x 64KB - 32KB. Keeps the rest
mh_lo:
    mov dx, [mh_b]
    mov ax, 0x8000
    dec dx
    ret

; mh_spanat - SI -> a span (dw B-relative 64KB units, dw offset, dw bytes;
; a unit of 8000h counts back from the claim's end) -> DX:AX, CX. Keeps the
; rest
mh_spanat:
    mov dx, [si]
    mov cx, [si+4]
    cmp dx, 0x8000
    je .end
    mov ax, [si+2]
    add dx, [mh_b]
    ret
.end:
    mov ax, [mh_rec+HW_LEN]         ; the claim's end, less the offset
    mov dx, [mh_rec+HW_LEN+2]
    sub ax, [si+2]
    sbb dx, 0
    ret

; mh_pat - DX:AX = an offset -> AL = the pattern there (AH and the rest kept)
mh_pat:
    push ax
    xor al, ah
    xor al, dl
    xor al, 0x5A
    mov [mh_t], al
    pop ax
    mov al, [mh_t]
    ret

; mh_fill - DS:DI, CX bytes = the pattern from offset DX:AX. Keeps all
mh_fill:
    push ax
    push cx
    push dx
    push di
    jcxz .o
.l:
    push ax
    call mh_pat
    mov [di], al
    pop ax
    inc di
    add ax, 1
    adc dx, 0
    loop .l
.o:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; mh_zero - DS:DI, CX bytes = 0. Keeps all
mh_zero:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; mh_cmp - DS:DI, CX bytes against the pattern from DX:AX: CF = 1 one
; differs. Keeps all
mh_cmp:
    push ax
    push cx
    push dx
    push di
    jcxz .ok
.l:
    push ax
    call mh_pat
    cmp [di], al
    pop ax
    jne .bad
    inc di
    add ax, 1
    adc dx, 0
    loop .l
.ok:
    clc
    jmp short .o
.bad:
    stc
.o:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; mh_kwrite - the 64KB from B - 32K by ONE OSAPI_FILE_WRITE -> AX = 1, or
; 2 + its AX. On a paragraph step the window B - 1's paragraph 800h, offset
; 0 (a real-mode segment walks on by itself); on a run of selectors window
; B - 1 at 8000h, which the kernel steps across a cut (14.169)
mh_kwrite:
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [mh_rec+HW_STEP]
    mov cx, [mh_b]
    dec cx
    mul cx
    add ax, [mh_rec+HW_SEG]         ; AX = window B - 1
    mov bx, 0x8000
    cmp word [mh_rec+HW_STEP], 0x1000
    jne .sel
    add ax, 0x0800
    xor bx, bx
.sel:
    mov es, ax
    mov si, mh_s_k
    mov dx, 1                       ; DX:CX = 64KB
    xor cx, cx
    call OSAPI_FILE_WRITE
    jc .no
    mov ax, 1
    jmp short .o
.no:
    add ax, 2
.o:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mh_wwrite - the same 64KB by hw_fwrite onto an os88_wseq stream -> AX
mh_wwrite:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, mh_s_w
    mov di, mh_ws
    mov bx, mh_wbuf
    mov cx, MH_BUF
    call os88_wseq_open
    mov ax, 2
    jc .o
    call mh_lo
    mov bx, mh_rec
    mov cx, 0x8000
    call hw_fwrite
    jc .put
    call mh_lo                      ; (hw_fwrite keeps all but AX)
    add ax, 0x8000
    adc dx, 0
    mov cx, 0x8000
    call hw_fwrite
    jc .put
    call os88_wseq_close
    mov ax, 4
    jc .o
    mov ax, 1
    jmp short .o
.put:
    call os88_wseq_close
    mov ax, 3
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mh_check - SI = a name: the file read back in 8 KB pieces (os88_rseq) and
; held to the pattern from B - 32K -> AX = 1, 2 a byte wrong, 3 refused,
; 4 short
mh_check:
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    push ds
    pop es
    mov di, mh_cur
    call os88_rseq_new
    xor ax, ax
    xor dx, dx
    mov bp, 8
.l:
    mov bx, mh_dst
    mov cx, MH_BUF
    push ax
    push dx
    call os88_rseq                  ; DX:AX = delivered
    jc .ref
    or dx, dx
    jnz .sh
    cmp ax, MH_BUF
    jne .sh
    pop dx
    pop ax
    push ax
    push dx
    push di
    add ax, 0x8000                  ; the pattern's offset: B - 32K + it
    adc dx, [mh_b]
    dec dx
    mov di, mh_dst
    call mh_cmp
    pop di
    pop dx
    pop ax
    jc .bad
    add ax, MH_BUF
    adc dx, 0
    dec bp
    jnz .l
    mov ax, 1
    jmp short .o
.ref:
    add sp, 4
    mov ax, 3
    jmp short .o
.sh:
    add sp, 4
    mov ax, 4
    jmp short .o
.bad:
    mov ax, 2
.o:
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    ret

; mh_dump - 'MEGAHW: ' and MH_RES's words from +8 onto E9, one line
mh_dump:
    push ax
    push cx
    push si
    mov si, mh_s_e9
.s:
    lodsb
    or al, al
    jz .w
    out 0xE9, al
    jmp short .s
.w:
    mov si, mh_res+8
    mov cx, 16
.l:
    lodsw
    call mh_hex4
    mov al, ' '
    out 0xE9, al
    loop .l
    mov al, 10
    out 0xE9, al
    pop si
    pop cx
    pop ax
    ret
mh_hex4:
    push ax
    mov al, ah
    call mh_hex2
    pop ax
mh_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

%include "os88rseq.inc"
%include "os88wseq.inc"
%include "os88huge.inc"

; the spans: B-relative 64KB units (8000h: back from the claim's end), the
; offset, the bytes - each at most MH_BUF
mh_spans:
    dw -1, 0xFF00, 0x0200           ; 256 bytes either side of B
    dw -1, 0xFFFF, 0x0002           ; B's last byte and B
    dw -1, 0xF001, 0x1FFF           ; odd, across B
    dw  0, 0x1230, 77               ; inside B's window
    dw -1, 0x0000, MH_BUF           ; B - 1's start
    dw -1, 0x8000, 0x0001           ; one byte
    dw 0x8000, 0x1001, 0x1001       ; the claim's last 4 KB and a byte
mh_spans_end:

mh_tpl:
    dw 8, 400, 200, 40
    dw mh_ttl, mh_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS mh_cls, 0, PAINT, mh_paint
mh_ttl:     db 'MegaHW', 0
mh_s_e9:    db 'MEGAHW: ', 0
mh_s_k:     db 'HWK.DAT', 0
mh_s_w:     db 'HWW.DAT', 0

mh_res:     db 'HWRES001'
            times 32 db 0
mh_dx       dw 0
mh_win      dw 0
mh_b        dw 0
mh_t        db 0
            align 2
mh_rec      times HW_SIZE db 0
mh_cur      times FSEQ_SIZE db 0
mh_ws       times WSEQ_SIZE db 0
mh_src      times MH_BUF db 0
mh_dst      times MH_BUF db 0
mh_wbuf     times MH_BUF db 0

OS88_IMAGE_END
